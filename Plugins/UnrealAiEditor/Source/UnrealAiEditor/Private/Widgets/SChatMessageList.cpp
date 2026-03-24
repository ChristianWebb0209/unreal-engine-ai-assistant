#include "Widgets/SChatMessageList.h"

#include "Widgets/SAssistantStreamBlock.h"
#include "Widgets/SThinkingSubline.h"
#include "Widgets/STodoPlanPanel.h"
#include "Widgets/SToolCallCard.h"
#include "Widgets/UnrealAiChatTranscript.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "HAL/PlatformTime.h"
#include "Style/UnrealAiEditorStyle.h"
#include "Templates/Function.h"
#include "UnrealAiEditorSettings.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Templates/SharedPointer.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SPanel.h"
#include "Layout/Geometry.h"
#include "Rendering/SlateRenderTransform.h"
#include "Containers/Ticker.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

/** Row padding around user messages. */
static const FMargin GChatMessageListRowMargin(12.f, 11.f, 12.f, 11.f);
/** Tighter vertical rhythm for assistant stream: tools, timestamps, thinking, notices. */
static const FMargin GChatMessageListAgentRowMargin(12.f, 4.f, 12.f, 4.f);

namespace UnrealAiChatListUi
{
	static TSharedRef<SWidget> WrapWithStepCaption(const FString& Caption, const TSharedRef<SWidget>& Body)
	{
		if (Caption.IsEmpty())
		{
			return Body;
		}
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Caption))
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
				.ColorAndOpacity(FUnrealAiEditorStyle::ColorDebugMuted())
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				Body
			];
	}

	static bool IsLastAssistantBlock(const TArray<FUnrealAiChatBlock>& Blocks, const int32 BlockIndex)
	{
		if (!Blocks.IsValidIndex(BlockIndex) || Blocks[BlockIndex].Kind != EUnrealAiChatBlockKind::Assistant)
		{
			return false;
		}
		for (int32 j = BlockIndex + 1; j < Blocks.Num(); ++j)
		{
			if (Blocks[j].Kind == EUnrealAiChatBlockKind::Assistant)
			{
				return false;
			}
		}
		return true;
	}
}

/** Footer under assistant markdown: live "reply · Ns" while the step is open, "Writing…" while streaming/typewriter is active. */
class SAssistantReplyTimingFooter final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAssistantReplyTimingFooter) {}
	SLATE_ARGUMENT(double, StepMonotonicStart)
	SLATE_ARGUMENT(FString, FinalCaption)
	SLATE_ARGUMENT(TFunction<bool()>, IsWritingMask)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	EActiveTimerReturnType TickFooter(double CurrentTime, float DeltaTime);
	void SyncLabel();

	double StepMonotonicStart = 0.0;
	FString FinalCaption;
	TFunction<bool()> IsWritingMask;
	TSharedPtr<STextBlock> Label;
};

