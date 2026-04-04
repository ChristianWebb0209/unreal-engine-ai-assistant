#include "UnrealAiBlueprintBuilderTargetKind.h"

EUnrealAiBlueprintBuilderTargetKind UnrealAiBlueprintBuilderTargetKind::ParseFromString(const FString& In)
{
	FString S = In;
	S.TrimStartAndEndInline();
	if (S.IsEmpty())
	{
		return EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint;
	}
	if (S.Equals(TEXT("script_blueprint"), ESearchCase::IgnoreCase))
	{
		return EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint;
	}
	if (S.Equals(TEXT("anim_blueprint"), ESearchCase::IgnoreCase))
	{
		return EUnrealAiBlueprintBuilderTargetKind::AnimBlueprint;
	}
	if (S.Equals(TEXT("material_instance"), ESearchCase::IgnoreCase))
	{
		return EUnrealAiBlueprintBuilderTargetKind::MaterialInstance;
	}
	if (S.Equals(TEXT("material_graph"), ESearchCase::IgnoreCase))
	{
		return EUnrealAiBlueprintBuilderTargetKind::MaterialGraph;
	}
	if (S.Equals(TEXT("niagara"), ESearchCase::IgnoreCase))
	{
		return EUnrealAiBlueprintBuilderTargetKind::Niagara;
	}
	if (S.Equals(TEXT("widget_blueprint"), ESearchCase::IgnoreCase))
	{
		return EUnrealAiBlueprintBuilderTargetKind::WidgetBlueprint;
	}
	return EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint;
}

FString UnrealAiBlueprintBuilderTargetKind::ToDomainString(EUnrealAiBlueprintBuilderTargetKind Kind)
{
	switch (Kind)
	{
	case EUnrealAiBlueprintBuilderTargetKind::AnimBlueprint:
		return TEXT("anim_blueprint");
	case EUnrealAiBlueprintBuilderTargetKind::MaterialInstance:
		return TEXT("material_instance");
	case EUnrealAiBlueprintBuilderTargetKind::MaterialGraph:
		return TEXT("material_graph");
	case EUnrealAiBlueprintBuilderTargetKind::Niagara:
		return TEXT("niagara");
	case EUnrealAiBlueprintBuilderTargetKind::WidgetBlueprint:
		return TEXT("widget_blueprint");
	case EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint:
	default:
		return TEXT("script_blueprint");
	}
}

FString UnrealAiBlueprintBuilderTargetKind::KindChunkFileName(EUnrealAiBlueprintBuilderTargetKind Kind)
{
	return FString::Printf(TEXT("blueprint-builder/kinds/%s.md"), *ToDomainString(Kind));
}
