#pragma once

#include "CoreMinimal.h"

class IAgentContextService;
class FUnrealAiModelProfileRegistry;
class FUnrealAiToolCatalog;
class FUnrealAiConversationStore;
struct FUnrealAiAgentTurnRequest;
struct FUnrealAiLlmRequest;
struct FUnrealAiToolSurfaceTelemetry;

/**
 * Assembles a chat request for ILlmTransport: prompt chunks + context + conversation + tools + model/endpoint fields.
 * Matches the common HTTPS chat-completions JSON shape used by multiple LLM vendors and aggregators.
 */
namespace UnrealAiTurnLlmRequestBuilder
{
	bool Build(
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
		FUnrealAiToolSurfaceTelemetry* OutToolSurfaceTelemetry = nullptr);
}
