#include "Tools/Presentation/UnrealAiToolEditorNoteBuilders.h"

#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#include "Tools/Presentation/UnrealAiBlueprintThumbnailCapture.h"

namespace
{
	static FString MakeSafeToolNoteFileBase(const FString& In)
	{
		FString S = In;
		S.ReplaceInline(TEXT("/"), TEXT("_"));
		S.ReplaceInline(TEXT("."), TEXT("_"));
		return S;
	}
}

TSharedPtr<FUnrealAiToolEditorPresentation> UnrealAiToolEditorNoteBuilders::MakeBlueprintToolNote(
	const FString& BlueprintObjectPath,
	const FString& GraphName,
	const FString& ToolMarkdownBody)
{
	if (BlueprintObjectPath.IsEmpty())
	{
		return nullptr;
	}

	UObject* Obj = LoadObject<UObject>(nullptr, *BlueprintObjectPath);
	FString Label = Obj ? Obj->GetName() : BlueprintObjectPath;

	TSharedPtr<FUnrealAiToolEditorPresentation> Pres = MakeShared<FUnrealAiToolEditorPresentation>();
	Pres->AssetLinks.Add(FUnrealAiToolAssetLink{Label, BlueprintObjectPath});

	// Compose markdown (small subset handled by markdown UI).
	// Tools are responsible for any additional markdown beyond this scaffold.
	if (!ToolMarkdownBody.IsEmpty())
	{
		Pres->MarkdownBody = ToolMarkdownBody + TEXT("\n\n");
	}
	Pres->MarkdownBody += FString::Printf(TEXT("### Blueprint\n- Asset: [%s](%s)\n"), *Label, *BlueprintObjectPath);
	if (!GraphName.IsEmpty())
	{
		Pres->MarkdownBody += FString::Printf(TEXT("- Graph: %s\n"), *GraphName);
	}

	// Best-effort thumbnail capture: keep it small; failures are non-fatal.
	const uint32 W = 420;
	const uint32 H = 300;
	const FString OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAiEditor"), TEXT("ToolNotes"));
	const FString SafeBase = MakeSafeToolNoteFileBase(BlueprintObjectPath);
	const FString OutPath = FPaths::Combine(OutDir, FString::Printf(TEXT("%s.png"), *SafeBase));

	if (UnrealAiBlueprintThumbnailCapture::TryCaptureBlueprintThumbnailPng(BlueprintObjectPath, OutPath, W, H))
	{
		Pres->ImageFilePath = OutPath;
	}

	return Pres;
}

