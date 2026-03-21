#include "Harness/UnrealAiTurnLlmRequestBuilder.h"

#include "Context/IAgentContextService.h"
#include "Harness/FUnrealAiConversationStore.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Harness/ILlmTransport.h"
#include "Harness/UnrealAiAgentTypes.h"
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
	FString& OutError)
{
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
	const FAgentContextBuildResult Built = ContextService->BuildContextWindow(Opt);

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
	P.bIncludeOrchestrationChunk = (Request.Mode == EUnrealAiAgentMode::Agent);

	const FString SystemContent = UnrealAiPromptBuilder::BuildSystemDeveloperContent(P);

	TArray<FUnrealAiConversationMessage> ApiMsgs;
	FUnrealAiConversationMessage Sys;
	Sys.Role = TEXT("system");
	Sys.Content = SystemContent;
	ApiMsgs.Add(Sys);
	ApiMsgs.Append(Conv->GetMessages());

	const int32 MaxChars = FMath::Max(4096, Caps.MaxContextTokens * CharPerTokenApprox);
	while (UnrealAiTurnLlmRequestBuilderPriv::EstimateCharsForMessages(ApiMsgs) > MaxChars && ApiMsgs.Num() > 2)
	{
		ApiMsgs.RemoveAt(1);
	}

	if (!Catalog->IsLoaded())
	{
		const_cast<FUnrealAiToolCatalog*>(Catalog)->LoadFromPlugin();
	}
	FString ToolsJson;
	Catalog->BuildOpenAiToolsJsonForMode(Request.Mode, Caps, ToolsJson);

	OutRequest = FUnrealAiLlmRequest();
	OutRequest.ApiModelName = Caps.ModelIdForApi;
	OutRequest.Messages = MoveTemp(ApiMsgs);
	OutRequest.ToolsJsonArray = MoveTemp(ToolsJson);
	OutRequest.bStream = GetDefault<UUnrealAiEditorSettings>()->bStreamLlmChat;
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
