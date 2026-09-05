#include "Widgets/UnrealAiAgentTypeDisplay.h"

#define LOCTEXT_NAMESPACE "UnrealAiAgentTypeDisplay"

namespace UnrealAiAgentTypeDisplay
{
	struct FRegistryRow
	{
		const TCHAR* Key = Keys::Unknown;
		FText Label;
		FLinearColor Color = FLinearColor::White;
	};

	static const TArray<FRegistryRow>& Registry()
	{
		static const TArray<FRegistryRow> Rows = {
			{ Keys::Unknown, LOCTEXT("Unknown", "Agent"), FLinearColor(0.68f, 0.68f, 0.72f, 1.f) },
			{ Keys::ModeAsk, LOCTEXT("ModeAsk", "Ask"), FLinearColor(0.23f, 0.65f, 0.95f, 1.f) },
			{ Keys::ModeAgent, LOCTEXT("ModeAgent", "Agent"), FLinearColor(0.36f, 0.83f, 0.48f, 1.f) },
			{ Keys::ModePlan, LOCTEXT("ModePlan", "Plan"), FLinearColor(0.90f, 0.66f, 0.27f, 1.f) },
			{ Keys::ExecutionMainRun, LOCTEXT("ExecutionMainRun", "Main run"), FLinearColor(0.58f, 0.72f, 0.96f, 1.f) },
			{ Keys::ExecutionPlannerPass, LOCTEXT("ExecutionPlannerPass", "Planner"), FLinearColor(0.90f, 0.61f, 0.22f, 1.f) },
			{ Keys::ExecutionPlanWorker, LOCTEXT("ExecutionPlanWorker", "Plan worker"), FLinearColor(0.38f, 0.86f, 0.89f, 1.f) },
			{ Keys::ExecutionBuilderSubturn, LOCTEXT("ExecutionBuilderSubturn", "Blueprint Builder"), FLinearColor(0.70f, 0.55f, 0.96f, 1.f) },
			{ Keys::ExecutionSpecialistSubturn, LOCTEXT("ExecutionSpecialistSubturn", "Specialist"), FLinearColor(0.95f, 0.53f, 0.63f, 1.f) },
			{ Keys::BlueprintScript, LOCTEXT("BlueprintScript", "Script Blueprint"), FLinearColor(0.71f, 0.68f, 0.96f, 1.f) },
			{ Keys::BlueprintAnim, LOCTEXT("BlueprintAnim", "Anim Blueprint"), FLinearColor(0.95f, 0.60f, 0.52f, 1.f) },
			{ Keys::BlueprintMaterialInstance, LOCTEXT("BlueprintMaterialInstance", "Material Instance"), FLinearColor(0.93f, 0.77f, 0.30f, 1.f) },
			{ Keys::BlueprintMaterialGraph, LOCTEXT("BlueprintMaterialGraph", "Material Graph"), FLinearColor(0.94f, 0.48f, 0.28f, 1.f) },
			{ Keys::BlueprintNiagara, LOCTEXT("BlueprintNiagara", "Niagara"), FLinearColor(0.31f, 0.92f, 0.95f, 1.f) },
			{ Keys::BlueprintWidget, LOCTEXT("BlueprintWidget", "Widget Blueprint"), FLinearColor(0.97f, 0.54f, 0.80f, 1.f) },
			{ Keys::SpecialistScene, LOCTEXT("SpecialistScene", "Scene specialist"), FLinearColor(0.95f, 0.52f, 0.52f, 1.f) },
			{ Keys::SpecialistAssets, LOCTEXT("SpecialistAssets", "Assets specialist"), FLinearColor(0.97f, 0.67f, 0.36f, 1.f) },
			{ Keys::SpecialistViewport, LOCTEXT("SpecialistViewport", "Viewport specialist"), FLinearColor(0.31f, 0.77f, 0.96f, 1.f) },
			{ Keys::SpecialistDiagnostics, LOCTEXT("SpecialistDiagnostics", "Diagnostics specialist"), FLinearColor(0.92f, 0.41f, 0.41f, 1.f) },
			{ Keys::SpecialistPlaytest, LOCTEXT("SpecialistPlaytest", "Playtest specialist"), FLinearColor(0.37f, 0.86f, 0.57f, 1.f) },
			{ Keys::SpecialistAnimation, LOCTEXT("SpecialistAnimation", "Animation specialist"), FLinearColor(0.82f, 0.55f, 0.96f, 1.f) },
			{ Keys::SpecialistProjectIntel, LOCTEXT("SpecialistProjectIntel", "Project intelligence specialist"), FLinearColor(0.47f, 0.73f, 0.95f, 1.f) },
			{ Keys::SpecialistEditorUi, LOCTEXT("SpecialistEditorUi", "Editor UI specialist"), FLinearColor(0.52f, 0.85f, 0.92f, 1.f) },
			{ Keys::SpecialistSettings, LOCTEXT("SpecialistSettings", "Settings specialist"), FLinearColor(0.89f, 0.71f, 0.34f, 1.f) },
			{ Keys::SpecialistMaterials, LOCTEXT("SpecialistMaterials", "Materials specialist"), FLinearColor(0.95f, 0.62f, 0.25f, 1.f) },
		};
		return Rows;
	}

