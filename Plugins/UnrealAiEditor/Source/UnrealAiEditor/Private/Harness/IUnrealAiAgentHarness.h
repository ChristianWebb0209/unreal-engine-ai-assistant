#pragma once

#include "CoreMinimal.h"
#include "Harness/UnrealAiAgentTypes.h"

class IAgentRunSink;

/** Single entry for UI: one user turn (may include multiple LLM round-trips for tools). */
class IUnrealAiAgentHarness
{
public:
	virtual ~IUnrealAiAgentHarness() = default;

	virtual void RunTurn(const FUnrealAiAgentTurnRequest& Request, TSharedPtr<IAgentRunSink> Sink) = 0;

	virtual void CancelTurn() = 0;

	/** True while a turn is actively streaming or executing tools. */
	virtual bool IsTurnInProgress() const = 0;
};
