#include "Harness/UnrealAiTurnLlmRequestBuilder.h"

#include "Context/IAgentContextService.h"
#include "Harness/FUnrealAiConversationStore.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Harness/ILlmTransport.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Misc/Paths.h"
#include "Misc/UnrealAiRuntimeDefaults.h"
#include "Misc/UnrealAiWaitTimePolicy.h"
#include "Prompt/UnrealAiPromptBuilder.h"
#include "Tools/UnrealAiAgentToolGate.h"
#include "Tools/UnrealAiBlueprintBuilderToolSurface.h"
#include "Tools/UnrealAiEnvironmentBuilderToolSurface.h"
#include "Tools/UnrealAiToolCatalog.h"
#include "Tools/UnrealAiToolSurfacePipeline.h"
#include "UnrealAiEditorSettings.h"
#include "Observability/UnrealAiGameThreadPerf.h"

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

	static bool AssistantHasSerializableToolCalls(const FUnrealAiConversationMessage& Am)
	{
		for (const FUnrealAiToolCallSpec& T : Am.ToolCalls)
		{
			if (!T.Name.TrimStartAndEnd().IsEmpty())
			{
				return true;
			}
		}
		return false;
	}

	/** Drop oldest messages after system without orphaning role=tool rows (OpenAI 400 if tool lacks prior assistant tool_calls). */
	static void TrimApiMessagesForContextBudget(TArray<FUnrealAiConversationMessage>& ApiMsgs, const int32 MaxChars)
	{
		while (EstimateCharsForMessages(ApiMsgs) > MaxChars && ApiMsgs.Num() > 2)
		{
			if (ApiMsgs[1].Role == TEXT("tool"))
			{
				ApiMsgs.RemoveAt(1);
				continue;
			}
			if (ApiMsgs[1].Role == TEXT("assistant") && AssistantHasSerializableToolCalls(ApiMsgs[1]))
			{
				int32 RemoveCount = 1;
				while (1 + RemoveCount < ApiMsgs.Num() && ApiMsgs[1 + RemoveCount].Role == TEXT("tool"))
				{
					++RemoveCount;
				}
				ApiMsgs.RemoveAt(1, RemoveCount);
				continue;
			}
			// Assistant with no serializable tool_calls but followed by tool rows: drop those tools too
			// (otherwise HTTP 400: tool without preceding assistant tool_calls in the trimmed payload).
			if (ApiMsgs[1].Role == TEXT("assistant") && !AssistantHasSerializableToolCalls(ApiMsgs[1]))
			{
				int32 RemoveCount = 1;
				while (1 + RemoveCount < ApiMsgs.Num() && ApiMsgs[1 + RemoveCount].Role == TEXT("tool"))
				{
					++RemoveCount;
				}
				if (RemoveCount > 1)
				{
					ApiMsgs.RemoveAt(1, RemoveCount);
					continue;
				}
			}
			ApiMsgs.RemoveAt(1);
		}
	}

}

