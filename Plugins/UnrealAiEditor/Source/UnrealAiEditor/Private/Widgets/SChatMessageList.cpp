#include "Widgets/SChatMessageList.h"

#include "Widgets/SAssistantStreamBlock.h"
#include "Widgets/SThinkingBlock.h"
#include "Widgets/STodoPlanPanel.h"
#include "Widgets/SAssistantToolsDropdown.h"
#include "Widgets/SToolCallCard.h"
#include "Widgets/UnrealAiChatTranscript.h"
#include "Style/UnrealAiEditorStyle.h"
#include "UnrealAiEditorSettings.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SHorizontalBox.h"
#include "Templates/SharedPointer.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SPanel.h"
#define LOCTEXT_NAMESPACE "UnrealAiEditor"

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
			SNew(SBox)
				.HAlign(HAlign_Fill)
				[
					SAssignNew(ScrollBox, SScrollBox)
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
		];
}

void SChatMessageList::AddUserMessage(const FString& Text)
{
	if (Transcript.IsValid())
	{
		Transcript->AddUserMessage(Text);
	}
}

void SChatMessageList::ClearTranscript()
{
	if (Transcript.IsValid())
	{
		Transcript->Clear();
	}
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
	static_cast<SPanel*>(MessageBox.Get())->ClearChildren();
	ActiveAssistantWidget.Reset();
	ActiveThinkingWidget.Reset();

	if (!Transcript.IsValid())
	{
		return;
	}

	const UUnrealAiEditorSettings* Set = GetDefault<UUnrealAiEditorSettings>();
	const bool bTw = Set->bAssistantTypewriter;
	const float Cps = Set->AssistantTypewriterCps;

	for (int32 i = 0; i < Transcript->Blocks.Num(); ++i)
	{
		const FUnrealAiChatBlock& B = Transcript->Blocks[i];
		switch (B.Kind)
		{
		case EUnrealAiChatBlockKind::User:
			MessageBox->AddSlot().AutoHeight().Padding(4.f)
				[
					SNew(SBorder)
						.BorderImage(FUnrealAiEditorStyle::GetBrush(TEXT("UnrealAiEditor.UserBubble")))
						.Padding(FMargin(8.f))
						[
							SNew(STextBlock)
								.AutoWrapText(true)
								.Text(FText::FromString(FString::Printf(TEXT("You: %s"), *B.UserText)))
						]
				];
			break;
		case EUnrealAiChatBlockKind::Thinking:
			if (B.ThinkingText.IsEmpty())
			{
				break;
			}
			{
				TSharedPtr<SThinkingBlock> Th;
				MessageBox->AddSlot().AutoHeight().Padding(4.f)
					[
						SAssignNew(Th, SThinkingBlock)
					];
				Th->SetFullText(B.ThinkingText);
				ActiveThinkingWidget = Th;
			}
			break;
		case EUnrealAiChatBlockKind::Assistant:
			{
				TArray<FString> SegmentTools;
				UnrealAiCollectToolsAfterAssistant(Transcript->Blocks, i, SegmentTools);

				const TSharedRef<SWidget> ToolsSlot = SegmentTools.Num() > 0
					? StaticCastSharedRef<SWidget>(SNew(SAssistantToolsDropdown).ToolNames(SegmentTools))
					: SNullWidget::NullWidget;

				TSharedPtr<SAssistantStreamBlock> As;
				MessageBox->AddSlot().AutoHeight().Padding(4.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Top).Padding(0.f, 0.f, 4.f, 0.f)
						[
							SAssignNew(As, SAssistantStreamBlock)
								.bEnableTypewriter(bTw)
								.TypewriterCps(Cps)
						]
						+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Top)
							.Padding(0.f, 2.f, 0.f, 0.f)
							[
								ToolsSlot
							]
					];
				As->SetFullText(B.AssistantText);
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
							SNew(STextBlock)
								.ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.7f, 0.85f, 1.f)))
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
							: FLinearColor(0.28f, 0.12f, 0.12f, 0.9f))
						.Padding(FMargin(8.f))
						[
							SNew(STextBlock)
								.AutoWrapText(true)
								.Text(FText::FromString(B.NoticeText))
						]
				];
			break;
		default:
			break;
		}
	}

	RequestScrollToEnd();
}

void SChatMessageList::OnAssistantDeltaUi(const FString& Chunk)
{
	if (ActiveAssistantWidget.IsValid())
	{
		ActiveAssistantWidget->AppendIncoming(Chunk);
	}
	RequestScrollToEnd();
}

void SChatMessageList::OnThinkingDeltaUi(const FString& Chunk, bool bFirstChunk)
{
	if (!bFirstChunk && ActiveThinkingWidget.IsValid())
	{
		ActiveThinkingWidget->Append(Chunk);
	}
	RequestScrollToEnd();
}

void SChatMessageList::RequestScrollToEnd()
{
	if (!ScrollBox.IsValid())
	{
		return;
	}
	RegisterActiveTimer(
		0.f,
		FWidgetActiveTimerDelegate::CreateLambda([WeakScroll = TWeakPtr<SScrollBox>(ScrollBox)](double, float)
		{
			if (TSharedPtr<SScrollBox> S = WeakScroll.Pin())
			{
				S->ScrollToEnd();
			}
			return EActiveTimerReturnType::Stop;
		}));
}

#undef LOCTEXT_NAMESPACE