void SAssistantReplyTimingFooter::Construct(const FArguments& InArgs)
{
	StepMonotonicStart = InArgs._StepMonotonicStart;
	FinalCaption = InArgs._FinalCaption;
	IsWritingMask = InArgs._IsWritingMask;

	ChildSlot
		[
			SAssignNew(Label, STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
				.ColorAndOpacity(FUnrealAiEditorStyle::ColorDebugMuted())
		];
	SyncLabel();
	if (FinalCaption.IsEmpty() && StepMonotonicStart > 0.0)
	{
		RegisterActiveTimer(
			0.125f,
			FWidgetActiveTimerDelegate::CreateSP(this, &SAssistantReplyTimingFooter::TickFooter));
	}
}

EActiveTimerReturnType SAssistantReplyTimingFooter::TickFooter(double CurrentTime, float DeltaTime)
{
	(void)CurrentTime;
	(void)DeltaTime;
	SyncLabel();
	if (!FinalCaption.IsEmpty() || StepMonotonicStart <= 0.0)
	{
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

void SAssistantReplyTimingFooter::SyncLabel()
{
	if (!Label.IsValid())
	{
		return;
	}
	if (StepMonotonicStart <= 0.0 && FinalCaption.IsEmpty())
	{
		SetVisibility(EVisibility::Collapsed);
		return;
	}
	SetVisibility(EVisibility::Visible);
	if (!FinalCaption.IsEmpty())
	{
		Label->SetText(FText::FromString(FinalCaption));
		return;
	}
	if (IsWritingMask && IsWritingMask())
	{
		Label->SetText(LOCTEXT("AssistantFooterWriting", "Writing…"));
		return;
	}
	const double Dur = FPlatformTime::Seconds() - StepMonotonicStart;
	Label->SetText(FText::FromString(FString::Printf(
		TEXT("reply · %s"),
		*UnrealAiFormatStepDurationForUi(Dur))));
}

/** Slides user message up + fades in (one-shot). */
class SChatUserMessageAnimated final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatUserMessageAnimated) {}
	SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_ARGUMENT(TWeakPtr<SChatMessageList>, OwnerList)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	EActiveTimerReturnType TickSlideIn(double, float DeltaTime);

	TWeakPtr<SChatMessageList> OwnerList;
	float SlideProgress = 0.f;
	float TimeSinceFollowScrollNotify = 0.f;
	static constexpr float SlideDurationSec = 0.48f;
	static constexpr float SlidePixels = 52.f;
	static constexpr float FollowScrollNotifyMinIntervalSec = 0.12f;
};

void SChatUserMessageAnimated::Construct(const FArguments& InArgs)
{
	OwnerList = InArgs._OwnerList;
	ChildSlot
		[
			InArgs._Content.Widget
		];
	SlideProgress = 0.f;
	TimeSinceFollowScrollNotify = FollowScrollNotifyMinIntervalSec;
	SetRenderOpacity(0.02f);
	SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2D(0.f, SlidePixels))));
	RegisterActiveTimer(
		1.f / 60.f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SChatUserMessageAnimated::TickSlideIn));
}

EActiveTimerReturnType SChatUserMessageAnimated::TickSlideIn(double, float DeltaTime)
{
	SlideProgress += DeltaTime / SlideDurationSec;
	const float T = FMath::Clamp(SlideProgress, 0.f, 1.f);
	// Smoothstep: ease in and out for a single smooth glide.
	const float S = T * T * (3.f - 2.f * T);
	SetRenderOpacity(S);
	SetRenderTransform(TOptional<FSlateRenderTransform>(
		FSlateRenderTransform(FVector2D(0.f, SlidePixels * (1.f - S)))));
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	TimeSinceFollowScrollNotify += DeltaTime;
	if (const TSharedPtr<SChatMessageList> L = OwnerList.Pin())
	{
		if (TimeSinceFollowScrollNotify >= FollowScrollNotifyMinIntervalSec)
		{
			TimeSinceFollowScrollNotify = 0.f;
			L->NotifyFollowingScrollToBottom();
		}
	}
	if (T >= 1.f - KINDA_SMALL_NUMBER)
	{
		SetRenderOpacity(1.f);
		SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2D::ZeroVector)));
		if (const TSharedPtr<SChatMessageList> L = OwnerList.Pin())
		{
			L->ClearUserAnimId();
		}
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

