#include "Tools/UnrealAiToolDispatch_ContentBrowserEx.h"

#include "UnrealAiEditorModule.h"
#include "Tools/UnrealAiToolJson.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

FUnrealAiToolInvocationResult UnrealAiDispatch_ContentBrowserNavigateFolder(const TSharedPtr<FJsonObject>& Args)
{
	FString FolderPath;
	if (!Args->TryGetStringField(TEXT("folder_path"), FolderPath) || FolderPath.IsEmpty())
	{
		Args->TryGetStringField(TEXT("path"), FolderPath);
	}
	if (FolderPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("folder_path is required"));
	}
	bool bUiSuppressed = false;
	if (FUnrealAiEditorModule::IsEditorFocusEnabled())
	{
		FContentBrowserModule& CBM = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		CBM.Get().SyncBrowserToFolders({FolderPath});
	}
	else
	{
		bUiSuppressed = true;
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("folder_path"), FolderPath);
	if (bUiSuppressed)
	{
		O->SetBoolField(TEXT("ui_suppressed"), true);
	}
	return UnrealAiToolJson::Ok(O);
}
