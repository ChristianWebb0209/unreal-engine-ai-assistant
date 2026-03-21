#include "Prompt/UnrealAiPromptBuilder.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UnrealAiPromptBuilderPriv
{
	static FString PluginPromptsDir()
	{
		if (TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(TEXT("UnrealAiEditor")); P.IsValid())
		{
			return FPaths::Combine(P->GetBaseDir(), TEXT("prompts/chunks"));
		}
		return FString();
	}

	static bool LoadChunk(const FString& FileName, FString& Out)
	{
		const FString Path = FPaths::Combine(PluginPromptsDir(), FileName);
		return FFileHelper::LoadFileToString(Out, *Path);
	}

	static FString AgentModeString(EUnrealAiAgentMode M)
	{
		switch (M)
		{
		case EUnrealAiAgentMode::Ask:
			return TEXT("ask");
		case EUnrealAiAgentMode::Fast:
			return TEXT("fast");
		case EUnrealAiAgentMode::Agent:
		default:
			return TEXT("agent");
		}
	}

	static FString ExtractOperatingModeSection(const FString& Chunk02, EUnrealAiAgentMode Mode)
	{
		const TCHAR* Start = nullptr;
		const TCHAR* End = nullptr;
		switch (Mode)
		{
		case EUnrealAiAgentMode::Ask:
			Start = TEXT("## Mode: Ask (`ask`)");
			End = TEXT("## Mode: Fast (`fast`)");
			break;
		case EUnrealAiAgentMode::Fast:
			Start = TEXT("## Mode: Fast (`fast`)");
			End = TEXT("## Mode: Agent (`agent`)");
			break;
		case EUnrealAiAgentMode::Agent:
		default:
			Start = TEXT("## Mode: Agent (`agent`)");
			End = nullptr;
			break;
		}
		const int32 Is = Chunk02.Find(Start);
		if (Is == INDEX_NONE)
		{
			return Chunk02;
		}
		int32 Ie = Chunk02.Len();
		if (End)
		{
			const int32 Found = Chunk02.Find(End, ESearchCase::CaseSensitive, ESearchDir::FromStart, Is + 1);
			if (Found != INDEX_NONE)
			{
				Ie = Found;
			}
		}
		// Shared title + intro only (before first mode subsection). Using Left(Is) would re-include prior modes for Fast/Agent.
		const int32 FirstModeHeader = Chunk02.Find(TEXT("## Mode:"));
		FString Preamble;
		if (FirstModeHeader != INDEX_NONE && FirstModeHeader <= Is)
		{
			Preamble = Chunk02.Left(FirstModeHeader);
		}
		else
		{
			Preamble = Chunk02.Left(Is);
		}
		FString Section = Chunk02.Mid(Is, Ie - Is);
		return Preamble + Section;
	}

	static void ReplaceAll(FString& S, const FString& From, const FString& To)
	{
		int32 Pos = 0;
		while ((Pos = S.Find(From, ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos)) != INDEX_NONE)
		{
			S = S.Left(Pos) + To + S.Mid(Pos + From.Len());
			Pos += To.Len();
		}
	}

	static void ApplyTemplateTokens(
		FString& Doc,
		const FUnrealAiPromptAssembleParams& P,
		const FAgentContextBuildResult& B)
	{
		ReplaceAll(Doc, TEXT("{{COMPLEXITY_BLOCK}}"), B.ComplexityBlock.IsEmpty() ? TEXT("[Complexity]\n(not available)") : B.ComplexityBlock);
		ReplaceAll(Doc, TEXT("{{CONTEXT_SERVICE_OUTPUT}}"), B.ContextBlock);
		ReplaceAll(Doc, TEXT("{{AGENT_MODE}}"), UnrealAiPromptBuilderPriv::AgentModeString(P.Mode));
		ReplaceAll(Doc, TEXT("{{MAX_PLAN_STEPS}}"), TEXT("12"));
		const FString TodoSum = B.ActiveTodoSummaryText.IsEmpty()
			? FString(TEXT("(no active plan on disk)"))
			: B.ActiveTodoSummaryText;
		ReplaceAll(Doc, TEXT("{{ACTIVE_TODO_SUMMARY}}"), TodoSum);
		const FString RoundStr = FString::Printf(TEXT("%d / %d"), FMath::Max(1, P.LlmRound), FMath::Max(1, P.MaxLlmRounds));
		ReplaceAll(Doc, TEXT("{{CONTINUATION_ROUND}}"), RoundStr);
		const FString Pointer = FString::Printf(
			TEXT("threadId=%s; storage=context.json activeTodoPlan + todoStepsDone"),
			P.ThreadId.IsEmpty() ? TEXT("(unknown)") : *P.ThreadId);
		ReplaceAll(Doc, TEXT("{{PLAN_POINTER}}"), Pointer);
	}
}

FString UnrealAiPromptBuilder::BuildSystemDeveloperContent(const FUnrealAiPromptAssembleParams& Params)
{
	if (!Params.Built)
	{
		return FString();
	}
	const FAgentContextBuildResult& B = *Params.Built;
	FString Acc;

	auto AppendChunk = [&Acc](const TCHAR* File)
	{
		FString C;
		if (UnrealAiPromptBuilderPriv::LoadChunk(File, C))
		{
			if (!Acc.IsEmpty())
			{
				Acc += TEXT("\n\n---\n\n");
			}
			Acc += C;
		}
	};

	AppendChunk(TEXT("01-identity.md"));

	{
		FString C2;
		if (UnrealAiPromptBuilderPriv::LoadChunk(TEXT("02-operating-modes.md"), C2))
		{
			C2 = UnrealAiPromptBuilderPriv::ExtractOperatingModeSection(C2, Params.Mode);
			if (!Acc.IsEmpty())
			{
				Acc += TEXT("\n\n---\n\n");
			}
			Acc += C2;
		}
	}

	AppendChunk(TEXT("03-complexity-and-todo-plan.md"));
	AppendChunk(TEXT("04-tool-calling-contract.md"));
	AppendChunk(TEXT("05-context-and-editor.md"));
	if (Params.bIncludeExecutionSubturnChunk)
	{
		AppendChunk(TEXT("06-execution-subturn.md"));
	}
	AppendChunk(TEXT("07-safety-banned.md"));
	AppendChunk(TEXT("08-output-style.md"));
	if (Params.bIncludeOrchestrationChunk)
	{
		AppendChunk(TEXT("09-orchestration-workers.md"));
	}

	UnrealAiPromptBuilderPriv::ApplyTemplateTokens(Acc, Params, B);

	if (!B.SystemOrDeveloperBlock.IsEmpty())
	{
		Acc = B.SystemOrDeveloperBlock + TEXT("\n\n---\n\n") + Acc;
	}

	if (Acc.IsEmpty())
	{
		return TEXT(
			"You are an AI assistant embedded in the Unreal Editor. Follow the user's instructions, respect tool and safety policies, and prefer read-only tools before writes.");
	}

	return Acc;
}
