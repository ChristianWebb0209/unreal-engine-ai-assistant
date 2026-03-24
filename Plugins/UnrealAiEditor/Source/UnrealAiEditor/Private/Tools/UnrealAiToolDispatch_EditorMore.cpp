#include "Tools/UnrealAiToolDispatch_EditorMore.h"

#include "Tools/UnrealAiToolActorLookup.h"
#include "Tools/UnrealAiToolJson.h"

#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/World.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "GameFramework/Actor.h"
#include "Misc/OutputDeviceNull.h"
#include "ScopedTransaction.h"

FUnrealAiToolInvocationResult UnrealAiDispatch_GlobalTabFocus(const TSharedPtr<FJsonObject>& Args)
{
	FString TabId;
	if (!Args->TryGetStringField(TEXT("tab_id"), TabId) || TabId.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("tab_id is required"));
	}
	const TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->TryInvokeTab(FTabId(FName(*TabId)));
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), Tab.IsValid());
	O->SetBoolField(TEXT("invoked"), Tab.IsValid());
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_MenuCommandInvoke(const TSharedPtr<FJsonObject>& Args)
{
	FString CommandId;
	if (!Args->TryGetStringField(TEXT("command_id"), CommandId) || CommandId.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("command_id is required"));
	}
	if (!GEditor)
	{
		return UnrealAiToolJson::Error(TEXT("GEditor not available"));
	}
	UWorld* W = GEditor->GetEditorWorldContext().World();
	if (!W)
	{
		return UnrealAiToolJson::Error(TEXT("No editor world for Exec"));
	}
	FOutputDeviceNull Null;
	const bool bExec = GEditor->Exec(W, *CommandId, Null);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), bExec);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_OutlinerFolderMove(const TSharedPtr<FJsonObject>& Args)
{
	const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
	FString FolderName;
	if (!Args->TryGetArrayField(TEXT("actor_paths"), Paths) || !Paths || Paths->Num() == 0
		|| !Args->TryGetStringField(TEXT("folder_name"), FolderName) || FolderName.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("actor_paths and folder_name are required"));
	}
	UWorld* World = UnrealAiGetEditorWorld();
	if (!World)
	{
		return UnrealAiToolJson::Error(TEXT("No editor world"));
	}
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnFolder", "Unreal AI: move to folder"));
	int32 Moved = 0;
	for (const TSharedPtr<FJsonValue>& V : *Paths)
	{
		FString P;
		if (!V.IsValid() || !V->TryGetString(P) || P.IsEmpty())
		{
			continue;
		}
		if (AActor* A = UnrealAiResolveActorInWorld(World, P))
		{
			A->SetFolderPath(FName(*FolderName));
			++Moved;
		}
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), Moved > 0);
	O->SetNumberField(TEXT("moved_count"), static_cast<double>(Moved));
	return UnrealAiToolJson::Ok(O);
}
