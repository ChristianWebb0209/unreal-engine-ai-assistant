#include "Context/UnrealAiEditorContextQueries.h"

#include "Context/AgentContextTypes.h"
#include "Context/IAgentContextService.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserItemPath.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
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
	// GetInternalPathString() asserts if no internal mapping exists; use virtual path as fallback.
	{
		const FContentBrowserItemPath ItemPath = CBS.GetCurrentPath();
		const FString PathStr = ItemPath.HasInternalPath()
			? ItemPath.GetInternalPathString()
			: ItemPath.GetVirtualPathString();
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
		Ctx->AddAttachment(AttachmentFromAssetData(Selected[i]));
	}
#endif
}

FContextAttachment UnrealAiEditorContextQueries::AttachmentFromAssetData(const FAssetData& AssetData)
{
	FContextAttachment A;
	A.Type = EContextAttachmentType::AssetPath;
	A.Payload = AssetData.GetObjectPathString();
	A.Label = AssetData.AssetName.ToString();
	A.IconClassPath = AssetData.AssetClassPath.ToString();
	return A;
}

FContextAttachment UnrealAiEditorContextQueries::AttachmentFromActor(AActor* Actor)
{
	FContextAttachment A;
	if (!Actor)
	{
		return A;
	}
	A.Type = EContextAttachmentType::ActorReference;
	A.Payload = Actor->GetPathName();
	A.Label = Actor->GetActorLabel().IsEmpty() ? Actor->GetName() : Actor->GetActorLabel();
	if (UClass* C = Actor->GetClass())
	{
		A.IconClassPath = C->GetPathName();
	}
	return A;
}

FContextAttachment UnrealAiEditorContextQueries::AttachmentFromViewportScreenshotFile(const FString& FullPathPng)
{
	FContextAttachment A;
	A.Type = EContextAttachmentType::FilePath;
	A.Payload = FullPathPng;
	A.Label = TEXT("Viewport screenshot (PNG)");
	/** UI: show thumbnail + preview; not a UObject path. */
	A.IconClassPath = TEXT("UnrealAi.ViewportScreenshot");
	return A;
}

bool UnrealAiEditorContextQueries::IsImageLikeAttachment(const FContextAttachment& A)
{
	if (A.Type == EContextAttachmentType::FilePath)
	{
		const FString Ext = FPaths::GetExtension(A.Payload, false).ToLower();
		if (!Ext.IsEmpty())
		{
			static const TCHAR* ImageExts[] = {
				TEXT("png"), TEXT("jpg"), TEXT("jpeg"), TEXT("webp"), TEXT("gif"), TEXT("bmp"), TEXT("tga"), TEXT("hdr"), TEXT("exr")};
			for (const TCHAR* E : ImageExts)
			{
				if (Ext == E)
				{
					return true;
				}
			}
		}
		if (A.Label.Contains(TEXT("Viewport screenshot"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		return false;
	}
#if WITH_EDITOR
	if (A.Type == EContextAttachmentType::AssetPath && !A.Payload.IsEmpty())
	{
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
		{
			return false;
		}
		FAssetRegistryModule& ARM = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		const FAssetData AD = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(A.Payload));
		if (!AD.IsValid())
		{
			return false;
		}
		const FString Cls = AD.AssetClassPath.ToString();
		return Cls.Contains(TEXT("Texture")) || Cls.Contains(TEXT("MediaTexture"));
	}
#endif
	return false;
}

FString UnrealAiEditorContextQueries::BuildRichAttachmentPayload(const FContextAttachment& Attachment)
{
#if WITH_EDITOR
	switch (Attachment.Type)
	{
	case EContextAttachmentType::AssetPath:
	{
		if (Attachment.Payload.IsEmpty())
		{
			return Attachment.Payload;
		}
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		const FAssetData AD = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(Attachment.Payload));
		if (!AD.IsValid())
		{
			return FString::Printf(TEXT("%s\n(Not found in Asset Registry.)"), *Attachment.Payload);
		}
		FString Out;
		Out += FString::Printf(TEXT("Object path: %s\n"), *AD.GetObjectPathString());
		Out += FString::Printf(TEXT("Asset class: %s\n"), *AD.AssetClassPath.ToString());
		Out += FString::Printf(TEXT("Package: %s\n"), *AD.PackageName.ToString());
		return Out;
	}
	case EContextAttachmentType::ActorReference:
	{
		FString Out;
		Out += FString::Printf(TEXT("Actor path: %s\n"), *Attachment.Payload);
		if (AActor* Act = FindObject<AActor>(nullptr, *Attachment.Payload))
		{
			Out += FString::Printf(TEXT("Label: %s\n"), *Act->GetActorLabel());
			Out += FString::Printf(TEXT("Class: %s\n"), *Act->GetClass()->GetPathName());
			Out += FString::Printf(TEXT("Location: %s\n"), *Act->GetActorLocation().ToString());
			Out += FString::Printf(TEXT("Rotation: %s\n"), *Act->GetActorRotation().ToString());
		}
		else
		{
			Out += TEXT("(Actor not resolved in editor — path only.)\n");
		}
		return Out;
	}
	case EContextAttachmentType::ContentFolder:
		return FString::Printf(
			TEXT("Content Browser folder virtual path: %s\n"),
			*Attachment.Payload);
	case EContextAttachmentType::FilePath:
	case EContextAttachmentType::FreeText:
	case EContextAttachmentType::BlueprintNodeRef:
	default:
		return Attachment.Payload;
	}
#else
	return Attachment.Payload;
#endif
}
