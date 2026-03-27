#pragma once

#include "Context/AgentContextTypes.h"

/**
 * Single source of truth for all manual context-ranking knobs.
 *
 * Rationale:
 * - We keep every tunable scalar in one place so product iteration does not require code spelunking.
 * - Values intentionally favor "what the user is actively touching now" over stale but verbose history.
 * - Values are additive and easy to reason about in debug traces.
 *
 * TODO(embeddings): Replace/augment heuristic semantic relevance with embedding/vector retrieval scores.
 */
namespace UnrealAiContextRankingPolicy
{
	enum class ECandidateType : uint8
	{
		EngineHeader,
		RecentTab,
		Attachment,
		ToolResult,
		EditorSnapshotField,
		MemorySnippet,
		RetrievalSnippet,
		TodoState,
		OrchestrateState,
	};

	struct FScoreWeights
	{
		float MentionHit = 65.f;
		// Slightly lower than explicit mention; overlap is noisy but useful.
		float HeuristicSemantic = 45.f;
		float Recency = 40.f;
		float FreshnessReliability = 18.f;
		float SafetyPenalty = -120.f;
		float ActiveBonus = 30.f;
		float ThreadOverlayBonus = 16.f;
		float Frequency = 1.5f;
		float VectorSimilarity = 50.f;
		float ThreadScope = 24.f;
	};

	struct FPerTypeBudgetCaps
	{
		int32 MaxRecentTab = 12;
		int32 MaxAttachment = 10;
		int32 MaxToolResult = 8;
		int32 MaxEditorSnapshotField = 20;
		int32 MaxMemorySnippet = 6;
		int32 MaxRetrievalSnippet = 6;
		// For short/direct user prompts we want live editor state to dominate over durable
		// memory to avoid spending most of the token budget on historical snippets.
		int32 MaxMemorySnippetShortPrompt = 2;
		int32 MaxTodoState = 4;
		int32 MaxOrchestrateState = 10;
	};

	inline constexpr int32 DefaultHardMaxCandidates = 256;
	inline constexpr int32 MinBudgetReserveChars = 180;
	// Soft packing constraints:
	// Even when we have plenty of budget, avoid filling the entire context window just because there are
	// many high-signal candidates. Prefer the top K by score and leave slack for the live conversation.
	inline constexpr float SoftContextFillFraction = 0.65f;
	inline constexpr int32 MinSoftBudgetChars = 1800;
	inline constexpr int32 MaxPackedCandidatesSoft = 32;
	inline constexpr int32 MaxMemoryCandidatesScanned = 24;
	inline constexpr float MinMemoryCandidateScoreToPack = 8.0f;
	inline constexpr float MinRetrievalCandidateScoreToPack = 6.0f;
	inline constexpr float RetrievalInThreadScopeBoost = 1.0f;
	inline constexpr float RetrievalCrossThreadPenalty = -1.0f;
	// Heuristic: when the user prompt is short/direct, treat it as a "short prompt" turn.
	// This is intentionally based on token-ish splitting and raw length (not embeddings).
	// Keep permissive: harness prompts include extra constraints even for simple tasks.
	inline constexpr int32 ShortPromptUserTokenThreshold = 48;
	inline constexpr int32 ShortPromptCharThreshold = 512;

	inline FScoreWeights GetScoreWeights()
	{
		return FScoreWeights{};
	}

	inline FPerTypeBudgetCaps GetPerTypeBudgetCaps()
	{
		return FPerTypeBudgetCaps{};
	}

	inline float GetBaseTypeImportance(const ECandidateType T)
	{
		switch (T)
		{
		case ECandidateType::EngineHeader:
			// Always high: engine version is compact and avoids wrong syntax/tooling assumptions.
			return 90.f;
		case ECandidateType::RecentTab:
			// Active/editor-near UI context usually beats long historical memory for next action quality.
			return 85.f;
		case ECandidateType::Attachment:
			// Explicit user-curated artifacts are highly actionable.
			return 80.f;
		case ECandidateType::EditorSnapshotField:
			// Snapshot fields are useful situational glue.
			return 65.f;
		case ECandidateType::ToolResult:
			// Valuable but can get stale/noisy quickly.
			return 55.f;
		case ECandidateType::TodoState:
			// Keeps execution continuity; moderate size and high utility during multi-step tasks.
			return 70.f;
		case ECandidateType::MemorySnippet:
			// Durable learned context; useful but should not dominate live editor state.
			return 68.f;
		case ECandidateType::RetrievalSnippet:
			// Retrieved local code/doc chunks can be high signal when query-matched.
			return 66.f;
		case ECandidateType::OrchestrateState:
			// Important in orchestration mode; generally less critical in simple turns.
			return 60.f;
		default:
			return 40.f;
		}
	}

	inline int32 GetPerTypeCap(const ECandidateType T)
	{
		const FPerTypeBudgetCaps Caps = GetPerTypeBudgetCaps();
		switch (T)
		{
		case ECandidateType::RecentTab: return Caps.MaxRecentTab;
		case ECandidateType::Attachment: return Caps.MaxAttachment;
		case ECandidateType::ToolResult: return Caps.MaxToolResult;
		case ECandidateType::EditorSnapshotField: return Caps.MaxEditorSnapshotField;
		case ECandidateType::MemorySnippet: return Caps.MaxMemorySnippet;
		case ECandidateType::RetrievalSnippet: return Caps.MaxRetrievalSnippet;
		case ECandidateType::TodoState: return Caps.MaxTodoState;
		case ECandidateType::OrchestrateState: return Caps.MaxOrchestrateState;
		case ECandidateType::EngineHeader: return 1;
		default: return 8;
		}
	}

	inline const TCHAR* CandidateTypeName(const ECandidateType T)
	{
		switch (T)
		{
		case ECandidateType::EngineHeader: return TEXT("engine_header");
		case ECandidateType::RecentTab: return TEXT("recent_tab");
		case ECandidateType::Attachment: return TEXT("attachment");
		case ECandidateType::ToolResult: return TEXT("tool_result");
		case ECandidateType::EditorSnapshotField: return TEXT("editor_snapshot_field");
		case ECandidateType::MemorySnippet: return TEXT("memory_snippet");
		case ECandidateType::RetrievalSnippet: return TEXT("retrieval_snippet");
		case ECandidateType::TodoState: return TEXT("todo_state");
		case ECandidateType::OrchestrateState: return TEXT("orchestrate_state");
		default: return TEXT("unknown");
		}
	}
}