void SChatMessageList::Construct(const FArguments& InArgs)
{
	BackendRegistry = InArgs._BackendRegistry;
	Session = InArgs._Session;
	Transcript = MakeShared<FUnrealAiChatTranscript>();
	Transcript->OnStructuralChange.AddSP(this, &SChatMessageList::ScheduleRebuildTranscript);
	Transcript->OnAssistantStreamDelta.AddSP(this, &SChatMessageList::OnAssistantDeltaUi);
	Transcript->OnThinkingStreamDelta.AddSP(this, &SChatMessageList::OnThinkingDeltaUi);

	ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBox)
					.HAlign(HAlign_Fill)
					[
						SAssignNew(ScrollBox, SScrollBox)
						.OnUserScrolled(FOnUserScrolled::CreateSP(this, &SChatMessageList::HandleUserScrolled))
						+ SScrollBox::Slot()
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
								  .FillWidth(1.f)
								  .VAlign(VAlign_Top)
								  .Padding(FMargin(10.f, 6.f, 10.f, 6.f))
							[
								SAssignNew(MessageBox, SVerticalBox)
							]
						]
					]
			]
			+ SOverlay::Slot()
				.VAlign(VAlign_Bottom)
				.HAlign(HAlign_Left)
				.Padding(FMargin(10.f, 0.f, 0.f, 10.f))
				[
					SNew(SBorder)
						.Visibility(this, &SChatMessageList::GetJumpToBottomVisibility)
						.BorderImage(FUnrealAiEditorStyle::GetBrush(TEXT("UnrealAiEditor.Elevated")))
						.Padding(FMargin(0))
						[
							SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
								.ContentPadding(FMargin(10.f, 6.f))
								.OnClicked(this, &SChatMessageList::OnJumpToBottomClicked)
								[
									SNew(STextBlock)
										.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
										.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextPrimary())
										.Text(LOCTEXT("JumpToBottom", "↓ New messages"))
								]
						]
				]
		];
}

FGuid SChatMessageList::AddUserMessage(const FString& Text)
{
	ForceScrollToBottomAndFollow();
	if (!Transcript.IsValid())
	{
		return FGuid();
	}
	const FGuid Id = FGuid::NewGuid();
	PendingUserAnimId = Id;
	return Transcript->AddUserMessage(Text, Id);
}

void SChatMessageList::ClearUserAnimId()
{
	PendingUserAnimId = FGuid();
}

void SChatMessageList::ClearTranscript()
{
	PendingUserAnimId = FGuid();
	ExpandedToolCallBlockIds.Reset();
	bStickToBottom = true;
	if (Transcript.IsValid())
	{
		Transcript->Clear();
	}
}

void SChatMessageList::HydrateTranscriptFromPersistedConversation(const TArray<FUnrealAiConversationMessage>& Messages)
{
	PendingUserAnimId = FGuid();
	bStickToBottom = true;
	if (Transcript.IsValid())
	{
		Transcript->Clear();
		Transcript->HydrateFromConversationMessages(Messages);
	}
	RebuildTranscript();
	ForceScrollToBottomAndFollow();
}

void SChatMessageList::NotifyFollowingScrollToBottom()
{
	ScheduleScrollToEndIfFollowing();
}

void SChatMessageList::ResetAssistant()
{
	// Legacy no-op: transcript drives assistant rows.
}

void SChatMessageList::ScheduleRebuildTranscript()
{
	bTranscriptRebuildPending = true;
	if (bTranscriptRebuildTickerActive)
	{
		return;
	}
	bTranscriptRebuildTickerActive = true;
	const TWeakPtr<SChatMessageList> WeakSelf(StaticCastSharedRef<SChatMessageList>(AsShared()));
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakSelf](float) -> bool
	{
		const TSharedPtr<SChatMessageList> Self = WeakSelf.Pin();
		if (!Self.IsValid())
		{
			return false;
		}
		Self->bTranscriptRebuildTickerActive = false;
		if (Self->bTranscriptRebuildPending)
		{
			Self->bTranscriptRebuildPending = false;
			Self->RebuildTranscript();
		}
		return false;
	}));
}

