#pragma once

#include "Context/AgentContextTypes.h"
#include "Context/UnrealAiContextRankingPolicy.h"

class IUnrealAiMemoryService;
struct FUnrealAiRetrievalSnippet;

namespace UnrealAiContextCandidates
{
	struct FFeatures
	{
		float MentionHit = 0.f;
		float HeuristicSemantic = 0.f;
		float Recency = 0.f;
		float FreshnessReliability = 0.f;
		float SafetyRisk = 0.f;
		float ActiveBonus = 0.f;
		float ThreadOverlayBonus = 0.f;
		float Frequency = 0.f;
		float VectorSimilarity = 0.f;
		float ThreadScope = 0.f;
	};

	struct FScoreBreakdown
	{
		float Base = 0.f;
		float MentionHit = 0.f;
		float HeuristicSemantic = 0.f;
		float Recency = 0.f;
		float FreshnessReliability = 0.f;
		float SafetyPenalty = 0.f;
		float ActiveBonus = 0.f;
		float ThreadOverlayBonus = 0.f;
		float Frequency = 0.f;
		float VectorSimilarity = 0.f;
		float ThreadScope = 0.f;
		float Total = 0.f;
	};

	struct FContextCandidateEnvelope
	{
		UnrealAiContextRankingPolicy::ECandidateType Type = UnrealAiContextRankingPolicy::ECandidateType::EditorSnapshotField;
		FString SourceId;
		FString Payload;
		FString RenderedText;
		int32 TokenCostEstimate = 0;
		FFeatures Features;
		FScoreBreakdown Score;
		bool bDropped = false;
		FString DropReason;
		bool bImageLikeAttachment = false;
	};

	struct FUnifiedContextBuildResult
	{
		FString ContextBlock;
		bool bTruncated = false;
		TArray<FString> Warnings;
		TArray<FString> TraceLines;
		TArray<FContextCandidateEnvelope> Packed;
		TArray<FContextCandidateEnvelope> Dropped;
		// Decision-log helpers (so we can explain why certain caps applied).
		int32 PromptCharCount = 0;
		int32 PromptTokenCount = 0;
		bool bShortPrompt = false;
		int32 MemorySnippetCapApplied = 0;
		int32 SoftBudgetCharsApplied = 0;
		int32 MaxPackedCandidatesApplied = 0;
	};

	void CollectCandidates(
		const FAgentContextState& State,
		const FAgentContextBuildOptions& Options,
		IUnrealAiMemoryService* MemoryService,
		const TArray<FUnrealAiRetrievalSnippet>* RetrievalSnippets,
		TArray<FContextCandidateEnvelope>& OutCandidates);

	void FilterHardPolicy(
		TArray<FContextCandidateEnvelope>& Candidates,
		const FAgentContextBuildOptions& Options);

	void ScoreCandidates(
		TArray<FContextCandidateEnvelope>& Candidates,
		const FAgentContextBuildOptions& Options);

	void PackCandidatesUnderBudget(
		TArray<FContextCandidateEnvelope>& Candidates,
		int32 BudgetChars,
		const FAgentContextBuildOptions& Options,
		FUnifiedContextBuildResult& OutResult);

	FUnifiedContextBuildResult BuildUnifiedContext(
		const FAgentContextState& State,
		const FAgentContextBuildOptions& Options,
		IUnrealAiMemoryService* MemoryService,
		const TArray<FUnrealAiRetrievalSnippet>* RetrievalSnippets,
		int32 BudgetChars);
}
