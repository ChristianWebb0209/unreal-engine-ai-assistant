#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"

/** Shared chat UI state (thread + model selection) between header and composer. */
struct FUnrealAiChatUiSession
{
	FGuid ThreadId;
	FString ModelProfileId;
	/** Optional AI-proposed name for this chat tab (resolved from first assistant reply). */
	FString ChatName;

	/** Fired when ChatName is set or cleared so the tab title and header can refresh. */
	FSimpleMulticastDelegate OnChatNameChanged;
};
