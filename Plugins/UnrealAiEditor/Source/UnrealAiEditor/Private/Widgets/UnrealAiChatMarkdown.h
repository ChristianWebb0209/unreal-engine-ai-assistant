#pragma once

#include "CoreMinimal.h"

#include "Templates/Function.h"

#include "Widgets/SWidget.h"

/** Build Slate body for assistant chat: headings, bullets, GitHub-style task list items. */
TSharedRef<SWidget> UnrealAiBuildMarkdownChatBody(const FString& Markdown);

/** Remove common inline markdown tokens for plain display. */
FString UnrealAiStripInlineMarkdown(const FString& Line);

/**
 * Build Slate body for tool/editor notes.
 * Supports inline asset links in the form `[label](/Game/Path.Object)` which will be rendered as clickable hyperlinks.
 */
TSharedRef<SWidget> UnrealAiBuildMarkdownToolNoteBody(
	const FString& Markdown,
	const TFunctionRef<void(const FString& ObjectPath)>& OnLinkClicked);
