#include "Widgets/Plan/SPlanNodeStatusBadge.h"

#include "Style/UnrealAiEditorStyle.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

FString SPlanNodeStatusBadge::NormalizeToken(const FString& Raw)
{
	FString S = Raw.TrimStartAndEnd();
	int32 Colon = INDEX_NONE;
	if (S.FindChar(TEXT(':'), Colon))
	{
		S = S.Left(Colon).TrimEnd();
	}
	return S.ToLower();
}

void SPlanNodeStatusBadge::Construct(const FArguments& InArgs)
{
	StatusRaw = InArgs._StatusRaw;

	ChildSlot
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
				.Padding(FMargin(8.f, 2.f))
				.BorderBackgroundColor_Lambda([this]()
				{
					const FString T = NormalizeToken(StatusRaw.IsEmpty() ? TEXT("pending") : StatusRaw);
					if (T == TEXT("success"))
					{
						return FLinearColor(0.06f, 0.24f, 0.12f, 0.75f);
					}
					if (T == TEXT("failed"))
					{
						return FLinearColor(0.24f, 0.08f, 0.08f, 0.75f);
					}
					if (T == TEXT("skipped"))
					{
						return FLinearColor(0.18f, 0.18f, 0.22f, 0.85f);
					}
					if (T == TEXT("running"))
					{
						return FLinearColor(0.23f, 0.18f, 0.0f, 0.55f);
					}
					return FLinearColor(0.16f, 0.16f, 0.18f, 0.9f);
				})
				[
					SNew(STextBlock)
						.Font(FUnrealAiEditorStyle::FontMono8())
						.ColorAndOpacity(FSlateColor(FLinearColor(0.93f, 0.92f, 0.9f, 1.f)))
						.Text_Lambda([this]()
						{
							const FString T = NormalizeToken(StatusRaw.IsEmpty() ? TEXT("pending") : StatusRaw);
							const FText Label = T == TEXT("success")	 ? LOCTEXT("PlanStSuccess", "success")
								: T == TEXT("failed")					 ? LOCTEXT("PlanStFailed", "failed")
								: T == TEXT("skipped")				 ? LOCTEXT("PlanStSkipped", "skipped")
								: T == TEXT("running")				 ? LOCTEXT("PlanStRunning", "running")
																		 : LOCTEXT("PlanStPending", "pending");
							return Label;
						})
				]
		];
}

void SPlanNodeStatusBadge::SetStatusRaw(const FString& NewStatus)
{
	StatusRaw = NewStatus;
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

#undef LOCTEXT_NAMESPACE
