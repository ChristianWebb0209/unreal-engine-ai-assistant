#include "Tools/UnrealAiToolDispatch_EditorUi.h"

#include "Tools/UnrealAiToolActorLookup.h"
#include "Tools/UnrealAiToolDispatch_ArgRepair.h"
#include "Tools/UnrealAiToolJson.h"

#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "Selection.h"
#include "UnrealEdGlobals.h"

namespace UnrealAiEditorSetSelectionInternal
{
	static bool IsLikelyLevelActorPath(const FString& Path)
	{
		return Path.Contains(TEXT("PersistentLevel")) || Path.Contains(TEXT(":PersistentLevel."));
	}

	/** Content object paths under /Game or /Engine (not level actor paths). */
	static bool IsLikelyContentAssetPath(const FString& Path)
	{
		const bool bContentRoot = Path.StartsWith(TEXT("/Game/")) || Path.StartsWith(TEXT("/Engine/"));
		if (!bContentRoot)
		{
			return false;
		}
		if (IsLikelyLevelActorPath(Path))
		{
			return false;
		}
		return Path.Contains(TEXT("."));
	}

	static FUnrealAiToolInvocationResult ErrorContentAssetVersusSelection(const FString& FirstContentPath)
	{
		const FString Msg = TEXT(
			"editor_set_selection: path(s) look like content asset object paths (/Game/.../Name.Name), not level actor paths. "
			"Use content_browser_sync_asset or asset_open_editor to focus assets; use asset_index_fuzzy_search to discover /Game paths. "
			"editor_set_selection only selects actors in the loaded level (paths containing PersistentLevel).");
		FUnrealAiToolInvocationResult R;
		R.bOk = false;
		R.ErrorMessage = Msg;
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("error"), Msg);
		O->SetBoolField(TEXT("ok"), false);
		O->SetStringField(TEXT("id_kind"), TEXT("content_asset"));
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("path"), FirstContentPath);
		TSharedPtr<FJsonObject> Suggested = MakeShared<FJsonObject>();
		Suggested->SetStringField(TEXT("tool_id"), TEXT("content_browser_sync_asset"));
		Suggested->SetObjectField(TEXT("arguments"), SuggestedArgs);
		O->SetObjectField(TEXT("suggested_correct_call"), Suggested);
		R.ContentForModel = UnrealAiToolJson::SerializeObject(O);
		return R;
	}
}

using namespace UnrealAiEditorSetSelectionInternal;

FUnrealAiToolInvocationResult UnrealAiDispatch_EditorGetSelection(const TSharedPtr<FJsonObject>& Args)
{
	(void)Args;
	if (!GEditor)
	{
		return UnrealAiToolJson::Error(TEXT("editor_get_selection: Unreal Editor API unavailable (GEditor is null)."));
	}
	USelection* Sel = GEditor->GetSelectedActors();
	if (!Sel)
	{
		return UnrealAiToolJson::Error(TEXT("editor_get_selection: actor selection object unavailable."));
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
	O->SetStringField(TEXT("tool"), TEXT("editor_get_selection"));
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
		Args->TryGetArrayField(TEXT("paths"), Paths);
	}
	TArray<TSharedPtr<FJsonValue>> NormalizedPaths;
	if (!Paths || Paths->Num() == 0)
	{
		FString SinglePath;
		if (!Args->TryGetStringField(TEXT("actor_path"), SinglePath) || SinglePath.IsEmpty())
		{
			if (!Args->TryGetStringField(TEXT("path"), SinglePath) || SinglePath.IsEmpty())
			{
				Args->TryGetStringField(TEXT("selection_path"), SinglePath);
			}
		}
		if (!SinglePath.IsEmpty())
		{
			NormalizedPaths.Add(MakeShareable(new FJsonValueString(SinglePath)));
			Paths = &NormalizedPaths;
		}
	}
	if (!Paths || Paths->Num() == 0)
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> SuggestedList;
		SuggestedList.Add(MakeShareable(new FJsonValueString(TEXT("PersistentLevel.PlayerStart"))));
		SuggestedArgs->SetArrayField(TEXT("actor_paths"), SuggestedList);
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("actor_paths array is required (aliases: paths, actor_path, path, selection_path)."),
			TEXT("editor_set_selection"),
			SuggestedArgs);
	}
	if (!GEditor)
	{
		return UnrealAiToolJson::Error(TEXT("editor_set_selection: Unreal Editor API unavailable (GEditor is null)."));
	}
	bool bAdditive = false;
	Args->TryGetBoolField(TEXT("additive"), bAdditive);
	UWorld* World = UnrealAiGetEditorWorld();
	if (!bAdditive)
	{
		GEditor->SelectNone(false, false);
	}
	int32 Selected = 0;
	int32 NonEmptyPaths = 0;
	FString FirstContentStylePath;
	for (const TSharedPtr<FJsonValue>& V : *Paths)
	{
		FString P;
		if (!V.IsValid() || !V->TryGetString(P))
		{
			continue;
		}
		UnrealAiToolDispatchArgRepair::SanitizeUnrealPathString(P);
		if (P.IsEmpty())
		{
			continue;
		}
		++NonEmptyPaths;
		if (FirstContentStylePath.IsEmpty() && IsLikelyContentAssetPath(P))
		{
			FirstContentStylePath = P;
		}
		if (AActor* A = UnrealAiResolveActorInWorld(World, P))
		{
			GEditor->SelectActor(A, true, true);
			++Selected;
		}
	}
	if (NonEmptyPaths > 0 && Selected == 0)
	{
		if (!FirstContentStylePath.IsEmpty())
		{
			return ErrorContentAssetVersusSelection(FirstContentStylePath);
		}
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("query"), TEXT("PlayerStart"));
		SuggestedArgs->SetNumberField(TEXT("max_results"), 20.0);
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("editor_set_selection: could not resolve any actor from the given path(s) in the loaded level. "
				 "Use world/level actor paths (e.g. ...PersistentLevel.ActorName). Try scene_fuzzy_search to discover actor paths."),
			TEXT("scene_fuzzy_search"),
			SuggestedArgs);
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
