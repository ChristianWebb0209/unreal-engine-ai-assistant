#include "UnrealAiBlueprintBuilderTargetKind.h"

bool UnrealAiBlueprintBuilderTargetKind::TryParseFromString(const FString& In, EUnrealAiBlueprintBuilderTargetKind& OutKind)
{
	FString S = In;
	S.TrimStartAndEndInline();
	if (S.IsEmpty())
	{
		return false;
	}
	if (S.Equals(TEXT("script_blueprint"), ESearchCase::IgnoreCase))
	{
		OutKind = EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint;
		return true;
	}
	if (S.Equals(TEXT("anim_blueprint"), ESearchCase::IgnoreCase))
	{
		OutKind = EUnrealAiBlueprintBuilderTargetKind::AnimBlueprint;
		return true;
	}
	if (S.Equals(TEXT("material_instance"), ESearchCase::IgnoreCase))
	{
		OutKind = EUnrealAiBlueprintBuilderTargetKind::MaterialInstance;
		return true;
	}
	if (S.Equals(TEXT("material_graph"), ESearchCase::IgnoreCase))
	{
		OutKind = EUnrealAiBlueprintBuilderTargetKind::MaterialGraph;
		return true;
	}
	if (S.Equals(TEXT("niagara"), ESearchCase::IgnoreCase))
	{
		OutKind = EUnrealAiBlueprintBuilderTargetKind::Niagara;
		return true;
	}
	if (S.Equals(TEXT("widget_blueprint"), ESearchCase::IgnoreCase))
	{
		OutKind = EUnrealAiBlueprintBuilderTargetKind::WidgetBlueprint;
		return true;
	}
	return false;
}

EUnrealAiBlueprintBuilderTargetKind UnrealAiBlueprintBuilderTargetKind::ParseFromString(const FString& In)
{
	EUnrealAiBlueprintBuilderTargetKind Kind = EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint;
	if (TryParseFromString(In, Kind))
	{
		return Kind;
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
