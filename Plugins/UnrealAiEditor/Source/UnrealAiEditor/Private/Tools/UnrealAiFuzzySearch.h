#pragma once

#include "CoreMinimal.h"

/**
 * Cursor-style fuzzy matching for tool search: substring, ordered subsequence, then edit distance.
 * Scores are 0–100 (higher is better). Tolerates misspellings and unknown exact names.
 */
namespace UnrealAiFuzzySearch
{
	/** Best score when comparing Query against several candidate strings (label, name, path, ...). */
	float ScoreAgainstCandidates(const FString& Query, const TArray<FString>& Candidates);

	/** Single string score (normalized internally). */
	float Score(const FString& Query, const FString& Candidate);

	/** Case-fold for comparison (ASCII-focused; sufficient for asset/code names). */
	void FoldCaseInline(FString& S);
}
