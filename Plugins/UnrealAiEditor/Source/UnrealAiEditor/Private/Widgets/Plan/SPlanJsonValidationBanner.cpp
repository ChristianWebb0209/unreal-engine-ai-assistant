#include "Widgets/Plan/SPlanJsonValidationBanner.h"

#include "Style/UnrealAiEditorStyle.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void SPlanJsonValidationBanner::Construct(const FArguments& InArgs)
{
	Message = InArgs._Message;
	bIsError = InArgs._bIsError;

	ChildSlot
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
				.BorderBackgroundColor_Lambda([this]()
				{
					return bIsError ? FLinearColor(0.28f, 0.12f, 0.12f, 0.9f) : FLinearColor(0.14f, 0.16f, 0.22f, 0.92f);
				})
				.Padding(FMargin(8.f, 6.f))
				.Visibility_Lambda([this]()
				{
					return Message.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
				})
				[
					SNew(STextBlock)
						.AutoWrapText(true)
						.Font(FUnrealAiEditorStyle::FontRegular10())
						.ColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.9f, 0.88f, 1.f)))
						.Text_Lambda([this]() { return FText::FromString(Message); })
				]
		];
}

void SPlanJsonValidationBanner::SetMessage(const FString& NewMessage, const bool bInIsError)
{
	Message = NewMessage;
	bIsError = bInIsError;
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

#undef LOCTEXT_NAMESPACE
