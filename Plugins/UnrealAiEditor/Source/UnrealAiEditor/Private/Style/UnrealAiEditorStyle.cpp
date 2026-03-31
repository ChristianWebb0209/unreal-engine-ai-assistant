#include "Style/UnrealAiEditorStyle.h"

#include "Brushes/SlateBoxBrush.h"
#include "Math/Color.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"
#if WITH_EDITOR
#include "Framework/Application/SlateApplication.h"
#endif

TSharedPtr<FSlateStyleSet> FUnrealAiEditorStyle::StyleSet = nullptr;

namespace UnrealAiEditorBubbleColors
{
	static FLinearColor UserBubbleFill()
	{
		// Must visually match run/notice boxes: a darker recessed rounded surface.
		// Using Panel can blend into the chat background, making the rounded "bubble"
		// look like it has no background at all.
		return FStyleColors::Recessed.GetSpecifiedColor();
	}

	static FLinearColor AssistantBubbleFill()
	{
		return FStyleColors::Panel.GetSpecifiedColor();
	}
} // namespace UnrealAiEditorBubbleColors

void FUnrealAiEditorStyle::Initialize()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	StyleSet = MakeShareable(new FSlateStyleSet(GetStyleSetName()));
	StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	FSlateStyleSet* Style = StyleSet.Get();

	Style->Set("UnrealAiEditor.PanelBackground", new FSlateColorBrush(FStyleColors::Panel.GetSpecifiedColor()));
	Style->Set("UnrealAiEditor.Elevated", new FSlateColorBrush(FStyleColors::Header.GetSpecifiedColor()));
	Style->Set("UnrealAiEditor.BorderSubtle", new FSlateColorBrush(FStyleColors::DropdownOutline.GetSpecifiedColor()));
	Style->Set("UnrealAiEditor.ToolPending", new FSlateColorBrush(FLinearColor(0.23f, 0.18f, 0.0f, 0.35f)));
	Style->Set("UnrealAiEditor.ToolSuccess", new FSlateColorBrush(FLinearColor(0.06f, 0.24f, 0.12f, 0.45f)));
	Style->Set("UnrealAiEditor.ToolError", new FSlateColorBrush(FLinearColor(0.24f, 0.08f, 0.08f, 0.45f)));
	// Todo plan card: muted forest (distinct from user taupe and assistant gray).
	Style->Set("UnrealAiEditor.TodoPlanPanel", new FSlateColorBrush(FLinearColor(0.11f, 0.16f, 0.13f, 1.f)));
	Style->Set("UnrealAiEditor.AssistantLane", new FSlateColorBrush(FStyleColors::Header.GetSpecifiedColor()));
	Style->Set("UnrealAiEditor.DebugNav", new FSlateColorBrush(FLinearColor(0.10f, 0.13f, 0.12f, 1.f)));
	Style->Set("UnrealAiEditor.DebugInspect", new FSlateColorBrush(FLinearColor(0.09f, 0.11f, 0.14f, 1.f)));
	// Chat renderer containers: rounded surfaces for notices and run progress (no inline colors in widgets).
	Style->Set(
		"UnrealAiEditor.ChatRunProgress",
		new FSlateRoundedBoxBrush(FStyleColors::Recessed.GetSpecifiedColor(), 8.f));
	Style->Set(
		"UnrealAiEditor.ChatNoticeInfo",
		new FSlateRoundedBoxBrush(FStyleColors::Panel.GetSpecifiedColor(), 8.f));
	Style->Set(
		"UnrealAiEditor.ChatNoticeError",
		new FSlateRoundedBoxBrush(FLinearColor(0.24f, 0.08f, 0.08f, 0.3f), 8.f));
	Style->Set(
		"UnrealAiEditor.ChatNoticeCancelled",
		new FSlateRoundedBoxBrush(FLinearColor(0.23f, 0.18f, 0.0f, 0.28f), 8.f));
	// Tool call cards: neutral shell + code wells (rounded; aligns with dark editor panels, not #000).
	Style->Set(
		"UnrealAiEditor.ToolCallCardOuter",
		new FSlateRoundedBoxBrush(FStyleColors::Header.GetSpecifiedColor(), 8.f));
	Style->Set(
		"UnrealAiEditor.ToolCallCodeWell",
		new FSlateRoundedBoxBrush(FStyleColors::Recessed.GetSpecifiedColor(), 4.f));
	ApplyChatBubbleColorsFromSettings();

	// Agent Chat branding: engine SVG under Content/Editor/Slate (avoids missing FAppStyle "Icons.*" names on some versions).
	{
		const FVector2D AgentIconSize(16.f, 16.f);
		Style->Set(
			"UnrealAiEditor.AgentChatTabIcon",
			new FSlateVectorImageBrush(
				Style->RootToContentDir(TEXT("Starship/AssetIcons/AIController_16"), TEXT(".svg")),
				AgentIconSize));
	}

	{
		const FCheckBoxStyle& EngineCheckbox = FAppStyle::Get().GetWidgetStyle<FCheckBoxStyle>(TEXT("Checkbox"));
		Style->Set(CheckboxStyleName(), EngineCheckbox);
	}

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FUnrealAiEditorStyle::ApplyChatBubbleColorsFromSettings()
{
	if (!StyleSet.IsValid())
	{
		return;
	}
	FSlateStyleSet* Style = StyleSet.Get();
	Style->Set(
		"UnrealAiEditor.UserBubble",
		new FSlateRoundedBoxBrush(UnrealAiEditorBubbleColors::UserBubbleFill(), 8.f));
	Style->Set(
		"UnrealAiEditor.AssistantBubble",
		new FSlateColorBrush(UnrealAiEditorBubbleColors::AssistantBubbleFill()));
#if WITH_EDITOR
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().InvalidateAllWidgets(true);
	}
