#pragma once

#include "CoreMinimal.h"

/** Ask / Agent / Plan — controls what enters the built context block. */
enum class EUnrealAiAgentMode : uint8
{
	Ask,
	Agent,
	Plan,
};

enum class EContextAttachmentType : uint8
{
	AssetPath,
	FilePath,
	FreeText,
	BlueprintNodeRef,
	/** Level/world actor (GetPathName() payload). */
	ActorReference,
	/** Content Browser folder virtual path (e.g. /Game/Foo). */
	ContentFolder,
};

struct FContextAttachment
{
	EContextAttachmentType Type = EContextAttachmentType::AssetPath;
	FString Payload;
	/** Optional display label (e.g. @BP_Foo). */
	FString Label;
	/** Optional class path for Slate icon (e.g. /Script/Engine.Material). */
	FString IconClassPath;
};

struct FToolContextEntry
{
	FString ToolName;
	FString TruncatedResult;
	FDateTime Timestamp = FDateTime::UtcNow();
};

enum class ERecentUiKind : uint8
{
	Unknown,
	SceneViewport,
	AssetEditor,
	DetailsPanel,
	ContentBrowser,
	SceneOutliner,
	OutputLog,
	BlueprintGraph,
	Inspector,
	ToolPanel,
	NomadTab,
};

enum class ERecentUiSource : uint8
{
	Unknown,
	SlateFocus,
	TabEvent,
	AssetEditorEvent,
	PollFallback,
	/** Focus followed a tool-driven editor navigation (optional; forward-compatible). */
	AgentToolNavigation,
};

struct FRecentUiEntry
{
	/** Stable dedupe/ranking key (UiKind + canonical id). */
	FString StableId;
	/** Human-readable label for prompt/context output. */
	FString DisplayName;
	/** Class/category used by hardcoded base-priority tables. */
	ERecentUiKind UiKind = ERecentUiKind::Unknown;
	/** Detection source used for diagnostics/tie-breakers. */
	ERecentUiSource Source = ERecentUiSource::Unknown;
	/** Most recent time this UI surface was observed/focused. */
	FDateTime LastSeenUtc = FDateTime::UtcNow();
	/** Frequency hint for ranking (higher = repeatedly used). */
	int32 SeenCount = 1;
	/** Best-effort current active marker at capture time. */
	bool bCurrentlyActive = false;
	/** True when this entry is tied to the active thread-local overlay. */
	bool bThreadLocalPreferred = false;
};

struct FEditorContextSnapshot
{
	/** Comma-separated selected actor labels or paths (best-effort). */
	FString SelectedActorsSummary;
	/** First/primary selected asset in Content Browser (compat with schema v1). */
	FString ActiveAssetPath;
	/** Current Content Browser folder path (e.g. /Game/Characters). */
	FString ContentBrowserPath;
	/** Selected assets in Content Browser (bounded when captured). */
	TArray<FString> ContentBrowserSelectedAssets;
	/** Assets with open editor tabs (bounded when captured). */
	TArray<FString> OpenEditorAssets;
	/** Canonical id of the currently active/focused UI entry (if known). */
	FString ActiveUiEntryId;
	/** Prioritized recent UI entries (all focusable panes/widgets, bounded). */
	TArray<FRecentUiEntry> RecentUiEntries;
	bool bValid = false;
};

struct FProjectTreePathPreference
{
	/** Asset family key (for example: blueprint, material_instance). */
	FString AssetFamily;
	/** Preferred package path for new assets in this family (for example: /Game/Blueprints). */
	FString PackagePath;
	/** Relative confidence [0,1]. */
	double Confidence = 0.0;
	/** Number of observed assets supporting this preference. */
	int32 ObservedCount = 0;
};

/** Origin of a thread working-set asset touch (MRU list for context + autofill). */
enum class EThreadAssetTouchSource : uint8
{
	Unknown,
	OpenEditor,
	ContentBrowserSelection,
	ToolResult,
	Attachment,
};

struct FThreadAssetWorkingEntry
{
	FString ObjectPath;
	FString AssetClassPath;
	FDateTime LastTouchedUtc = FDateTime::UtcNow();
	EThreadAssetTouchSource TouchSource = EThreadAssetTouchSource::Unknown;
	FString LastToolName;
};

struct FProjectTreeSummary
{
	/** Sampler version marker for diagnostics / migration. */
	FString SamplerVersion;
	/** Last successful refresh time (UTC). */
	FDateTime UpdatedUtc = FDateTime::MinValue();
	/** Last refresh attempt start/end for observability. */
	FDateTime LastQueryStartUtc = FDateTime::MinValue();
	FDateTime LastQueryEndUtc = FDateTime::MinValue();
	/** Last query status string (ok, skipped_fresh, no_registry, error_...). */
	FString LastQueryStatus;
	/** Last query wall time in milliseconds. */
	double LastQueryDurationMs = 0.0;
	/** Loose top-level /Game tree hints (for example /Game/Blueprints, /Game/Materials). */
	TArray<FString> TopLevelFolders;
	/** Per-family package path recommendations used by underspecified create requests. */
	TArray<FProjectTreePathPreference> PreferredCreatePaths;
};

