#pragma once

#include "CoreMinimal.h"

struct FSlateBrush;

/** Thumbnail brush for a PNG on disk; cached by absolute path. Falls back to document icon on failure. */
const FSlateBrush* UnrealAiGetOrCreateScreenshotThumbnailBrush(const FString& AbsolutePathPng);

/** Opens a modal window showing the image (PNG/JPG etc. via engine import). */
void UnrealAiOpenImagePreviewWindow(const FString& AbsolutePath);
