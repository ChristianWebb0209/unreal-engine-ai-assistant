#pragma once

#include "CoreMinimal.h"
#include "Harness/UnrealAiAgentTypes.h"

class IUnrealAiPersistence;

/** One named API endpoint + key (OpenAI-compatible). */
struct FUnrealAiProviderEntry
{
	FString Id;
	FString BaseUrl;
	FString ApiKey;
};

/** Loads `settings/plugin_settings.json`: legacy `api`, optional `providers[]`, per-model caps + providerId. */
class FUnrealAiModelProfileRegistry
{
public:
	explicit FUnrealAiModelProfileRegistry(IUnrealAiPersistence* InPersistence);

	void Reload();

	FString GetApiBaseUrl() const { return ApiBaseUrl; }
	FString GetApiKey() const { return ApiKey; }
	FString GetDefaultModelId() const { return DefaultModelId; }
	FString GetDefaultProviderId() const { return DefaultProviderId; }

	void GetEffectiveCapabilities(const FString& ModelProfileId, FUnrealAiModelCapabilities& Out) const;

	/** All registered model keys in `models{}` (sorted). */
	void EnumerateModelProfileIds(TArray<FString>& Out) const;
	void EnumerateProviderIds(TArray<FString>& Out) const;

	/** True if legacy `api.apiKey` or any `providers[].apiKey` is non-empty. */
	bool HasAnyConfiguredApiKey() const;

	/**
	 * Resolve HTTPS base URL + bearer key for a model turn.
	 * Uses per-model providerId, else defaultProviderId, else legacy `api` fields.
	 */
	bool TryResolveApiForModel(const FString& ModelProfileId, FString& OutBaseUrl, FString& OutApiKey) const;

private:
	IUnrealAiPersistence* Persistence = nullptr;
	FString ApiBaseUrl;
	FString ApiKey;
	FString DefaultModelId;
	/** When set, models without providerId use this provider from `providers`. */
	FString DefaultProviderId;
	TMap<FString, FUnrealAiModelCapabilities> ModelMap;
	TMap<FString, FUnrealAiProviderEntry> ProvidersById;

	const FUnrealAiProviderEntry* FindProvider(const FString& Id) const;
};
