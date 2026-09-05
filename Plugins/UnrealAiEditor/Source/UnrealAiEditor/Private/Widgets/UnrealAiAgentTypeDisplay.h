#pragma once

#include "CoreMinimal.h"

struct FUnrealAiAgentTypeDisplayInfo
{
	FString Key;
	FText Label;
	FLinearColor Color = FLinearColor::White;
};

namespace UnrealAiAgentTypeDisplay
{
	namespace Keys
	{
		inline constexpr const TCHAR* Unknown = TEXT("unknown");
		inline constexpr const TCHAR* ModeAsk = TEXT("mode.ask");
		inline constexpr const TCHAR* ModeAgent = TEXT("mode.agent");
		inline constexpr const TCHAR* ModePlan = TEXT("mode.plan");

		inline constexpr const TCHAR* ExecutionMainRun = TEXT("execution.main_run");
		inline constexpr const TCHAR* ExecutionPlannerPass = TEXT("execution.planner_pass");
		inline constexpr const TCHAR* ExecutionPlanWorker = TEXT("execution.plan_worker");
		inline constexpr const TCHAR* ExecutionBuilderSubturn = TEXT("execution.builder_subturn");
		inline constexpr const TCHAR* ExecutionSpecialistSubturn = TEXT("execution.specialist_subturn");

		inline constexpr const TCHAR* BlueprintScript = TEXT("blueprint.script_blueprint");
		inline constexpr const TCHAR* BlueprintAnim = TEXT("blueprint.anim_blueprint");
		inline constexpr const TCHAR* BlueprintMaterialInstance = TEXT("blueprint.material_instance");
		inline constexpr const TCHAR* BlueprintMaterialGraph = TEXT("blueprint.material_graph");
		inline constexpr const TCHAR* BlueprintNiagara = TEXT("blueprint.niagara");
		inline constexpr const TCHAR* BlueprintWidget = TEXT("blueprint.widget_blueprint");

		inline constexpr const TCHAR* SpecialistScene = TEXT("specialist.scene");
		inline constexpr const TCHAR* SpecialistAssets = TEXT("specialist.assets");
		inline constexpr const TCHAR* SpecialistViewport = TEXT("specialist.viewport");
		inline constexpr const TCHAR* SpecialistDiagnostics = TEXT("specialist.diagnostics");
		inline constexpr const TCHAR* SpecialistPlaytest = TEXT("specialist.playtest");
		inline constexpr const TCHAR* SpecialistAnimation = TEXT("specialist.animation");
		inline constexpr const TCHAR* SpecialistProjectIntel = TEXT("specialist.project_intel");
		inline constexpr const TCHAR* SpecialistEditorUi = TEXT("specialist.editor_ui");
		inline constexpr const TCHAR* SpecialistSettings = TEXT("specialist.settings");
		inline constexpr const TCHAR* SpecialistMaterials = TEXT("specialist.materials");
	}

	FUnrealAiAgentTypeDisplayInfo Resolve(const FString& Key, const FString& LabelOverride = FString());
	FLinearColor ColorFor(const FString& Key);
	FText LabelFor(const FString& Key, const FString& LabelOverride = FString());
	FString SpecialistDisplayNameToKey(const FString& BuilderDisplayName);
	FString BlueprintDomainToKey(const FString& Domain);
}
