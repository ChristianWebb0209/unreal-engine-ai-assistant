#include "Widgets/UnrealAiChatTranscriptStyle.h"

#include "Style/UnrealAiEditorStyle.h"
#include "Widgets/UnrealAiChatTranscript.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Plan/UnrealAiPlanUiTokens.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

namespace UnrealAiChatTranscriptStyleInternal
{
	/** Brushes must exist in UE Starship styles (StarshipCoreStyle + Editor StarshipStyle); wrong names render as empty squares. */
	static const FSlateBrush* AppIcon(const TCHAR* Name)
	{
		return FAppStyle::Get().GetBrush(Name);
	}
} // namespace UnrealAiChatTranscriptStyleInternal

const FTextBlockStyle& UnrealAiChatTranscriptStyle::TranscriptReadOnlyBodyTextStyle()
{
	static FTextBlockStyle Style = []
	{
		FTextBlockStyle S = FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("NormalText"));
		S.SetFont(FUnrealAiEditorStyle::FontBodyRegular11());
		S.SetColorAndOpacity(FUnrealAiEditorStyle::ColorTextPrimary());
		return S;
	}();
	return Style;
}

TSharedRef<SWidget> UnrealAiChatTranscriptStyle::MakeStepTimingCaptionRow(const FString& Caption)
{
	if (Caption.IsEmpty())
	{
		return SNullWidget::NullWidget;
	}
	FTextBlockStyle CaptionStyle =
		FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("NormalText"));
	CaptionStyle.SetFont(FUnrealAiEditorStyle::FontBodySmall());
	CaptionStyle.SetColorAndOpacity(FUnrealAiEditorStyle::ColorDebugMuted());
	const FSlateBrush* ClockBrush = UnrealAiChatTranscriptStyleInternal::AppIcon(TEXT("Icons.Recent"));
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 1.f, 5.f, 0.f).VAlign(VAlign_Top)
		[
			SNew(SBox)
				.WidthOverride(12.f)
				.HeightOverride(12.f)
				[
					SNew(SImage)
						.Image(ClockBrush)
						.ColorAndOpacity(FSlateColor(FUnrealAiEditorStyle::ColorDebugMuted()))
				]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			SNew(SMultiLineEditableText)
				.IsReadOnly(true)
				.AutoWrapText(true)
				.AllowContextMenu(true)
				.SelectAllTextWhenFocused(false)
				.TextStyle(&CaptionStyle)
				.Text(FText::FromString(Caption))
		];
}

const FSlateBrush* UnrealAiChatTranscriptStyle::GetRoleIconBrush(const FUnrealAiChatBlock& Block)
{
	using namespace UnrealAiChatTranscriptStyleInternal;
	switch (Block.Kind)
	{
	case EUnrealAiChatBlockKind::User:
		// Starship: Icons.Comment (editor); Log.TabIcon = OutputLog (core) for harness/system lines.
		return Block.bHarnessSystemUser ? AppIcon(TEXT("Log.TabIcon")) : AppIcon(TEXT("Icons.Comment"));
	case EUnrealAiChatBlockKind::Thinking:
		return AppIcon(TEXT("Icons.Help"));
	case EUnrealAiChatBlockKind::Assistant:
		return FUnrealAiEditorStyle::GetAgentChatTabIconBrush();
	case EUnrealAiChatBlockKind::ToolCall:
		// Core registers Icons.Blueprints; editor also has Icons.Blueprint — prefer core name.
		return AppIcon(TEXT("Icons.Blueprints"));
	case EUnrealAiChatBlockKind::TodoPlan:
		return AppIcon(TEXT("Icons.Check"));
	case EUnrealAiChatBlockKind::PlanDraftPending:
		return AppIcon(TEXT("Icons.Edit"));
	case EUnrealAiChatBlockKind::Notice:
		if (Block.bRunCancelled)
		{
			return AppIcon(TEXT("Icons.XCircle"));
		}
		if (Block.bNoticeError)
		{
			return AppIcon(TEXT("Icons.Error"));
		}
		return AppIcon(TEXT("Icons.Info"));
	case EUnrealAiChatBlockKind::RunProgress:
		return AppIcon(TEXT("Icons.Recent"));
	case EUnrealAiChatBlockKind::PlanWorkerLane:
		return AppIcon(TEXT("Icons.Edit"));
	default:
		return AppIcon(TEXT("Icons.Help"));
	}
}

