#include "Harness/UnrealAiHarnessScenarioRunner.h"

#include "Async/TaskGraphInterfaces.h"
#include "Dom/JsonObject.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/IAgentContextService.h"
#include "Context/UnrealAiContextMentionParser.h"
#include "Context/UnrealAiProjectId.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Harness/FAgentRunFileSink.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Planning/FUnrealAiPlanExecutor.h"
#include "Harness/IUnrealAiAgentHarness.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/UnrealAiHarnessProgressTelemetry.h"
#include "Misc/UnrealAiRuntimeDefaults.h"
#include "Misc/UnrealAiWaitTimePolicy.h"
#include "Misc/Paths.h"
#include "UnrealAiEditorModule.h"

namespace UnrealAiHarnessScenarioRunnerPriv
{
	static uint32 GetEffectiveHarnessSyncIdleAbortMs(IUnrealAiAgentHarness* Harness, const bool bScenarioSyncWait)
	{
		uint32 IdleMs = UnrealAiWaitTime::HarnessSyncIdleAbortMs;
		if (bScenarioSyncWait && Harness && Harness->IsPlanPipelineActive())
		{
			const uint32 PlanPipelineMs = UnrealAiWaitTime::HarnessPlanPipelineSyncIdleAbortMs;
			if (PlanPipelineMs > 0)
			{
				IdleMs = PlanPipelineMs;
			}
		}
		return IdleMs;
	}

	/** Early exit when HTTP is done, stream telemetry is idle, and harness is not doing tool work. */
	static bool TryHarnessIdleAbort(IUnrealAiAgentHarness* Harness, const bool bScenarioSyncWait)
	{
		const uint32 IdleMs = GetEffectiveHarnessSyncIdleAbortMs(Harness, bScenarioSyncWait);
		if (IdleMs == 0 || !Harness)
		{
			UnrealAiHarnessProgressTelemetry::SetIdleAbortSkipReason(IdleMs == 0 ? TEXT("idle_abort_disabled") : TEXT("no_harness"));
			return false;
		}
		// Headed sync wait: do not block idle abort when both turn and plan report inactive — that is the
		// "DoneEvent never signaled" dead state (see headed plan-mode smoke runs). UI/chat does not use this helper.
		if (!bScenarioSyncWait && !Harness->IsTurnInProgress() && !Harness->IsPlanPipelineActive())
		{
			UnrealAiHarnessProgressTelemetry::SetIdleAbortSkipReason(TEXT("turn_not_in_progress"));
			return false;
		}
		// Non-scenario: never idle-abort while the transport still considers a request in flight (UI/chat).
		// Headed scenario: a wedged connection can keep ActiveRequest true for the full HTTP timeout (~4 min) with
		// no tokens — fall through and let telemetry (assistant + LLM-submit idle) decide.
		if (Harness->HasActiveLlmTransportRequest() && !bScenarioSyncWait)
		{
			UnrealAiHarnessProgressTelemetry::SetIdleAbortSkipReason(TEXT("active_llm_transport"));
			return false;
		}
		if (Harness->ShouldSuppressIdleAbort())
		{
			UnrealAiHarnessProgressTelemetry::SetIdleAbortSkipReason(TEXT("suppress_idle_abort"));
			return false;
		}
		// Headed scenario: do not idle-abort while a request is in flight and no assistant delta has
		// arrived yet (time-to-first-token can exceed HarnessSyncIdleAbortMs). Post-first-token stall
		// uses normal idle thresholds. UI path still uses active_llm_transport skip above.
		if (bScenarioSyncWait && Harness->HasActiveLlmTransportRequest())
		{
			double AsstPre = -1.0;
			double HttpPre = -1.0;
			double LlmPre = -1.0;
			UnrealAiHarnessProgressTelemetry::GetStreamIdleSeconds(AsstPre, HttpPre, LlmPre);
			if (AsstPre < 0.0)
			{
				UnrealAiHarnessProgressTelemetry::SetIdleAbortSkipReason(TEXT("awaiting_first_assistant_delta"));
				return false;
			}
		}
		double AsstIdle = -1.0;
		double HttpIdle = -1.0;
		double LlmIdle = -1.0;
		UnrealAiHarnessProgressTelemetry::GetStreamIdleSeconds(AsstIdle, HttpIdle, LlmIdle);
		const double ThreshSec = static_cast<double>(IdleMs) / 1000.0;
		const bool bHttpTelemetryValid = (HttpIdle >= 0.0);
		if (bHttpTelemetryValid)
		{
			if (HttpIdle < ThreshSec)
			{
				UnrealAiHarnessProgressTelemetry::SetIdleAbortSkipReason(TEXT("http_not_idle_enough"));
				return false;
			}
		}
		else
		{
			UnrealAiHarnessProgressTelemetry::SetIdleAbortSkipReason(TEXT("http_idle_unset_assistant_only"));
		}
		const bool bAssistantStale = (AsstIdle >= ThreshSec) || (AsstIdle < 0.0);
		if (!bAssistantStale)
		{
			UnrealAiHarnessProgressTelemetry::SetIdleAbortSkipReason(TEXT("assistant_not_idle_enough"));
			return false;
		}
		if (LlmIdle >= 0.0 && LlmIdle < ThreshSec)
		{
			UnrealAiHarnessProgressTelemetry::SetIdleAbortSkipReason(TEXT("llm_submit_recent"));
			return false;
		}
		UnrealAiHarnessProgressTelemetry::SetIdleAbortSkipReason(TEXT(""));
		return true;
	}

