#pragma once

#include "CoreMinimal.h"

/**
 * Editor-side helpers for environment tools: static mesh inventory as JSON.
 * Used by internal tooling; identifiers are UnrealAiEditor-prefixed only.
 */
namespace UnrealAiEditorEnvironmentBridge
{
	/** JSON object with meshes[] and count for all /Game static meshes. */
	FString ExportStaticMeshesJson();

	/** JSON object with meshes[] and count for static meshes under a /Game-relative or /Game path. */
	FString ExportStaticMeshesJsonForPath(const FString& FolderPath);
}
