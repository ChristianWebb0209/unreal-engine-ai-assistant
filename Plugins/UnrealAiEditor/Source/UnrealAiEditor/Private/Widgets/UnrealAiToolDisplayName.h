#pragma once

#include "CoreMinimal.h"

class FUnrealAiToolCatalog;

/**
 * Human-facing tool title for chat UI only (not sent to the model).
 * Uses optional catalog fields ui_label / display_name, then falls back to Title Case from tool_id.
 */
FString UnrealAiResolveToolUserFacingName(const FString& ToolId, const FUnrealAiToolCatalog* Catalog);

/** Snake_case / dispatch id → "Title Case Words" (no catalog). */
FString UnrealAiFormatToolIdAsTitleWords(const FString& ToolId);
