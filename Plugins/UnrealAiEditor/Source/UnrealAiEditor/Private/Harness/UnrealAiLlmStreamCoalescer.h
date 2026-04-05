#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "HAL/CriticalSection.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include <atomic>

/**
 * Coalesces high-frequency LLM stream events from any thread and applies them on the game thread
 * via a bounded-rate ticker (or immediate drain for scenario sync runs).
 */
class FUnrealAiLlmStreamCoalescer final : public TSharedFromThis<FUnrealAiLlmStreamCoalescer>
{
public:
	struct FConfig
	{
		/** When true (headed scenario sync), every enqueue schedules a full drain to the game thread. */
		bool bImmediateFlush = false;
		float FlushIntervalSec = 0.024f;
		int32 MaxCharsPerPartialFlush = 8192;
	};

	FUnrealAiLlmStreamCoalescer() = default;

	void Begin(FConfig InConfig, TFunction<void(const FUnrealAiLlmStreamEvent&)> InEmitOnGameThread);
	void ShutdownDropPending();
	void FlushAllSynchronouslyOnGameThread();
	void Enqueue(const FUnrealAiLlmStreamEvent& Ev);

private:
	static void AppendMergedLocked(TArray<FUnrealAiLlmStreamEvent>& Queue, const FUnrealAiLlmStreamEvent& Ev);
	bool HasPendingLocked() const;
	void RemoveTickerIfAny();
	void PumpPartialOnGameThread();
	void PumpUntilEmptyOnGameThread();
	void ScheduleDrainAllToGameThread();
	bool TickCoalesce(float DeltaTime);

	FCriticalSection Mutex;
	TArray<FUnrealAiLlmStreamEvent> Pending;
	TFunction<void(const FUnrealAiLlmStreamEvent&)> EmitOnGameThread;
	FConfig Config;
	FTSTicker::FDelegateHandle TickerHandle;
	std::atomic<bool> bActive{false};
	float AccumSec = 0.f;
	std::atomic<bool> bDrainAllScheduled{false};
};
