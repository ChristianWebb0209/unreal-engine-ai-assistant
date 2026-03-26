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
	};

	struct FPerTypeBudgetCaps
	{
		int32 MaxRecentTab = 12;
		int32 MaxAttachment = 10;
		int32 MaxToolResult = 8;
		int32 MaxEditorSnapshotField = 20;
		int32 MaxMemorySnippet = 6;
		int32 MaxTodoState = 4;
		int32 MaxOrchestrateState = 10;
	};

	inline constexpr int32 DefaultHardMaxCandidates = 256;
	inline constexpr int32 MinBudgetReserveChars = 180;
	inline constexpr int32 MaxMemoryCandidatesScanned = 24;
	inline constexpr float MinMemoryCandidateScoreToPack = 8.0f;

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
		case ECandidateType::TodoState: return TEXT("todo_state");
		case ECandidateType::OrchestrateState: return TEXT("orchestrate_state");
		default: return TEXT("unknown");
		}
	}
}
