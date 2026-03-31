#include "Tools/Presentation/UnrealAiToolEditorNoteBuilders.h"

#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"

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
	if (!ToolMarkdownBody.IsEmpty())
	{
		Pres->MarkdownBody = ToolMarkdownBody + TEXT("\n\n");
	}
	Pres->MarkdownBody += FString::Printf(TEXT("### Blueprint\n- Asset: [%s](%s)\n"), *Label, *BlueprintObjectPath);
	if (!GraphName.IsEmpty())
	{
		Pres->MarkdownBody += FString::Printf(TEXT("- Graph: %s\n"), *GraphName);
	}

	// No ImageFilePath: blueprint RenderThumbnail output is often unusable in chat (magenta / garbage when
	// graph previews or materials do not resolve cleanly). Use the markdown link + asset hyperlink row instead.

	return Pres;
}