	static bool FindByKey(const FString& Key, FRegistryRow& Out)
	{
		for (const FRegistryRow& Row : Registry())
		{
			if (Key.Equals(Row.Key, ESearchCase::CaseSensitive))
			{
				Out = Row;
				return true;
			}
		}
		return false;
	}

	FUnrealAiAgentTypeDisplayInfo Resolve(const FString& Key, const FString& LabelOverride)
	{
		FRegistryRow Row;
		if (!FindByKey(Key, Row))
		{
			FindByKey(Keys::Unknown, Row);
		}
		FUnrealAiAgentTypeDisplayInfo Info;
		Info.Key = Key.IsEmpty() ? FString(Keys::Unknown) : Key;
		Info.Label = LabelOverride.IsEmpty() ? Row.Label : FText::FromString(LabelOverride);
		Info.Color = Row.Color;
		return Info;
	}

	FLinearColor ColorFor(const FString& Key)
	{
		return Resolve(Key).Color;
	}

	FText LabelFor(const FString& Key, const FString& LabelOverride)
	{
		return Resolve(Key, LabelOverride).Label;
	}

	FString SpecialistDisplayNameToKey(const FString& BuilderDisplayName)
	{
		const FString Lower = BuilderDisplayName.ToLower();
		if (Lower.Contains(TEXT("scene specialist")))
		{
			return Keys::SpecialistScene;
		}
		if (Lower.Contains(TEXT("assets specialist")))
		{
			return Keys::SpecialistAssets;
		}
		if (Lower.Contains(TEXT("viewport specialist")))
		{
			return Keys::SpecialistViewport;
		}
		if (Lower.Contains(TEXT("diagnostics specialist")))
		{
			return Keys::SpecialistDiagnostics;
		}
		if (Lower.Contains(TEXT("playtest specialist")))
		{
			return Keys::SpecialistPlaytest;
		}
		if (Lower.Contains(TEXT("animation specialist")))
		{
			return Keys::SpecialistAnimation;
		}
		if (Lower.Contains(TEXT("project intelligence specialist")))
		{
			return Keys::SpecialistProjectIntel;
		}
		if (Lower.Contains(TEXT("editor ui specialist")))
		{
			return Keys::SpecialistEditorUi;
		}
		if (Lower.Contains(TEXT("settings specialist")))
		{
			return Keys::SpecialistSettings;
		}
		if (Lower.Contains(TEXT("materials specialist")))
		{
			return Keys::SpecialistMaterials;
		}
		return Keys::ExecutionSpecialistSubturn;
	}

	FString BlueprintDomainToKey(const FString& Domain)
	{
		const FString Lower = Domain.TrimStartAndEnd().ToLower();
		if (Lower == TEXT("script_blueprint"))
		{
			return Keys::BlueprintScript;
		}
		if (Lower == TEXT("anim_blueprint"))
		{
			return Keys::BlueprintAnim;
		}
		if (Lower == TEXT("material_instance"))
		{
			return Keys::BlueprintMaterialInstance;
		}
		if (Lower == TEXT("material_graph"))
		{
			return Keys::BlueprintMaterialGraph;
		}
		if (Lower == TEXT("niagara"))
		{
			return Keys::BlueprintNiagara;
		}
		if (Lower == TEXT("widget_blueprint"))
		{
			return Keys::BlueprintWidget;
		}
		return Keys::ExecutionBuilderSubturn;
	}
}

#undef LOCTEXT_NAMESPACE
