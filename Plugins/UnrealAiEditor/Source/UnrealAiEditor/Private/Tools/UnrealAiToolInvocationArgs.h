#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Consumes optional tool argument "focused" (UI hint, not forwarded to handlers).
 * @return true if the caller asked to raise related editor UI after a successful run.
 */
bool UnrealAiConsumeFocusedFlag(const TSharedPtr<FJsonObject>& Args);
