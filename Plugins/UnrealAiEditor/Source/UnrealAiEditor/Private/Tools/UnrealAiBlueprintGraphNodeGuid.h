#pragma once

#include "CoreMinimal.h"

struct FGuid;

/**
 * Parse Blueprint graph node_guid strings from agents: dashed UUID, 32-hex compact, guid: prefix, optional braces/quotes.
 * On success, optionally fills OutCanonicalLex with LexToString(OutGuid) for stable guid:... wiring.
 */
bool UnrealAiTryParseBlueprintGraphNodeGuid(FString In, FGuid& OutGuid, FString* OutCanonicalLex = nullptr);
