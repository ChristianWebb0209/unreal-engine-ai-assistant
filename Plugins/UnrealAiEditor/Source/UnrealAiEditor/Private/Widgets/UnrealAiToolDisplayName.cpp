#include "Widgets/UnrealAiToolDisplayName.h"

#include "Tools/UnrealAiToolCatalog.h"
#include "Dom/JsonObject.h"

FString UnrealAiFormatToolIdAsTitleWords(const FString& ToolId)
{
	FString Id = ToolId.TrimStartAndEnd();
	if (Id.IsEmpty())
	{
		return TEXT("Tool");
	}
	TArray<FString> Parts;
	Id.ParseIntoArray(Parts, TEXT("_"), true);
	for (FString& Word : Parts)
	{
		Word.TrimStartAndEndInline();
		if (Word.IsEmpty())
		{
			continue;
		}
		Word[0] = FChar::ToUpper(Word[0]);
		for (int32 i = 1; i < Word.Len(); ++i)
		{
			Word[i] = FChar::ToLower(Word[i]);
		}
	}
	return FString::Join(Parts, TEXT(" "));
}

FString UnrealAiResolveToolUserFacingName(const FString& ToolId, const FUnrealAiToolCatalog* Catalog)
{
	const FString Id = ToolId.TrimStartAndEnd();
	if (Id.IsEmpty())
	{
		return TEXT("Tool");
	}
	const FString LowerId = Id.ToLower();
	if (LowerId == TEXT("unreal_ai_dispatch"))
	{
		return TEXT("Run tool");
	}
	if (LowerId == TEXT("agent_emit_todo_plan"))
	{
		return TEXT("Update todo plan");
	}
	if (Catalog)
	{
		const TSharedPtr<FJsonObject> Def = Catalog->FindToolDefinition(Id);
		if (Def.IsValid())
		{
			FString UiLabel;
			if (Def->TryGetStringField(TEXT("ui_label"), UiLabel))
			{
				UiLabel.TrimStartAndEndInline();
				if (!UiLabel.IsEmpty())
				{
					return UiLabel;
				}
			}
			FString DisplayName;
			if (Def->TryGetStringField(TEXT("display_name"), DisplayName))
			{
				DisplayName.TrimStartAndEndInline();
				if (!DisplayName.IsEmpty())
				{
					return DisplayName;
				}
			}
		}
	}
	return UnrealAiFormatToolIdAsTitleWords(Id);
}
