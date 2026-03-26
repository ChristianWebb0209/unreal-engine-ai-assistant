#pragma once

#include "CoreMinimal.h"

enum class EUnrealAiMemoryScope : uint8
{
	Project,
	Thread,
};

enum class EUnrealAiMemoryStatus : uint8
{
	Active,
	Disabled,
	Archived,
};

struct FUnrealAiMemorySourceRef
{
	FString Kind;
	FString Value;
};

struct FUnrealAiMemoryRecord
{
	FString Id;
	FString Title;
	FString Description;
	FString Body;
	TArray<FString> Tags;
	EUnrealAiMemoryScope Scope = EUnrealAiMemoryScope::Project;
	EUnrealAiMemoryStatus Status = EUnrealAiMemoryStatus::Active;
	float Confidence = 0.0f;
	int32 TtlDays = 0;
	int32 UseCount = 0;
	FDateTime CreatedAtUtc = FDateTime::UtcNow();
	FDateTime UpdatedAtUtc = FDateTime::UtcNow();
	FDateTime LastUsedAtUtc = FDateTime::UtcNow();
	TArray<FUnrealAiMemorySourceRef> SourceRefs;
};

struct FUnrealAiMemoryIndexRow
{
	FString Id;
	FString Title;
	FString Description;
	TArray<FString> Tags;
	EUnrealAiMemoryScope Scope = EUnrealAiMemoryScope::Project;
	EUnrealAiMemoryStatus Status = EUnrealAiMemoryStatus::Active;
	float Confidence = 0.0f;
	int32 UseCount = 0;
	FDateTime UpdatedAtUtc = FDateTime::UtcNow();
	FDateTime LastUsedAtUtc = FDateTime::UtcNow();
};

struct FUnrealAiMemoryTombstone
{
	FString Id;
	FDateTime DeletedAtUtc = FDateTime::UtcNow();
};

struct FUnrealAiMemoryQuery
{
	FString QueryText;
	TArray<FString> RequiredTags;
	bool bIncludeBodies = false;
	bool bPreferThreadScope = true;
	bool bTitleDescriptionOnly = true;
	int32 MaxResults = 20;
	float MinConfidence = 0.0f;
};

struct FUnrealAiMemoryQueryResult
{
	FUnrealAiMemoryIndexRow IndexRow;
	float Score = 0.0f;
	TOptional<FUnrealAiMemoryRecord> Record;
};

struct FUnrealAiMemoryCompactionInput
{
	FString ProjectId;
	FString ThreadId;
	FString ConversationJson;
	FString ContextJson;
	TArray<FString> ToolResultSnippets;
	bool bApiKeyConfigured = true;
	bool bExpectProviderGeneration = false;
};

struct FUnrealAiMemoryCompactionResult
{
	int32 Candidates = 0;
	int32 Accepted = 0;
	TArray<FString> AcceptedMemoryIds;
};

enum class EUnrealAiMemoryGenerationState : uint8
{
	Idle,
	Running,
	Success,
	Failed,
};

enum class EUnrealAiMemoryGenerationErrorCode : uint8
{
	None,
	MissingApiKey,
	ProviderUnavailable,
	InvalidResponse,
	RateLimited,
	Unknown,
};

struct FUnrealAiMemoryGenerationStatus
{
	EUnrealAiMemoryGenerationState State = EUnrealAiMemoryGenerationState::Idle;
	EUnrealAiMemoryGenerationErrorCode ErrorCode = EUnrealAiMemoryGenerationErrorCode::None;
	FString ErrorMessage;
	FDateTime LastAttemptAtUtc = FDateTime::UtcNow();
	FDateTime LastSuccessAtUtc = FDateTime();
};
