#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class IUnrealAiPersistence;

/** JSON helpers and default template for plugin_settings.json (LLM stack). */
namespace UnrealAiSettingsJsonUtil
{
	const TCHAR* GetDefaultSettingsJson();

	TSharedPtr<FJsonObject> CloneJsonObject(const TSharedPtr<FJsonObject>& Src);
	bool DeserializeRoot(const FString& Json, TSharedPtr<FJsonObject>& OutRoot);
	int32 ParsePositiveInt(const FString& S, int32 Default);

	FString GuessCompanyPresetFromUrl(const FString& Url);
	FString PresetToSectionId(const FString& PresetId);
	FString PresetToSectionLabel(const FString& PresetId);

	const TMap<FString, TArray<FString>>& GetKnownProviderModels();
	void BuildModelOptionsForPreset(const FString& PresetId, const FString& FilterText, TArray<TSharedPtr<FString>>& OutOptions);

	bool MigrateSettingsToV4IfNeeded(TSharedPtr<FJsonObject>& Root, IUnrealAiPersistence* Persist);
}