bool UnrealAiTurnLlmRequestBuilder::Build(
	FUnrealAiAgentTurnRequest& Request,
	int32 LlmRound,
	int32 MaxLlmRounds,
	const FString& RetrievalTurnKey,
	IAgentContextService* ContextService,
	FUnrealAiModelProfileRegistry* Profiles,
	FUnrealAiToolCatalog* Catalog,
	FUnrealAiConversationStore* Conv,
	int32 CharPerTokenApprox,
	FUnrealAiLlmRequest& OutRequest,
	TArray<FString>& OutContextUserMessages,
	FString& OutError,
	FUnrealAiToolSurfaceTelemetry* OutToolSurfaceTelemetry)
{
	OutContextUserMessages.Reset();
	if (OutToolSurfaceTelemetry)
	{
		*OutToolSurfaceTelemetry = FUnrealAiToolSurfaceTelemetry();
		OutToolSurfaceTelemetry->ToolSurfaceMode = TEXT("off");
	}
	if (!ContextService || !Profiles || !Catalog || !Conv)
	{
		OutError = TEXT("Turn builder: missing dependency.");
		return false;
	}

	UNREALAI_GT_PERF_SCOPE("Harness.TurnLlmRequestBuilder.Build");

	FUnrealAiModelCapabilities Caps;
	Profiles->GetEffectiveCapabilities(Request.ModelProfileId, Caps);

	FAgentContextBuildOptions Opt;
	Opt.Mode = Request.Mode;
	Opt.UserMessageForComplexity = Request.ContextComplexityUserText.IsEmpty() ? Request.UserText : Request.ContextComplexityUserText;
	Opt.RetrievalTurnKey = RetrievalTurnKey;
	Opt.bModelSupportsImages = Caps.bSupportsImages;
	Opt.ContextBuildInvocationReason = TEXT("request_build");
	FAgentContextBuildResult Built = ContextService->BuildContextWindow(Opt);
	OutContextUserMessages = Built.UserVisibleMessages;

	const FString EffectiveUser =
		Request.ContextComplexityUserText.IsEmpty() ? Request.UserText : Request.ContextComplexityUserText;

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
	if (Request.Mode == EUnrealAiAgentMode::Agent && Request.ThreadId.Contains(TEXT("_plan_")))
	{
		P.bIncludePlanNodeExecutionChunk = true;
	}
	P.bIncludePlanDagChunk = (Request.Mode == EUnrealAiAgentMode::Plan);
	P.bBlueprintBuilderMode = Request.bBlueprintBuilderTurn;
	P.BlueprintBuilderTargetKind = Request.BlueprintBuilderTargetKind;
	P.bEnvironmentBuilderMode = Request.bEnvironmentBuilderTurn;
	P.EnvironmentBuilderTargetKind = Request.EnvironmentBuilderTargetKind;

	const bool bResumeChunk = Request.bInjectBlueprintBuilderResumeChunk;
	Request.bInjectBlueprintBuilderResumeChunk = false;
	P.bInjectBlueprintBuilderResumeChunk =
		bResumeChunk && !Request.bBlueprintBuilderTurn && Request.Mode == EUnrealAiAgentMode::Agent;

	const bool bEnvResumeChunk = Request.bInjectEnvironmentBuilderResumeChunk;
	Request.bInjectEnvironmentBuilderResumeChunk = false;
	P.bInjectEnvironmentBuilderResumeChunk =
		bEnvResumeChunk && !Request.bEnvironmentBuilderTurn && Request.Mode == EUnrealAiAgentMode::Agent;

	FString SystemContent;
	{
		UNREALAI_GT_PERF_SCOPE("Harness.TurnLlmRequestBuilder.Prompt");
		SystemContent = UnrealAiPromptBuilder::BuildSystemDeveloperContent(P);
	}
	FString SystemAugmented = SystemContent;
	{
		const FString UProjectBase = FPaths::GetCleanFilename(FPaths::GetProjectFilePath());
		if (!UProjectBase.IsEmpty())
		{
			SystemAugmented += FString::Printf(
				TEXT("\n\n## Project workspace (factual)\n- **Project manifest file (basename):** `%s` — for `project_file_read_text` / manifest reads, use this exact basename (not placeholder names from documentation examples).\n"),
				*UProjectBase);
		}
	}

	if (!Catalog->IsLoaded())
	{
		const_cast<FUnrealAiToolCatalog*>(Catalog)->LoadFromPlugin();
	}
	FUnrealAiToolPackOptions PackOpt;
	{
		if (UnrealAiRuntimeDefaults::ToolPackRestrictToCore)
		{
			PackOpt.bRestrictToCorePack = true;
		}
		{
			const FString ExtraEnv(UnrealAiRuntimeDefaults::ToolPackExtraCommaSeparated);
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
	}
	const FUnrealAiToolPackOptions* PackPtr = PackOpt.bRestrictToCorePack ? &PackOpt : nullptr;

	const auto ToolSurfaceFilter = [&](const FString& Tid) -> bool
	{
		return UnrealAiAgentToolGate::PassesToolSurfaceFilter(Request, Tid, Catalog);
	};

	FString ToolsJson;
	const bool bWantDispatchSurface = UnrealAiRuntimeDefaults::ToolSurfaceUseDispatch && !Request.bForceNativeToolSurface;
	bool bUsingDispatchSurface = false;
	{
		UNREALAI_GT_PERF_SCOPE("Harness.TurnLlmRequestBuilder.ToolSurface");
		if (Request.Mode != EUnrealAiAgentMode::Plan && Caps.bSupportsNativeTools && bWantDispatchSurface)
		{
		int32 BlueprintBuilderAppendixBudgetChars = 0;
		if (Request.bBlueprintBuilderTurn && Request.Mode == EUnrealAiAgentMode::Agent)
		{
			FString BpBudgetErr;
			if (!UnrealAiBlueprintBuilderToolSurface::TryComputeAppendixBudgetFromModelContext(
					Caps.MaxContextTokens,
					CharPerTokenApprox,
					BlueprintBuilderAppendixBudgetChars,
					&BpBudgetErr))
			{
				OutError = BpBudgetErr;
				return false;
			}
		}
		int32 EnvironmentBuilderAppendixBudgetChars = 0;
		if (Request.bEnvironmentBuilderTurn && Request.Mode == EUnrealAiAgentMode::Agent)
		{
			FString EnvBudgetErr;
			if (!UnrealAiEnvironmentBuilderToolSurface::TryComputeAppendixBudgetFromModelContext(
					Caps.MaxContextTokens,
					CharPerTokenApprox,
					EnvironmentBuilderAppendixBudgetChars,
					&EnvBudgetErr))
			{
				OutError = EnvBudgetErr;
				return false;
			}
		}
		FString ToolIndexMd;
		FUnrealAiToolSurfaceTelemetry Tel;
		const bool bTiered = UnrealAiToolSurfacePipeline::TryBuildTieredToolSurface(
			Request,
			LlmRound,
			ContextService,
			Catalog,
			Caps,
			PackPtr,
			true,
			ToolIndexMd,
			Tel,
			BlueprintBuilderAppendixBudgetChars,
			EnvironmentBuilderAppendixBudgetChars);
		if (!bTiered)
		{
			Catalog->BuildCompactToolIndexAppendix(Request.Mode, Caps, PackPtr, ToolSurfaceFilter, ToolIndexMd);
		}
		if (OutToolSurfaceTelemetry)
		{
			*OutToolSurfaceTelemetry = Tel;
		}
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
			Catalog->BuildLlmToolsJsonArrayForMode(Request.Mode, Caps, PackPtr, ToolSurfaceFilter, ToolsJson);
		}
	}

	TArray<FUnrealAiConversationMessage> ApiMsgs;
	FUnrealAiConversationMessage Sys;
	Sys.Role = TEXT("system");
	Sys.Content = SystemAugmented;
	ApiMsgs.Add(Sys);
	ApiMsgs.Append(Conv->GetMessages());

	const int32 MaxChars = FMath::Max(4096, Caps.MaxContextTokens * CharPerTokenApprox);
	UnrealAiTurnLlmRequestBuilderPriv::TrimApiMessagesForContextBudget(ApiMsgs, MaxChars);

	if (Request.Mode == EUnrealAiAgentMode::Plan)
	{
		// Planner pass must be DAG-only in v1; executor child turns run in Agent mode.
		ToolsJson = TEXT("[]");
		OutRequest.HttpTimeoutOverrideSec = UnrealAiWaitTime::PlannerHttpRequestTimeoutSec;
	}
	else
	{
		// Wiring / latency checks only: full tool array is often 50k+ chars and dominates upload + server time vs. curl smoke tests.
		bool bExplicitOmitTools = false;
		if (UnrealAiRuntimeDefaults::HarnessOmitTools)
		{
			ToolsJson = TEXT("[]");
			bExplicitOmitTools = true;
		}
		// Guardrail: in normal agent turns we should expose at least one callable tool.
		if (!bExplicitOmitTools
			&& Request.Mode != EUnrealAiAgentMode::Ask
			&& ToolsJson.TrimStartAndEnd() == TEXT("[]"))
		{
			OutError = TEXT("Turn builder: empty tool surface in non-ask mode (enable HarnessOmitTools in UnrealAiRuntimeDefaults only for transport smoke tests).");
			return false;
		}
	}

	OutRequest = FUnrealAiLlmRequest();
	OutRequest.ApiModelName = Caps.ModelIdForApi;
	OutRequest.Messages = MoveTemp(ApiMsgs);
	OutRequest.ToolsJsonArray = MoveTemp(ToolsJson);
	OutRequest.bStream = GetDefault<UUnrealAiEditorSettings>()->bStreamLlmChat;
	OutRequest.MaxOutputTokens = FMath::Clamp(Caps.MaxOutputTokens, 1, 128000);
	if (Request.Mode == EUnrealAiAgentMode::Plan)
	{
		OutRequest.HttpTimeoutOverrideSec = UnrealAiWaitTime::PlannerHttpRequestTimeoutSec;
	}

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
