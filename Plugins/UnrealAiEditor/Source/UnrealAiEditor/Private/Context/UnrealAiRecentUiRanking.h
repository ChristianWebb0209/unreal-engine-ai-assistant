#pragma once

#include "Context/AgentContextTypes.h"

namespace UnrealAiRecentUiRanking
{
	struct FScoreBreakdown
	{
		float Score = 0.f;
		float BaseImportance = 0.f;
		float Recency = 0.f;
		float Frequency = 0.f;
		float ActiveBonus = 0.f;
		float ThreadOverlayBonus = 0.f;
	};

	float GetBaseImportance(ERecentUiKind Kind);
	FScoreBreakdown ScoreEntry(const FRecentUiEntry& Entry, const FDateTime& NowUtc);
	void MergeAndRank(
		const TArray<FRecentUiEntry>& GlobalHistory,
		const TArray<FRecentUiEntry>& ThreadOverlay,
		int32 MaxOutput,
		TArray<FRecentUiEntry>& OutRanked);
}
