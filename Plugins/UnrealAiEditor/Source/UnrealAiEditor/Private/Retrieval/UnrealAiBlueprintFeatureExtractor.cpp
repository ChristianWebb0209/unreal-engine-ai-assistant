#include "Retrieval/UnrealAiBlueprintFeatureExtractor.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#endif

namespace
{
	static FString ClampAndNormalize(const FString& In, const int32 MaxLen)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("\r"), TEXT(" "));
		Out.ReplaceInline(TEXT("\n"), TEXT(" "));
		Out.TrimStartAndEndInline();
		if (Out.Len() > MaxLen)
		{
			Out = Out.Left(MaxLen);
		}
		return Out;
	}
}

void FUnrealAiBlueprintFeatureExtractor::ExtractFeatureRecords(TArray<FUnrealAiBlueprintFeatureRecord>& OutRecords, const int32 MaxRecords)
{
	OutRecords.Reset();
#if WITH_EDITOR
	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FARFilter Filter;
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")));
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	RegistryModule.Get().GetAssets(Filter, Assets);

	const int32 MaxFeatures = (MaxRecords > 0) ? MaxRecords : 4000;
	int32 Added = 0;
	for (const FAssetData& Asset : Assets)
	{
		if (Added >= MaxFeatures)
		{
			break;
		}
		FUnrealAiBlueprintFeatureRecord Record;
		Record.AssetPath = Asset.GetSoftObjectPath().ToString();
		const FString ParentClass = Asset.GetTagValueRef<FString>(TEXT("ParentClassPath"));
		const FString GeneratedClass = Asset.GetTagValueRef<FString>(TEXT("GeneratedClass"));
		const FString SimpleName = Asset.AssetName.ToString();
		Record.Text = FString::Printf(
			TEXT("blueprint asset=%s name=%s parent=%s generated=%s"),
			*Record.AssetPath,
			*SimpleName,
			*ClampAndNormalize(ParentClass, 256),
			*ClampAndNormalize(GeneratedClass, 256));
		OutRecords.Add(MoveTemp(Record));
		++Added;
	}
#endif
}
