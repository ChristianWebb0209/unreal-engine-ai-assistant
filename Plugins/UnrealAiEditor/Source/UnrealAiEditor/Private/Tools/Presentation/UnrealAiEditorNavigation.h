#pragma once

#include "CoreMinimal.h"

/** UI-only navigation helpers for tool/editor notes. */
namespace UnrealAiEditorNavigation
{
	/** Opens the asset in its editor (standalone window; avoids engine WorldCentric TabManager asserts from tools). */
	void OpenAssetEditorPreferDocked(UObject* Asset);

	/** Open an asset editor for the given Unreal object path (best-effort). */
	bool NavigateToAssetObjectPath(const FString& ObjectPath);

	/**
	 * Resolve a markdown/chat link target: opens http(s)/mailto in the system browser,
	 * otherwise tries Unreal object paths (assets, maps, and subpaths such as PersistentLevel actors).
	 */
	bool NavigateFromChatMarkdownTarget(const FString& Target);
}

