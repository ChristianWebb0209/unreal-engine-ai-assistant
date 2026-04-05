#include "Harness/UnrealAiLlmStreamCoalescer.h"

#include "Async/Async.h"
#include "Misc/AssertionMacros.h"
#include "Observability/UnrealAiGameThreadPerf.h"

void FUnrealAiLlmStreamCoalescer::AppendMergedLocked(TArray<FUnrealAiLlmStreamEvent>& Queue, const FUnrealAiLlmStreamEvent& Ev)
{
	switch (Ev.Type)
	{
	case EUnrealAiLlmStreamEventType::AssistantDelta:
		if (Queue.Num() > 0 && Queue.Last().Type == EUnrealAiLlmStreamEventType::AssistantDelta)
		{
			Queue.Last().DeltaText += Ev.DeltaText;
		}
		else
		{
			Queue.Add(Ev);
		}
		break;
	case EUnrealAiLlmStreamEventType::ThinkingDelta:
		if (Queue.Num() > 0 && Queue.Last().Type == EUnrealAiLlmStreamEventType::ThinkingDelta)
		{
			Queue.Last().DeltaText += Ev.DeltaText;
		}
		else
		{
			Queue.Add(Ev);
		}
		break;
	case EUnrealAiLlmStreamEventType::ToolCalls:
		if (Queue.Num() > 0 && Queue.Last().Type == EUnrealAiLlmStreamEventType::ToolCalls)
		{
			Queue.Last().ToolCalls.Append(Ev.ToolCalls);
		}
		else
		{
			Queue.Add(Ev);
		}
		break;
	default:
		Queue.Add(Ev);
		break;
	}
}

bool FUnrealAiLlmStreamCoalescer::HasPendingLocked() const
{
	return Pending.Num() > 0;
}

void FUnrealAiLlmStreamCoalescer::RemoveTickerIfAny()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	AccumSec = 0.f;
}

void FUnrealAiLlmStreamCoalescer::Begin(FConfig InConfig, TFunction<void(const FUnrealAiLlmStreamEvent&)> InEmitOnGameThread)
{
	ShutdownDropPending();
	Config = InConfig;
	EmitOnGameThread = MoveTemp(InEmitOnGameThread);
	bActive.store(true, std::memory_order_relaxed);
	if (!Config.bImmediateFlush)
	{
		const TWeakPtr<FUnrealAiLlmStreamCoalescer> Weak = AsShared();
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[Weak](float DeltaTime) -> bool
			{
				const TSharedPtr<FUnrealAiLlmStreamCoalescer> Pinned = Weak.Pin();
				if (!Pinned.IsValid())
				{
					return false;
				}
				return Pinned->TickCoalesce(DeltaTime);
			}));
	}
}

void FUnrealAiLlmStreamCoalescer::ShutdownDropPending()
{
	RemoveTickerIfAny();
	{
		FScopeLock Lock(&Mutex);
		Pending.Reset();
	}
	bActive.store(false, std::memory_order_relaxed);
	bDrainAllScheduled.store(false, std::memory_order_relaxed);
	EmitOnGameThread = nullptr;
}

void FUnrealAiLlmStreamCoalescer::FlushAllSynchronouslyOnGameThread()
{
	check(IsInGameThread());
	if (!bActive.load(std::memory_order_relaxed))
	{
		return;
	}
	PumpUntilEmptyOnGameThread();
}

void FUnrealAiLlmStreamCoalescer::Enqueue(const FUnrealAiLlmStreamEvent& Ev)
{
	{
		FScopeLock Lock(&Mutex);
		if (!bActive.load(std::memory_order_relaxed))
		{
			return;
		}
		AppendMergedLocked(Pending, Ev);
	}

	const bool bTerm = Ev.Type == EUnrealAiLlmStreamEventType::Finish || Ev.Type == EUnrealAiLlmStreamEventType::Error;
	if (Config.bImmediateFlush || bTerm)
	{
		ScheduleDrainAllToGameThread();
	}
}

