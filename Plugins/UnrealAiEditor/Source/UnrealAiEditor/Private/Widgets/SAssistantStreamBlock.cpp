#include "Widgets/SAssistantStreamBlock.h"

#include "Widgets/UnrealAiChatMarkdown.h"
#include "Widgets/Layout/SBox.h"

void SAssistantStreamBlock::Construct(const FArguments& InArgs)
{
	bEnableTypewriter = InArgs._bEnableTypewriter;
	TypewriterCps = InArgs._TypewriterCps > 1.f ? InArgs._TypewriterCps : 400.f;
	OnRevealTick = InArgs._OnRevealTick;
	// When the editor setting disables typewriter, still reveal progressively (fast) so non-streaming replies animate.
	if (!bEnableTypewriter)
	{
		TypewriterCps = FMath::Max(TypewriterCps, 4000.f);
	}

	ChildSlot
		[
			SAssignNew(BodyBox, SBox)
			[
				UnrealAiBuildMarkdownChatBody(VisibleText)
			]
		];
}

void SAssistantStreamBlock::SyncMarkdownBody()
{
	if (BodyBox.IsValid())
	{
		BodyBox->SetContent(UnrealAiBuildMarkdownChatBody(VisibleText));
	}
}

void SAssistantStreamBlock::SetFullText(const FString& Full, bool bInstantReveal)
{
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
	const float Cps = FMath::Max(40.f, TypewriterCps);
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