	static FString AgentModeToLabel(const EUnrealAiAgentMode Mode)
	{
		switch (Mode)
		{
		case EUnrealAiAgentMode::Ask:
			return TEXT("ask");
		case EUnrealAiAgentMode::Agent:
			return TEXT("agent");
		case EUnrealAiAgentMode::Plan:
			return TEXT("plan");
		default:
			return TEXT("unknown");
		}
	}

	/**
	 * Cannot use a blocking DoneEvent->Wait on the game thread: UE HTTP and harness stream handlers
	 * dispatch completion work to the game thread — while waiting, nothing pumps the queue → apparent hang.
	 */
	static bool WaitForHarnessEventWhilePumpingGameThread(
		FEvent* Event,
		uint32 WaitMs,
		IUnrealAiAgentHarness* HarnessForIdle = nullptr,
		bool* bOutIdleAbort = nullptr)
	{
		if (!Event)
		{
			return false;
		}
		if (bOutIdleAbort)
		{
			*bOutIdleAbort = false;
		}
		const double DeadlineSec = FPlatformTime::Seconds() + static_cast<double>(WaitMs) / 1000.0;
		while (FPlatformTime::Seconds() < DeadlineSec)
		{
			if (Event->Wait(0u))
			{
				return true;
			}
			if (HarnessForIdle && bOutIdleAbort && TryHarnessIdleAbort(HarnessForIdle, true))
			{
				*bOutIdleAbort = true;
				return false;
			}
			FHttpModule::Get().GetHttpManager().Tick(0.f);
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			FPlatformProcess::SleepNoStats(0.001f);
		}
		return Event->Wait(0u);
	}

	enum class EPlanHarnessSyncSegment : uint8
	{
		Done,
		SubTurn,
		Timeout,
		IdleAbort
	};

	/** Prefer Done over SubTurn when both could be set in the same pump. */
	static EPlanHarnessSyncSegment WaitForDoneOrPlanSubTurnWhilePumpingGameThread(
		FEvent* DoneEvent,
		FEvent* SubTurnEvent,
		uint32 WaitMs,
		IUnrealAiAgentHarness* HarnessForIdle,
		bool* bOutIdleAbort)
	{
		if (bOutIdleAbort)
		{
			*bOutIdleAbort = false;
		}
		const double DeadlineSec = FPlatformTime::Seconds() + static_cast<double>(WaitMs) / 1000.0;
		while (FPlatformTime::Seconds() < DeadlineSec)
		{
			if (DoneEvent && DoneEvent->Wait(0u))
			{
				return EPlanHarnessSyncSegment::Done;
			}
			if (SubTurnEvent && SubTurnEvent->Wait(0u))
			{
				return EPlanHarnessSyncSegment::SubTurn;
			}
			if (HarnessForIdle && bOutIdleAbort && TryHarnessIdleAbort(HarnessForIdle, true))
			{
				*bOutIdleAbort = true;
				return EPlanHarnessSyncSegment::IdleAbort;
			}
			FHttpModule::Get().GetHttpManager().Tick(0.f);
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			FPlatformProcess::SleepNoStats(0.001f);
		}
		if (DoneEvent && DoneEvent->Wait(0u))
		{
			return EPlanHarnessSyncSegment::Done;
		}
		if (SubTurnEvent && SubTurnEvent->Wait(0u))
		{
			return EPlanHarnessSyncSegment::SubTurn;
		}
		if (HarnessForIdle && bOutIdleAbort && TryHarnessIdleAbort(HarnessForIdle, true))
		{
			*bOutIdleAbort = true;
			return EPlanHarnessSyncSegment::IdleAbort;
		}
		return EPlanHarnessSyncSegment::Timeout;
	}

