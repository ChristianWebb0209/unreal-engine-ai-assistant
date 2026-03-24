#pragma once

#include "CoreMinimal.h"

/** Best-effort helpers for generating PNG images for blueprint-related tool editor notes. */
namespace UnrealAiBlueprintThumbnailCapture
{
	/**
	 * Renders a thumbnail for the blueprint object and saves it as a PNG on disk.
	 * @return true if a PNG was written.
	 */
	bool TryCaptureBlueprintThumbnailPng(
		const FString& BlueprintObjectPath,
		const FString& OutAbsPngPath,
		uint32 MaxWidth,
		uint32 MaxHeight);
}

