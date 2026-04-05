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
	, bBlueprintFormatUseWireKnots(false)
	, bBlueprintFormatPreserveExistingPositions(false)
	, bBlueprintFormatReflowCommentsByGeometry(true)
	, PlanAutoReplanMaxAttemptsPerRun(2)
	, bLogLlmRequestsToHarnessRunFile(true)
	, bConsoleCommandLegacyWideExec(false)
	, bGameThreadPerfLogging(true)
	, GameThreadPerfLogThresholdMs(0.f)
	, GameThreadPerfHitchThresholdMs(1000.f)
	, GameThreadPerfRingMax(256)
	, MaxOpsPerBlueprintGraphPatch(128)
	, bBlueprintGraphPatchInternalValidateBeforeApply(true)
	, bBlueprintGraphPatchKeepOpsOnFailure(true)
{
	AgentContentPathExcludeFromPreferredInference.Add(TEXT("UnrealAiHarness"));
}

FName UUnrealAiEditorSettings::GetCategoryName() const
{
	return FName(TEXT("Plugins"));
}
