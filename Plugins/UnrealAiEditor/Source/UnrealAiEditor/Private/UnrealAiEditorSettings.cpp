#include "UnrealAiEditorSettings.h"

UUnrealAiEditorSettings::UUnrealAiEditorSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultAgentId(TEXT("stub-openrouter"))
	, bAutoConnect(false)
	, bVerboseLogging(false)
	, bStreamLlmChat(true)
	, bAssistantTypewriter(true)
	, AssistantTypewriterCps(400.f)
	, bAutoContinuePlanSteps(true)
{
}

FName UUnrealAiEditorSettings::GetCategoryName() const
{
	return FName(TEXT("Plugins"));
}
