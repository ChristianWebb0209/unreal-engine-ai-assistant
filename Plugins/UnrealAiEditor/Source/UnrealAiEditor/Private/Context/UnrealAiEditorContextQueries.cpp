#include "Context/UnrealAiEditorContextQueries.h"

#include "Context/AgentContextTypes.h"
#include "Context/IAgentContextService.h"

#if WITH_EDITOR
#include "ContentBrowserItemPath.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "IContentBrowserSingleton.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/SoftObjectPath.h"
#endif

void UnrealAiEditorContextQueries::PopulateContentBrowserAndSelection(FEditorContextSnapshot& Snap)
{
#if WITH_EDITOR
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("ContentBrowser")))
	{
		return;
	}
	FContentBrowserModule& CBM = FModuleManager::GetModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	IContentBrowserSingleton& CBS = CBM.Get();

	// Current browsed path in the active Content Browser (when valid).
	{
		const FContentBrowserItemPath ItemPath = CBS.GetCurrentPath();
		const FString PathStr = ItemPath.GetInternalPathString();
		if (!PathStr.IsEmpty())
		{
			Snap.ContentBrowserPath = PathStr;
		}
	}

	TArray<FAssetData> SelectedAssets;
	CBS.GetSelectedAssets(SelectedAssets);
	Snap.ContentBrowserSelectedAssets.Reset();
	const int32 Cap = FMath::Min(SelectedAssets.Num(), MaxContentBrowserSelectedAssets);
	for (int32 i = 0; i < Cap; ++i)
	{
		const FString ObjPath = SelectedAssets[i].GetObjectPathString();
		Snap.ContentBrowserSelectedAssets.Add(ObjPath);
	}
	if (Snap.ContentBrowserSelectedAssets.Num() > 0)
	{
		Snap.ActiveAssetPath = Snap.ContentBrowserSelectedAssets[0];
	}
#endif
}

void UnrealAiEditorContextQueries::PopulateOpenEditorAssets(FEditorContextSnapshot& Snap)
{
#if WITH_EDITOR
	if (!GEditor)
	{
		return;
	}
	UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AES)
	{
		return;
	}
	Snap.OpenEditorAssets.Reset();
	const TArray<UObject*> Edited = AES->GetAllEditedAssets();
	TSet<FString> Seen;
	const int32 Cap = FMath::Min(Edited.Num(), MaxOpenEditorAssets);
	for (int32 i = 0; i < Edited.Num() && Snap.OpenEditorAssets.Num() < Cap; ++i)
	{
		UObject* Obj = Edited[i];
		if (!Obj)
		{
			continue;
		}
		const FString Path = FSoftObjectPath(Obj).ToString();
		if (Path.IsEmpty() || Seen.Contains(Path))
		{
			continue;
		}
		Seen.Add(Path);
		Snap.OpenEditorAssets.Add(Path);
	}
#endif
}

void UnrealAiEditorContextQueries::AddContentBrowserSelectionAsAttachments(IAgentContextService* Ctx)
{
#if WITH_EDITOR
	if (!Ctx || !FModuleManager::Get().IsModuleLoaded(TEXT("ContentBrowser")))
	{
		return;
	}
	FContentBrowserModule& CBM = FModuleManager::GetModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FAssetData> Selected;
	CBM.Get().GetSelectedAssets(Selected);
	const int32 Cap = FMath::Min(Selected.Num(), MaxContentBrowserSelectedAssets);
	for (int32 i = 0; i < Cap; ++i)
	{
		FContextAttachment A;
		A.Type = EContextAttachmentType::AssetPath;
		A.Payload = Selected[i].GetObjectPathString();
		A.Label = Selected[i].AssetName.ToString();
		Ctx->AddAttachment(A);
	}
#endif
}
