#include "Widgets/SAssistantStreamBlock.h"

#include "Widgets/UnrealAiChatMarkdown.h"
#include "Widgets/Layout/SBox.h"

void SAssistantStreamBlock::Construct(const FArguments& InArgs)
{
	bEnableTypewriter = InArgs._bEnableTypewriter;
	TypewriterCps = InArgs._TypewriterCps > 1.f ? InArgs._TypewriterCps : 88.f;
	OnRevealTick = InArgs._OnRevealTick;
	// When typewriter is off, still reveal progressively at a readable rate (not instant wall of text).
	if (!bEnableTypewriter)
	{
		TypewriterCps = FMath::Clamp(FMath::Max(TypewriterCps, 520.f), 400.f, 2200.f);
	}

	ChildSlot
		[
			SAssignNew(BodyBox, SBox)
			[
				UnrealAiBuildMarkdownChatBody(VisibleText)
			]
		];
}

void SAssistantStreamBlock::UpdateTextFadeOpacity()
{
	if (bSkipTextFade)
	{
		SetRenderOpacity(1.f);
		return;
	}
	// Subtle fade-in over the first ~72 visible characters (streaming + typewriter).
	const int32 Len = VisibleText.Len();
	if (Len <= 0)
	{
		SetRenderOpacity(0.94f);
		return;
	}
	const float T = FMath::Clamp(static_cast<float>(Len) / 72.f, 0.f, 1.f);
	const float Eased = T * T * (3.f - 2.f * T);
	const float Opac = FMath::Lerp(0.88f, 0.96f, Eased);
	SetRenderOpacity(Opac);
}

void SAssistantStreamBlock::SyncMarkdownBody()
{
	if (BodyBox.IsValid())
	{
		BodyBox->SetContent(UnrealAiBuildMarkdownChatBody(VisibleText));
	}
	UpdateTextFadeOpacity();
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SAssistantStreamBlock::SetFullText(const FString& Full, bool bInstantReveal)
{
	bSkipTextFade = bInstantReveal;
	if (bInstantReveal)
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
			1.f / 60.f,
			FWidgetActiveTimerDelegate::CreateSP(this, &SAssistantStreamBlock::TickTypewriter));
	}
}

void SAssistantStreamBlock::AppendIncoming(const FString& Chunk)
{
	if (Chunk.IsEmpty())
	{
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
	(void)CurrentTime;
	(void)DeltaTime;
	const float Cps = FMath::Max(18.f, TypewriterCps);
	const int32 PerTick = FMath::Max(1, FMath::CeilToInt((Cps / 60.f)));
	for (int32 i = 0; i < PerTick && PendingBuffer.Len() > 0; ++i)
	{
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
	bTimerActive = false;
	return EActiveTimerReturnType::Stop;
}
