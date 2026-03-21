#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/** Assistant text with optional typewriter reveal (smooth output). */
class SAssistantStreamBlock final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssistantStreamBlock) {}
	SLATE_ARGUMENT(bool, bEnableTypewriter)
	SLATE_ARGUMENT(float, TypewriterCps)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Append new text from the model (may queue behind typewriter). */
	void AppendIncoming(const FString& Chunk);

	/** Replace buffer (e.g. after rebuild). */
	void SetFullText(const FString& Full);

private:
	EActiveTimerReturnType TickTypewriter(double CurrentTime, float DeltaTime);

	bool bEnableTypewriter = true;
	float TypewriterCps = 400.f;
	FString PendingBuffer;
	FString VisibleText;
	TSharedPtr<class STextBlock> TextWidget;
	bool bTimerActive = false;
};
