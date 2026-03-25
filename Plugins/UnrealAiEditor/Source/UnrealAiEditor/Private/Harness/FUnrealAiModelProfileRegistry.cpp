#include "Harness/FUnrealAiModelProfileRegistry.h"

#include "Backend/IUnrealAiPersistence.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAiModelProfileRegistryUtil
{
	static void ParseCapabilitiesObject(const TSharedPtr<FJsonObject>& O, FUnrealAiModelCapabilities& Out)
	{
		if (!O.IsValid())
		{
			return;
		}
		O->TryGetStringField(TEXT("modelIdForApi"), Out.ModelIdForApi);
		O->TryGetStringField(TEXT("providerId"), Out.ProviderId);
		double Mct = 0;
		if (O->TryGetNumberField(TEXT("maxContextTokens"), Mct))
		{
			Out.MaxContextTokens = static_cast<int32>(Mct);
		}
		double Mot = 0;
		if (O->TryGetNumberField(TEXT("maxOutputTokens"), Mot))
		{
			Out.MaxOutputTokens = static_cast<int32>(Mot);
		}
		O->TryGetBoolField(TEXT("supportsNativeTools"), Out.bSupportsNativeTools);
		O->TryGetBoolField(TEXT("supportsParallelToolCalls"), Out.bSupportsParallelToolCalls);
		{
			bool B = Out.bSupportsImages;
			if (O->TryGetBoolField(TEXT("supportsImages"), B))
			{
				Out.bSupportsImages = B;
			}
		}
		double Mar = 0;
		if (O->TryGetNumberField(TEXT("maxAgentLlmRounds"), Mar))
		{
			Out.MaxAgentLlmRounds = static_cast<int32>(Mar);
		}
	}

	static FUnrealAiModelCapabilities DefaultsForId(const FString& ModelId)
	{
		FUnrealAiModelCapabilities C;
		C.ModelIdForApi = ModelId;
		C.MaxContextTokens = 128000;
		C.MaxOutputTokens = 4096;
		C.bSupportsNativeTools = true;
		C.bSupportsParallelToolCalls = true;
		C.bSupportsImages = true;
		C.MaxAgentLlmRounds = 32;
		return C;
	}
}

FUnrealAiModelProfileRegistry::FUnrealAiModelProfileRegistry(IUnrealAiPersistence* InPersistence)
	: Persistence(InPersistence)
{
	Reload();
}

