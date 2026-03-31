#include "Context/UnrealAiProjectTreeSampler.h"

#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "Engine/Blueprint.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#endif

namespace UnrealAiProjectTreeSampler
{
	namespace
	{
		static TMap<FString, FProjectTreeSummary> GProjectSummaryCache;

		static FString NormalizeTopFolder(const FString& ObjectPath)
		{
			if (!ObjectPath.StartsWith(TEXT("/Game/")))
			{
				return FString();
			}
			const FString Rest = ObjectPath.Mid(6);
			int32 Slash = INDEX_NONE;
			if (!Rest.FindChar(TEXT('/'), Slash))
			{
				return TEXT("/Game");
			}
			return FString::Printf(TEXT("/Game/%s"), *Rest.Left(Slash));
		}

		static void AppendPreferredPath(
			FProjectTreeSummary& Summary,
			const FString& AssetFamily,
			const TMap<FString, int32>& FolderCounts,
			const FString& Fallback)
		{
			FString BestPath = Fallback;
			int32 BestCount = 0;
			for (const TPair<FString, int32>& Pair : FolderCounts)
			{
				if (Pair.Value > BestCount)
				{
					BestCount = Pair.Value;
					BestPath = Pair.Key;
				}
			}

			FProjectTreePathPreference P;
			P.AssetFamily = AssetFamily;
			P.PackagePath = BestPath;
			P.ObservedCount = BestCount;
			P.Confidence = BestCount > 0 ? 1.0 : 0.2;
			Summary.PreferredCreatePaths.Add(MoveTemp(P));
		}

		static FString NormalizeTopFolderFromGamePath(const FString& GamePathLike)
		{
			const int32 GameAt = GamePathLike.Find(TEXT("/Game/"), ESearchCase::CaseSensitive);
			if (GameAt == INDEX_NONE)
			{
				return FString();
			}

			FString Slice = GamePathLike.Mid(GameAt);
			int32 Dot = INDEX_NONE;
			if (Slice.FindChar(TEXT('.'), Dot))
			{
				Slice = Slice.Left(Dot);
			}

			const FString Rest = Slice.Mid(6);
			int32 Slash = INDEX_NONE;
			if (!Rest.FindChar(TEXT('/'), Slash))
			{
				return TEXT("/Game");
			}
			return FString::Printf(TEXT("/Game/%s"), *Rest.Left(Slash));
		}

		static FString ExtractPackagePathFromSourceOrText(const FString& SourceId, const FString& Text)
		{
			auto ToPackagePath = [](const FString& InPath) -> FString
			{
				const int32 GameAt = InPath.Find(TEXT("/Game/"), ESearchCase::CaseSensitive);
				if (GameAt == INDEX_NONE)
				{
					return FString();
				}
				FString Slice = InPath.Mid(GameAt);
				int32 Dot = INDEX_NONE;
				if (Slice.FindChar(TEXT('.'), Dot))
				{
					Slice = Slice.Left(Dot);
				}
				int32 LastSlash = INDEX_NONE;
				if (Slice.FindLastChar(TEXT('/'), LastSlash) && LastSlash > 0)
				{
					return Slice.Left(LastSlash);
				}
				return FString();
			};

			FString P = ToPackagePath(SourceId);
			if (!P.IsEmpty())
			{
				return P;
			}
			return ToPackagePath(Text);
		}

		static void UpsertPreferredPath(
			FProjectTreeSummary& Summary,
			const FString& AssetFamily,
			const FString& PackagePath,
			const int32 AddedObservedCount)
		{
			if (AssetFamily.IsEmpty() || PackagePath.IsEmpty())
			{
				return;
			}
			for (FProjectTreePathPreference& Existing : Summary.PreferredCreatePaths)
			{
				if (Existing.AssetFamily.Equals(AssetFamily, ESearchCase::IgnoreCase))
				{
					if (AddedObservedCount > Existing.ObservedCount)
					{
						Existing.PackagePath = PackagePath;
						Existing.ObservedCount = AddedObservedCount;
						Existing.Confidence = 0.9;
					}
					return;
				}
			}

			FProjectTreePathPreference Added;
			Added.AssetFamily = AssetFamily;
			Added.PackagePath = PackagePath;
			Added.ObservedCount = AddedObservedCount;
			Added.Confidence = 0.9;
			Summary.PreferredCreatePaths.Add(MoveTemp(Added));
		}
	}