void SChatMessageList::RebuildTranscript()
{
	if (!MessageBox.IsValid())
	{
		return;
	}
	bSuppressToolExpansionCallbacks = true;
	MessageBox->ClearChildren();
	ActiveAssistantWidget.Reset();
	ActiveThinkingWidget.Reset();

	if (!Transcript.IsValid())
	{
		bSuppressToolExpansionCallbacks = false;
		return;
	}

	const UUnrealAiEditorSettings* Set = GetDefault<UUnrealAiEditorSettings>();
	const bool bTw = Set->bAssistantTypewriter;
	const float Cps = Set->AssistantTypewriterCps;

	const TWeakPtr<SChatMessageList> WeakList(StaticCastSharedRef<SChatMessageList>(AsShared()));

	for (int32 i = 0; i < Transcript->Blocks.Num(); ++i)
	{
		const FUnrealAiChatBlock& B = Transcript->Blocks[i];
		// Reasoning is shown as a subline under the following assistant bubble.
		if (B.Kind == EUnrealAiChatBlockKind::Thinking && i + 1 < Transcript->Blocks.Num()
			&& Transcript->Blocks[i + 1].Kind == EUnrealAiChatBlockKind::Assistant)
		{
			continue;
		}
		switch (B.Kind)
		{
		case EUnrealAiChatBlockKind::User:
			{
				const TSharedRef<SWidget> UserBubble = B.bHarnessSystemUser
					? StaticCastSharedRef<SWidget>(
						  SNew(SBox)
							  .Padding(FMargin(12.f, 2.f, 12.f, 8.f))
							  .HAlign(HAlign_Fill)
							  [
								  SNew(STextBlock)
									  .AutoWrapText(true)
									  .WrapTextAt(800.f)
									  .Text(FText::FromString(B.UserText))
									  .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11))
									  .ColorAndOpacity(FUnrealAiEditorStyle::ColorDebugMuted())
							  ])
					: StaticCastSharedRef<SWidget>(
						  SNew(SBorder)
							  .BorderImage(FUnrealAiEditorStyle::GetBrush(TEXT("UnrealAiEditor.UserBubble")))
							  .Padding(FMargin(10.f, 9.f))
							  [
								  SNew(SBox)
									  .HAlign(HAlign_Fill)
									  [
										  SNew(SMultiLineEditableText)
											  .IsReadOnly(true)
											  .AutoWrapText(true)
											  .Text(FText::FromString(B.UserText))
									  ]
							  ]);

				if (B.Id == PendingUserAnimId)
				{
					MessageBox->AddSlot().AutoHeight().Padding(GChatMessageListRowMargin)
						[
							SNew(SChatUserMessageAnimated)
								.OwnerList(WeakList)
								[
									UserBubble
								]
						];
				}
				else
				{
					MessageBox->AddSlot().AutoHeight().Padding(GChatMessageListRowMargin)
						[
							UserBubble
						];
				}
			}
			break;
		case EUnrealAiChatBlockKind::Thinking:
			{
				TSharedPtr<SThinkingSubline> Th;
				MessageBox->AddSlot().AutoHeight().Padding(GChatMessageListAgentRowMargin)
					[
						UnrealAiChatListUi::WrapWithStepCaption(
							B.StepTimingCaption,
							SAssignNew(Th, SThinkingSubline))
					];
				Th->SetFullText(B.ThinkingText);
				ActiveThinkingWidget = Th;
			}
			break;
		case EUnrealAiChatBlockKind::Assistant:
			{
				const bool bInstantReveal =
					!(Transcript->IsAssistantSegmentOpen()
					  && UnrealAiChatListUi::IsLastAssistantBlock(Transcript->Blocks, i));

				const bool bMergedThinking = i > 0
					&& Transcript->Blocks[i - 1].Kind == EUnrealAiChatBlockKind::Thinking;

				const FString MergedThinkingCaption =
					bMergedThinking ? Transcript->Blocks[i - 1].StepTimingCaption : FString();

				TSharedPtr<SAssistantStreamBlock> As;
				TSharedPtr<SThinkingSubline> ThinkLine;

				const TSharedRef<SWidget> AssistantBubble =
					SNew(SBox)
						.Clipping(EWidgetClipping::ClipToBounds)
						.Padding(FMargin(4.f, 3.f, 4.f, 3.f))
						[
							SAssignNew(As, SAssistantStreamBlock)
								.bEnableTypewriter(bTw)
								.TypewriterCps(Cps)
								.OnRevealTick(FSimpleDelegate::CreateSP(this, &SChatMessageList::OnAssistantRevealTick))
						];

				const bool bThisBlockIsLiveAssistantTail =
					Transcript->IsAssistantSegmentOpen() && !Transcript->Blocks.IsEmpty()
					&& Transcript->Blocks.Last().Kind == EUnrealAiChatBlockKind::Assistant
					&& Transcript->Blocks.Last().Id == B.Id;
				TWeakPtr<SAssistantStreamBlock> WeakAs(As);
				const TSharedPtr<FUnrealAiChatTranscript> TranscriptPin = Transcript;
				const TFunction<bool()> FooterWritingMask = [TranscriptPin, WeakAs, bThisBlockIsLiveAssistantTail]()
				{
					if (!bThisBlockIsLiveAssistantTail || !TranscriptPin.IsValid())
					{
						return false;
					}
					if (TranscriptPin->IsAssistantStreamRecentlyActive(0.28f))
					{
						return true;
					}
					if (const TSharedPtr<SAssistantStreamBlock> P = WeakAs.Pin())
					{
						return P->HasPendingReveal();
					}
					return false;
				};

				const TSharedRef<SWidget> ReplyFooter = SNew(SAssistantReplyTimingFooter)
														   .StepMonotonicStart(B.StepMonotonicStart)
														   .FinalCaption(B.StepTimingCaption)
														   .IsWritingMask(FooterWritingMask);

				if (bMergedThinking)
				{
					MessageBox->AddSlot().AutoHeight().Padding(GChatMessageListAgentRowMargin)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
								  .AutoHeight()
								  .Padding(0.f, 0.f, 0.f, MergedThinkingCaption.IsEmpty() ? 0.f : 3.f)
							[
								SNew(STextBlock)
								.Visibility(
									MergedThinkingCaption.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
								.Text(FText::FromString(MergedThinkingCaption))
								.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
								.ColorAndOpacity(FUnrealAiEditorStyle::ColorDebugMuted())
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								AssistantBubble
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
							[
								ReplyFooter
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SAssignNew(ThinkLine, SThinkingSubline)
							]
						];
					ThinkLine->SetFullText(Transcript->Blocks[i - 1].ThinkingText);
					ActiveThinkingWidget = ThinkLine;
				}
				else
				{
					MessageBox->AddSlot().AutoHeight().Padding(GChatMessageListAgentRowMargin)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								AssistantBubble
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
							[
								ReplyFooter
							]
						];
				}

				As->SetFullText(B.AssistantText, bInstantReveal);
				ActiveAssistantWidget = As;
			}
			break;
		case EUnrealAiChatBlockKind::ToolCall:
			MessageBox->AddSlot().AutoHeight().Padding(GChatMessageListAgentRowMargin)
				[
					UnrealAiChatListUi::WrapWithStepCaption(
						B.StepTimingCaption,
						SNew(SToolCallCard)
							.ToolName(B.ToolName)
							.ArgumentsPreview(B.ToolArgsPreview)
							.ResultPreview(B.ToolResultPreview)
							.bRunning(B.bToolRunning)
							.bSuccess(B.bToolOk)
							.EditorPresentation(B.ToolEditorPresentation)
							.bInitiallyCollapsed(!ExpandedToolCallBlockIds.Contains(B.Id))
							.OnExpansionChanged(FOnBooleanValueChanged::CreateLambda(
								[this, BlockId = B.Id](bool bExpanded)
								{
									if (bSuppressToolExpansionCallbacks)
									{
										return;
									}
									if (bExpanded)
									{
										ExpandedToolCallBlockIds.Add(BlockId);
									}
									else
									{
										ExpandedToolCallBlockIds.Remove(BlockId);
									}
								})))
				];
			break;
		case EUnrealAiChatBlockKind::TodoPlan:
			MessageBox->AddSlot().AutoHeight().Padding(GChatMessageListAgentRowMargin)
				[
					SNew(STodoPlanPanel)
						.Title(B.TodoTitle)
						.PlanJson(B.TodoJson)
						.BackendRegistry(BackendRegistry)
						.Session(Session)
				];
			break;
		case EUnrealAiChatBlockKind::RunProgress:
			MessageBox->AddSlot().AutoHeight().Padding(GChatMessageListAgentRowMargin)
				[
					SNew(SBorder)
						.BorderBackgroundColor(FLinearColor(0.2f, 0.2f, 0.25f, 0.6f))
						.Padding(FMargin(6.f))
						[
							SNew(SMultiLineEditableText)
								.IsReadOnly(true)
								.AutoWrapText(true)
								.Text(FText::FromString(B.ProgressLabel))
						]
				];
			break;
		case EUnrealAiChatBlockKind::Notice:
			MessageBox->AddSlot().AutoHeight().Padding(GChatMessageListAgentRowMargin)
				[
					SNew(SBorder)
						.BorderBackgroundColor(B.bRunCancelled
							? FLinearColor(0.25f, 0.22f, 0.12f, 0.9f)
							: (B.bNoticeError
								  ? FLinearColor(0.28f, 0.12f, 0.12f, 0.9f)
								  : FLinearColor(0.14f, 0.16f, 0.22f, 0.92f)))
						.Padding(FMargin(8.f))
						[
							SNew(SMultiLineEditableText)
								.IsReadOnly(true)
								.AutoWrapText(true)
								.Text(FText::FromString(B.NoticeText))
						]
				];
			break;
		default:
			break;
		}
	}

	// Only the rebuild that immediately follows AddUserMessage should match PendingUserAnimId; clear so
	// later structural updates do not replay the slide-in.
	PendingUserAnimId = FGuid();

	bSuppressToolExpansionCallbacks = false;
	ScheduleScrollToEndIfFollowing();
}