struct FAgentContextState
{
	static const int32 SchemaVersion = 7;

	int32 SchemaVersionField = SchemaVersion;
	TArray<FContextAttachment> Attachments;
	TArray<FToolContextEntry> ToolResults;
	TOptional<FEditorContextSnapshot> EditorSnapshot;
	/** Max chars for the assembled ContextBlock (post-format); 0 = default from options. */
	int32 MaxContextChars = 0;
	/** Legacy `unreal_ai.todo_plan` JSON (deprecated tool; may persist from old sessions). */
	FString ActiveTodoPlanJson;
	/** Parallel to `steps` in ActiveTodoPlanJson (best-effort). */
	TArray<bool> TodoStepsDone;
	/** Canonical plan DAG JSON produced by planner pass (schema: unreal_ai.plan_dag). */
	FString ActivePlanDagJson;
	/** Node-level execution status for the active DAG. */
	TMap<FString, FString> PlanNodeStatusById;
	/** Optional one-line summary per completed/failed node for parent context carry-forward. */
	TMap<FString, FString> PlanNodeSummaryById;
	/** Thread-local recent UI overlay (bounded, persisted per thread). */
	TArray<FRecentUiEntry> ThreadRecentUiOverlay;
	/** Cached project tree inference used for path grounding in context. */
	FProjectTreeSummary ProjectTreeSummary;
	/**
	 * MRU asset paths touched this thread (open editors, selection, tools, attachments).
	 * Bounded ring; feeds ranked context + harness autofill.
	 */
	TArray<FThreadAssetWorkingEntry> ThreadAssetWorkingSet;
};

struct FContextRecordPolicy
{
	/** Max chars stored per tool result before RecordToolResult truncates. */
	int32 MaxStoredCharsPerResult = 4096;
};

/** Raise per-tool result caps (e.g. compile logs) so iterative fix-up keeps diagnostics in context. */
inline void UnrealAiApplyToolSpecificRecordPolicy(FContextRecordPolicy& Policy, const FName& ToolName)
{
	const FString S = ToolName.ToString();
	if (S == TEXT("blueprint_compile") || S == TEXT("cpp_project_compile"))
	{
		Policy.MaxStoredCharsPerResult = FMath::Max(Policy.MaxStoredCharsPerResult, 98304);
	}
}

struct FAgentContextBuildOptions
{
	EUnrealAiAgentMode Mode = EUnrealAiAgentMode::Agent;
	/** Hard cap on formatted context string; trims oldest tool results first, then attachments. */
	int32 MaxContextChars = 32000;
	bool bIncludeToolResults = true;
	bool bIncludeAttachments = true;
	bool bIncludeEditorSnapshot = true;
	/** Optional static block (e.g. safety rules) prepended by caller; not persisted in state. */
	FString StaticSystemPrefix;
	/** Latest user message for this turn — feeds context ranking/retrieval hints. */
	FString UserMessageForComplexity;
	/** Active thread id at build time, used for thread-preferred memory retrieval. */
	FString ThreadIdForMemory;
	/** Stable key for this LLM turn/round retrieval prefetch request. */
	FString RetrievalTurnKey;
	/** Optional per-entity multipliers for retrieval relevance scaling (phase 2). */
	TMap<FString, float> RetrievalUtilityMultiplierByEntity;
	/** Optional long-tail floor entity ids that should survive aggressive demotion. */
	TSet<FString> RetrievalLongTailEntityFloor;
	/** Deterministic high-utility head entities used to promote retrieval representation. */
	TSet<FString> RetrievalHeadEntitySet;
	/** Optional explicit adjacency list used for bounded neighborhood expansion. */
	TMap<FString, TArray<FString>> RetrievalNeighborsByEntity;
	/** Max number of retrieval long-tail floor entities to force through ranking gates. */
	int32 RetrievalLongTailFloorCount = 0;
	/** Context aggression scalar [0,1] controlling fill/caps/gates (0.5 = current default behavior). */
	float ContextAggression = 0.5f;
	/** From model profile: if false, image-like attachments are stripped when building context. */
	bool bModelSupportsImages = true;
	/** Optional call-site label for decision log attribution (for example: request_build, harness_dump_run_started). */
	FString ContextBuildInvocationReason;
	/**
	 * If true, BuildContextWindow emits a verbose, out-of-band trace explaining
	 * which parts were added/dropped (mode, image stripping, and budget trimming).
	 * This trace must not be injected into the prompt context.
	 */
	bool bVerboseContextBuild = false;
};

struct FAgentContextBuildResult
{
	FString SystemOrDeveloperBlock;
	FString ContextBlock;
	TArray<FString> Warnings;
	/** Short line for {{ACTIVE_TODO_SUMMARY}} when a plan exists. */
	FString ActiveTodoSummaryText;
	bool bTruncated = false;
	/** Shown in chat by the UI when attachments are dropped (e.g. model does not support images). */
	TArray<FString> UserVisibleMessages;
	/** Out-of-band trace lines for qualitative analysis (only when options requested). */
	TArray<FString> VerboseTraceLines;
};
