#include "Harness/UnrealAiTurnLlmRequestBuilder.h"

#include "Context/IAgentContextService.h"
#include "Harness/FUnrealAiConversationStore.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Harness/ILlmTransport.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "HAL/PlatformMisc.h"
#include "Prompt/UnrealAiPromptBuilder.h"
#include "Tools/UnrealAiToolCatalog.h"
#include "UnrealAiEditorSettings.h"

namespace UnrealAiTurnLlmRequestBuilderPriv
{
	static int32 EstimateCharsForMessages(const TArray<FUnrealAiConversationMessage>& Ms)
	{
		int32 N = 0;
		for (const FUnrealAiConversationMessage& M : Ms)
		{
			N += M.Content.Len();
			for (const FUnrealAiToolCallSpec& T : M.ToolCalls)
			{
				N += T.ArgumentsJson.Len() + T.Name.Len();
			}
		}
		return N;
	}
}

bool UnrealAiTurnLlmRequestBuilder::Build(
	const FUnrealAiAgentTurnRequest& Request,
	int32 LlmRound,
	int32 MaxLlmRounds,
	IAgentContextService* ContextService,
	FUnrealAiModelProfileRegistry* Profiles,
	FUnrealAiToolCatalog* Catalog,
	FUnrealAiConversationStore* Conv,
	int32 CharPerTokenApprox,
	FUnrealAiLlmRequest& OutRequest,
	TArray<FString>& OutContextUserMessages,
	FString& OutError)
{
	OutContextUserMessages.Reset();
	if (!ContextService || !Profiles || !Catalog || !Conv)
	{
		OutError = TEXT("Turn builder: missing dependency.");
		return false;
	}

	FUnrealAiModelCapabilities Caps;
	Profiles->GetEffectiveCapabilities(Request.ModelProfileId, Caps);

	FAgentContextBuildOptions Opt;
	Opt.Mode = Request.Mode;
	Opt.UserMessageForComplexity = Request.UserText;
	Opt.bModelSupportsImages = Caps.bSupportsImages;
	Opt.ContextBuildInvocationReason = TEXT("request_build");
	const FAgentContextBuildResult Built = ContextService->BuildContextWindow(Opt);
	OutContextUserMessages = Built.UserVisibleMessages;

	FUnrealAiPromptAssembleParams P;
	P.Built = &Built;
	P.Mode = Request.Mode;
	P.LlmRound = LlmRound;
	P.MaxLlmRounds = MaxLlmRounds;
	P.ThreadId = Request.ThreadId;
	{
		const FString ProjectId = Request.ProjectId;
		const FString Tid = Request.ThreadId;
		if (const FAgentContextState* St = ContextService->GetState(ProjectId, Tid))
		{
			P.bIncludeExecutionSubturnChunk = !St->ActiveTodoPlanJson.IsEmpty();
		}
	}
	P.bIncludeOrchestrationChunk = (Request.Mode == EUnrealAiAgentMode::Orchestrate);

	const FString SystemContent = UnrealAiPromptBuilder::BuildSystemDeveloperContent(P);

	if (!Catalog->IsLoaded())
	{
		const_cast<FUnrealAiToolCatalog*>(Catalog)->LoadFromPlugin();
	}
	FUnrealAiToolPackOptions PackOpt;
	{
		const FString PackEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_TOOL_PACK")).TrimStartAndEnd().ToLower();
		if (PackEnv == TEXT("core"))
		{
			PackOpt.bRestrictToCorePack = true;
		}
		const FString ExtraEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_TOOL_PACK_EXTRA"));
		if (!ExtraEnv.IsEmpty())
		{
			TArray<FString> RawParts;
			ExtraEnv.ParseIntoArray(RawParts, TEXT(","), true);
			for (FString& Part : RawParts)
			{
				Part.TrimStartAndEndInline();
				if (!Part.IsEmpty())
				{
					PackOpt.AdditionalToolIds.Add(Part);
				}
			}
		}
	}
	const FUnrealAiToolPackOptions* PackPtr = PackOpt.bRestrictToCorePack ? &PackOpt : nullptr;

	FString SystemAugmented = SystemContent;
	FString ToolsJson;
	// Default: dispatch (tiny tools[] + index in system text). Set UNREAL_AI_TOOL_SURFACE=native for full per-tool JSON Schema.
	const FString SurfaceEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_TOOL_SURFACE")).TrimStartAndEnd();
	const bool bWantDispatchSurface = !SurfaceEnv.Equals(TEXT("native"), ESearchCase::IgnoreCase);
	bool bUsingDispatchSurface = false;
	if (Request.Mode != EUnrealAiAgentMode::Orchestrate && Caps.bSupportsNativeTools && bWantDispatchSurface)
	{
		FString ToolIndexMd;
		Catalog->BuildCompactToolIndexAppendix(Request.Mode, Caps, PackPtr, ToolIndexMd);
		if (!ToolIndexMd.IsEmpty())
		{
			Catalog->BuildUnrealAiDispatchToolsJson(Request.Mode, Caps, PackPtr, ToolsJson);
			bUsingDispatchSurface = true;
			SystemAugmented += TEXT("\n\n---\n\n## Unreal tool index (`unreal_ai_dispatch`)\n");
			SystemAugmented += TEXT("Use the function tool **`unreal_ai_dispatch`** with ");
			SystemAugmented += TEXT("`{\"tool_id\":\"<id>\",\"arguments\":{ ... }}` ");
			SystemAugmented += TEXT("(`arguments` must match the JSON schema for that tool in the catalog). ");
			SystemAugmented += TEXT("Enabled tools for this session:\n\n");
			SystemAugmented += ToolIndexMd;
		}
	}
	if (!bUsingDispatchSurface)
	{
		Catalog->BuildLlmToolsJsonArrayForMode(Request.Mode, Caps, PackPtr, ToolsJson);
	}

	TArray<FUnrealAiConversationMessage> ApiMsgs;
	FUnrealAiConversationMessage Sys;
	Sys.Role = TEXT("system");
	Sys.Content = SystemAugmented;
	ApiMsgs.Add(Sys);
	ApiMsgs.Append(Conv->GetMessages());

	const int32 MaxChars = FMath::Max(4096, Caps.MaxContextTokens * CharPerTokenApprox);
	while (UnrealAiTurnLlmRequestBuilderPriv::EstimateCharsForMessages(ApiMsgs) > MaxChars && ApiMsgs.Num() > 2)
	{
		ApiMsgs.RemoveAt(1);
	}

	if (Request.Mode == EUnrealAiAgentMode::Orchestrate)
	{
		// Planner pass must be DAG-only in v1; executor child turns run in Agent mode.
		ToolsJson = TEXT("[]");
	}
	else
	{
		// Wiring / latency checks only: full tool array is often 50k+ chars and dominates upload + server time vs. curl smoke tests.
		bool bExplicitOmitTools = false;
		const FString OmitToolsEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_HARNESS_OMIT_TOOLS"));
		if (!OmitToolsEnv.IsEmpty())
		{
			const FString O = OmitToolsEnv.TrimStartAndEnd().ToLower();
			if (O == TEXT("1") || O == TEXT("true") || O == TEXT("yes"))
			{
				ToolsJson = TEXT("[]");
				bExplicitOmitTools = true;
			}
		}
		// Guardrail: in normal agent turns we should expose at least one callable tool.
		if (!bExplicitOmitTools
			&& Request.Mode != EUnrealAiAgentMode::Ask
			&& ToolsJson.TrimStartAndEnd() == TEXT("[]"))
		{
			OutError = TEXT("Turn builder: empty tool surface in non-ask mode (set UNREAL_AI_HARNESS_OMIT_TOOLS=1 only for transport smoke tests).");
			return false;
		}
	}

	OutRequest = FUnrealAiLlmRequest();
	OutRequest.ApiModelName = Caps.ModelIdForApi;
	OutRequest.Messages = MoveTemp(ApiMsgs);
	OutRequest.ToolsJsonArray = MoveTemp(ToolsJson);
	OutRequest.bStream = GetDefault<UUnrealAiEditorSettings>()->bStreamLlmChat;
	// UNREAL_AI_LLM_STREAM: 0/false/off = non-streaming (single JSON body, often more reliable with UE HTTP); 1/true = stream
	{
		const FString StreamOverride = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_LLM_STREAM"));
		if (!StreamOverride.IsEmpty())
		{
			const FString S = StreamOverride.TrimStartAndEnd().ToLower();
			if (S == TEXT("0") || S == TEXT("false") || S == TEXT("no") || S == TEXT("off"))
			{
				OutRequest.bStream = false;
			}
			else if (S == TEXT("1") || S == TEXT("true") || S == TEXT("yes") || S == TEXT("on"))
			{
				OutRequest.bStream = true;
			}
		}
	}
	OutRequest.MaxOutputTokens = FMath::Clamp(Caps.MaxOutputTokens, 1, 128000);

	if (Profiles->HasAnyConfiguredApiKey())
	{
		if (!Profiles->TryResolveApiForModel(Request.ModelProfileId, OutRequest.ApiBaseUrl, OutRequest.ApiKey))
		{
			OutError = TEXT("Could not resolve API base URL + key for this model. Check providers[] and per-model providerId in settings.");
			return false;
		}
	}

	return true;
}
