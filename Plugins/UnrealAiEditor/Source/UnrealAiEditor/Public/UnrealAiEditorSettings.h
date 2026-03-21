#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UnrealAiEditorSettings.generated.h"

struct FPropertyChangedEvent;

UCLASS(Config = Editor, defaultconfig, meta = (DisplayName = "Unreal AI Editor"))
class UUnrealAiEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UUnrealAiEditorSettings(const FObjectInitializer& ObjectInitializer);

	virtual FName GetCategoryName() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UPROPERTY(EditAnywhere, Config, Category = "Agent")
	FString DefaultAgentId;

	UPROPERTY(EditAnywhere, Config, Category = "Agent")
	bool bAutoConnect;

	UPROPERTY(EditAnywhere, Config, Category = "Agent")
	bool bVerboseLogging;

	UPROPERTY(EditAnywhere, Config, Category = "OpenRouter")
	FString OpenRouterApiKey;

	UPROPERTY(EditAnywhere, Config, Category = "OpenRouter")
	FString DefaultModelId;

	/** Open Agent Chat automatically when the Level Editor finishes loading (Nomad tab). */
	UPROPERTY(EditAnywhere, Config, Category = "UI", meta = (DisplayName = "Open Agent Chat on editor startup"))
	bool bOpenAgentChatOnStartup;

	/** Stream LLM responses for smoother chat output (recommended). */
	UPROPERTY(EditAnywhere, Config, Category = "Chat")
	bool bStreamLlmChat;

	/** Reveal assistant text with a typewriter effect (also smooths single-chunk responses). */
	UPROPERTY(EditAnywhere, Config, Category = "Chat")
	bool bAssistantTypewriter;

	/** Characters per second for typewriter mode. */
	UPROPERTY(EditAnywhere, Config, Category = "Chat", meta = (ClampMin = "10", ClampMax = "8000"))
	float AssistantTypewriterCps;

	/** Background for your messages in Agent Chat (warm dark; distinct from agent). */
	UPROPERTY(EditAnywhere, Config, Category = "Chat", meta = (DisplayName = "User message bubble color"))
	FLinearColor UserChatBubbleColor;

	/** Background for assistant messages in Agent Chat (cool dark slate). */
	UPROPERTY(EditAnywhere, Config, Category = "Chat", meta = (DisplayName = "Agent message bubble color"))
	FLinearColor AgentChatBubbleColor;

	/** When off, the harness should pause between plan steps (UI hint; harness wiring may follow). */
	UPROPERTY(EditAnywhere, Config, Category = "Agent", meta = (DisplayName = "Auto-continue plan steps"))
	bool bAutoContinuePlanSteps;
};
