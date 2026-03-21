#include "Widgets/SThinkingBlock.h"

#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void SThinkingBlock::Construct(const FArguments& InArgs)
{
	ChildSlot
		[
			SNew(SBorder)
				.BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.14f, 0.55f))
				.Padding(FMargin(4.f))
				[
					SNew(SExpandableArea)
						.InitiallyCollapsed(true)
						.BorderBackgroundColor(FLinearColor(0.14f, 0.14f, 0.16f, 0.9f))
						.HeaderContent()
						[
							SNew(STextBlock)
								.Text(LOCTEXT("ThinkingHdr", "Thinking"))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.68f, 0.75f, 1.f)))
						]
						.BodyContent()
						[
							SAssignNew(TextBlock, STextBlock)
								.AutoWrapText(true)
								.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.58f, 0.92f)))
								.Text_Lambda([this]() { return FText::FromString(Accumulated); })
						]
				]
		];
}

void SThinkingBlock::Append(const FString& Chunk)
{
	Accumulated += Chunk;
	if (TextBlock.IsValid())
	{
		TextBlock->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
}

void SThinkingBlock::SetFullText(const FString& Text)
{
	Accumulated = Text;
	if (TextBlock.IsValid())
	{
		TextBlock->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
}

#undef LOCTEXT_NAMESPACE