	bool MaybeRefreshProjectTreeSummary(
		const FString& ProjectId,
		FProjectTreeSummary& InOutSummary,
		const bool bForceRefresh,
		const FString& RefreshReason)
	{
		const FDateTime Now = FDateTime::UtcNow();
		if (!bForceRefresh && InOutSummary.UpdatedUtc != FDateTime::MinValue())
		{
			const FTimespan Age = Now - InOutSummary.UpdatedUtc;
			if (Age.GetTotalMinutes() < static_cast<double>(RefreshMinutes))
			{
				InOutSummary.LastQueryStatus = RefreshReason.IsEmpty()
					? TEXT("skipped_fresh")
					: FString::Printf(TEXT("skipped_fresh reason=%s"), *RefreshReason);
				return false;
			}
		}

		InOutSummary.LastQueryStartUtc = Now;
		const double StartSec = FPlatformTime::Seconds();
		InOutSummary.SamplerVersion = TEXT("project_tree_sampler_v1");

#if WITH_EDITOR
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
		{
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		}
		FAssetRegistryModule* ARM = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry"));
		if (!ARM)
		{
			InOutSummary.LastQueryStatus = TEXT("no_registry");
			InOutSummary.LastQueryEndUtc = FDateTime::UtcNow();
			InOutSummary.LastQueryDurationMs = (FPlatformTime::Seconds() - StartSec) * 1000.0;
			return false;
		}

		TMap<FString, int32> TopFolders;
		TMap<FString, int32> BlueprintFolders;
		TMap<FString, int32> MaterialInstanceFolders;

		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.PackagePaths.Add(FName(TEXT("/Game")));
		Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")));
		Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("MaterialInstanceConstant")));
		Filter.bRecursiveClasses = true;

		TArray<FAssetData> Assets;
		ARM->Get().GetAssets(Filter, Assets);
		for (const FAssetData& AD : Assets)
		{
			const FString ObjPath = AD.GetObjectPathString();
			const FString Top = NormalizeTopFolder(ObjPath);
			if (!Top.IsEmpty())
			{
				TopFolders.FindOrAdd(Top) += 1;
			}

			const FString PkgPath = AD.PackagePath.ToString();
			if (AD.AssetClassPath == UBlueprint::StaticClass()->GetClassPathName())
			{
				BlueprintFolders.FindOrAdd(PkgPath) += 1;
			}
			else if (AD.AssetClassPath.GetAssetName().ToString().Contains(TEXT("MaterialInstance"), ESearchCase::IgnoreCase))
			{
				MaterialInstanceFolders.FindOrAdd(PkgPath) += 1;
			}
		}

		InOutSummary.TopLevelFolders.Reset();
		TopFolders.KeySort([](const FString& A, const FString& B) { return A < B; });
		for (const TPair<FString, int32>& Pair : TopFolders)
		{
			InOutSummary.TopLevelFolders.Add(Pair.Key);
			if (InOutSummary.TopLevelFolders.Num() >= 8)
			{
				break;
			}
		}

		InOutSummary.PreferredCreatePaths.Reset();
		AppendPreferredPath(InOutSummary, TEXT("blueprint"), BlueprintFolders, TEXT("/Game/Blueprints"));
		AppendPreferredPath(InOutSummary, TEXT("material_instance"), MaterialInstanceFolders, TEXT("/Game/Materials"));
		InOutSummary.UpdatedUtc = FDateTime::UtcNow();
		InOutSummary.LastQueryStatus = RefreshReason.IsEmpty()
			? FString::Printf(TEXT("ok assets=%d project=%s"), Assets.Num(), *ProjectId)
			: FString::Printf(TEXT("ok reason=%s assets=%d project=%s"), *RefreshReason, Assets.Num(), *ProjectId);