void FUnrealAiModelProfileRegistry::Reload()
{
	ModelMap.Reset();
	ProvidersById.Reset();
	ApiBaseUrl = TEXT("https://openrouter.ai/api/v1");
	ApiKey.Reset();
	DefaultModelId = TEXT("openai/gpt-4o-mini");
	DefaultProviderId.Reset();

	if (!Persistence)
	{
		ModelMap.Add(DefaultModelId, UnrealAiModelProfileRegistryUtil::DefaultsForId(DefaultModelId));
		return;
	}

	FString Json;
	if (!Persistence->LoadSettingsJson(Json) || Json.IsEmpty())
	{
		ModelMap.Add(DefaultModelId, UnrealAiModelProfileRegistryUtil::DefaultsForId(DefaultModelId));
		return;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		ModelMap.Add(DefaultModelId, UnrealAiModelProfileRegistryUtil::DefaultsForId(DefaultModelId));
		return;
	}

	const TSharedPtr<FJsonObject>* ApiObj = nullptr;
	if (Root->TryGetObjectField(TEXT("api"), ApiObj) && ApiObj->IsValid())
	{
		FString Tmp;
		if ((*ApiObj)->TryGetStringField(TEXT("baseUrl"), Tmp))
		{
			ApiBaseUrl = Tmp.TrimStartAndEnd();
		}
		if ((*ApiObj)->TryGetStringField(TEXT("apiKey"), Tmp))
		{
			ApiKey = Tmp.TrimStartAndEnd();
		}
		if ((*ApiObj)->TryGetStringField(TEXT("defaultModel"), Tmp))
		{
			DefaultModelId = Tmp.TrimStartAndEnd();
		}
		if ((*ApiObj)->TryGetStringField(TEXT("defaultProviderId"), Tmp))
		{
			DefaultProviderId = Tmp.TrimStartAndEnd();
		}
	}

	bool bLoadedSections = false;
	const TArray<TSharedPtr<FJsonValue>>* SectionsArr = nullptr;
	if (Root->TryGetArrayField(TEXT("sections"), SectionsArr) && SectionsArr && SectionsArr->Num() > 0)
	{
		bLoadedSections = true;
		for (const TSharedPtr<FJsonValue>& Sv : *SectionsArr)
		{
			const TSharedPtr<FJsonObject>* So = nullptr;
			if (!Sv.IsValid() || !Sv->TryGetObject(So) || !So->IsValid())
			{
				continue;
			}
			FUnrealAiProviderEntry E;
			FString TmpS;
			if ((*So)->TryGetStringField(TEXT("id"), TmpS))
			{
				E.Id = TmpS.TrimStartAndEnd();
			}
			if ((*So)->TryGetStringField(TEXT("baseUrl"), TmpS))
			{
				E.BaseUrl = TmpS.TrimStartAndEnd();
			}
			if ((*So)->TryGetStringField(TEXT("apiKey"), TmpS))
			{
				E.ApiKey = TmpS.TrimStartAndEnd();
			}
			if (E.Id.IsEmpty())
			{
				continue;
			}
			ProvidersById.Add(E.Id, MoveTemp(E));

			const TArray<TSharedPtr<FJsonValue>>* ModelsArr = nullptr;
			if ((*So)->TryGetArrayField(TEXT("models"), ModelsArr) && ModelsArr)
			{
				for (const TSharedPtr<FJsonValue>& Mv : *ModelsArr)
				{
					const TSharedPtr<FJsonObject>* Mo = nullptr;
					if (!Mv.IsValid() || !Mv->TryGetObject(Mo) || !Mo->IsValid())
					{
						continue;
					}
					FString ProfileKey;
					(*Mo)->TryGetStringField(TEXT("profileKey"), ProfileKey);
					if (ProfileKey.IsEmpty())
					{
						continue;
					}
					FUnrealAiModelCapabilities Cap = UnrealAiModelProfileRegistryUtil::DefaultsForId(ProfileKey);
					Cap.ModelIdForApi = ProfileKey;
					UnrealAiModelProfileRegistryUtil::ParseCapabilitiesObject(*Mo, Cap);
					Cap.ProviderId = E.Id;
					if (Cap.ModelIdForApi.IsEmpty())
					{
						Cap.ModelIdForApi = ProfileKey;
					}
					ModelMap.Add(ProfileKey, Cap);
				}
			}
		}
	}

	if (!bLoadedSections)
	{
		const TArray<TSharedPtr<FJsonValue>>* ProvidersArr = nullptr;
		if (Root->TryGetArrayField(TEXT("providers"), ProvidersArr) && ProvidersArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *ProvidersArr)
			{
				const TSharedPtr<FJsonObject>* Po = nullptr;
				if (!V.IsValid() || !V->TryGetObject(Po) || !Po || !(*Po).IsValid())
				{
					continue;
				}
				FUnrealAiProviderEntry E;
				FString TmpP;
				if ((*Po)->TryGetStringField(TEXT("id"), TmpP))
				{
					E.Id = TmpP.TrimStartAndEnd();
				}
				if ((*Po)->TryGetStringField(TEXT("baseUrl"), TmpP))
				{
					E.BaseUrl = TmpP.TrimStartAndEnd();
				}
				if ((*Po)->TryGetStringField(TEXT("apiKey"), TmpP))
				{
					E.ApiKey = TmpP.TrimStartAndEnd();
				}
				if (!E.Id.IsEmpty())
				{
					ProvidersById.Add(E.Id, MoveTemp(E));
				}
			}
		}

		const TSharedPtr<FJsonObject>* ModelsObj = nullptr;
		if (Root->TryGetObjectField(TEXT("models"), ModelsObj) && ModelsObj && (*ModelsObj).IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ModelsObj)->Values)
			{
				const FString& ModelKey = Pair.Key;
				const TSharedPtr<FJsonObject>* CapObj = nullptr;
				if (!(*ModelsObj)->TryGetObjectField(ModelKey, CapObj) || !CapObj || !(*CapObj).IsValid())
				{
					continue;
				}
				FUnrealAiModelCapabilities Cap = UnrealAiModelProfileRegistryUtil::DefaultsForId(ModelKey);
				Cap.ModelIdForApi = ModelKey;
				UnrealAiModelProfileRegistryUtil::ParseCapabilitiesObject(*CapObj, Cap);
				if (Cap.ModelIdForApi.IsEmpty())
				{
					Cap.ModelIdForApi = ModelKey;
				}
				ModelMap.Add(ModelKey, Cap);
			}
		}
	}

	if (!ModelMap.Contains(DefaultModelId))
	{
		ModelMap.Add(DefaultModelId, UnrealAiModelProfileRegistryUtil::DefaultsForId(DefaultModelId));
	}
}

