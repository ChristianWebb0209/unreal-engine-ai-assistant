#include "Tools/UnrealAiToolDispatch_Pie.h"

#include "Tools/UnrealAiToolJson.h"

#include "Async/TaskGraphInterfaces.h"
#include "Editor/EditorEngine.h"
#include "Editor.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

FUnrealAiToolInvocationResult UnrealAiDispatch_PieStart(const TSharedPtr<FJsonObject>& Args)
{
	if (!GEditor)
	{
		return UnrealAiToolJson::Error(TEXT("GEditor not available"));
	}
	if (GEditor->IsPlayingSessionInEditor())
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("note"), TEXT("PIE already running"));
		O->SetBoolField(TEXT("playing_in_editor"), true);
		return UnrealAiToolJson::Ok(O);
	}
	FRequestPlaySessionParams Params;
	Params.SessionDestination = EPlaySessionDestinationType::InProcess;
	Params.WorldType = EPlaySessionWorldType::PlayInEditor;
	if (Args.IsValid())
	{
		FString Mode;
		if (Args->TryGetStringField(TEXT("mode"), Mode) && Mode.Equals(TEXT("standalone"), ESearchCase::IgnoreCase))
		{
			Params.SessionDestination = EPlaySessionDestinationType::NewProcess;
		}
	}
	GEditor->RequestPlaySession(Params);
	// RequestPlaySession only queues; the editor normally starts on the next tick.
	// In headless/automation-style runs that can stall, immediately start queued request
	// (same pattern as Engine automation tests).
	GEditor->StartQueuedPlaySessionRequest();
	// Best-effort wait for PIE session info to become active (bounded).
	{
		const double Deadline = FPlatformTime::Seconds() + 20.0;
		while (!GEditor->IsPlayingSessionInEditor() && FPlatformTime::Seconds() < Deadline)
		{
			FPlatformProcess::Sleep(0.02f);
		}
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetBoolField(TEXT("playing_in_editor"), GEditor->IsPlayingSessionInEditor());
	O->SetBoolField(TEXT("play_session_in_progress"), GEditor->IsPlaySessionInProgress());
	O->SetStringField(TEXT("note"), TEXT("PIE session start attempted"));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_PieStop(const TSharedPtr<FJsonObject>& Args)
{
	(void)Args;
	if (!GEditor)
	{
		return UnrealAiToolJson::Error(TEXT("GEditor not available"));
	}
	if (!GEditor->IsPlayingSessionInEditor())
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("note"), TEXT("PIE not running"));
		return UnrealAiToolJson::Ok(O);
	}
	// RequestEndPlayMap only queues end-play; we need to pump the game thread so the teardown runs
	// before we return, otherwise immediate pie_status checks can observe stale "still playing" state.
	GEditor->RequestEndPlayMap();
	{
		// EndPlayMap itself is executed from UEditorEngine::Tick when the queued flag is observed.
		// We cannot safely rely on harness pumping here, so tick the editor a small number of times.
		// Keep it bounded to avoid deadlocks/hangs.
		const double Deadline = FPlatformTime::Seconds() + 10.0;
		for (int32 i = 0; i < 50 && FPlatformTime::Seconds() < Deadline; ++i)
		{
			const bool bAllStopped = !GEditor->IsPlayingSessionInEditor()
				&& !GEditor->IsPlaySessionInProgress()
				&& !GEditor->IsPlaySessionRequestQueued();
			if (bAllStopped)
			{
				break;
			}

			FHttpModule::Get().GetHttpManager().Tick(0.f);
			// bIdleMode=true reduces expensive/editor-idle side effects vs idleMode=false.
			GEditor->Tick(0.f, true /*bIdleMode*/);
			FPlatformProcess::SleepNoStats(0.001f);
		}
	}
	const bool bPlaying = GEditor->IsPlayingSessionInEditor();
	const bool bInProgress = GEditor->IsPlaySessionInProgress();
	const bool bQueued = GEditor->IsPlaySessionRequestQueued();
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetBoolField(TEXT("playing_in_editor"), bPlaying);
	O->SetBoolField(TEXT("play_session_in_progress"), bInProgress);
	O->SetBoolField(TEXT("play_session_request_queued"), bQueued);
	O->SetStringField(TEXT("note"), TEXT("PIE stop requested; pumped teardown up to 15s."));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_PieStatus(const TSharedPtr<FJsonObject>& Args)
{
	(void)Args;
	const bool bPlaying = GEditor && GEditor->IsPlayingSessionInEditor();
	const bool bQueued = GEditor && GEditor->IsPlaySessionRequestQueued();
	const bool bInProgress = GEditor && GEditor->IsPlaySessionInProgress();
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetBoolField(TEXT("playing_in_editor"), bPlaying);
	O->SetBoolField(TEXT("play_session_request_queued"), bQueued);
	O->SetBoolField(TEXT("play_session_in_progress"), bInProgress);
	return UnrealAiToolJson::Ok(O);
}
