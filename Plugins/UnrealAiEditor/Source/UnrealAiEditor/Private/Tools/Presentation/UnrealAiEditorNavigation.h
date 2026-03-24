#pragma once

#include "CoreMinimal.h"

/** UI-only navigation helpers for tool/editor notes. */
namespace UnrealAiEditorNavigation
{
	/** Open an asset editor for the given Unreal object path (best-effort). */
	bool NavigateToAssetObjectPath(const FString& ObjectPath);
}

