#pragma once

#include "CoreMinimal.h"

#include "Widgets/SWidget.h"

/**
 * Build Slate body for assistant chat: fenced ``` code blocks (mono panel), headings, bullets, ordered lists,
 * GitHub-style task lists (- / * / + / 1. variants), **bold**, links, inline code.
 */
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