void SChatMessageList::OnAssistantDeltaUi(const FString& Chunk)
{
	if (ActiveAssistantWidget.IsValid())
	{
		ActiveAssistantWidget->AppendIncoming(Chunk);
	}
	ScheduleScrollToEndIfFollowing();
}

void SChatMessageList::OnThinkingDeltaUi(const FString& Chunk, bool bFirstChunk)
{
	if (!bFirstChunk && ActiveThinkingWidget.IsValid())
	{
		ActiveThinkingWidget->Append(Chunk);
	}
	ScheduleScrollToEndIfFollowing();
}

void SChatMessageList::HandleUserScrolled(float CurrentScrollOffset)
{
	if (!ScrollBox.IsValid())
	{
		return;
	}
	const float EndOffset = ScrollBox->GetScrollOffsetOfEnd();
	const float DistanceFromEnd = EndOffset - CurrentScrollOffset;
	const bool bWasAtBottom = bStickToBottom;
	bStickToBottom = DistanceFromEnd <= StickToBottomThresholdPx;
	if (bWasAtBottom != bStickToBottom)
	{
		Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
}

void SChatMessageList::OnAssistantRevealTick()
{
	ScheduleScrollToEndIfFollowing();
}

void SChatMessageList::ScheduleScrollToEndIfFollowing()
{
	if (!bStickToBottom || !ScrollBox.IsValid())
	{
		return;
	}
	if (bSmoothFollowScrollActive)
	{
		return;
	}
	bSmoothFollowScrollActive = true;
	RegisterActiveTimer(
		0.f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SChatMessageList::TickSmoothFollowScroll));
}

