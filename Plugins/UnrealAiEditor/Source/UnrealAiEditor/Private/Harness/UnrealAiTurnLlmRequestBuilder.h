#pragma once

#include "CoreMinimal.h"

class IAgentContextService;
class FUnrealAiModelProfileRegistry;
class FUnrealAiToolCatalog;
class FUnrealAiConversationStore;
struct FUnrealAiAgentTurnRequest;
struct FUnrealAiLlmRequest;

/**
 * Builds a full OpenAI-compatible request: prompt chunks + context + conversation + tools + model/endpoint fields.
 * HTTP POST is performed by ILlmTransport (see FUnrealAiLlmInvocationService).
 */
namespace UnrealAiTurnLlmRequestBuilder
{
	bool Build(
		const FUnrealAiAgentTurnRequest& Request,
		int32 LlmRound,
		int32 MaxLlmRounds,
		IAgentContextService* ContextService,
		FUnrealAiModelProfileRegistry* Profiles,
		FUnrealAiToolCatalog* Catalog,
		FUnrealAiConversationStore* Conv,
		int32 CharPerTokenApprox,
		FUnrealAiLlmRequest& OutRequest,
		FString& OutError);
}