const FUnrealAiProviderEntry* FUnrealAiModelProfileRegistry::FindProvider(const FString& Id) const
{
	if (Id.IsEmpty())
	{
		return nullptr;
	}
	return ProvidersById.Find(Id);
}

bool FUnrealAiModelProfileRegistry::HasAnyConfiguredApiKey() const
{
	if (!ApiKey.IsEmpty())
	{
		return true;
	}
	for (const TPair<FString, FUnrealAiProviderEntry>& P : ProvidersById)
	{
		if (!P.Value.ApiKey.IsEmpty())
		{
			return true;
		}
	}
	return false;
}

bool FUnrealAiModelProfileRegistry::TryResolveApiForModel(const FString& ModelProfileId, FString& OutBaseUrl, FString& OutApiKey) const
{
	OutBaseUrl.Reset();
	OutApiKey.Reset();

	FUnrealAiModelCapabilities Caps;
	GetEffectiveCapabilities(ModelProfileId, Caps);

	FString ProviderToUse = Caps.ProviderId;
	if (ProviderToUse.IsEmpty())
	{
		ProviderToUse = DefaultProviderId;
	}

	if (!ProviderToUse.IsEmpty())
	{
		if (const FUnrealAiProviderEntry* P = FindProvider(ProviderToUse))
		{
			OutBaseUrl = P->BaseUrl.IsEmpty() ? ApiBaseUrl : P->BaseUrl;
			OutApiKey = P->ApiKey.IsEmpty() ? ApiKey : P->ApiKey;
			return !OutBaseUrl.IsEmpty() && !OutApiKey.IsEmpty();
		}
	}

	OutBaseUrl = ApiBaseUrl;
	OutApiKey = ApiKey;
	return !OutBaseUrl.IsEmpty() && !OutApiKey.IsEmpty();
}

void FUnrealAiModelProfileRegistry::GetEffectiveCapabilities(const FString& ModelProfileId, FUnrealAiModelCapabilities& Out) const
{
	const FString Key = ModelProfileId.IsEmpty() ? DefaultModelId : ModelProfileId;
	if (const FUnrealAiModelCapabilities* Found = ModelMap.Find(Key))
	{
		Out = *Found;
		return;
	}
	Out = UnrealAiModelProfileRegistryUtil::DefaultsForId(Key);
	Out.ModelIdForApi = Key;
}

void FUnrealAiModelProfileRegistry::EnumerateModelProfileIds(TArray<FString>& Out) const
{
	ModelMap.GetKeys(Out);
	Out.Sort();
}

void FUnrealAiModelProfileRegistry::EnumerateProviderIds(TArray<FString>& Out) const
{
	Out.Reset();
	for (const TPair<FString, FUnrealAiProviderEntry>& P : ProvidersById)
	{
		Out.Add(P.Key);
	}
	Out.Sort();
}
