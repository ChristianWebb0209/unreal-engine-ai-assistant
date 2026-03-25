#include "Harness/UnrealAiHarnessScenarioRunner.h"

#include "Async/TaskGraphInterfaces.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/IAgentContextService.h"
#include "Context/UnrealAiContextMentionParser.h"
#include "Context/UnrealAiProjectId.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Harness/FAgentRunFileSink.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Harness/IUnrealAiAgentHarness.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "UnrealAiEditorModule.h"

namespace UnrealAiHarnessScenarioRunnerPriv
{
	/**
	 * Cannot use a blocking DoneEvent->Wait on the game thread: UE HTTP and harness stream handlers
	 * dispatch completion work to the game thread — while waiting, nothing pumps the queue → apparent hang.
	 */
	static bool WaitForHarnessEventWhilePumpingGameThread(FEvent* Event, uint32 WaitMs)
	{
		if (!Event)
		{
			return false;
		}
		const double DeadlineSec = FPlatformTime::Seconds() + static_cast<double>(WaitMs) / 1000.0;
		while (FPlatformTime::Seconds() < DeadlineSec)
		{
			if (Event->Wait(0u))
			{
				return true;
			}
			FHttpModule::Get().GetHttpManager().Tick(0.f);
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			FPlatformProcess::SleepNoStats(0.001f);
		}
		return Event->Wait(0u);
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
			&FinishErr);

		// Bounded wait; must exceed typical HTTP streaming latency plus at least one tool round.
		// Override with UNREAL_AI_HARNESS_SYNC_WAIT_MS (10000-900000).
		uint32 WaitMs = 150 * 1000;
		{
			const FString WaitEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_HARNESS_SYNC_WAIT_MS"));
			if (!WaitEnv.IsEmpty())
			{
				const int32 Parsed = FCString::Atoi(*WaitEnv);
				if (Parsed >= 10000 && Parsed <= 900000)
				{
					WaitMs = static_cast<uint32>(Parsed);
				}
			}
		}
		UE_LOG(LogTemp, Display,
			TEXT("UnrealAi harness: starting agent turn (game thread wait pumps HTTP + GT queue; viewport may look idle). "
				 "Tail jsonl or use Output Log: %s | sync wait up to %u s."),
			*OutJsonlPath,
			WaitMs / 1000);

		Harness->RunTurn(Req, Sink);
		const bool bSignaled = WaitForHarnessEventWhilePumpingGameThread(DoneEvent, WaitMs);

		if (!bSignaled)
		{
			UE_LOG(LogTemp, Warning, TEXT("UnrealAi harness: sync_wait_timeout after %u ms; issuing CancelTurn()."), WaitMs);
			Harness->CancelTurn();
			constexpr uint32 CancelDrainWaitMs = 3000;
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
			OutError = TEXT("Harness run timed out waiting for completion");
			bOutSuccess = false;
			FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
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
