#pragma once

#include "CoreMinimal.h"

struct FUnrealAiRetrievalSettings
{
	bool bEnabled = false;
	FString EmbeddingModel;
	int32 MaxSnippetsPerTurn = 6;
	int32 MaxSnippetTokens = 256;
	bool bAutoIndexOnProjectOpen = true;
	int32 PeriodicScrubMinutes = 30;
	bool bAllowMixedModelCompatibility = false;
};

struct FUnrealAiRetrievalQuery
{
	FString ProjectId;
	FString ThreadId;
	FString QueryText;
	int32 MaxResults = 0;
};

struct FUnrealAiRetrievalSnippet
{
	FString SnippetId;
	FString SourceId;
	FString ThreadId;
	FString Text;
	float Score = 0.0f;
};

struct FUnrealAiRetrievalQueryResult
{
	TArray<FUnrealAiRetrievalSnippet> Snippets;
	TArray<FString> Warnings;
};

struct FUnrealAiRetrievalProjectStatus
{
	bool bEnabled = false;
	FString StateText = TEXT("disabled");
	int32 FilesIndexed = 0;
	int32 ChunksIndexed = 0;
	bool bBusy = false;
};
