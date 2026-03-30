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
	, BlueprintDefaultLayoutStrategy(EUnrealAiBlueprintDefaultLayoutStrategy::SingleRow)
	, BlueprintDefaultWireKnotMode(EUnrealAiBlueprintDefaultWireKnotMode::Off)
	, PlanAutoReplanMaxAttemptsPerRun(2)
	, bLogLlmRequestsToHarnessRunFile(true)
{
}

FName UUnrealAiEditorSettings::GetCategoryName() const
{
	return FName(TEXT("Plugins"));
}
