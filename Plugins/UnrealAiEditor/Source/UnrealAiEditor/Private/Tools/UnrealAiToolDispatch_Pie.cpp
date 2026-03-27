#include "Tools/UnrealAiToolDispatch_Pie.h"

#include "Tools/UnrealAiToolJson.h"

#include "Editor/EditorEngine.h"
#include "Editor.h"
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
	GEditor->RequestEndPlayMap();
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
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
