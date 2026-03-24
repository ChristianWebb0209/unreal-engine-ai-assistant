#pragma once

#include "CoreMinimal.h"

/** Shared chat UI state (thread + model selection) between header and composer. */
struct FUnrealAiChatUiSession
{
	FGuid ThreadId;
	FString ModelProfileId;
	/** Optional AI-proposed name for this chat tab (resolved from first assistant reply). */
	FString ChatName;
};
