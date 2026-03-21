#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/** Muted "reasoning" lane (real provider content only; styled like Cursor). */
class SThinkingBlock final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SThinkingBlock) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void Append(const FString& Chunk);
	void SetFullText(const FString& Text);

private:
	FString Accumulated;
	TSharedPtr<class STextBlock> TextBlock;
};
