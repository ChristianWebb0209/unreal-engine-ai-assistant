#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Containers/UnrealString.h"

class FUnrealAiBackendRegistry;
class IAgentRunSink;

/**
 * Polls Slate while an agent turn is executing tools. When a modal editor dialog appears
 * (overwrite, confirm save, etc.), notifies the active chat sink and records text for the
 * in-flight tool result so the model can react on the next LLM round after the user dismisses it.
 */
class FUnrealAiEditorModalMonitor
{
public:
	static void Startup(const TSharedPtr<FUnrealAiBackendRegistry>& Registry);
	static void Shutdown();

	static void NotifyAgentTurnStarted(const TSharedPtr<IAgentRunSink>& Sink);
	/** Clears hooks only if this sink is still the active run (avoids cancel/finish races when a new run started). */
	static void NotifyAgentTurnEndedForSink(const TSharedPtr<IAgentRunSink>& FinishedSink);

	/** Appended to the tool message the harness sends back to the model (cleared after read). */
	static FString ConsumePendingToolDialogFootnote();

private:
	static bool OnTick(float DeltaTime);
	static bool TitleSuggestsBlockingEditorDialog(const FString& Title);

	static TWeakPtr<FUnrealAiBackendRegistry> BackendRegistry;
	static TWeakPtr<IAgentRunSink> ActiveSink;
	static FTSTicker::FDelegateHandle TickerHandle;
	static TSet<uint64> SeenModalKeys;
	static FString PendingToolFootnote;
	static float TickAccumSec;
};