void FUnrealAiLlmStreamCoalescer::PumpPartialOnGameThread()
{
	UNREALAI_GT_PERF_SCOPE("UI.LlmStreamCoalescer.PumpPartial");
	if (!EmitOnGameThread)
	{
		return;
	}
	TArray<FUnrealAiLlmStreamEvent> Batch;
	Batch.Reserve(8);
	int32 CharBudget = 0;
	bool bTookOne = false;
	{
		FScopeLock Lock(&Mutex);
		while (Pending.Num() > 0)
		{
			const FUnrealAiLlmStreamEvent& Peek = Pending[0];
			const bool bTerm = Peek.Type == EUnrealAiLlmStreamEventType::Finish || Peek.Type == EUnrealAiLlmStreamEventType::Error;
			if (bTerm)
			{
				Batch.Add(Pending[0]);
				Pending.RemoveAt(0);
				break;
			}
			if (Peek.Type == EUnrealAiLlmStreamEventType::ToolCalls)
			{
				Batch.Add(Pending[0]);
				Pending.RemoveAt(0);
				break;
			}
			int32 DeltaChars = 0;
			if (Peek.Type == EUnrealAiLlmStreamEventType::AssistantDelta
				|| Peek.Type == EUnrealAiLlmStreamEventType::ThinkingDelta)
			{
				DeltaChars = Peek.DeltaText.Len();
			}
			if (bTookOne && DeltaChars > 0 && CharBudget + DeltaChars > Config.MaxCharsPerPartialFlush)
			{
				break;
			}
			Batch.Add(Pending[0]);
			Pending.RemoveAt(0);
			CharBudget += DeltaChars;
			bTookOne = true;
		}
	}
	for (const FUnrealAiLlmStreamEvent& OutEv : Batch)
	{
		EmitOnGameThread(OutEv);
	}
}

void FUnrealAiLlmStreamCoalescer::PumpUntilEmptyOnGameThread()
{
	while (true)
	{
		bool bAny = false;
		{
			FScopeLock Lock(&Mutex);
			bAny = Pending.Num() > 0;
		}
		if (!bAny)
		{
			break;
		}
		PumpPartialOnGameThread();
	}
}

void FUnrealAiLlmStreamCoalescer::ScheduleDrainAllToGameThread()
{
	if (!bActive.load(std::memory_order_relaxed))
	{
		return;
	}
	if (IsInGameThread())
	{
		PumpUntilEmptyOnGameThread();
		return;
	}
	bool Expected = false;
	if (!bDrainAllScheduled.compare_exchange_strong(Expected, true, std::memory_order_acq_rel))
	{
		return;
	}
	const TWeakPtr<FUnrealAiLlmStreamCoalescer> Weak = AsShared();
	AsyncTask(ENamedThreads::GameThread,
			  [Weak]()
			  {
				  const TSharedPtr<FUnrealAiLlmStreamCoalescer> Self = Weak.Pin();
				  if (!Self.IsValid())
				  {
					  return;
				  }
				  Self->PumpUntilEmptyOnGameThread();
				  Self->bDrainAllScheduled.store(false, std::memory_order_release);
				  bool bMore = false;
				  {
					  FScopeLock Lock(&Self->Mutex);
					  bMore = Self->Pending.Num() > 0;
				  }
				  if (bMore && Self->bActive.load(std::memory_order_relaxed))
				  {
					  Self->ScheduleDrainAllToGameThread();
				  }
			  });
}

bool FUnrealAiLlmStreamCoalescer::TickCoalesce(float DeltaTime)
{
	if (!bActive.load(std::memory_order_relaxed))
	{
		return false;
	}
	AccumSec += DeltaTime;
	if (AccumSec < Config.FlushIntervalSec)
	{
		return true;
	}
	AccumSec = 0.f;
	PumpPartialOnGameThread();
	return true;
}
