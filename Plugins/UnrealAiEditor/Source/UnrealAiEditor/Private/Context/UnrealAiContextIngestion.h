#pragma once

#include "Context/UnrealAiContextCandidates.h"

class IUnrealAiMemoryService;
struct FUnrealAiRetrievalSnippet;
struct FProjectTreeSummary;

namespace UnrealAiContextIngestion
{
	void AppendAllCandidates(
		const FAgentContextState& State,
		const FAgentContextBuildOptions& Options,
		IUnrealAiMemoryService* MemoryService,
		const TArray<FUnrealAiRetrievalSnippet>* RetrievalSnippets,
		const FProjectTreeSummary* ProjectTreeForIngestion,
		TArray<UnrealAiContextCandidates::FContextCandidateEnvelope>& OutCandidates);
}
