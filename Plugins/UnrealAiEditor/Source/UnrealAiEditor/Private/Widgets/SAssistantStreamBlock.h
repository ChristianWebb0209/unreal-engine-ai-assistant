#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SBox;

/** Assistant text with optional typewriter reveal (smooth output). */
class SAssistantStreamBlock final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssistantStreamBlock) {}
	SLATE_ARGUMENT(bool, bEnableTypewriter)
	SLATE_ARGUMENT(float, TypewriterCps)
	/** Fired when visible text advances (typewriter); parent can coalesce auto-scroll. */
	SLATE_EVENT(FSimpleDelegate, OnRevealTick)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Append new text from the model (may queue behind typewriter). */
	void AppendIncoming(const FString& Chunk);

	/**
	 * Replace buffer (e.g. after transcript rebuild).
	 * @param bInstantReveal If false, reveal with typewriter (for live assistant text). If true, show immediately (history / scroll-back).
	 */
	void SetFullText(const FString& Full, bool bInstantReveal);

private:
	EActiveTimerReturnType TickTypewriter(double CurrentTime, float DeltaTime);
	void SyncMarkdownBody();
	void UpdateTextFadeOpacity();

	bool bEnableTypewriter = true;
	float TypewriterCps = 400.f;
	/** When true (history / scroll-back), skip subtle fade and use full opacity. */
	bool bSkipTextFade = false;
	FSimpleDelegate OnRevealTick;
	FString PendingBuffer;
	FString VisibleText;
	TSharedPtr<SBox> BodyBox;
	bool bTimerActive = false;
};
