#include "Widgets/SAssistantStreamBlock.h"

#include "Tools/UnrealAiBuildBlueprintTag.h"
#include "Widgets/UnrealAiChatMarkdown.h"
#include "Widgets/UnrealAiChatTranscript.h"
#include "Style/UnrealAiEditorStyle.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/SMultiLineEditableText.h"

namespace
{
	TSharedRef<SWidget> BuildStreamingPlainBody(const FString& Text)
	{
		FTextBlockStyle BodyStyle =
			FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("NormalText"));
		BodyStyle.SetFont(FUnrealAiEditorStyle::FontBodyRegular11());
		BodyStyle.SetColorAndOpacity(FUnrealAiEditorStyle::ColorMarkdownBody());
		return SNew(SMultiLineEditableText)
			.IsReadOnly(true)
			.AutoWrapText(true)
			.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
			.AllowContextMenu(true)
			.SelectAllTextWhenFocused(false)
			.TextStyle(&BodyStyle)
			.Text(FText::FromString(Text));
	}
}

void SAssistantStreamBlock::Construct(const FArguments& InArgs)
{
	bEnableTypewriter = InArgs._bEnableTypewriter;
	TypewriterCps = FMath::Clamp(InArgs._TypewriterCps > 1.f ? InArgs._TypewriterCps : 88.f, 10.f, 8000.f);
	OnRevealTick = InArgs._OnRevealTick;

	ChildSlot
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
				.Padding(FMargin(0.f))
				[
					SAssignNew(BodyBox, SBox)
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Top)
						.MinDesiredHeight(18.f)
						.Clipping(EWidgetClipping::ClipToBounds)
						[
							SNullWidget::NullWidget
						]
				]
		];
	SyncMarkdownBody();
}

void SAssistantStreamBlock::SyncMarkdownBody()
{
	const FString ForDisplay = bEnableTypewriter ? VisibleText : (VisibleText + PendingBuffer);
	FString MutableDisplay = ForDisplay;
	UnrealAiStripTranscriptStyleDelimiterLines(MutableDisplay);
	UnrealAiBuildBlueprintTag::StripProtocolMarkersForUi(MutableDisplay);
	if (BodyBox.IsValid())
	{
		// During live typewriter reveal, keep a plain wrapped text body to avoid
		// markdown subtree churn causing transient horizontal drift near line wraps.
		if (bEnableTypewriter && PendingBuffer.Len() > 0)
		{
			BodyBox->SetContent(BuildStreamingPlainBody(MutableDisplay));
		}
		else
		{
			BodyBox->SetContent(UnrealAiBuildMarkdownChatBody(MutableDisplay));
		}
	}
	SetRenderOpacity(1.f);
	// Replacing the markdown subtree requires layout + prepass; paint-only leaves stale line heights until the next frame.
	Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Prepass | EInvalidateWidgetReason::Paint);
}

void SAssistantStreamBlock::SetFullText(const FString& Full, bool bInstantReveal)
{
	RevealCarry = 0.f;
	if (bInstantReveal || !bEnableTypewriter)
	{
		PendingBuffer.Reset();
		bTimerActive = false;
		VisibleText = Full;
		SyncMarkdownBody();
		return;
	}

	VisibleText.Reset();
	PendingBuffer = Full;
	SyncMarkdownBody();
	if (Full.IsEmpty())
	{
		bTimerActive = false;
		return;
	}
	if (!bTimerActive)
	{
		bTimerActive = true;
		RegisterActiveTimer(
			0.f,
			FWidgetActiveTimerDelegate::CreateSP(this, &SAssistantStreamBlock::TickTypewriter));
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
		SyncMarkdownBody();
		if (OnRevealTick.IsBound())
		{
			OnRevealTick.Execute();
		}
		return;
	}
	PendingBuffer += Chunk;
	if (!bTimerActive)
	{
		bTimerActive = true;
		RegisterActiveTimer(
			0.f,
			FWidgetActiveTimerDelegate::CreateSP(this, &SAssistantStreamBlock::TickTypewriter));
	}
}

EActiveTimerReturnType SAssistantStreamBlock::TickTypewriter(double CurrentTime, float DeltaTime)
{
	(void)CurrentTime;
	const float Cps = FMath::Max(1.f, TypewriterCps);
	RevealCarry += DeltaTime * Cps;
	while (RevealCarry >= 1.f && PendingBuffer.Len() > 0)
	{
		RevealCarry -= 1.f;
		VisibleText += PendingBuffer.Left(1);
		PendingBuffer = PendingBuffer.Mid(1);
	}
	SyncMarkdownBody();
	if (OnRevealTick.IsBound())
	{
		OnRevealTick.Execute();
	}
	if (PendingBuffer.Len() > 0)
	{
		return EActiveTimerReturnType::Continue;
	}
	RevealCarry = 0.f;
	bTimerActive = false;
	return EActiveTimerReturnType::Stop;
}
