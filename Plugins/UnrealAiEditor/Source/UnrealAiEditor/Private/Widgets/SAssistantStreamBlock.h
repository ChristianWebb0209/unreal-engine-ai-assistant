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

	/** True while typewriter still has queued characters (not caught up to the streaming buffer). */
	bool HasPendingReveal() const { return PendingBuffer.Len() > 0; }

	/**
	 * Replace buffer (e.g. after transcript rebuild).
	 * @param bInstantReveal If false and typewriter is enabled, reveal character-by-character. If true (or typewriter off), show immediately.
	 */
	void SetFullText(const FString& Full, bool bInstantReveal);

private:
	EActiveTimerReturnType TickTypewriter(double CurrentTime, float DeltaTime);
	void SyncMarkdownBody();

	bool bEnableTypewriter = true;
	float TypewriterCps = 400.f;
	FSimpleDelegate OnRevealTick;
	FString PendingBuffer;
	FString VisibleText;
	/** Fractional carry for time-based CPS (one reveal step = one UTF-16 code unit). */
	float RevealCarry = 0.f;
	TSharedPtr<SBox> BodyBox;
	bool bTimerActive = false;
};
