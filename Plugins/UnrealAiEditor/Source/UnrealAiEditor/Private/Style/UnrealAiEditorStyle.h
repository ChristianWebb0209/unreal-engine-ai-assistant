#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"

/**
 * Central UI tokens for the plugin: prefer these over ad-hoc FLinearColor / FCoreStyle font sizes
 * so chat, settings, and tool cards stay visually consistent.
 */
class FUnrealAiEditorStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static const ISlateStyle& Get();
	static FName GetStyleSetName();

	static const FSlateBrush* GetBrush(FName PropertyName);

	// --- Typography (FCoreStyle defaults; single place to swap later) ---
	static FSlateFontInfo FontCaption()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 8);
	}
	static FSlateFontInfo FontLabelBold()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10);
	}
	static FSlateFontInfo FontComposerBadge()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11);
	}
	static FSlateFontInfo FontSectionHeading()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12);
	}
	static FSlateFontInfo FontWindowTitle()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14);
	}
	/** Popover section titles (@ / slash panels). */
	static FSlateFontInfo FontPopoverTitle()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 8);
	}
	/** Dense list rows (autocomplete, attachment lines). */
	static FSlateFontInfo FontBodySmall()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9);
	}
	static FSlateFontInfo FontListRowTitle()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 9);
	}
	static FSlateFontInfo FontBodyRegular11()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11);
	}
	static FSlateFontInfo FontRegular10()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10);
	}
	static FSlateFontInfo FontMono8()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 8);
	}
	static FSlateFontInfo FontMono9()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 9);
	}
	static FSlateFontInfo FontItalicCaption()
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Italic"), 9);
	}
	/** Bold at arbitrary point size (markdown headings, dynamic UI). */
	static FSlateFontInfo FontBold(int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
	}
	/** Agent Chat tab/menus/mode: Starship AIController_16 vector icon from engine Editor/Slate (always present). */
	static FName AgentChatTabIconName();
	static const FSlateBrush* GetAgentChatTabIconBrush();
	/** Refreshes fixed chat bubble brushes (plugin-defined; not user-configurable). */
	static void ApplyChatBubbleColorsFromSettings();
	static FSlateColor ColorBackgroundCanvas();
	static FSlateColor ColorTextPrimary();
	/** User-authored chat bubble text (warm off-white; not #fff — easier on dark taupe bubbles). */
	static FSlateColor ColorChatUserMessage();
	/** Secondary / hint text (sidebars, metadata, panel hints). */
	static FSlateColor ColorTextMuted();
	/** Footer and lowest-emphasis labels (composer hint line). */
	static FSlateColor ColorTextFooter();
	/** Bright foreground on dark controls (e.g. Send button, primary composer actions). */
	static FSlateColor ColorComposerForegroundBright();
	/** Muted lines in tool cards and autocomplete (between primary and footer). */
	static FSlateColor ColorTextMetaHint();
	/** Italic “thinking” subline (slightly transparent). */
	static FSlateColor ColorThinkingSubline();
	/** Assistant markdown headings — bright but not pure white (cool-tinted). */
	static FSlateColor ColorMarkdownHeading();
	/** Assistant reply body (cool blue-gray; contrasts with ColorChatUserMessage). */
	static FSlateColor ColorMarkdownBody();
	/** Todo checkbox color when item is done (markdown). */
	static FLinearColor LinearColorMarkdownTodoDoneCheck();
	static FSlateColor ColorAccent();
	/** Same as ColorTextMuted(); retained for existing “debug” naming. */
	static FSlateColor ColorDebugMuted();
	static FSlateColor ColorDebugNavFolder();

	/** Composer @mention / slash popover panel backgrounds. */
	static FLinearColor LinearColorPanelMentionBg();
	static FLinearColor LinearColorPanelSlashBg();
	/** Agent mode dropdown panel. */
	static FLinearColor LinearColorModeMenuPopoverBg();
	/** Chat window title strip behind header text. */
	static FLinearColor LinearColorChatHeaderStrip();

	/**
	 * Tool call card (SToolCallCard): inner panel behind header + body — Starship / Cursor-style charcoal
	 * with a slight cool bias; avoids pure black (#000) on expandable + JSON wells.
	 */
	static FLinearColor LinearColorToolCallCardInset();
	/** Read-only arguments / result text wells inside tool cards (slightly deeper than inset). */
	static FLinearColor LinearColorToolCallCodeWell();

	/** Single plugin-wide checkbox / toggle appearance (clone of editor default; customize in Initialize). */
	static FName CheckboxStyleName();
	static const FCheckBoxStyle& GetCheckboxStyle();

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
