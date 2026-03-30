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
	}

	bool MaybeRefreshProjectTreeSummary(const FString& ProjectId, FProjectTreeSummary& InOutSummary, const bool bForceRefresh)
	{
		const FDateTime Now = FDateTime::UtcNow();
		if (!bForceRefresh && InOutSummary.UpdatedUtc != FDateTime::MinValue())
		{
			const FTimespan Age = Now - InOutSummary.UpdatedUtc;
			if (Age.GetTotalMinutes() < static_cast<double>(RefreshMinutes))
			{
				InOutSummary.LastQueryStatus = TEXT("skipped_fresh");
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
		InOutSummary.LastQueryStatus = FString::Printf(TEXT("ok assets=%d project=%s"), Assets.Num(), *ProjectId);
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
		InOutSummary.LastQueryStatus = TEXT("non_editor_defaults");
#endif

		InOutSummary.LastQueryEndUtc = FDateTime::UtcNow();
		InOutSummary.LastQueryDurationMs = (FPlatformTime::Seconds() - StartSec) * 1000.0;
		return true;
	}

	const FProjectTreeSummary& GetOrRefreshProjectSummary(const FString& ProjectId, const bool bForceRefresh)
	{
		FProjectTreeSummary& Summary = GProjectSummaryCache.FindOrAdd(ProjectId);
		MaybeRefreshProjectTreeSummary(ProjectId, Summary, bForceRefresh);
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
