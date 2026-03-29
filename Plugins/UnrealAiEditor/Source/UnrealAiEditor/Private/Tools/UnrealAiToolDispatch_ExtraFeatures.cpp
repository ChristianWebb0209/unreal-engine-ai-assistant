#include "Tools/UnrealAiToolDispatch_ExtraFeatures.h"

#include "Tools/UnrealAiToolDispatch_MoreAssets.h"
#include "Tools/UnrealAiToolJson.h"

#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/Paths.h"
#include "Sound/SoundBase.h"

static FUnrealAiToolInvocationResult OpenAssetPathTool(const TCHAR* ObjectPathField, const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	if (!Args->TryGetStringField(ObjectPathField, Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(FString::Printf(TEXT("%s is required"), ObjectPathField));
	}
	TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
	A->SetStringField(TEXT("object_path"), Path);
	return UnrealAiDispatch_AssetOpenEditor(A);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_AudioComponentPreview(const TSharedPtr<FJsonObject>& Args)
{
	FString SoundPath;
	if (!Args->TryGetStringField(TEXT("sound_path"), SoundPath) || SoundPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("sound_path is required"));
	}
	USoundBase* Sound = LoadObject<USoundBase>(nullptr, *SoundPath);
	if (!Sound)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load sound asset"));
	}
	if (!GEditor)
	{
		return UnrealAiToolJson::Error(TEXT("GEditor not available"));
	}
	UWorld* W = GEditor->GetEditorWorldContext().World();
	if (!W)
	{
		return UnrealAiToolJson::Error(TEXT("No editor world"));
	}
	UGameplayStatics::PlaySound2D(W, Sound, 1.f, 1.f, 0.f, nullptr, nullptr, false);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_RenderTargetReadbackEditor(const TSharedPtr<FJsonObject>& Args)
{
	FString AssetPath;
	FString ExportPath;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty()
		|| !Args->TryGetStringField(TEXT("export_path"), ExportPath) || ExportPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("asset_path and export_path are required"));
	}
	UTextureRenderTarget2D* RT = LoadObject<UTextureRenderTarget2D>(nullptr, *AssetPath);
	if (!RT)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load UTextureRenderTarget2D"));
	}
	if (!GEditor)
	{
		return UnrealAiToolJson::Error(TEXT("GEditor not available"));
	}
	UWorld* W = GEditor->GetEditorWorldContext().World();
	if (!W)
	{
		return UnrealAiToolJson::Error(TEXT("No editor world"));
	}
	FString Abs = ExportPath;
	if (FPaths::IsRelative(Abs))
	{
		Abs = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir(), Abs);
	}
	const FString ExportDir = FPaths::GetPath(Abs);
	const FString ExportFile = FPaths::GetCleanFilename(Abs);
	UKismetRenderingLibrary::ExportRenderTarget(W, RT, ExportDir, ExportFile);
	const bool bOk = true;
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), bOk);
	O->SetStringField(TEXT("export_path"), Abs);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_SequencerOpen(const TSharedPtr<FJsonObject>& Args)
{
	return OpenAssetPathTool(TEXT("sequence_path"), Args);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_MetasoundOpenEditor(const TSharedPtr<FJsonObject>& Args)
{
	return OpenAssetPathTool(TEXT("asset_path"), Args);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_FoliagePaintInstances(const TSharedPtr<FJsonObject>& Args)
{
	(void)Args;
	return UnrealAiToolJson::Error(
		TEXT("foliage_paint_instances: automated foliage painting is not implemented in this plugin build; use editor foliage mode."));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_LandscapeImportHeightmap(const TSharedPtr<FJsonObject>& Args)
{
	(void)Args;
	return UnrealAiToolJson::Error(
		TEXT("landscape_import_heightmap: use the Landscape editor import UI; no headless API wired in this build."));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_PcgGenerate(const TSharedPtr<FJsonObject>& Args)
{
	(void)Args;
	return UnrealAiToolJson::Error(TEXT("pcg_generate: requires PCG component context; not implemented in this plugin build."));
}
