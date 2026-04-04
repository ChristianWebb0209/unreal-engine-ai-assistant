#include "UnrealAiEditorSettings.h"

UUnrealAiEditorSettings::UUnrealAiEditorSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultAgentId(TEXT("stub-openrouter"))
	, bAutoConnect(false)
	, bVerboseLogging(false)
	, bOpenAgentChatOnStartup(true)
	, bStreamLlmChat(true)
	, bAssistantTypewriter(true)
	, AssistantTypewriterCps(88.f)
	, bAutoContinuePlanSteps(true)
	, bPlanAutoReplan(true)
	, BlueprintCommentsMode(EUnrealAiBlueprintCommentsMode::Minimal)
	, BlueprintFormatSpacingDensity(EUnrealAiBlueprintFormatSpacingDensity::Medium)
	, bBlueprintFormatUseWireKnots(false)
	, bBlueprintFormatPreserveExistingPositions(false)
	, bBlueprintFormatReflowCommentsByGeometry(true)
	, PlanAutoReplanMaxAttemptsPerRun(2)
	, bLogLlmRequestsToHarnessRunFile(true)
	, bConsoleCommandLegacyWideExec(false)
{
	AgentContentPathExcludeFromPreferredInference.Add(TEXT("UnrealAiHarness"));
}

FName UUnrealAiEditorSettings::GetCategoryName() const
{
	return FName(TEXT("Plugins"));
}
