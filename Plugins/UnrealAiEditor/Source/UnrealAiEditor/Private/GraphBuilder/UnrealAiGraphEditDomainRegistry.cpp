#include "GraphBuilder/UnrealAiGraphEditDomain.h"

namespace UnrealAiGraphEditDomainEntry
{
	IUnrealAiGraphEditDomain* ScriptBlueprint();
	IUnrealAiGraphEditDomain* AnimBlueprint();
	IUnrealAiGraphEditDomain* MaterialInstance();
	IUnrealAiGraphEditDomain* MaterialGraph();
	IUnrealAiGraphEditDomain* Niagara();
	IUnrealAiGraphEditDomain* WidgetBlueprint();
}

IUnrealAiGraphEditDomain* FUnrealAiGraphEditDomainRegistry::Get(EUnrealAiBlueprintBuilderTargetKind Kind)
{
	using namespace UnrealAiGraphEditDomainEntry;
	switch (Kind)
	{
	case EUnrealAiBlueprintBuilderTargetKind::AnimBlueprint:
		return AnimBlueprint();
	case EUnrealAiBlueprintBuilderTargetKind::MaterialInstance:
		return MaterialInstance();
	case EUnrealAiBlueprintBuilderTargetKind::MaterialGraph:
		return MaterialGraph();
	case EUnrealAiBlueprintBuilderTargetKind::Niagara:
		return Niagara();
	case EUnrealAiBlueprintBuilderTargetKind::WidgetBlueprint:
		return WidgetBlueprint();
	case EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint:
	default:
		return ScriptBlueprint();
	}
}
