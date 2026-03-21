#pragma once

#include "CoreMinimal.h"

/** Stable id for the current .uproject (for chats/context paths). */
namespace UnrealAiProjectId
{
	/** Hex-ish id from project file path; fallback if none. */
	FString GetCurrentProjectId();
}
