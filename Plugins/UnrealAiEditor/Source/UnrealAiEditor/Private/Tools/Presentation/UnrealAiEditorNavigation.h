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
	 * Resolve a markdown/chat link target: opens http(s)/mailto in the system browser; `/Game`/`/Engine` folder
	 * paths sync the Content Browser; concrete asset paths sync the browser to the asset without opening an editor;
	 * map/subobject strings with `:` prefer actor selection / subobject editor.
	 */
	bool NavigateFromChatMarkdownTarget(const FString& Target);

	/**
	 * Call from chat/note link widgets. Defers Unreal Content Browser sync by one frame so a click that dismisses
	 * the Content Drawer still applies navigation on the same gesture; URLs open immediately.
	 */
	void NavigateFromChatMarkdownTargetFromChatLink(const FString& Target);
}

