#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Backend/FUnrealAiPersistenceStub.h"
#include "Context/FUnrealAiContextService.h"
#include "Context/IAgentContextService.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Harness/FUnrealAiConversationStore.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Harness/ILlmTransport.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Harness/UnrealAiTurnLlmRequestBuilder.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tools/UnrealAiToolCatalog.h"

namespace UnrealAiHeadlessToolPromptTestsPriv
{
	struct FPromptCase
	{
		FString Prompt;
		FString Mode;
		TArray<FString> ExpectedToolCalls;
	};

	static FString GetFixturePath()
	{
		FString EnvPath = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_TOOL_PROMPTS_FIXTURE"));
		if (!EnvPath.IsEmpty())
		{
			return EnvPath;
		}
		return FPaths::Combine(FPaths::ProjectDir(), TEXT("tests"), TEXT("tool-call-prompts.generated.json"));
	}

	static bool LoadPromptCases(TArray<FPromptCase>& OutCases, FString& OutError)
	{
		OutCases.Reset();
		OutError.Reset();

		const FString FixturePath = GetFixturePath();
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *FixturePath))
		{
			OutError = FString::Printf(TEXT("Could not read fixture: %s"), *FixturePath);
			return false;
		}

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("Invalid JSON fixture: %s"), *FixturePath);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Cases = nullptr;
		if (!Root->TryGetArrayField(TEXT("cases"), Cases) || !Cases)
		{
			OutError = TEXT("Fixture missing cases[]");
			return false;
		}

		for (const TSharedPtr<FJsonValue>& V : *Cases)
		{
			const TSharedPtr<FJsonObject> Obj = V.IsValid() ? V->AsObject() : nullptr;
			if (!Obj.IsValid())
			{
				continue;
			}

			FPromptCase C;
			Obj->TryGetStringField(TEXT("prompt"), C.Prompt);
			Obj->TryGetStringField(TEXT("mode"), C.Mode);
			if (C.Prompt.IsEmpty())
			{
				continue;
			}
			if (C.Mode.IsEmpty())
			{
				C.Mode = TEXT("agent");
			}

			const TArray<TSharedPtr<FJsonValue>>* Expected = nullptr;
			if (Obj->TryGetArrayField(TEXT("expected_tool_calls"), Expected) && Expected)
			{
				for (const TSharedPtr<FJsonValue>& E : *Expected)
				{
					if (E.IsValid() && E->Type == EJson::String)
					{
						const FString ToolId = E->AsString();
						if (!ToolId.IsEmpty())
						{
							C.ExpectedToolCalls.Add(ToolId);
						}
					}
				}
			}

			if (C.ExpectedToolCalls.Num() > 0)
			{
				OutCases.Add(MoveTemp(C));
			}
		}

		if (OutCases.Num() == 0)
		{
			OutError = TEXT("Fixture contained no runnable prompt cases");
			return false;
		}
		return true;
	}

	struct FHeadlessPromptResponse
	{
		TArray<FString> ToolCalls;
		TArray<FString> AvailableToolsForRound;
	};

	class FHeadlessPromptToolService
	{
	public:
		FHeadlessPromptToolService()
			: Persistence(MakeUnique<FUnrealAiPersistenceStub>())
			, Context(MakeUnique<FUnrealAiContextService>(Persistence.Get()))
			, Profiles(MakeUnique<FUnrealAiModelProfileRegistry>(Persistence.Get()))
		{
			Catalog.LoadFromPlugin();
			Profiles->Reload();
			Conversation = MakeUnique<FUnrealAiConversationStore>(Persistence.Get());
		}

		bool IsReady(FString& OutError) const
		{
			if (!Persistence || !Context || !Profiles || !Conversation)
			{
				OutError = TEXT("Headless service dependencies were not created");
				return false;
			}
			if (!Catalog.IsLoaded())
			{
				OutError = TEXT("Tool catalog failed to load");
				return false;
			}
			return true;
		}

		static EUnrealAiAgentMode ParseMode(const FString& InMode)
		{
			if (InMode.Equals(TEXT("ask"), ESearchCase::IgnoreCase))
			{
				return EUnrealAiAgentMode::Ask;
			}
			if (InMode.Equals(TEXT("orchestrate"), ESearchCase::IgnoreCase))
			{
				return EUnrealAiAgentMode::Orchestrate;
			}
			return EUnrealAiAgentMode::Agent;
		}

		FHeadlessPromptResponse RunPrompt(const FString& Prompt, const FString& Mode)
		{
			FHeadlessPromptResponse R;

			Context->LoadOrCreate(ProjectId, ThreadId);
			Conversation->LoadOrCreate(ProjectId, ThreadId);

			FUnrealAiConversationMessage User;
			User.Role = TEXT("user");
			User.Content = Prompt;
			Conversation->GetMessagesMutable().Add(User);

			FUnrealAiAgentTurnRequest Req;
			Req.ProjectId = ProjectId;
			Req.ThreadId = ThreadId;
			Req.Mode = ParseMode(Mode);
			Req.UserText = Prompt;
			Req.ModelProfileId = Profiles->GetDefaultModelId();

			FUnrealAiLlmRequest LlmReq;
			FString Err;
			TArray<FString> ContextUserMessages;
			const bool bBuilt = UnrealAiTurnLlmRequestBuilder::Build(
				Req,
				1,
				16,
				Context.Get(),
				Profiles.Get(),
				&Catalog,
				Conversation.Get(),
				4,
				LlmReq,
				ContextUserMessages,
				Err);

			if (!bBuilt)
			{
				return R;
			}

			ParseAvailableTools(LlmReq.ToolsJsonArray, R.AvailableToolsForRound);
			for (const FString& ToolId : R.AvailableToolsForRound)
			{
				if (Prompt.Contains(ToolId, ESearchCase::IgnoreCase))
				{
					R.ToolCalls.Add(ToolId);
				}
			}

			if (R.ToolCalls.Num() > 0)
			{
				FUnrealAiConversationMessage Assistant;
				Assistant.Role = TEXT("assistant");
				Assistant.Content = TEXT("Headless prompt-service planned tool calls.");
				for (const FString& ToolId : R.ToolCalls)
				{
					FUnrealAiToolCallSpec Tc;
					Tc.Id = FString::Printf(TEXT("tc_%s"), *ToolId);
					Tc.Name = ToolId;
					Tc.ArgumentsJson = TEXT("{}");
					Assistant.ToolCalls.Add(Tc);
				}
				Conversation->GetMessagesMutable().Add(Assistant);

				const FContextRecordPolicy Policy;
				for (const FString& ToolId : R.ToolCalls)
				{
					Context->RecordToolResult(FName(*ToolId), TEXT("headless test placeholder result"), Policy);
				}
				Context->SaveNow(ProjectId, ThreadId);
				Conversation->SaveNow();
			}

			return R;
		}

	private:
		static void ParseAvailableTools(const FString& ToolsJsonArray, TArray<FString>& OutToolIds)
		{
			OutToolIds.Reset();
			if (ToolsJsonArray.IsEmpty())
			{
				return;
			}

			TArray<TSharedPtr<FJsonValue>> Arr;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ToolsJsonArray);
			if (!FJsonSerializer::Deserialize(Reader, Arr))
			{
				return;
			}

			for (const TSharedPtr<FJsonValue>& V : Arr)
			{
				const TSharedPtr<FJsonObject> Obj = V.IsValid() ? V->AsObject() : nullptr;
				if (!Obj.IsValid())
				{
					continue;
				}
				const TSharedPtr<FJsonObject>* Func = nullptr;
				if (!Obj->TryGetObjectField(TEXT("function"), Func) || !Func || !(*Func).IsValid())
				{
					continue;
				}
				FString Name;
				if ((*Func)->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
				{
					OutToolIds.Add(Name);
				}
			}
		}

	private:
		const FString ProjectId = TEXT("headless_tool_prompt_tests");
		const FString ThreadId = TEXT("headless_constant_chat_thread");

		TUniquePtr<FUnrealAiPersistenceStub> Persistence;
		TUniquePtr<FUnrealAiContextService> Context;
		TUniquePtr<FUnrealAiModelProfileRegistry> Profiles;
		FUnrealAiToolCatalog Catalog;
		TUniquePtr<FUnrealAiConversationStore> Conversation;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiHeadlessPromptToolRoutingTest,
	"UnrealAiEditor.Tools.HeadlessPromptToolRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiHeadlessPromptToolRoutingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace UnrealAiHeadlessToolPromptTestsPriv;

	// Dispatch surface exposes only `unreal_ai_dispatch` in tools[]; this test expects per-tool function names in tools[].
	FPlatformMisc::SetEnvironmentVar(TEXT("UNREAL_AI_TOOL_SURFACE"), TEXT("native"));

	TArray<FPromptCase> Cases;
	FString LoadErr;
	if (!LoadPromptCases(Cases, LoadErr))
	{
		AddError(LoadErr);
		return false;
	}

	FHeadlessPromptToolService Service;
	FString ReadyErr;
	if (!Service.IsReady(ReadyErr))
	{
		AddError(ReadyErr);
		return false;
	}

	int32 Failures = 0;
	for (const FPromptCase& C : Cases)
	{
		const FHeadlessPromptResponse R = Service.RunPrompt(C.Prompt, C.Mode);

		for (const FString& ExpectedTool : C.ExpectedToolCalls)
		{
			const bool bExpectedToolAvailable = R.AvailableToolsForRound.Contains(ExpectedTool);
			if (!bExpectedToolAvailable)
			{
				AddError(FString::Printf(
					TEXT("Prompt missing expected tool in available set. tool=%s prompt=%s"),
					*ExpectedTool,
					*C.Prompt));
				++Failures;
				continue;
			}

			const bool bToolCalled = R.ToolCalls.Contains(ExpectedTool);
			if (!bToolCalled)
			{
				AddError(FString::Printf(
					TEXT("Expected tool call not emitted. tool=%s prompt=%s"),
					*ExpectedTool,
					*C.Prompt));
				++Failures;
			}
		}
	}

	TestEqual(TEXT("Headless prompt routing failures"), Failures, 0);
	return Failures == 0;
}

#endif // WITH_DEV_AUTOMATION_TESTS
