#include "Style/UnrealAiEditorStyle.h"

#include "Brushes/SlateBoxBrush.h"
#include "Math/Color.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"
#if WITH_EDITOR
#include "Framework/Application/SlateApplication.h"
#endif

TSharedPtr<FSlateStyleSet> FUnrealAiEditorStyle::StyleSet = nullptr;

namespace UnrealAiEditorBubbleColors
{
	/** User bubble fill — slightly warm charcoal (vs. flat #1e1e1e) for separation from assistant lane. */
	static FLinearColor UserBubbleFill()
	{
		return FLinearColor(FColor(0x26, 0x23, 0x21, 0xff));
	}

	static FLinearColor AssistantBubbleFill()
	{
		return FLinearColor(0.092f, 0.098f, 0.114f, 1.f);
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

	Style->Set("UnrealAiEditor.PanelBackground", new FSlateColorBrush(FLinearColor(0.118f, 0.118f, 0.118f, 1.f)));
	Style->Set("UnrealAiEditor.Elevated", new FSlateColorBrush(FLinearColor(0.145f, 0.145f, 0.145f, 1.f)));
	Style->Set("UnrealAiEditor.BorderSubtle", new FSlateColorBrush(FLinearColor(0.235f, 0.235f, 0.235f, 1.f)));
	Style->Set("UnrealAiEditor.ToolPending", new FSlateColorBrush(FLinearColor(0.23f, 0.18f, 0.0f, 0.35f)));
	Style->Set("UnrealAiEditor.ToolSuccess", new FSlateColorBrush(FLinearColor(0.06f, 0.24f, 0.12f, 0.45f)));
	Style->Set("UnrealAiEditor.ToolError", new FSlateColorBrush(FLinearColor(0.24f, 0.08f, 0.08f, 0.45f)));
	// Todo plan card: muted forest (distinct from user taupe and assistant gray).
	Style->Set("UnrealAiEditor.TodoPlanPanel", new FSlateColorBrush(FLinearColor(0.11f, 0.16f, 0.13f, 1.f)));
	Style->Set("UnrealAiEditor.AssistantLane", new FSlateColorBrush(FLinearColor(0.14f, 0.16f, 0.2f, 0.55f)));
	Style->Set("UnrealAiEditor.DebugNav", new FSlateColorBrush(FLinearColor(0.10f, 0.13f, 0.12f, 1.f)));
	Style->Set("UnrealAiEditor.DebugInspect", new FSlateColorBrush(FLinearColor(0.09f, 0.11f, 0.14f, 1.f)));
	// Tool call cards: neutral shell + code wells (rounded; aligns with dark editor panels, not #000).
	Style->Set(
		"UnrealAiEditor.ToolCallCardOuter",
		new FSlateRoundedBoxBrush(FLinearColor(0.13f, 0.14f, 0.165f, 1.f), 8.f));
	Style->Set(
		"UnrealAiEditor.ToolCallCodeWell",
		new FSlateRoundedBoxBrush(FLinearColor(0.105f, 0.11f, 0.128f, 1.f), 4.f));
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
	return FSlateColor(FLinearColor(0.118f, 0.118f, 0.118f, 1.f));
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
	return FLinearColor(0.118f, 0.124f, 0.142f, 1.f);
}

FLinearColor FUnrealAiEditorStyle::LinearColorToolCallCodeWell()
{
	return FLinearColor(0.102f, 0.108f, 0.125f, 1.f);
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
