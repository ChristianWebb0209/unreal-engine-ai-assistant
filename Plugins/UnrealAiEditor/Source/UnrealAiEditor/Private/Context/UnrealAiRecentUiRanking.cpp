#include "Context/UnrealAiRecentUiRanking.h"

namespace UnrealAiRecentUiRanking
{
	float GetBaseImportance(const ERecentUiKind Kind)
	{
		switch (Kind)
		{
		case ERecentUiKind::DetailsPanel:
		case ERecentUiKind::Inspector:
		case ERecentUiKind::AssetEditor:
		case ERecentUiKind::SceneViewport:
			return 100.f;
		case ERecentUiKind::ContentBrowser:
		case ERecentUiKind::SceneOutliner:
			return 75.f;
		case ERecentUiKind::BlueprintGraph:
		case ERecentUiKind::ToolPanel:
		case ERecentUiKind::NomadTab:
			return 60.f;
		case ERecentUiKind::OutputLog:
			return 40.f;
		default:
			return 30.f;
		}
	}

	FScoreBreakdown ScoreEntry(const FRecentUiEntry& Entry, const FDateTime& NowUtc)
	{
		FScoreBreakdown B;
		B.BaseImportance = GetBaseImportance(Entry.UiKind);

		const FTimespan Age = NowUtc - Entry.LastSeenUtc;
		const double AgeSeconds = FMath::Max(0.0, Age.GetTotalSeconds());
		B.Recency = static_cast<float>(FMath::Max(0.0, 80.0 - (AgeSeconds / 3.0)));
		B.Frequency = static_cast<float>(FMath::Clamp(Entry.SeenCount, 0, 25)) * 2.0f;
		B.ActiveBonus = Entry.bCurrentlyActive ? 40.f : 0.f;
		B.ThreadOverlayBonus = Entry.bThreadLocalPreferred ? 20.f : 0.f;

		B.Score = B.BaseImportance + B.Recency + B.Frequency + B.ActiveBonus + B.ThreadOverlayBonus;
		return B;
	}

	void MergeAndRank(
		const TArray<FRecentUiEntry>& GlobalHistory,
		const TArray<FRecentUiEntry>& ThreadOverlay,
		const int32 MaxOutput,
		TArray<FRecentUiEntry>& OutRanked)
	{
		OutRanked.Reset();
		TMap<FString, FRecentUiEntry> Merged;
		auto MergeEntry = [&Merged](const FRecentUiEntry& In, const bool bThreadOverlay)
		{
			if (In.StableId.IsEmpty())
			{
				return;
			}
			FRecentUiEntry* Existing = Merged.Find(In.StableId);
			if (!Existing)
			{
				FRecentUiEntry NewEntry = In;
				NewEntry.bThreadLocalPreferred = bThreadOverlay || NewEntry.bThreadLocalPreferred;
				Merged.Add(NewEntry.StableId, MoveTemp(NewEntry));
				return;
			}
			if (In.LastSeenUtc > Existing->LastSeenUtc)
			{
				Existing->LastSeenUtc = In.LastSeenUtc;
				if (!In.DisplayName.IsEmpty())
				{
					Existing->DisplayName = In.DisplayName;
				}
				Existing->UiKind = In.UiKind;
				Existing->Source = In.Source;
			}
			Existing->SeenCount = FMath::Max(Existing->SeenCount, In.SeenCount);
			Existing->bCurrentlyActive = Existing->bCurrentlyActive || In.bCurrentlyActive;
			Existing->bThreadLocalPreferred = Existing->bThreadLocalPreferred || bThreadOverlay || In.bThreadLocalPreferred;
		};

		for (const FRecentUiEntry& E : GlobalHistory)
		{
			MergeEntry(E, false);
		}
		for (const FRecentUiEntry& E : ThreadOverlay)
		{
			MergeEntry(E, true);
		}

		Merged.GenerateValueArray(OutRanked);
		const FDateTime NowUtc = FDateTime::UtcNow();
		OutRanked.Sort([&NowUtc](const FRecentUiEntry& A, const FRecentUiEntry& B)
		{
			const FScoreBreakdown SA = ScoreEntry(A, NowUtc);
			const FScoreBreakdown SB = ScoreEntry(B, NowUtc);
			if (!FMath::IsNearlyEqual(SA.Score, SB.Score))
			{
				return SA.Score > SB.Score;
			}
			return A.LastSeenUtc > B.LastSeenUtc;
		});

		if (MaxOutput > 0 && OutRanked.Num() > MaxOutput)
		{
			OutRanked.SetNum(MaxOutput, EAllowShrinking::No);
		}
	}
}
