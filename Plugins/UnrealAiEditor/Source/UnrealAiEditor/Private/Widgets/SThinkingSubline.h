#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;

/** Single muted line of model reasoning below the assistant bubble (Cursor-style). */
class SThinkingSubline final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SThinkingSubline) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void Append(const FString& Chunk);
	void SetFullText(const FString& Text);

private:
	EActiveTimerReturnType TickDots(double CurrentTime, float DeltaTime);
	static FString ToOneLinePreview(const FString& Text);

	TSharedPtr<STextBlock> LineText;
	FString Accumulated;
	int32 DotsPhase = 0;
};