#endif
}

void FUnrealAiEditorStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		StyleSet.Reset();
	}
}

const ISlateStyle& FUnrealAiEditorStyle::Get()
{
	return *StyleSet.Get();
}

FName FUnrealAiEditorStyle::GetStyleSetName()
{
	static FName Name(TEXT("UnrealAiEditorStyle"));
	return Name;
}

const FSlateBrush* FUnrealAiEditorStyle::GetBrush(FName PropertyName)
{
	return StyleSet.IsValid() ? StyleSet->GetBrush(PropertyName) : nullptr;
}

FName FUnrealAiEditorStyle::AgentChatTabIconName()
{
	static const FName N(TEXT("UnrealAiEditor.AgentChatTabIcon"));
	return N;
}

const FSlateBrush* FUnrealAiEditorStyle::GetAgentChatTabIconBrush()
{
	return GetBrush(AgentChatTabIconName());
}

FSlateColor FUnrealAiEditorStyle::ColorBackgroundCanvas()
{
	return FSlateColor(FStyleColors::Panel.GetSpecifiedColor());
}

FSlateColor FUnrealAiEditorStyle::ColorTextPrimary()
{
	// Editor-style soft gray (Starship labels); avoids pure white on dark panels.
	return FSlateColor(FLinearColor(0.84f, 0.86f, 0.89f, 1.f));
}

FSlateColor FUnrealAiEditorStyle::ColorChatUserMessage()
{
	// Warm off-white: readable on UserBubble without harsh #ffffff.
	return FSlateColor(FLinearColor(0.9f, 0.87f, 0.83f, 1.f));
}

FSlateColor FUnrealAiEditorStyle::ColorTextMuted()
{
	return FSlateColor(FLinearColor(0.55f, 0.58f, 0.62f, 1.f));
}

FSlateColor FUnrealAiEditorStyle::ColorTextFooter()
{
	return FSlateColor(FLinearColor(0.5f, 0.52f, 0.55f, 1.f));
}

FSlateColor FUnrealAiEditorStyle::ColorComposerForegroundBright()
{
	return FSlateColor(FLinearColor(0.88f, 0.9f, 0.94f, 1.f));
}

FSlateColor FUnrealAiEditorStyle::ColorTextMetaHint()
{
	return FSlateColor(FLinearColor(0.65f, 0.7f, 0.78f, 1.f));
}

FSlateColor FUnrealAiEditorStyle::ColorThinkingSubline()
{
	return FSlateColor(FLinearColor(0.5f, 0.52f, 0.56f, 0.72f));
}

FSlateColor FUnrealAiEditorStyle::ColorMarkdownHeading()
{
	return FSlateColor(FLinearColor(0.86f, 0.88f, 0.93f, 1.f));
}

FSlateColor FUnrealAiEditorStyle::ColorMarkdownBody()
{
	// Cool slate — clearly distinct from ColorChatUserMessage (warm) on the timeline.
	return FSlateColor(FLinearColor(0.76f, 0.81f, 0.87f, 1.f));
}

FLinearColor FUnrealAiEditorStyle::LinearColorMarkdownTodoDoneCheck()
{
	return FLinearColor(0.4f, 0.82f, 0.52f, 1.f);
}

FSlateColor FUnrealAiEditorStyle::ColorAccent()
{
	return FSlateColor(FLinearColor(0.231f, 0.510f, 0.965f, 1.f));
}

FSlateColor FUnrealAiEditorStyle::ColorDebugMuted()
{
	return ColorTextMuted();
}

FSlateColor FUnrealAiEditorStyle::ColorDebugNavFolder()
{
	return FSlateColor(FLinearColor(0.45f, 0.72f, 0.55f, 1.f));
}

FLinearColor FUnrealAiEditorStyle::LinearColorPanelMentionBg()
{
	return FLinearColor(0.16f, 0.16f, 0.18f, 0.95f);
}

FLinearColor FUnrealAiEditorStyle::LinearColorPanelSlashBg()
{
	return FLinearColor(0.14f, 0.18f, 0.16f, 0.95f);
}

FLinearColor FUnrealAiEditorStyle::LinearColorModeMenuPopoverBg()
{
	return FLinearColor(0.12f, 0.12f, 0.13f, 0.98f);
}

FLinearColor FUnrealAiEditorStyle::LinearColorChatHeaderStrip()
{
	return FLinearColor(0.12f, 0.12f, 0.12f, 1.f);
}

FLinearColor FUnrealAiEditorStyle::LinearColorToolCallCardInset()
{
	return FStyleColors::Panel.GetSpecifiedColor();
}

FLinearColor FUnrealAiEditorStyle::LinearColorToolCallCodeWell()
{
	return FStyleColors::Recessed.GetSpecifiedColor();
}

FName FUnrealAiEditorStyle::CheckboxStyleName()
{
	static const FName N(TEXT("UnrealAiEditor.Checkbox"));
	return N;
}

const FCheckBoxStyle& FUnrealAiEditorStyle::GetCheckboxStyle()
{
	return Get().GetWidgetStyle<FCheckBoxStyle>(CheckboxStyleName());
}