#else
		InOutSummary.TopLevelFolders = { TEXT("/Game") };
		InOutSummary.PreferredCreatePaths.Reset();
		FProjectTreePathPreference P;
		P.AssetFamily = TEXT("blueprint");
		P.PackagePath = TEXT("/Game/Blueprints");
		P.ObservedCount = 0;
		P.Confidence = 0.2;
		InOutSummary.PreferredCreatePaths.Add(MoveTemp(P));
		InOutSummary.UpdatedUtc = FDateTime::UtcNow();
		InOutSummary.LastQueryStatus = RefreshReason.IsEmpty()
			? TEXT("non_editor_defaults")
			: FString::Printf(TEXT("non_editor_defaults reason=%s"), *RefreshReason);
#endif

		InOutSummary.LastQueryEndUtc = FDateTime::UtcNow();
		InOutSummary.LastQueryDurationMs = (FPlatformTime::Seconds() - StartSec) * 1000.0;
		return true;
	}

	const FProjectTreeSummary& GetOrRefreshProjectSummary(
		const FString& ProjectId,
		const bool bForceRefresh,
		const FString& RefreshReason)
	{
		FProjectTreeSummary& Summary = GProjectSummaryCache.FindOrAdd(ProjectId);
		MaybeRefreshProjectTreeSummary(ProjectId, Summary, bForceRefresh, RefreshReason);
		return Summary;
	}

	FString GetPreferredPackagePath(const FProjectTreeSummary& Summary, const FString& AssetFamily, const FString& FallbackPath)
	{
		for (const FProjectTreePathPreference& P : Summary.PreferredCreatePaths)
		{
			if (P.AssetFamily.Equals(AssetFamily, ESearchCase::IgnoreCase) && P.PackagePath.StartsWith(TEXT("/Game")))
			{
				return P.PackagePath;
			}
		}
		return FallbackPath;
	}

	FString GetPreferredPackagePathForProject(
		const FString& ProjectId,
		const FString& AssetFamily,
		const FString& FallbackPath,
		const bool bForceRefresh)
	{
		const FProjectTreeSummary& Summary = GetOrRefreshProjectSummary(ProjectId, bForceRefresh);
		return GetPreferredPackagePath(Summary, AssetFamily, FallbackPath);
	}

	void ApplyRetrievalHintsToSummary(
		FProjectTreeSummary& InOutSummary,
		const TArray<FUnrealAiRetrievalSnippet>& RetrievalSnippets)
	{
		if (RetrievalSnippets.Num() == 0)
		{
			return;
		}

		TMap<FString, int32> FolderHits;
		TMap<FString, int32> BlueprintFolderHits;
		TMap<FString, int32> MaterialFolderHits;
		int32 UsableHintCount = 0;

		for (const FUnrealAiRetrievalSnippet& Snippet : RetrievalSnippets)
		{
			const FString TopFromSource = NormalizeTopFolderFromGamePath(Snippet.SourceId);
			const FString TopFromText = TopFromSource.IsEmpty() ? NormalizeTopFolderFromGamePath(Snippet.Text) : TopFromSource;
			if (!TopFromText.IsEmpty())
			{
				FolderHits.FindOrAdd(TopFromText) += 1;
				++UsableHintCount;
			}

			const FString PackagePath = ExtractPackagePathFromSourceOrText(Snippet.SourceId, Snippet.Text);
			if (!PackagePath.IsEmpty())
			{
				const FString Lower = (Snippet.SourceId + TEXT(" ") + Snippet.Text).ToLower();
				if (Lower.Contains(TEXT("blueprint")) || Lower.Contains(TEXT("bp_")))
				{
					BlueprintFolderHits.FindOrAdd(PackagePath) += 1;
					++UsableHintCount;
				}
				else if (Lower.Contains(TEXT("materialinstance")) || Lower.Contains(TEXT("material_instance")) || Lower.Contains(TEXT("mi_")))
				{
					MaterialFolderHits.FindOrAdd(PackagePath) += 1;
					++UsableHintCount;
				}
			}
		}

		if (UsableHintCount <= 0)
		{
			return;
		}

		if (FolderHits.Num() > 0)
		{
			const TArray<FString> ExistingTopFolders = InOutSummary.TopLevelFolders;
			TArray<TPair<FString, int32>> RankedFolders;
			RankedFolders.Reserve(FolderHits.Num());
			for (const TPair<FString, int32>& Pair : FolderHits)
			{
				RankedFolders.Add(Pair);
			}
			RankedFolders.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
			{
				if (A.Value != B.Value)
				{
					return A.Value > B.Value;
				}
				return A.Key < B.Key;
			});

			TArray<FString> Merged;
			for (const TPair<FString, int32>& Pair : RankedFolders)
			{
				Merged.Add(Pair.Key);
				if (Merged.Num() >= 8)
				{
					break;
				}
			}
			for (const FString& Existing : ExistingTopFolders)
			{
				if (Merged.Num() >= 8)
				{
					break;
				}
				if (!Merged.Contains(Existing))
				{
					Merged.Add(Existing);
				}
			}
			InOutSummary.TopLevelFolders = MoveTemp(Merged);
		}

		if (BlueprintFolderHits.Num() > 0)
		{
			FString BestPath;
			int32 BestCount = -1;
			for (const TPair<FString, int32>& Pair : BlueprintFolderHits)
			{
				if (Pair.Value > BestCount)
				{
					BestPath = Pair.Key;
					BestCount = Pair.Value;
				}
			}
			UpsertPreferredPath(InOutSummary, TEXT("blueprint"), BestPath, BestCount);
		}
		if (MaterialFolderHits.Num() > 0)
		{
			FString BestPath;
			int32 BestCount = -1;
			for (const TPair<FString, int32>& Pair : MaterialFolderHits)
			{
				if (Pair.Value > BestCount)
				{
					BestPath = Pair.Key;
					BestCount = Pair.Value;
				}
			}
			UpsertPreferredPath(InOutSummary, TEXT("material_instance"), BestPath, BestCount);
		}

		InOutSummary.SamplerVersion = TEXT("project_tree_sampler_v1+retrieval_hints");
		InOutSummary.LastQueryStatus += FString::Printf(TEXT(" retrieval_hints=%d"), UsableHintCount);
	}

	FString BuildFooterLine(const FProjectTreeSummary& Summary)
	{
		const FString Updated = (Summary.UpdatedUtc == FDateTime::MinValue()) ? TEXT("never") : Summary.UpdatedUtc.ToIso8601();
		const int32 AgeMinutes = (Summary.UpdatedUtc == FDateTime::MinValue())
			? -1
			: static_cast<int32>((FDateTime::UtcNow() - Summary.UpdatedUtc).GetTotalMinutes());
		return FString::Printf(
			TEXT("Background ops: ProjectTree updated=%s age_min=%d status=%s last_query_ms=%.1f"),
			*Updated,
			AgeMinutes,
			Summary.LastQueryStatus.IsEmpty() ? TEXT("unknown") : *Summary.LastQueryStatus,
			Summary.LastQueryDurationMs);
	}

	FString BuildContextBlurb(const FProjectTreeSummary& Summary)
	{
		FString Out;
		Out += TEXT("### Project paths & conventions (dynamic)\n");
		if (Summary.TopLevelFolders.Num() > 0)
		{
			Out += TEXT("- Top folders: ");
			Out += FString::Join(Summary.TopLevelFolders, TEXT(", "));
			Out += TEXT("\n");
		}
		for (const FProjectTreePathPreference& P : Summary.PreferredCreatePaths)
		{
			Out += FString::Printf(
				TEXT("- Preferred create path (%s): `%s` (confidence=%.2f observed=%d)\n"),
				*P.AssetFamily,
				*P.PackagePath,
				P.Confidence,
				P.ObservedCount);
		}
		Out += TEXT("- Canonical examples: package_path `/Game/Folder`, object_path `/Game/Folder/Asset.Asset`\n");
		Out += FString::Printf(TEXT("- %s\n"), *BuildFooterLine(Summary));
		return Out;
	}
}
