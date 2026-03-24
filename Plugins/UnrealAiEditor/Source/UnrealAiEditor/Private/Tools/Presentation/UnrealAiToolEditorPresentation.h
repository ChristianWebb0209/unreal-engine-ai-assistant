#pragma once

#include "CoreMinimal.h"

/** One clickable link inside tool editor notes (object path -> navigate in editor). */
struct FUnrealAiToolAssetLink
{
	/** Human-friendly label (e.g. blueprint name). */
	FString Label;

	/** Unreal object path (e.g. /Game/Folder/BP_MyAsset.BP_MyAsset). */
	FString ObjectPath;
};

/** UI-only payload for showing richer editor notes with a tool call result. */
struct FUnrealAiToolEditorPresentation
{
	/** Tool note markdown (rendered in UI, never sent to the model). */
	FString MarkdownBody;

	/** Optional image shown under the tool card (png path owned by Saved/ by default).
	 * Phase 2 idea: optionally regenerate/carry this across chat reloads if desired.
	 */
	FString ImageFilePath;

	/** Optional clickable links row. */
	TArray<FUnrealAiToolAssetLink> AssetLinks;
};

