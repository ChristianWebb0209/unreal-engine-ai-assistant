#include "Tools/UnrealAiToolDispatch_EditorUi.h"

#include "Tools/UnrealAiToolActorLookup.h"
#include "Tools/UnrealAiToolJson.h"

#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "Selection.h"
#include "UnrealEdGlobals.h"

FUnrealAiToolInvocationResult UnrealAiDispatch_EditorGetSelection(const TSharedPtr<FJsonObject>& Args)
{
	(void)Args;
	if (!GEditor)
	{
		return UnrealAiToolJson::Error(TEXT("GEditor not available"));
	}
	USelection* Sel = GEditor->GetSelectedActors();
	if (!Sel)
	{
		return UnrealAiToolJson::Error(TEXT("No selection object"));
	}
	TArray<TSharedPtr<FJsonValue>> Paths;
	TArray<TSharedPtr<FJsonValue>> Labels;
	for (FSelectionIterator It(*Sel); It; ++It)
	{
		if (AActor* A = Cast<AActor>(*It))
		{
			Paths.Add(MakeShareable(new FJsonValueString(A->GetPathName())));
			Labels.Add(MakeShareable(new FJsonValueString(A->GetActorLabel())));
		}
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetArrayField(TEXT("actor_paths"), Paths);
	O->SetArrayField(TEXT("labels"), Labels);
	O->SetNumberField(TEXT("count"), static_cast<double>(Paths.Num()));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_EditorSetSelection(const TSharedPtr<FJsonObject>& Args)
{
	const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
	if (!Args->TryGetArrayField(TEXT("actor_paths"), Paths) || !Paths)
	{
		return UnrealAiToolJson::Error(TEXT("actor_paths array is required"));
	}
	if (!GEditor)
	{
		return UnrealAiToolJson::Error(TEXT("GEditor not available"));
	}
	bool bAdditive = false;
	Args->TryGetBoolField(TEXT("additive"), bAdditive);
	UWorld* World = UnrealAiGetEditorWorld();
	if (!bAdditive)
	{
		GEditor->SelectNone(false, false);
	}
	int32 Selected = 0;
	for (const TSharedPtr<FJsonValue>& V : *Paths)
	{
		FString P;
		if (!V.IsValid() || !V->TryGetString(P))
		{
			continue;
		}
		if (AActor* A = UnrealAiFindActorByPath(World, P))
		{
			GEditor->SelectActor(A, true, true);
			++Selected;
		}
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetNumberField(TEXT("selected_count"), static_cast<double>(Selected));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_EditorSetMode(const TSharedPtr<FJsonObject>& Args)
{
	FString ModeId;
	if (!Args->TryGetStringField(TEXT("mode_id"), ModeId) || ModeId.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("mode_id is required"));
	}
	const FString M = ModeId.ToLower();
	FEditorModeID Mode = FBuiltinEditorModes::EM_Default;
	if (M.Contains(TEXT("landscape")))
	{
		Mode = FBuiltinEditorModes::EM_Landscape;
	}
	else if (M.Contains(TEXT("foliage")))
	{
		Mode = FBuiltinEditorModes::EM_Foliage;
	}
	else if (M.Contains(TEXT("place")) || M.Contains(TEXT("default")))
	{
		Mode = FBuiltinEditorModes::EM_Default;
	}
	GLevelEditorModeTools().ActivateMode(Mode);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("activated_mode_id"), ModeId);
	return UnrealAiToolJson::Ok(O);
}
