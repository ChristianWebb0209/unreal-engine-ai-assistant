#pragma once

#include "CoreMinimal.h"

#include "Widgets/SWidget.h"

/** Build Slate body for assistant chat: headings, bullets, GitHub-style task list items. */
TSharedRef<SWidget> UnrealAiBuildMarkdownChatBody(const FString& Markdown);

/** Remove common inline markdown tokens for plain display. */
FString UnrealAiStripInlineMarkdown(const FString& Line);

/**
 * Single clickable link (blue hyperlink styling, small leading icon for web vs editor targets).
 * Target is passed to navigation (http(s)/mailto vs Unreal object paths).
 */
TSharedRef<SWidget> UnrealAiMakeChatHyperlink(const FString& Label, const FString& Target);

/**
 * Build Slate body for tool/editor notes.
 * Supports `[label](url-or-/Game/...)` and bare `https://` URLs.
 */
TSharedRef<SWidget> UnrealAiBuildMarkdownToolNoteBody(const FString& Markdown);
