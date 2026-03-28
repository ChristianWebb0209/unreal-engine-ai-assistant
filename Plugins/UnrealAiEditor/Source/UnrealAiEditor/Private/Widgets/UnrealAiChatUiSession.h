#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FUnrealAiPlanDraftBuild, const FString& /*DagJson*/);

/** Shared chat UI state (thread + model selection) between header and composer. */
struct FUnrealAiChatUiSession
{
	FGuid ThreadId;
	FString ModelProfileId;
	/** Optional AI-proposed name for this chat tab (resolved from first assistant reply). */
	FString ChatName;

	/** Fired when ChatName is set or cleared so the tab title and header can refresh. */
	FSimpleMulticastDelegate OnChatNameChanged;

	/** Plan draft Build clicked (DAG JSON from editor, possibly user-edited). */
	FUnrealAiPlanDraftBuild OnPlanDraftBuild;
};
