#pragma once

#include "Context/AgentContextTypes.h"
#include "CoreMinimal.h"
#include "Harness/UnrealAiAgentTypes.h"

class FUnrealAiToolCatalog;
struct FUnrealAiToolPackOptions;

/** Lightweight BM25-style lexical index over catalog text (§2.4 hybrid lexical leg). */
class FUnrealAiToolBm25Index
{
public:
	void RebuildForEnabledTools(
		const FUnrealAiToolCatalog& Catalog,
		EUnrealAiAgentMode Mode,
		const FUnrealAiModelCapabilities& Caps,
		const FUnrealAiToolPackOptions* PackOptions);

	void ScoreQuery(const FString& QueryText, TMap<FString, float>& OutScoresByToolId) const;

private:
	struct FDoc
	{
		TMap<FString, int32> TermFreq;
		int32 Len = 0;
	};
	TMap<FString, FDoc> Docs;
	TMap<FString, float> Idf;
	float AvgDocLen = 1.f;
	int32 NumDocs = 0;
};
