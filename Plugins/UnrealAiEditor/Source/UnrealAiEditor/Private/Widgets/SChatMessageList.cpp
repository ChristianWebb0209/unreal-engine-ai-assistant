#include "Widgets/SChatMessageList.h"

#include "Widgets/SAssistantStreamBlock.h"
#include "Widgets/SThinkingSubline.h"
#include "Widgets/STodoPlanPanel.h"
#include "Widgets/SAssistantToolsDropdown.h"
#include "Widgets/SToolCallCard.h"
#include "Widgets/UnrealAiChatTranscript.h"
#include "Style/UnrealAiEditorStyle.h"
#include "UnrealAiEditorSettings.h"
#include "Styling/AppStyle.h"
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

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

namespace UnrealAiChatListUi
{
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
	static constexpr float SlideDurationSec = 0.22f;
	static constexpr float SlidePixels = 22.f;
};

void SChatUserMessageAnimated::Construct(const FArguments& InArgs)
{
	OwnerList = InArgs._OwnerList;
	ChildSlot
		[
			InArgs._Content.Widget
		];
	SlideProgress = 0.f;
	SetRenderOpacity(0.f);
	SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2D(0.f, SlidePixels))));
	RegisterActiveTimer(
		1.f / 60.f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SChatUserMessageAnimated::TickSlideIn));
}

