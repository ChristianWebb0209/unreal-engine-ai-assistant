#include "Settings/UnrealAiSettingsJsonUtil.h"

#include "Backend/IUnrealAiPersistence.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealAiSettingsJsonUtil
{
	namespace Internal
	{
		static const TCHAR* DefaultSettingsJson = TEXT(
			"{\n"
			"\t\"version\": 4,\n"
			"\t\"api\": {\n"
			"\t\t\"baseUrl\": \"https://api.openai.com/v1\",\n"
			"\t\t\"apiKey\": \"\",\n"
			"\t\t\"defaultModel\": \"gpt-4o-mini\",\n"
			"\t\t\"defaultProviderId\": \"openai\"\n"
			"\t},\n"
			"\t\"sections\": [\n"
			"\t\t{\n"
			"\t\t\t\"id\": \"openai\",\n"
			"\t\t\t\"label\": \"OpenAI\",\n"
			"\t\t\t\"companyPreset\": \"openai\",\n"
			"\t\t\t\"baseUrl\": \"https://api.openai.com/v1\",\n"
			"\t\t\t\"apiKey\": \"\",\n"
			"\t\t\t\"models\": [\n"
			"\t\t\t\t{\n"
			"\t\t\t\t\t\"profileKey\": \"gpt-4o-mini\",\n"
			"\t\t\t\t\t\"maxContextTokens\": 128000,\n"
			"\t\t\t\t\t\"maxOutputTokens\": 4096,\n"
			"\t\t\t\t\t\"supportsNativeTools\": true,\n"
			"\t\t\t\t\t\"supportsParallelToolCalls\": true,\n"
			"\t\t\t\t\t\"supportsImages\": true\n"
			"\t\t\t\t}\n"
			"\t\t\t]\n"
			"\t\t}\n"
			"\t],\n"
			"\t\"retrieval\": {\n"
			"\t\t\"enabled\": false,\n"
			"\t\t\"embeddingModel\": \"text-embedding-3-small\",\n"
			"\t\t\"maxSnippetsPerTurn\": 6,\n"
			"\t\t\"maxSnippetTokens\": 256,\n"
			"\t\t\"autoIndexOnProjectOpen\": true,\n"
			"\t\t\"periodicScrubMinutes\": 30,\n"
			"\t\t\"allowMixedModelCompatibility\": false,\n"
			"\t\t\"rootPreset\": \"minimal\",\n"
			"\t\t\"indexedExtensions\": [],\n"
			"\t\t\"maxFilesPerRebuild\": 0,\n"
			"\t\t\"maxTotalChunksPerRebuild\": 0,\n"
			"\t\t\"maxEmbeddingCallsPerRebuild\": 0,\n"
			"\t\t\"maxChunksPerFile\": 96,\n"
			"\t\t\"chunkChars\": 4000,\n"
			"\t\t\"chunkOverlap\": 150,\n"
			"\t\t\"assetRegistryMaxAssets\": 0,\n"
			"\t\t\"assetRegistryIncludeEngineAssets\": false,\n"
			"\t\t\"blueprintIncludeEngineAssets\": false,\n"
			"\t\t\"indexDirectorySummaries\": true,\n"
			"\t\t\"indexAssetFamilySummaries\": true,\n"
			"\t\t\"indexEditorScene\": false,\n"
			"\t\t\"embeddingBatchSize\": 128,\n"
			"\t\t\"minDelayMsBetweenEmbeddingBatches\": 0,\n"
			"\t\t\"indexFirstWaveTimeBudgetSeconds\": 0,\n"
			"\t\t\"indexRetrievalWave4\": false,\n"
			"\t\t\"indexMemoryRecordsInVectorStore\": false,\n"
			"\t\t\"blueprintMaxFeatureRecords\": 0,\n"
			"\t\t\"contextAggression\": 0.5\n"
			"\t},\n"
			"\t\"agent\": {\n"
			"\t\t\"useSubagents\": true,\n"
			"\t\t\"codeTypePreference\": \"auto\",\n"
			"\t\t\"autoConfirmDestructive\": true,\n"
			"\t\t\"enablePieTools\": false\n"
			"\t},\n"
			"\t\"ui\": {\n"
			"\t\t\"editorFocus\": false\n"
			"\t}\n"
			"}\n");

		static void AppendModelJsonToSection(
			const FString& ProfileKey,
			const TSharedPtr<FJsonObject>& CapObj,
			const TSharedPtr<FJsonObject>& SectionObj)
		{
			if (!CapObj.IsValid() || !SectionObj.IsValid() || ProfileKey.IsEmpty())
			{
				return;
			}
			TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
			M->SetStringField(TEXT("profileKey"), ProfileKey);
			{
				FString S;
				if (CapObj->TryGetStringField(TEXT("modelIdForApi"), S) && !S.IsEmpty())
				{
					M->SetStringField(TEXT("modelIdForApi"), S);
				}
			}
			double Mct = 128000;
			if (CapObj->TryGetNumberField(TEXT("maxContextTokens"), Mct))
			{
				M->SetNumberField(TEXT("maxContextTokens"), Mct);
			}
			else
			{
				M->SetNumberField(TEXT("maxContextTokens"), 128000);
			}
			double Mot = 4096;
			if (CapObj->TryGetNumberField(TEXT("maxOutputTokens"), Mot))
			{
				M->SetNumberField(TEXT("maxOutputTokens"), Mot);
			}
			else
			{
				M->SetNumberField(TEXT("maxOutputTokens"), 4096);
			}
			bool B = true;
			if (CapObj->TryGetBoolField(TEXT("supportsNativeTools"), B))
			{
				M->SetBoolField(TEXT("supportsNativeTools"), B);
			}
			else
			{
				M->SetBoolField(TEXT("supportsNativeTools"), true);
			}
			B = true;
			if (CapObj->TryGetBoolField(TEXT("supportsParallelToolCalls"), B))
			{
				M->SetBoolField(TEXT("supportsParallelToolCalls"), B);
			}
			else
			{
				M->SetBoolField(TEXT("supportsParallelToolCalls"), true);
			}
			B = true;
			if (CapObj->TryGetBoolField(TEXT("supportsImages"), B))
			{
				M->SetBoolField(TEXT("supportsImages"), B);
			}
			else
			{
				M->SetBoolField(TEXT("supportsImages"), true);
			}

			const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
			TArray<TSharedPtr<FJsonValue>> Arr;
			if (SectionObj->TryGetArrayField(TEXT("models"), Existing) && Existing)
			{
				Arr = *Existing;
			}
			Arr.Add(MakeShared<FJsonValueObject>(M.ToSharedRef()));
			SectionObj->SetArrayField(TEXT("models"), Arr);
		}
	}

	const TCHAR* GetDefaultSettingsJson()
	{
		return Internal::DefaultSettingsJson;
	}

	TSharedPtr<FJsonObject> CloneJsonObject(const TSharedPtr<FJsonObject>& Src)
	{
		if (!Src.IsValid())
		{
			return MakeShared<FJsonObject>();
		}
		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		if (!FJsonSerializer::Serialize(Src.ToSharedRef(), W))
		{
			return MakeShared<FJsonObject>();
		}
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Out);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return MakeShared<FJsonObject>();
		}
		return Root;
	}

	bool DeserializeRoot(const FString& Json, TSharedPtr<FJsonObject>& OutRoot)
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutRoot) && OutRoot.IsValid();
	}

	int32 ParsePositiveInt(const FString& S, int32 Default)
	{
		if (S.IsEmpty())
		{
			return Default;
		}
		return FMath::Max(0, FCString::Atoi(*S));
	}

	FString GuessCompanyPresetFromUrl(const FString& Url)
	{
		const FString L = Url.ToLower();
		if (L.Contains(TEXT("openai.com")))
		{
			return TEXT("openai");
		}
		if (L.Contains(TEXT("api.anthropic")) || L.Contains(TEXT("anthropic")))
		{
			return TEXT("anthropic");
		}
		if (L.Contains(TEXT("groq.com")))
		{
			return TEXT("groq");
		}
		if (L.Contains(TEXT("deepseek")))
		{
			return TEXT("deepseek");
		}
		if (L.Contains(TEXT("x.ai")))
		{
			return TEXT("xai");
		}
		return TEXT("custom");
	}

	FString PresetToSectionId(const FString& PresetId)
	{
		if (PresetId == TEXT("openai")
			|| PresetId == TEXT("anthropic")
			|| PresetId == TEXT("groq")
			|| PresetId == TEXT("deepseek")
			|| PresetId == TEXT("xai")
			|| PresetId == TEXT("azure"))
		{
			return PresetId;
		}
		return TEXT("custom");
	}

	FString PresetToSectionLabel(const FString& PresetId)
	{
		if (PresetId == TEXT("openai")) return TEXT("OpenAI");
		if (PresetId == TEXT("anthropic")) return TEXT("Anthropic");
		if (PresetId == TEXT("groq")) return TEXT("Groq");
		if (PresetId == TEXT("deepseek")) return TEXT("DeepSeek");
		if (PresetId == TEXT("xai")) return TEXT("xAI");
		if (PresetId == TEXT("azure")) return TEXT("Azure OpenAI");
		return TEXT("Custom");
	}

	const TMap<FString, TArray<FString>>& GetKnownProviderModels()
	{
		static const TMap<FString, TArray<FString>> Catalog = {
			{ TEXT("openai"), {
				TEXT("gpt-5"),
				TEXT("gpt-5-mini"),
				TEXT("gpt-4o"),
				TEXT("gpt-4o-mini"),
				TEXT("o3"),
				TEXT("o4-mini")
			}},
			{ TEXT("anthropic"), {
				TEXT("claude-3-7-sonnet-latest"),
				TEXT("claude-3-5-sonnet-latest"),
				TEXT("claude-3-5-haiku-latest")
			}},
			{ TEXT("groq"), {
				TEXT("llama-3.3-70b-versatile"),
				TEXT("llama-3.1-8b-instant"),
				TEXT("deepseek-r1-distill-llama-70b"),
				TEXT("qwen-qwq-32b")
			}},
			{ TEXT("deepseek"), {
				TEXT("deepseek-chat"),
				TEXT("deepseek-reasoner")
			}},
			{ TEXT("xai"), {
				TEXT("grok-3"),
				TEXT("grok-3-mini"),
				TEXT("grok-2-vision")
			}}
		};
		return Catalog;
	}

	void BuildModelOptionsForPreset(const FString& PresetId, const FString& FilterText, TArray<TSharedPtr<FString>>& OutOptions)
	{
		OutOptions.Reset();
		const TMap<FString, TArray<FString>>& Catalog = GetKnownProviderModels();
		const TArray<FString>* Models = Catalog.Find(PresetId);
		if (!Models)
		{
			return;
		}
		const FString Filter = FilterText.ToLower();
		for (const FString& Model : *Models)
		{
			if (!Filter.IsEmpty() && !Model.ToLower().Contains(Filter))
			{
				continue;
			}
			OutOptions.Add(MakeShared<FString>(Model));
		}
	}

	bool MigrateSettingsToV4IfNeeded(TSharedPtr<FJsonObject>& Root, IUnrealAiPersistence* Persist)
	{
		if (!Root.IsValid())
		{
			return false;
		}
		double Ver = 0;
		Root->TryGetNumberField(TEXT("version"), Ver);
		const TArray<TSharedPtr<FJsonValue>>* SecTest = nullptr;
		const bool bHasSections = Root->TryGetArrayField(TEXT("sections"), SecTest) && SecTest && SecTest->Num() > 0;
		if (Ver >= 4.0 && bHasSections)
		{
			return false;
		}

		TMap<FString, TSharedPtr<FJsonObject>> SecById;
		TArray<TSharedPtr<FJsonValue>> SectionOrder;

		const TArray<TSharedPtr<FJsonValue>>* ProvidersArr = nullptr;
		if (Root->TryGetArrayField(TEXT("providers"), ProvidersArr) && ProvidersArr && ProvidersArr->Num() > 0)
		{
			for (const TSharedPtr<FJsonValue>& V : *ProvidersArr)
			{
				const TSharedPtr<FJsonObject>* Po = nullptr;
				if (!V.IsValid() || !V->TryGetObject(Po) || !Po->IsValid())
				{
					continue;
				}
				FString Id, BaseUrl, ApiKey;
				(*Po)->TryGetStringField(TEXT("id"), Id);
				(*Po)->TryGetStringField(TEXT("baseUrl"), BaseUrl);
				(*Po)->TryGetStringField(TEXT("apiKey"), ApiKey);
				if (Id.IsEmpty())
				{
					continue;
				}
				TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
				S->SetStringField(TEXT("id"), Id);
				S->SetStringField(TEXT("label"), Id);
				S->SetStringField(TEXT("companyPreset"), GuessCompanyPresetFromUrl(BaseUrl));
				S->SetStringField(TEXT("baseUrl"), BaseUrl);
				S->SetStringField(TEXT("apiKey"), ApiKey);
				S->SetArrayField(TEXT("models"), TArray<TSharedPtr<FJsonValue>>());
				SecById.Add(Id, S);
				SectionOrder.Add(MakeShared<FJsonValueObject>(S.ToSharedRef()));
			}
		}
		else
		{
			const TSharedPtr<FJsonObject>* ApiObj = nullptr;
			FString BaseUrl = TEXT("https://api.openai.com/v1");
			FString ApiKey;
			if (Root->TryGetObjectField(TEXT("api"), ApiObj) && ApiObj->IsValid())
			{
				(*ApiObj)->TryGetStringField(TEXT("baseUrl"), BaseUrl);
				(*ApiObj)->TryGetStringField(TEXT("apiKey"), ApiKey);
			}
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetStringField(TEXT("id"), TEXT("default"));
			S->SetStringField(TEXT("label"), TEXT("Default"));
			S->SetStringField(TEXT("companyPreset"), GuessCompanyPresetFromUrl(BaseUrl));
			S->SetStringField(TEXT("baseUrl"), BaseUrl);
			S->SetStringField(TEXT("apiKey"), ApiKey);
			S->SetArrayField(TEXT("models"), TArray<TSharedPtr<FJsonValue>>());
			SecById.Add(TEXT("default"), S);
			SectionOrder.Add(MakeShared<FJsonValueObject>(S.ToSharedRef()));
		}

		FString DefaultProviderId;
		{
			const TSharedPtr<FJsonObject>* ApiObj = nullptr;
			if (Root->TryGetObjectField(TEXT("api"), ApiObj) && ApiObj->IsValid())
			{
				(*ApiObj)->TryGetStringField(TEXT("defaultProviderId"), DefaultProviderId);
			}
		}

		FString FirstSecId;
		if (SectionOrder.Num() > 0)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (SectionOrder[0].IsValid() && SectionOrder[0]->TryGetObject(O) && O->IsValid())
			{
				(*O)->TryGetStringField(TEXT("id"), FirstSecId);
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
				FString Pid;
				(*CapObj)->TryGetStringField(TEXT("providerId"), Pid);
				if (Pid.IsEmpty())
				{
					Pid = DefaultProviderId;
				}
				if (Pid.IsEmpty())
				{
					Pid = FirstSecId.IsEmpty() ? TEXT("default") : FirstSecId;
				}
				TSharedPtr<FJsonObject>* Found = SecById.Find(Pid);
				if (!Found || !Found->IsValid())
				{
					TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
					S->SetStringField(TEXT("id"), Pid);
					S->SetStringField(TEXT("label"), Pid);
					S->SetStringField(TEXT("companyPreset"), TEXT("custom"));
					S->SetStringField(TEXT("baseUrl"), TEXT(""));
					S->SetStringField(TEXT("apiKey"), TEXT(""));
					S->SetArrayField(TEXT("models"), TArray<TSharedPtr<FJsonValue>>());
					SecById.Add(Pid, S);
					SectionOrder.Add(MakeShared<FJsonValueObject>(S.ToSharedRef()));
					Found = SecById.Find(Pid);
				}
				Internal::AppendModelJsonToSection(ModelKey, *CapObj, *Found);
			}
		}

		Root->SetArrayField(TEXT("sections"), SectionOrder);
		Root->SetNumberField(TEXT("version"), 4);
		Root->RemoveField(TEXT("providers"));
		Root->RemoveField(TEXT("models"));

		if (Persist)
		{
			FString Out;
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
			if (FJsonSerializer::Serialize(Root.ToSharedRef(), W))
			{
				Persist->SaveSettingsJson(Out);
			}
		}
		return true;
	}
}
