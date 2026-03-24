#pragma once

#include "CoreMinimal.h"
#include "Tools/Presentation/UnrealAiToolEditorPresentation.h"

/** Shared builders for tool editor notes (markdown, links, optional images). */
namespace UnrealAiToolEditorNoteBuilders
{
	/** Generic blueprint editor note. */
	TSharedPtr<FUnrealAiToolEditorPresentation> MakeBlueprintToolNote(
		const FString& BlueprintObjectPath,
		const FString& GraphName,
		const FString& ToolMarkdownBody);
}