EActiveTimerReturnType SChatUserMessageAnimated::TickSlideIn(double, float DeltaTime)
{
	SlideProgress += DeltaTime / SlideDurationSec;
	const float T = FMath::Clamp(SlideProgress, 0.f, 1.f);
	const float Eased = FMath::InterpEaseInOut(0.f, 1.f, T, 2.f);
	SetRenderOpacity(Eased);
	SetRenderTransform(TOptional<FSlateRenderTransform>(
		FSlateRenderTransform(FVector2D(0.f, SlidePixels * (1.f - Eased)))));
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	if (const TSharedPtr<SChatMessageList> L = OwnerList.Pin())
	{
		L->NotifyFollowingScrollToBottom();
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
	Transcript->OnStructuralChange.AddSP(this, &SChatMessageList::RebuildTranscript);
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
							SNew(SBox)
								.HAlign(HAlign_Center)
								[
									SNew(SBox)
										.MaxDesiredWidth(720.f)
										[
											SAssignNew(MessageBox, SVerticalBox)
										]
								]
						]
					]
			]
			+ SOverlay::Slot()
				.VAlign(VAlign_Bottom)
				.HAlign(HAlign_Left)
				.Padding(FMargin(10.f, 0.f, 0.f, 10.f))
				[
					SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), TEXT("FlatButton"))
						.ContentPadding(FMargin(10.f, 6.f))
						.Visibility(this, &SChatMessageList::GetJumpToBottomVisibility)
						.OnClicked(this, &SChatMessageList::OnJumpToBottomClicked)
						[
							SNew(STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
								.Text(LOCTEXT("JumpToBottom", "↓ New messages"))
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

void SChatMessageList::SetUserMessageComplexity(const FGuid& UserBlockId, const FString& ComplexityLabel)
{
	if (Transcript.IsValid())
	{
		Transcript->SetUserComplexity(UserBlockId, ComplexityLabel);
	}
}

void SChatMessageList::ClearTranscript()
{
	PendingUserAnimId = FGuid();
	bStickToBottom = true;
	if (Transcript.IsValid())
	{
		Transcript->Clear();
	}
}

void SChatMessageList::NotifyFollowingScrollToBottom()
{
	ScheduleScrollToEndIfFollowing();
}

void SChatMessageList::ResetAssistant()
{
	// Legacy no-op: transcript drives assistant rows.
}

void SChatMessageList::RebuildTranscript()
{
	if (!MessageBox.IsValid())
	{
		return;
	}
	MessageBox->ClearChildren();
	ActiveAssistantWidget.Reset();
	ActiveThinkingWidget.Reset();

	if (!Transcript.IsValid())
	{
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
				const TSharedRef<SWidget> UserBubble = SNew(SBorder)
					.BorderImage(FUnrealAiEditorStyle::GetBrush(TEXT("UnrealAiEditor.UserBubble")))
					.Padding(FMargin(8.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SMultiLineEditableText)
								.IsReadOnly(true)
								.AutoWrapText(true)
								.Text(FText::FromString(FString::Printf(TEXT("You: %s"), *B.UserText)))
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
						[
							SNew(STextBlock)
								.Visibility(
									B.UserComplexityLabel.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.58f, 0.62f, 1.f)))
								.Text(FText::FromString(
									FString::Printf(TEXT("Complexity: %s"), *B.UserComplexityLabel)))
						]
					];

				if (B.Id == PendingUserAnimId)
				{
					MessageBox->AddSlot().AutoHeight().Padding(4.f)
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
					MessageBox->AddSlot().AutoHeight().Padding(4.f)
						[
							UserBubble
						];
				}
			}
			break;
		case EUnrealAiChatBlockKind::Thinking:
			{
				TSharedPtr<SThinkingSubline> Th;
				MessageBox->AddSlot().AutoHeight().Padding(4.f)
					[
						SAssignNew(Th, SThinkingSubline)
					];
				Th->SetFullText(B.ThinkingText);
				ActiveThinkingWidget = Th;
			}
			break;
		case EUnrealAiChatBlockKind::Assistant:
			{
				TArray<FUnrealAiAssistantSegmentToolInfo> ToolDetails;
				UnrealAiCollectToolDetailsAfterAssistant(Transcript->Blocks, i, ToolDetails);

				const TSharedRef<SWidget> ToolsSlot = ToolDetails.Num() > 0
					? StaticCastSharedRef<SWidget>(
						  SNew(SAssistantToolsDropdown).ToolDetails(ToolDetails).RunId(B.RunId))
					: SNullWidget::NullWidget;

				const bool bInstantReveal =
					!(Transcript->IsAssistantSegmentOpen()
					  && UnrealAiChatListUi::IsLastAssistantBlock(Transcript->Blocks, i));

				const bool bMergedThinking = i > 0
					&& Transcript->Blocks[i - 1].Kind == EUnrealAiChatBlockKind::Thinking;

				TSharedPtr<SAssistantStreamBlock> As;
				TSharedPtr<SThinkingSubline> ThinkLine;

				const TSharedRef<SWidget> AssistantBubble = SNew(SBorder)
					.BorderImage(FUnrealAiEditorStyle::GetBrush(TEXT("UnrealAiEditor.AssistantBubble")))
					.Padding(FMargin(6.f, 4.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Top).Padding(FMargin(0.f, 0.f, 4.f, 0.f))
						[
							SAssignNew(As, SAssistantStreamBlock)
								.bEnableTypewriter(bTw)
								.TypewriterCps(Cps)
								.OnRevealTick(FSimpleDelegate::CreateSP(this, &SChatMessageList::OnAssistantRevealTick))
						]
						+ SHorizontalBox::Slot()
								  .AutoWidth()
								  .VAlign(VAlign_Top)
								  .Padding(FMargin(0.f, 2.f, 0.f, 0.f))
						[
							ToolsSlot
						]
					];

				if (bMergedThinking)
				{
					MessageBox->AddSlot().AutoHeight().Padding(4.f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								AssistantBubble
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
					MessageBox->AddSlot().AutoHeight().Padding(4.f)
						[
							AssistantBubble
						];
				}

				As->SetFullText(B.AssistantText, bInstantReveal);
				ActiveAssistantWidget = As;
			}
			break;
		case EUnrealAiChatBlockKind::ToolCall:
			MessageBox->AddSlot().AutoHeight().Padding(4.f)
				[
					SNew(SToolCallCard)
						.ToolName(B.ToolName)
						.ArgumentsPreview(B.ToolArgsPreview)
						.ResultPreview(B.ToolResultPreview)
						.bRunning(B.bToolRunning)
						.bSuccess(B.bToolOk)
				];
			break;
		case EUnrealAiChatBlockKind::TodoPlan:
			MessageBox->AddSlot().AutoHeight().Padding(4.f)
				[
					SNew(STodoPlanPanel)
						.Title(B.TodoTitle)
						.PlanJson(B.TodoJson)
						.BackendRegistry(BackendRegistry)
						.Session(Session)
				];
			break;
		case EUnrealAiChatBlockKind::RunProgress:
			MessageBox->AddSlot().AutoHeight().Padding(4.f)
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
			MessageBox->AddSlot().AutoHeight().Padding(4.f)
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
	// later structural updates (e.g. complexity label) do not replay the slide-in.
	PendingUserAnimId = FGuid();

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
	if (bScrollToEndTimerPending)
	{
		return;
	}
	bScrollToEndTimerPending = true;
	RegisterActiveTimer(
		0.f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SChatMessageList::FlushPendingScrollToEnd));
}

EActiveTimerReturnType SChatMessageList::FlushPendingScrollToEnd(double, float)
{
	bScrollToEndTimerPending = false;
	if (bStickToBottom && ScrollBox.IsValid())
	{
		ScrollBox->ScrollToEnd();
	}
	return EActiveTimerReturnType::Stop;
}

void SChatMessageList::ForceScrollToBottomAndFollow()
{
	bStickToBottom = true;
	bScrollToEndTimerPending = false;
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