FLinearColor UnrealAiChatTranscriptStyle::GetRoleAccentLinear(const FUnrealAiChatBlock& Block)
{
	switch (Block.Kind)
	{
	case EUnrealAiChatBlockKind::User:
		return Block.bHarnessSystemUser ? FUnrealAiEditorStyle::LinearColorChatTranscriptAccentUserSystem()
										: FUnrealAiEditorStyle::LinearColorChatTranscriptAccentUser();
	case EUnrealAiChatBlockKind::Thinking:
		return FUnrealAiEditorStyle::LinearColorChatTranscriptAccentThinking();
	case EUnrealAiChatBlockKind::Assistant:
		return FUnrealAiEditorStyle::LinearColorChatTranscriptAccentAssistant();
	case EUnrealAiChatBlockKind::ToolCall:
		return FUnrealAiEditorStyle::LinearColorChatTranscriptAccentTool();
	case EUnrealAiChatBlockKind::TodoPlan:
		return FUnrealAiEditorStyle::LinearColorChatTranscriptAccentTodo();
	case EUnrealAiChatBlockKind::PlanDraftPending:
		return FUnrealAiEditorStyle::LinearColorChatTranscriptAccentPlanDraft();
	case EUnrealAiChatBlockKind::Notice:
		if (Block.bRunCancelled)
		{
			return FUnrealAiEditorStyle::LinearColorChatTranscriptAccentNoticeCancelled();
		}
		if (Block.bNoticeError)
		{
			return FUnrealAiEditorStyle::LinearColorChatTranscriptAccentNoticeError();
		}
		return FUnrealAiEditorStyle::LinearColorChatTranscriptAccentNoticeInfo();
	case EUnrealAiChatBlockKind::RunProgress:
		return FUnrealAiEditorStyle::LinearColorChatTranscriptAccentRun();
	case EUnrealAiChatBlockKind::PlanWorkerLane:
		return FUnrealAiPlanUiTokens::PlanAccent();
	default:
		return FUnrealAiEditorStyle::LinearColorChatTranscriptAccentAssistant();
	}
}

FText UnrealAiChatTranscriptStyle::GetRoleLabelText(const FUnrealAiChatBlock& Block)
{
	switch (Block.Kind)
	{
	case EUnrealAiChatBlockKind::User:
		return Block.bHarnessSystemUser ? LOCTEXT("ChatRoleSystem", "System")
										: LOCTEXT("ChatRoleYou", "You");
	case EUnrealAiChatBlockKind::Thinking:
		return LOCTEXT("ChatRoleThinking", "Reasoning");
	case EUnrealAiChatBlockKind::Assistant:
		return LOCTEXT("ChatRoleAssistant", "Assistant");
	case EUnrealAiChatBlockKind::ToolCall:
		return LOCTEXT("ChatRoleTool", "Tool");
	case EUnrealAiChatBlockKind::TodoPlan:
		return LOCTEXT("ChatRolePlan", "Plan");
	case EUnrealAiChatBlockKind::PlanDraftPending:
		return LOCTEXT("ChatRolePlanDraft", "Plan draft");
	case EUnrealAiChatBlockKind::Notice:
		if (Block.bRunCancelled)
		{
			return LOCTEXT("ChatRoleStopped", "Stopped");
		}
		if (Block.bNoticeError)
		{
			return LOCTEXT("ChatRoleError", "Error");
		}
		return LOCTEXT("ChatRoleNote", "Note");
	case EUnrealAiChatBlockKind::RunProgress:
		return LOCTEXT("ChatRoleRun", "Run");
	case EUnrealAiChatBlockKind::PlanWorkerLane:
		return LOCTEXT("ChatRolePlanWorker", "Plan node");
	default:
		return FText::GetEmpty();
	}
}

TSharedRef<SWidget> UnrealAiChatTranscriptStyle::WrapTranscriptBlockBody(
	const FUnrealAiChatBlock& Block,
	const TSharedRef<SWidget>& Body,
	const EUnrealAiChatTranscriptChromeMode Mode)
{
	if (Mode == EUnrealAiChatTranscriptChromeMode::None)
	{
		return Body;
	}
	const FLinearColor Accent = GetRoleAccentLinear(Block);
	const FSlateColor AccentSlate(Accent);

	TSharedRef<SWidget> Inner = Body;
	if (Mode == EUnrealAiChatTranscriptChromeMode::Full)
	{
		const FSlateBrush* IconBrush = GetRoleIconBrush(Block);
		const FText Label = GetRoleLabelText(Block);
		Inner = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 5.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f).VAlign(VAlign_Center)
				[
					SNew(SBox)
						.WidthOverride(RoleIconSize)
						.HeightOverride(RoleIconSize)
						[
							SNew(SImage).Image(IconBrush).ColorAndOpacity(AccentSlate)
						]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
						.Text(Label)
						.Font(FUnrealAiEditorStyle::FontListRowTitle())
						.ColorAndOpacity(AccentSlate)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				Body
			];
	}

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SBox)
				.WidthOverride(RoleAccentBarWidth)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SNew(SImage)
						.Image(FAppStyle::GetBrush(TEXT("WhiteBrush")))
						.ColorAndOpacity(AccentSlate)
				]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f)
		[
			Inner
		];
}

#undef LOCTEXT_NAMESPACE