	static bool RunAgentTurnSync_GameThread(
		const FString& UserMessage,
		const FString& ThreadIdDigitsWithHyphens,
		const EUnrealAiAgentMode Mode,
		const FString& OutputRootDir,
		FString& OutJsonlPath,
		FString& OutRunDir,
		bool& bOutSuccess,
		FString& OutError,
		const bool bDumpContextAfterEachTool)
	{
		const TSharedPtr<FUnrealAiBackendRegistry> Reg = FUnrealAiEditorModule::GetBackendRegistry();
		if (!Reg.IsValid())
		{
			OutError = TEXT("Backend registry not available");
			return false;
		}
		IUnrealAiAgentHarness* Harness = Reg->GetAgentHarness();
		IAgentContextService* Ctx = Reg->GetContextService();
		FUnrealAiModelProfileRegistry* Profiles = Reg->GetModelProfileRegistry();
		if (!Harness || !Ctx || !Profiles)
		{
			OutError = TEXT("Harness or context not available");
			return false;
		}

		const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
		FString RunDir = OutputRootDir;
		if (RunDir.IsEmpty())
		{
			RunDir = FPaths::Combine(
				FPaths::ProjectSavedDir(),
				TEXT("UnrealAiEditor"),
				TEXT("HarnessRuns"),
				FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")));
		}
		IFileManager::Get().MakeDirectory(*RunDir, true);
		OutRunDir = RunDir;
		OutJsonlPath = FPaths::Combine(RunDir, TEXT("run.jsonl"));
		FFileHelper::SaveStringToFile(FString(), *OutJsonlPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

		FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
		FEvent* PlanSubTurnEvent = nullptr;
		if (Mode == EUnrealAiAgentMode::Plan)
		{
			PlanSubTurnEvent = FPlatformProcess::GetSynchEventFromPool(true);
		}
		bool bFinishSuccess = false;
		FString FinishErr;

		Ctx->LoadOrCreate(ProjectId, ThreadIdDigitsWithHyphens);
		UnrealAiContextMentionParser::ApplyMentionsFromPrompt(Ctx, ProjectId, ThreadIdDigitsWithHyphens, UserMessage);
		Ctx->RefreshEditorSnapshotFromEngine();

		FUnrealAiAgentTurnRequest Req;
		Req.ProjectId = ProjectId;
		Req.ThreadId = ThreadIdDigitsWithHyphens;
		Req.Mode = Mode;
		Req.UserText = UserMessage;
		Req.ModelProfileId = Profiles->GetDefaultModelId();
		Req.bRecordAssistantAsStubToolResult = false;

		const TSharedPtr<FAgentRunFileSink> Sink = MakeShared<FAgentRunFileSink>(
			OutJsonlPath,
			Ctx,
			ProjectId,
			ThreadIdDigitsWithHyphens,
			bDumpContextAfterEachTool,
			true,
			DoneEvent,
			&bFinishSuccess,
			&FinishErr,
			PlanSubTurnEvent);

		// Bounded wait; must exceed typical HTTP streaming latency plus at least one tool round.
		// Plan mode: optional total wall in FUnrealAiPlanExecutor (UnrealAiWaitTime::HarnessPlanMaxWallMs) — separate from per-segment sync.
		const uint32 WaitMs = UnrealAiWaitTime::HarnessSyncWaitMs;
		UE_LOG(LogTemp, Display,
			TEXT("UnrealAi harness: starting agent turn (game thread wait pumps HTTP + GT queue; viewport may look idle). "
				 "Tail jsonl or use Output Log: %s | sync wait up to %u s per segment%s."),
			*OutJsonlPath,
			WaitMs / 1000,
			Mode == EUnrealAiAgentMode::Plan ? TEXT(" (Plan mode: per planner + per node)") : TEXT(""));

		// Must outlive the entire sync wait: FCollectingSink calls WeakExec.Pin() -> OnPlannerFinished -> Finish() ->
		// FAgentRunFileSink::OnRunFinished (DoneEvent). If the executor is destroyed when the Plan { } block ends, Pin()
		// is null, the parent sink never finishes, and the runner hits the forced-timeout path even after idle abort.
		TSharedPtr<FUnrealAiPlanExecutor> PlanExecutorLifetime;
		if (Mode == EUnrealAiAgentMode::Plan)
		{
			// Same pipeline as SChatComposer Plan mode: planner DAG pass + per-node Agent runs.
			FUnrealAiPlanExecutorStartOptions PlanOpts;
			if (FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_HEADED_PLAN_HARNESS_PLANNER_ONLY")).TrimStartAndEnd() == TEXT("1"))
			{
				PlanOpts.bHarnessPlannerOnlyNoExecute = true;
			}
			PlanExecutorLifetime = FUnrealAiPlanExecutor::Start(Harness, Ctx, Req, Sink, PlanOpts);
		}
		else
		{
			Harness->RunTurn(Req, Sink);
		}

		bool bSignaled = false;
		bool bIdleAbort = false;
		int32 PlanSubTurnCompletionsBeforeTimeout = 0;
		// Plan: fresh HarnessSyncWaitMs per segment; PlanSubTurnEvent resets when FUnrealAiPlanExecutor calls OnPlanHarnessSubTurnComplete (after planner parse and each node).
		if (Mode == EUnrealAiAgentMode::Plan && PlanSubTurnEvent)
		{
			for (;;)
			{
				const EPlanHarnessSyncSegment Seg = WaitForDoneOrPlanSubTurnWhilePumpingGameThread(
					DoneEvent,
					PlanSubTurnEvent,
					WaitMs,
					Harness,
					&bIdleAbort);
				if (Seg == EPlanHarnessSyncSegment::Done)
				{
					bSignaled = true;
					break;
				}
				if (Seg == EPlanHarnessSyncSegment::Timeout || Seg == EPlanHarnessSyncSegment::IdleAbort)
				{
					break;
				}
				++PlanSubTurnCompletionsBeforeTimeout;
				PlanSubTurnEvent->Reset();
				UE_LOG(
					LogTemp,
					Display,
					TEXT("UnrealAi harness: plan sub-turn completed; sync wait window reset (%u s)."),
					WaitMs / 1000);
			}
		}
		else
		{
			bSignaled = WaitForHarnessEventWhilePumpingGameThread(DoneEvent, WaitMs, Harness, &bIdleAbort);
		}

		if (!bSignaled)
		{
			const FString ModeLabel = AgentModeToLabel(Mode);
			if (bIdleAbort)
			{
				const uint32 IdleAbortMs = UnrealAiHarnessScenarioRunnerPriv::GetEffectiveHarnessSyncIdleAbortMs(Harness, true);
				const TSharedPtr<FJsonObject> IdleDiag = UnrealAiHarnessProgressTelemetry::BuildHarnessSyncIdleAbortDiagnosticJson(
					ModeLabel,
					IdleAbortMs,
					Mode == EUnrealAiAgentMode::Plan,
					Harness->HasActiveLlmTransportRequest(),
					Harness->ShouldSuppressIdleAbort());
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("UnrealAi harness: sync_idle_abort (idle_abort_ms=%u); issuing CancelTurn(). %s"),
					IdleAbortMs,
					*UnrealAiHarnessProgressTelemetry::FormatHarnessSyncIdleAbortLogLine(
						ModeLabel,
						IdleAbortMs,
						Mode == EUnrealAiAgentMode::Plan,
						Harness->HasActiveLlmTransportRequest(),
						Harness->ShouldSuppressIdleAbort()));
				if (Sink.IsValid() && IdleDiag.IsValid())
				{
					Sink->AppendHarnessDiagnosticJson(IdleDiag);
				}
				Harness->FailInProgressTurnForScenarioIdleAbort();
			}
			else
			{
				const TSharedPtr<FJsonObject> TimeoutDiag = UnrealAiHarnessProgressTelemetry::BuildHarnessSyncTimeoutDiagnosticJson(
					ModeLabel,
					WaitMs,
					Mode == EUnrealAiAgentMode::Plan ? PlanSubTurnCompletionsBeforeTimeout : 0,
					Mode == EUnrealAiAgentMode::Plan,
					UnrealAiHarnessProgressTelemetry::GetIdleAbortSkipReason());
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("UnrealAi harness: sync_wait_timeout after %u ms; issuing CancelTurn(). %s"),
					WaitMs,
					*UnrealAiHarnessProgressTelemetry::FormatHarnessSyncTimeoutLogLine(
						ModeLabel,
						WaitMs,
						Mode == EUnrealAiAgentMode::Plan ? PlanSubTurnCompletionsBeforeTimeout : 0,
						Mode == EUnrealAiAgentMode::Plan));
				if (Sink.IsValid() && TimeoutDiag.IsValid())
				{
					Sink->AppendHarnessDiagnosticJson(TimeoutDiag);
				}
			}
			Harness->CancelTurn();
			const uint32 CancelDrainWaitMs = UnrealAiWaitTime::HarnessCancelDrainWaitMs;
			const bool bSignaledAfterCancel = WaitForHarnessEventWhilePumpingGameThread(DoneEvent, CancelDrainWaitMs);
			UE_LOG(
				LogTemp,
				Display,
				TEXT("UnrealAi harness: runner_terminal_set after cancel=%s drain_wait_ms=%u."),
				bSignaledAfterCancel ? TEXT("yes") : TEXT("no"),
				CancelDrainWaitMs);
			if (bSignaledAfterCancel)
			{
				UE_LOG(
					LogTemp,
					Display,
					TEXT("UnrealAi harness: sink_finish_received success=%s err=%s"),
					bFinishSuccess ? TEXT("yes") : TEXT("no"),
					FinishErr.IsEmpty() ? TEXT("<none>") : *FinishErr);
			}
			else if (Sink.IsValid())
			{
				const FString ForcedErr = TEXT("Harness run timed out waiting for completion (forced terminal after cancel)");
				Sink->OnRunFinished(false, ForcedErr);
				UE_LOG(LogTemp, Warning, TEXT("UnrealAi harness: forced terminal failure emitted from runner timeout path."));
			}
			OutError = bIdleAbort ? TEXT("Harness run aborted: sync idle stall (post-stream)")
								  : TEXT("Harness run timed out waiting for completion");
			bOutSuccess = false;
			FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
			if (PlanSubTurnEvent)
			{
				FPlatformProcess::ReturnSynchEventToPool(PlanSubTurnEvent);
			}
			return false;
		}

		bOutSuccess = bFinishSuccess;
		OutError = FinishErr;
		UE_LOG(
			LogTemp,
			Display,
			TEXT("UnrealAi harness: sink_finish_received success=%s err=%s"),
			bOutSuccess ? TEXT("yes") : TEXT("no"),
			OutError.IsEmpty() ? TEXT("<none>") : *OutError);
		FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
		if (PlanSubTurnEvent)
		{
			FPlatformProcess::ReturnSynchEventToPool(PlanSubTurnEvent);
		}
		return true;
	}
}

bool UnrealAiHarnessScenarioRunner::RunAgentTurnSync(
	const FString& UserMessage,
	const FString& ThreadIdDigitsWithHyphens,
	const EUnrealAiAgentMode Mode,
	const FString& OutputRootDir,
	FString& OutJsonlPath,
	FString& OutRunDir,
	bool& bOutSuccess,
	FString& OutError,
	const bool bDumpContextAfterEachTool)
{
	if (!IsInGameThread())
	{
		OutError = TEXT("RunAgentTurnSync must run on the game thread (editor console or automation).");
		bOutSuccess = false;
		return false;
	}
	return UnrealAiHarnessScenarioRunnerPriv::RunAgentTurnSync_GameThread(
		UserMessage,
		ThreadIdDigitsWithHyphens,
		Mode,
		OutputRootDir,
		OutJsonlPath,
		OutRunDir,
		bOutSuccess,
		OutError,
		bDumpContextAfterEachTool);
}
