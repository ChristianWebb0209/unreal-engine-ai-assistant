#include "UnrealAiEnvironmentBuilderTargetKind.h"

EUnrealAiEnvironmentBuilderTargetKind UnrealAiEnvironmentBuilderTargetKind::ParseFromString(const FString& In)
{
	FString S = In;
	S.TrimStartAndEndInline();
	if (S.IsEmpty())
	{
		return EUnrealAiEnvironmentBuilderTargetKind::PcgScene;
	}
	if (S.Equals(TEXT("pcg_scene"), ESearchCase::IgnoreCase))
	{
		return EUnrealAiEnvironmentBuilderTargetKind::PcgScene;
	}
	if (S.Equals(TEXT("landscape_terrain"), ESearchCase::IgnoreCase))
	{
		return EUnrealAiEnvironmentBuilderTargetKind::LandscapeTerrain;
	}
	if (S.Equals(TEXT("foliage_scatter"), ESearchCase::IgnoreCase))
	{
		return EUnrealAiEnvironmentBuilderTargetKind::FoliageScatter;
	}
	if (S.Equals(TEXT("mixed"), ESearchCase::IgnoreCase))
	{
		return EUnrealAiEnvironmentBuilderTargetKind::Mixed;
	}
	return EUnrealAiEnvironmentBuilderTargetKind::PcgScene;
}

FString UnrealAiEnvironmentBuilderTargetKind::ToDomainString(EUnrealAiEnvironmentBuilderTargetKind Kind)
{
	switch (Kind)
	{
	case EUnrealAiEnvironmentBuilderTargetKind::LandscapeTerrain:
		return TEXT("landscape_terrain");
	case EUnrealAiEnvironmentBuilderTargetKind::FoliageScatter:
		return TEXT("foliage_scatter");
	case EUnrealAiEnvironmentBuilderTargetKind::Mixed:
		return TEXT("mixed");
	case EUnrealAiEnvironmentBuilderTargetKind::PcgScene:
	default:
		return TEXT("pcg_scene");
	}
}

FString UnrealAiEnvironmentBuilderTargetKind::KindChunkFileName(EUnrealAiEnvironmentBuilderTargetKind Kind)
{
	return FString::Printf(TEXT("environment-builder/kinds/%s.md"), *ToDomainString(Kind));
}
