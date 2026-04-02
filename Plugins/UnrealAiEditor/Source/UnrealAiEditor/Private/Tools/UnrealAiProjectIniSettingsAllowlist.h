#pragma once

#include "CoreMinimal.h"

/**
 * Project-scope settings_set writes to GEngineIni (/Script/Section keys) are allowlisted explicitly
 * to avoid arbitrary DefaultEngine.ini corruption. Empty list means no keys are writable until added here.
 */
bool UnrealAiIsAllowlistedProjectEngineIniWrite(const FString& Section, const FString& Name);