EActiveTimerReturnType SChatMessageList::TickSmoothFollowScroll(double, float DeltaTime)
{
	if (!bStickToBottom || !ScrollBox.IsValid())
	{
		bSmoothFollowScrollActive = false;
		return EActiveTimerReturnType::Stop;
	}

	const float EndOffset = ScrollBox->GetScrollOffsetOfEnd();
	const float Current = ScrollBox->GetScrollOffset();
	const float Next = FMath::FInterpTo(Current, EndOffset, DeltaTime, FollowScrollInterpSpeed);
	ScrollBox->SetScrollOffset(Next);

	if (FMath::IsNearlyEqual(Next, EndOffset, 0.85f))
	{
		ScrollBox->SetScrollOffset(EndOffset);
		bSmoothFollowScrollActive = false;
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

void SChatMessageList::ForceScrollToBottomAndFollow()
{
	bStickToBottom = true;
	bSmoothFollowScrollActive = false;
	if (ScrollBox.IsValid())
	{
		ScrollBox->ScrollToEnd();
	}
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

EVisibility SChatMessageList::GetJumpToBottomVisibility() const
{
	return bStickToBottom ? EVisibility::Collapsed : EVisibility::Visible;
}

FReply SChatMessageList::OnJumpToBottomClicked()
{
	ForceScrollToBottomAndFollow();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
