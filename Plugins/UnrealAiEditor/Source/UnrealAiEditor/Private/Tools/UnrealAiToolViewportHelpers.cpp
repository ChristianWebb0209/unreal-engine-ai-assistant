#include "Tools/UnrealAiToolViewportHelpers.h"

#include "Editor.h"
#include "EditorViewportClient.h"
#include "UnrealEdGlobals.h"

FEditorViewportClient* UnrealAiGetActiveLevelViewportClient()
{
	if (!GEditor)
	{
		return nullptr;
	}
	FViewport* Viewport = GEditor->GetActiveViewport();
	if (!Viewport)
	{
		return nullptr;
	}
	return static_cast<FEditorViewportClient*>(Viewport->GetClient());
}
