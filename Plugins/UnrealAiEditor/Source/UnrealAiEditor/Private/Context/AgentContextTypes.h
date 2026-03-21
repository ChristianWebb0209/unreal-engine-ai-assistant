#pragma once

#include "CoreMinimal.h"

/** Ask / Fast / Agent — controls what enters the built context block. */
enum class EUnrealAiAgentMode : uint8
{
	Ask,
	Fast,
	Agent,
};

enum class EContextAttachmentType : uint8
{
	AssetPath,
	FilePath,
	FreeText,
	BlueprintNodeRef,
};

struct FContextAttachment
{
	EContextAttachmentType Type = EContextAttachmentType::AssetPath;
	FString Payload;
	/** Optional display label (e.g. @BP_Foo). */
	FString Label;
};

struct FToolContextEntry
{
	FString ToolName;
	FString TruncatedResult;
	FDateTime Timestamp = FDateTime::UtcNow();
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
	bool bValid = false;
};

struct FAgentContextState
{
	static const int32 SchemaVersion = 3;

	int32 SchemaVersionField = SchemaVersion;
	TArray<FContextAttachment> Attachments;
	TArray<FToolContextEntry> ToolResults;
	TOptional<FEditorContextSnapshot> EditorSnapshot;
	/** Max chars for the assembled ContextBlock (post-format); 0 = default from options. */
	int32 MaxContextChars = 0;
	/** Canonical `unreal_ai.todo_plan` JSON from agent_emit_todo_plan (persisted). */
	FString ActiveTodoPlanJson;
	/** Parallel to `steps` in ActiveTodoPlanJson (best-effort). */
	TArray<bool> TodoStepsDone;
};

struct FContextRecordPolicy
{
	/** Max chars stored per tool result before RecordToolResult truncates. */
	int32 MaxStoredCharsPerResult = 4096;
};

struct FAgentContextBuildOptions
{
	EUnrealAiAgentMode Mode = EUnrealAiAgentMode::Fast;
	/** Hard cap on formatted context string; trims oldest tool results first, then attachments. */
	int32 MaxContextChars = 32000;
	bool bIncludeToolResults = true;
	bool bIncludeAttachments = true;
	bool bIncludeEditorSnapshot = true;
	/** Optional static block (e.g. safety rules) prepended by caller; not persisted in state. */
	FString StaticSystemPrefix;
	/** Latest user message for this turn — feeds `FUnrealAiComplexityAssessor`. */
	FString UserMessageForComplexity;
};

struct FAgentContextBuildResult
{
	FString SystemOrDeveloperBlock;
	FString ContextBlock;
	TArray<FString> Warnings;
	/** `low` / `medium` / `high` (from complexity assessor). */
	FString ComplexityLabel;
	float ComplexityScoreNormalized = 0.f;
	bool bRecommendPlanGate = false;
	TArray<FString> ComplexitySignals;
	/** Full [Complexity] block for prompt injection. */
	FString ComplexityBlock;
	/** Short line for {{ACTIVE_TODO_SUMMARY}} when a plan exists. */
	FString ActiveTodoSummaryText;
	bool bTruncated = false;
};
