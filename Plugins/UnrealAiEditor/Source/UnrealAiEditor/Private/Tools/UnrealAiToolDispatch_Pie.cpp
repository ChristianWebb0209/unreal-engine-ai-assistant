#include "Tools/UnrealAiToolDispatch_Pie.h"

#include "Tools/UnrealAiToolJson.h"

#include "Editor/EditorEngine.h"
#include "Editor.h"
#include "UnrealEdGlobals.h"

FUnrealAiToolInvocationResult UnrealAiDispatch_PieStart(const TSharedPtr<FJsonObject>& Args)
{
	(void)Args;
	if (!GEditor)
	{
		return UnrealAiToolJson::Error(TEXT("GEditor not available"));
	}
	if (GEditor->IsPlayingSessionInEditor())
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), true);
		O->SetStringField(TEXT("note"), TEXT("PIE already running"));
		return UnrealAiToolJson::Ok(O);
	}
	GEditor->RequestPlaySession(FRequestPlaySessionParams());
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("note"), TEXT("PIE session requested"));
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
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetBoolField(TEXT("playing_in_editor"), bPlaying);
	return UnrealAiToolJson::Ok(O);
}
