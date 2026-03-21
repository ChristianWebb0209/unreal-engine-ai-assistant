#pragma once

#include "CoreMinimal.h"

#include "Widgets/SWidget.h"

/** Build Slate body for assistant chat: headings, bullets, GitHub-style task list items. */
TSharedRef<SWidget> UnrealAiBuildMarkdownChatBody(const FString& Markdown);

/** Remove common inline markdown tokens for plain display. */
FString UnrealAiStripInlineMarkdown(const FString& Line);
