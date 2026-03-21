#include "Widgets/SAssistantStreamBlock.h"

#include "Widgets/Text/STextBlock.h"

void SAssistantStreamBlock::Construct(const FArguments& InArgs)
{
	bEnableTypewriter = InArgs._bEnableTypewriter;
	TypewriterCps = InArgs._TypewriterCps > 1.f ? InArgs._TypewriterCps : 400.f;

	ChildSlot
		[
			SAssignNew(TextWidget, STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.92f, 0.92f, 1.f)))
				.Text_Lambda([this]() { return FText::FromString(VisibleText); })
		];
}

void SAssistantStreamBlock::SetFullText(const FString& Full)
{
	PendingBuffer.Reset();
	VisibleText = Full;
	if (TextWidget.IsValid())
	{
		TextWidget->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
}

void SAssistantStreamBlock::AppendIncoming(const FString& Chunk)
{
	if (Chunk.IsEmpty())
	{
		return;
	}
	if (!bEnableTypewriter)
	{
		VisibleText += Chunk;
		if (TextWidget.IsValid())
		{
			TextWidget->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
		}
		return;
	}
	PendingBuffer += Chunk;
	if (!bTimerActive)
	{
		bTimerActive = true;
		RegisterActiveTimer(
			1.f / 60.f,
			FWidgetActiveTimerDelegate::CreateSP(this, &SAssistantStreamBlock::TickTypewriter));
	}
}

EActiveTimerReturnType SAssistantStreamBlock::TickTypewriter(double CurrentTime, float DeltaTime)
{
	const int32 PerTick = FMath::Max(1, FMath::CeilToInt((TypewriterCps / 60.f)));
	for (int32 i = 0; i < PerTick && PendingBuffer.Len() > 0; ++i)
	{
		VisibleText += PendingBuffer.Left(1);
		PendingBuffer = PendingBuffer.Mid(1);
	}
	if (TextWidget.IsValid())
	{
		TextWidget->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
	if (PendingBuffer.Len() > 0)
	{
		return EActiveTimerReturnType::Continue;
	}
	bTimerActive = false;
	return EActiveTimerReturnType::Stop;
}
