#include "UnrealAiEditorSettings.h"

#if WITH_EDITOR
#include "UObject/UnrealType.h"
#include "Style/UnrealAiEditorStyle.h"
#endif

UUnrealAiEditorSettings::UUnrealAiEditorSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultAgentId(TEXT("stub-openrouter"))
	, bAutoConnect(false)
	, bVerboseLogging(false)
	, bOpenAgentChatOnStartup(true)
	, bStreamLlmChat(true)
	, bAssistantTypewriter(true)
	, AssistantTypewriterCps(400.f)
	, UserChatBubbleColor(FLinearColor(0.13f, 0.105f, 0.095f, 1.f))
	, AgentChatBubbleColor(FLinearColor(0.092f, 0.098f, 0.114f, 1.f))
	, bAutoContinuePlanSteps(true)
{
}

#if WITH_EDITOR
void UUnrealAiEditorSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property)
	{
		const FName N = PropertyChangedEvent.Property->GetFName();
		if (N == GET_MEMBER_NAME_CHECKED(UUnrealAiEditorSettings, UserChatBubbleColor)
			|| N == GET_MEMBER_NAME_CHECKED(UUnrealAiEditorSettings, AgentChatBubbleColor))
		{
			FUnrealAiEditorStyle::ApplyChatBubbleColorsFromSettings();
		}
	}
}
#endif

FName UUnrealAiEditorSettings::GetCategoryName() const
{
	return FName(TEXT("Plugins"));
}
