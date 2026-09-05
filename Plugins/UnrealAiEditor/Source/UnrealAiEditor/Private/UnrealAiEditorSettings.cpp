#include "UnrealAiEditorSettings.h"

#include "UnrealAiEditorModule.h"

#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

UUnrealAiEditorSettings::UUnrealAiEditorSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultAgentId(TEXT("stub-agent"))
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

#if WITH_EDITOR
void UUnrealAiEditorSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropName = PropertyChangedEvent.GetPropertyName();
	if (PropName == GET_MEMBER_NAME_CHECKED(UUnrealAiEditorSettings, bMcpBridgeEnabled)
		|| PropName == GET_MEMBER_NAME_CHECKED(UUnrealAiEditorSettings, McpBridgePort)
		|| PropName == GET_MEMBER_NAME_CHECKED(UUnrealAiEditorSettings, McpBridgeInvokeTimeoutSeconds))
	{
		FUnrealAiEditorModule::Get().RefreshMcpBridge();
	}
}
#endif
