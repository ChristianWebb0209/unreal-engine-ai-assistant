#include "Widgets/SThinkingSubline.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

FString SThinkingSubline::ToOneLinePreview(const FString& Text)
{
	FString T = Text;
	T.ReplaceInline(TEXT("\r"), TEXT(""));
	T.ReplaceInline(TEXT("\n"), TEXT(" "));
	T.ReplaceInline(TEXT("\t"), TEXT(" "));
	while (T.ReplaceInline(TEXT("  "), TEXT(" ")))
	{
	}
	T.TrimStartAndEndInline();
	const int32 MaxLen = 400;
	if (T.Len() > MaxLen)
	{
		T.LeftInline(MaxLen);
		T.Append(TEXT("…"));
	}
	return T;
}

void SThinkingSubline::Construct(const FArguments& InArgs)
{
	(void)InArgs;
	ChildSlot
		[
			SNew(SBox)
				.Padding(FMargin(2.f, 4.f, 0.f, 0.f))
				[
					SAssignNew(LineText, STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Italic"), 9))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.52f, 0.56f, 0.72f)))
						.AutoWrapText(false)
						.Text(FText::FromString(TEXT("Thinking...")))
				]
		];

	RegisterActiveTimer(
		0.35f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SThinkingSubline::TickDots));
}

EActiveTimerReturnType SThinkingSubline::TickDots(double, float)
{
	static const TCHAR* Phases[] = {
		TEXT("Thinking."),
		TEXT("Thinking.."),
		TEXT("Thinking..."),
		TEXT("Thinking..")
	};
	// Stop animating once we have real reasoning text; otherwise this timer runs forever (looks like an
	// infinite "thinking" loop even when the model is idle or waiting on tools).
	if (!Accumulated.IsEmpty())
	{
		return EActiveTimerReturnType::Stop;
	}
	if (LineText.IsValid())
	{
		DotsPhase = (DotsPhase + 1) % 4;
		LineText->SetText(FText::FromString(Phases[DotsPhase]));
	}
	return EActiveTimerReturnType::Continue;
}

void SThinkingSubline::Append(const FString& Chunk)
{
	Accumulated += Chunk;
	if (LineText.IsValid())
	{
		if (Accumulated.IsEmpty())
		{
			return;
		}
		LineText->SetText(FText::FromString(ToOneLinePreview(Accumulated)));
	}
}

void SThinkingSubline::SetFullText(const FString& Text)
{
	Accumulated = Text;
	if (!LineText.IsValid())
	{
		return;
	}
	if (Accumulated.IsEmpty())
	{
		LineText->SetText(FText::FromString(TEXT("Thinking...")));
	}
	else
	{
		LineText->SetText(FText::FromString(ToOneLinePreview(Accumulated)));
	}
}

#undef LOCTEXT_NAMESPACE
