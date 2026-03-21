#include "Tools/UnrealAiToolDispatch_ContentBrowserEx.h"

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
	FContentBrowserModule& CBM = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	CBM.Get().SyncBrowserToFolder(FName(*FolderPath));
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("folder_path"), FolderPath);
	return UnrealAiToolJson::Ok(O);
}
