#include "Tabs/SUnrealAiEditorSettingsTab.h"

#include "UnrealAiEditorModule.h"
#include "Style/UnrealAiEditorStyle.h"
#include "UnrealAiEditorSettings.h"
#include "Backend/FUnrealAiUsageTracker.h"
#include "Context/UnrealAiProjectId.h"
#include "Retrieval/IUnrealAiRetrievalService.h"
#include "Retrieval/UnrealAiRetrievalTypes.h"
#include "Backend/IUnrealAiModelConnector.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Framework/Docking/TabManager.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Memory/IUnrealAiMemoryService.h"
#include "Memory/UnrealAiMemoryTypes.h"
#include "Pricing/FUnrealAiModelPricingCatalog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SUnrealAiLocalJsonInspectorPanel.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

namespace UnrealAiVectorDbOverviewUi
{
	static FString FormatOverviewText(const FUnrealAiRetrievalVectorDbOverview& O)
	{
		FString S;
		S += TEXT("Local vector index — overview\n\n");
		S += TEXT(
			"What is indexed: UTF-8 text for whitelisted extensions under the chosen root preset, optional Asset Registry "
			"metadata shards (virtual paths), Blueprint-derived lines, and optionally memory chunks. Chat transcripts are not embedded here.\n");
		S += TEXT("At query time: embed the query → cosine Top-K in SQLite → lexical fallback when needed.\n\n");

		S += FString::Printf(TEXT("Retrieval enabled in settings: %s\n"), O.bRetrievalEnabledInSettings ? TEXT("yes") : TEXT("no"));
		S += FString::Printf(TEXT("Root preset: %s\n"), *O.RootPresetLabel);
		S += FString::Printf(
			TEXT("Indexed extensions (%d): %s\n"),
			O.IndexedExtensionCount,
			O.IndexedExtensionsPreview.IsEmpty() ? TEXT("(none)") : *O.IndexedExtensionsPreview);
		S += FString::Printf(TEXT("Asset Registry corpus: %s\n"), O.bAssetRegistryCorpusEnabled ? TEXT("on") : TEXT("off"));
		S += FString::Printf(TEXT("Memory chunks in index: %s\n"), O.bMemoryCorpusEnabled ? TEXT("on") : TEXT("off"));
		if (O.bBlueprintCapCustom)
		{
			S += FString::Printf(TEXT("Blueprint feature cap: %d\n"), O.BlueprintMaxFeatureRecords);
		}
		else
		{
			S += TEXT("Blueprint feature cap: default (extractor internal limit)\n");
		}
		S += LINE_TERMINATOR;

		if (!O.bStoreAvailable)
		{
			S += FString::Printf(
				TEXT("Index store: unavailable (%s)\n"),
				O.StoreError.IsEmpty() ? TEXT("unknown error") : *O.StoreError);
			return S;
		}

		S += TEXT("Index store: available\n");
		S += FString::Printf(TEXT("SQLite: %s\n"), *O.IndexDbPath);
		S += FString::Printf(TEXT("Manifest: %s\n"), *O.ManifestPath);
		S += FString::Printf(TEXT("Status: %s\n"), *O.ManifestStatus);
		if (!O.ManifestEmbeddingModel.IsEmpty())
		{
			S += FString::Printf(TEXT("Embedding model (manifest): %s\n"), *O.ManifestEmbeddingModel);
		}
		S += FString::Printf(TEXT("Migration state: %s\n"), *O.MigrationState);
		S += FString::Printf(TEXT("Rebuild in progress: %s\n"), O.bIndexBusy ? TEXT("yes") : TEXT("no"));
		S += FString::Printf(TEXT("Sources (rows) in DB: %d\n"), O.FilesIndexed);
		S += FString::Printf(TEXT("Chunks in DB: %d\n"), O.ChunksIndexed);
		if (O.LastFullScanUtc > FDateTime::MinValue())
		{
			S += FString::Printf(TEXT("Last full scan (UTC): %s\n"), *O.LastFullScanUtc.ToIso8601());
		}
		S += FString::Printf(TEXT("SQLite integrity check: %s\n"), O.bIntegrityOk ? TEXT("ok") : TEXT("failed"));
		if (!O.bIntegrityOk && !O.IntegrityError.IsEmpty())
		{
			S += FString::Printf(TEXT("Integrity detail: %s\n"), *O.IntegrityError);
		}
		if (O.TopSourcesByChunkCount.Num() > 0)
		{
			S += TEXT("\nTop sources by chunk count:\n");
			for (const TPair<FString, int32>& P : O.TopSourcesByChunkCount)
			{
				S += FString::Printf(TEXT("  %d  %s\n"), P.Value, *P.Key);
			}
		}
		return S;
	}
}

namespace UnrealAiSettingsTemplate
{
	static const TCHAR* DefaultSettingsJson = TEXT(
		"{\n"
		"\t\"version\": 4,\n"
		"\t\"api\": {\n"
		"\t\t\"baseUrl\": \"https://openrouter.ai/api/v1\",\n"
		"\t\t\"apiKey\": \"\",\n"
		"\t\t\"defaultModel\": \"openai/gpt-4o-mini\",\n"
		"\t\t\"defaultProviderId\": \"openrouter\"\n"
		"\t},\n"
		"\t\"sections\": [\n"
		"\t\t{\n"
		"\t\t\t\"id\": \"openrouter\",\n"
		"\t\t\t\"label\": \"OpenRouter\",\n"
		"\t\t\t\"companyPreset\": \"openrouter\",\n"
		"\t\t\t\"baseUrl\": \"https://openrouter.ai/api/v1\",\n"
		"\t\t\t\"apiKey\": \"\",\n"
		"\t\t\t\"models\": [\n"
		"\t\t\t\t{\n"
		"\t\t\t\t\t\"profileKey\": \"openai/gpt-4o-mini\",\n"
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
		"\t\t\"chunkChars\": 1200,\n"
		"\t\t\"chunkOverlap\": 200,\n"
		"\t\t\"assetRegistryMaxAssets\": 0,\n"
		"\t\t\"assetRegistryIncludeEngineAssets\": false,\n"
		"\t\t\"embeddingBatchSize\": 8,\n"
		"\t\t\"minDelayMsBetweenEmbeddingBatches\": 0,\n"
		"\t\t\"indexMemoryRecordsInVectorStore\": false,\n"
		"\t\t\"blueprintMaxFeatureRecords\": 0\n"
		"\t},\n"
		"\t\"agent\": {\n"
		"\t\t\"useSubagents\": true,\n"
		"\t\t\"codeTypePreference\": \"auto\",\n"
		"\t\t\"autoConfirmDestructive\": true\n"
		"\t},\n"
		"\t\"ui\": {\n"
		"\t\t\"editorFocus\": false\n"
		"\t}\n"
		"}\n");
}

namespace UnrealAiSettingsTabUtil
{
	static TSharedPtr<FJsonObject> CloneJsonObject(const TSharedPtr<FJsonObject>& Src)
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

	static bool DeserializeRoot(const FString& Json, TSharedPtr<FJsonObject>& OutRoot)
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutRoot) && OutRoot.IsValid();
	}

	static int32 ParsePositiveInt(const FString& S, int32 Default)
	{
		if (S.IsEmpty())
		{
			return Default;
		}
		return FMath::Max(0, FCString::Atoi(*S));
	}

	static FString GuessCompanyPresetFromUrl(const FString& Url)
	{
		const FString L = Url.ToLower();
		if (L.Contains(TEXT("openrouter")))
		{
			return TEXT("openrouter");
		}
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

	static const TMap<FString, TArray<FString>>& GetKnownProviderModels()
	{
		static const TMap<FString, TArray<FString>> Catalog = {
			{ TEXT("openrouter"), {
				TEXT("openai/gpt-5"),
				TEXT("openai/gpt-5-mini"),
				TEXT("openai/gpt-4o"),
				TEXT("openai/gpt-4o-mini"),
				TEXT("anthropic/claude-3.7-sonnet"),
				TEXT("anthropic/claude-3.5-sonnet"),
				TEXT("google/gemini-2.5-pro"),
				TEXT("google/gemini-2.5-flash"),
				TEXT("meta-llama/llama-3.3-70b-instruct"),
				TEXT("x-ai/grok-3-mini")
			}},
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

	static void BuildModelOptionsForPreset(const FString& PresetId, const FString& FilterText, TArray<TSharedPtr<FString>>& OutOptions)
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

	static bool MigrateSettingsToV4IfNeeded(TSharedPtr<FJsonObject>& Root, IUnrealAiPersistence* Persist)
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
			FString BaseUrl = TEXT("https://openrouter.ai/api/v1");
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
				AppendModelJsonToSection(ModelKey, *CapObj, *Found);
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

namespace
{
	static FString MemoryGenStateToString(const EUnrealAiMemoryGenerationState S)
	{
		switch (S)
		{
		case EUnrealAiMemoryGenerationState::Running: return TEXT("running");
		case EUnrealAiMemoryGenerationState::Success: return TEXT("success");
		case EUnrealAiMemoryGenerationState::Failed: return TEXT("failed");
		default: return TEXT("idle");
		}
	}

	static FString MemoryGenErrToString(const EUnrealAiMemoryGenerationErrorCode E)
	{
		switch (E)
		{
		case EUnrealAiMemoryGenerationErrorCode::MissingApiKey: return TEXT("missing_api_key");
		case EUnrealAiMemoryGenerationErrorCode::ProviderUnavailable: return TEXT("provider_unavailable");
		case EUnrealAiMemoryGenerationErrorCode::InvalidResponse: return TEXT("invalid_response");
		case EUnrealAiMemoryGenerationErrorCode::RateLimited: return TEXT("rate_limited");
		case EUnrealAiMemoryGenerationErrorCode::Unknown: return TEXT("unknown");
		default: return TEXT("none");
		}
	}
}

void SUnrealAiEditorSettingsTab::Construct(const FArguments& InArgs)
{
	BackendRegistry = InArgs._BackendRegistry;
	StatusText = LOCTEXT("StatusIdle", "");

	CompanyPresetOptions.Add(MakeShared<FString>(TEXT("custom")));
	CompanyPresetOptions.Add(MakeShared<FString>(TEXT("openrouter")));
	CompanyPresetOptions.Add(MakeShared<FString>(TEXT("openai")));
	CompanyPresetOptions.Add(MakeShared<FString>(TEXT("anthropic")));
	CompanyPresetOptions.Add(MakeShared<FString>(TEXT("groq")));
	CompanyPresetOptions.Add(MakeShared<FString>(TEXT("deepseek")));
	CompanyPresetOptions.Add(MakeShared<FString>(TEXT("xai")));
	CompanyPresetOptions.Add(MakeShared<FString>(TEXT("azure")));

	RetrievalRootPresetOptions.Add(MakeShared<FString>(TEXT("minimal")));
	RetrievalRootPresetOptions.Add(MakeShared<FString>(TEXT("standard")));
	RetrievalRootPresetOptions.Add(MakeShared<FString>(TEXT("extended")));

	CodeTypePreferenceOptions.Add(MakeShared<FString>(TEXT("auto")));
	CodeTypePreferenceOptions.Add(MakeShared<FString>(TEXT("blueprint_first")));
	CodeTypePreferenceOptions.Add(MakeShared<FString>(TEXT("cpp_first")));
	CodeTypePreferenceOptions.Add(MakeShared<FString>(TEXT("blueprint_only")));
	CodeTypePreferenceOptions.Add(MakeShared<FString>(TEXT("cpp_only")));

	ChildSlot
		[SNew(SBorder)
				.Padding(FMargin(12.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
					[
						SAssignNew(UsageSummaryBlock, STextBlock)
							.Font(FUnrealAiEditorStyle::FontBodySmall())
							.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
							.WrapTextAt(560.f)
							.Text(LOCTEXT("UsagePlaceholder", "Usage: loading…"))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
					[
						SNew(STextBlock)
							.Text(LOCTEXT("SettingsTitle", "AI Settings"))
							.Font(FUnrealAiEditorStyle::FontWindowTitle())
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
						[
							SNew(SButton)
								.Text(LOCTEXT("SettingsSegConfig", "Configuration"))
								.OnClicked_Lambda([this]()
								{
									return OnSettingsSegmentClicked(0);
								})
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton)
								.Text(LOCTEXT("SettingsChatHistory", "Chat history"))
								.OnClicked_Lambda([this]()
								{
									return OnSettingsSegmentClicked(1);
								})
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(8.f, 0.f, 0.f, 0.f))
						[
							SNew(SButton)
								.Text(LOCTEXT("SettingsMemories", "Memories"))
								.OnClicked_Lambda([this]()
								{
									return OnSettingsSegmentClicked(2);
								})
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(8.f, 0.f, 0.f, 0.f))
						[
							SNew(SButton)
								.Text(LOCTEXT("SettingsVectorDb", "Vector DB"))
								.OnClicked_Lambda([this]()
								{
									return OnSettingsSegmentClicked(3);
								})
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(8.f, 0.f, 0.f, 0.f))
						[
							SNew(SButton)
								.Text(LOCTEXT("SettingsContext", "Context"))
								.ToolTipText(LOCTEXT(
									"SettingsContextTip",
									"Inspect per-chat context.json / conversation.json and related local thread artifacts."))
								.OnClicked_Lambda([this]()
								{
									return OnSettingsSegmentClicked(4);
								})
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(8.f, 0.f, 0.f, 0.f))
						[
							SNew(SButton)
								.Text(LOCTEXT("SettingsProjectIndex", "Project index"))
								.ToolTipText(LOCTEXT(
									"SettingsProjectIndexTip",
									"Inspect threads_index.json and drill into per-thread context artifacts."))
								.OnClicked_Lambda([this]()
								{
									return OnSettingsSegmentClicked(5);
								})
						]
					]
					+ SVerticalBox::Slot().FillHeight(1.f).Padding(FMargin(0.f, 4.f))
					[
						SAssignNew(SettingsMainSwitcher, SWidgetSwitcher)
						+ SWidgetSwitcher::Slot()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
							[
								SNew(STextBlock)
									.AutoWrapText(true)
									.Text(LOCTEXT(
										"SettingsHelp",
										"Add API key sections (one provider account each) and attach any number of model profiles. "
										"Use company preset + searchable model picker to choose known OpenAI-compatible ids quickly; "
										"you can still edit model ids manually. "
										"Settings save automatically; use Save and apply for an immediate LLM reload."))
							]
							+ SVerticalBox::Slot().FillHeight(1.f)
							[
								SNew(SBox)
									.MinDesiredHeight(280.f)
									[
										SNew(SScrollBox)
										+ SScrollBox::Slot()
										[
											SNew(SVerticalBox)
											+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 12.f))
											[
												SNew(STextBlock)
													.Font(FUnrealAiEditorStyle::FontSectionHeading())
													.Text(LOCTEXT("SecGlobal", "Global API defaults"))
											]
											+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 12.f))
											[
												SNew(SGridPanel).FillColumn(1, 1.f)
												+ SGridPanel::Slot(0, 0).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("BaseUrl", "Base URL"))
												]
												+ SGridPanel::Slot(1, 0).Padding(4.f)
												[
													SAssignNew(ApiBaseUrlBox, SEditableTextBox)
														.Text(FText::FromString(TEXT("https://openrouter.ai/api/v1")))
														.OnTextChanged(this, &SUnrealAiEditorSettingsTab::OnAnySettingsChanged)
												]
												+ SGridPanel::Slot(0, 1).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("ApiKeyGlobal", "Global API key (optional)"))
												]
												+ SGridPanel::Slot(1, 1).Padding(4.f)
												[
													SAssignNew(ApiKeyBox, SEditableTextBox)
														.OnTextChanged(this, &SUnrealAiEditorSettingsTab::OnAnySettingsChanged)
												]
												+ SGridPanel::Slot(0, 2).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("DefaultModel", "Default model profile key"))
												]
												+ SGridPanel::Slot(1, 2).Padding(4.f)
												[
													SAssignNew(ApiDefaultModelBox, SEditableTextBox)
														.Text(FText::FromString(TEXT("openai/gpt-4o-mini")))
														.OnTextChanged(this, &SUnrealAiEditorSettingsTab::OnAnySettingsChanged)
												]
												+ SGridPanel::Slot(0, 3).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("DefaultProviderId", "Default section id"))
												]
												+ SGridPanel::Slot(1, 3).Padding(4.f)
												[
													SNew(SHorizontalBox)
													+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
													[
														SAssignNew(ApiDefaultProviderIdBox, SEditableTextBox)
															.OnTextChanged(this, &SUnrealAiEditorSettingsTab::OnAnySettingsChanged)
													]
													+ SHorizontalBox::Slot().AutoWidth()
													[
														SAssignNew(ApiDefaultProviderDropdown, SComboBox<TSharedPtr<FString>>)
															.OptionsSource(&ProviderIdOptions)
															.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
															{
																return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : FString()));
															})
															.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Sel, ESelectInfo::Type)
															{
																if (!Sel.IsValid() || !ApiDefaultProviderIdBox.IsValid())
																{
																	return;
																}
																ApiDefaultProviderIdBox->SetText(FText::FromString(*Sel));
																OnAnySettingsChanged(FText::GetEmpty());
															})
															.Content()
															[
																SNew(STextBlock)
																	.Text(LOCTEXT("DefaultProviderDropdownLabel", "Pick section"))
															]
													]
												]
											]
											+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 12.f))
											[
												SNew(SHorizontalBox)
												+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
												[
													SNew(SCheckBox).Style(&FUnrealAiEditorStyle::GetCheckboxStyle())
														.IsChecked_Lambda([]()
														{
															return FUnrealAiEditorModule::IsSubagentsEnabled()
																? ECheckBoxState::Checked
																: ECheckBoxState::Unchecked;
														})
														.OnCheckStateChanged_Lambda([](ECheckBoxState S)
														{
															FUnrealAiEditorModule::SetSubagentsEnabled(S == ECheckBoxState::Checked);
														})
												]
												+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
												[
													SNew(STextBlock)
														.AutoWrapText(true)
														.WrapTextAt(520.f)
														.Text(LOCTEXT(
															"UseSubagentsHelp",
															"Use subagents: when on, plan execution may schedule independent ready nodes as subagent waves (guarded policy). When off, plan nodes run serially."))
												]
											]
											+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 12.f))
											[
												SNew(SHorizontalBox)
												+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
												[
													SNew(STextBlock)
														.Text(LOCTEXT("CodeTypePrefLabel", "Code type preference"))
												]
												+ SHorizontalBox::Slot().FillWidth(1.f)
												[
													SAssignNew(CodeTypePreferenceCombo, SComboBox<TSharedPtr<FString>>)
														.OptionsSource(&CodeTypePreferenceOptions)
														.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
														{
															return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : FString()));
														})
														.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Sel, ESelectInfo::Type)
														{
															if (Sel.IsValid())
															{
																FUnrealAiEditorModule::SetAgentCodeTypePreference(*Sel);
																OnAnySettingsChanged(FText::GetEmpty());
															}
														})
														.Content()
														[
															SNew(STextBlock)
																.Text_Lambda([]()
																{
																	return FText::FromString(FUnrealAiEditorModule::GetAgentCodeTypePreference());
																})
														]
												]
											]
											+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 12.f))
											[
												SNew(SHorizontalBox)
												+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
												[
													SNew(SCheckBox).Style(&FUnrealAiEditorStyle::GetCheckboxStyle())
														.IsChecked_Lambda([]()
														{
															return FUnrealAiEditorModule::IsAutoConfirmDestructiveEnabled()
																? ECheckBoxState::Checked
																: ECheckBoxState::Unchecked;
														})
														.OnCheckStateChanged_Lambda([](ECheckBoxState S)
														{
															FUnrealAiEditorModule::SetAutoConfirmDestructiveEnabled(S == ECheckBoxState::Checked);
														})
												]
												+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
												[
													SNew(STextBlock)
														.AutoWrapText(true)
														.WrapTextAt(520.f)
														.Text(LOCTEXT(
															"AutoConfirmDestructiveHelp",
															"When on, destructive tools that require confirm default to confirmed so the model is not slowed by a retry. Turn off only if you want strict manual confirmation in tool args."))
												]
											]
											+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
											[
												SNew(STextBlock)
													.Font(FUnrealAiEditorStyle::FontSectionHeading())
													.Text(LOCTEXT("SecEditorUi", "Editor integration"))
											]
											+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 12.f))
											[
												SNew(SHorizontalBox)
												+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
												[
													SNew(SCheckBox).Style(&FUnrealAiEditorStyle::GetCheckboxStyle())
														.IsChecked_Lambda([]()
														{
															return FUnrealAiEditorModule::IsEditorFocusEnabled()
																? ECheckBoxState::Checked
																: ECheckBoxState::Unchecked;
														})
														.OnCheckStateChanged_Lambda([](ECheckBoxState S)
														{
															FUnrealAiEditorModule::SetEditorFocusEnabled(S == ECheckBoxState::Checked);
														})
												]
												+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
												[
													SNew(STextBlock)
														.AutoWrapText(true)
														.WrapTextAt(520.f)
														.Text(LOCTEXT(
															"EditorFocusHelp",
															"Editor focus: when on, the agent may follow along in the editor (Content Browser sync, viewport framing, post-tool navigation). When off, optional UI is skipped; tools that must open an editor still open."))
												]
											]
											+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
											[
												SNew(STextBlock)
													.Font(FUnrealAiEditorStyle::FontSectionHeading())
													.Text(LOCTEXT("SecRetrieval", "Retrieval (local vector index)"))
											]
											+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 12.f))
											[
												SNew(SGridPanel).FillColumn(1, 1.f)
												+ SGridPanel::Slot(0, 0).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalEnabledLbl", "Enabled"))
												]
												+ SGridPanel::Slot(1, 0).Padding(4.f)
												[
													SNew(SCheckBox).Style(&FUnrealAiEditorStyle::GetCheckboxStyle())
														.IsChecked_Lambda([this]() { return bRetrievalEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
														.OnCheckStateChanged_Lambda([this](ECheckBoxState S)
														{
															bRetrievalEnabled = (S == ECheckBoxState::Checked);
															OnAnySettingsChanged(FText::GetEmpty());
														})
												]
												+ SGridPanel::Slot(0, 1).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalModelLbl", "Embedding model"))
												]
												+ SGridPanel::Slot(1, 1).Padding(4.f)
												[
													SNew(SEditableTextBox)
														.Text_Lambda([this]() { return FText::FromString(RetrievalEmbeddingModel); })
														.OnTextChanged_Lambda([this](const FText& T)
														{
															RetrievalEmbeddingModel = T.ToString();
															OnAnySettingsChanged(T);
														})
												]
												+ SGridPanel::Slot(0, 2).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalMaxSnipsLbl", "Max snippets per turn"))
												]
												+ SGridPanel::Slot(1, 2).Padding(4.f)
												[
													SNew(SEditableTextBox)
														.Text_Lambda([this]() { return FText::FromString(RetrievalMaxSnippetsPerTurnStr); })
														.OnTextChanged_Lambda([this](const FText& T)
														{
															RetrievalMaxSnippetsPerTurnStr = T.ToString();
															OnAnySettingsChanged(T);
														})
												]
												+ SGridPanel::Slot(0, 3).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalMaxTokLbl", "Max snippet tokens"))
												]
												+ SGridPanel::Slot(1, 3).Padding(4.f)
												[
													SNew(SEditableTextBox)
														.Text_Lambda([this]() { return FText::FromString(RetrievalMaxSnippetTokensStr); })
														.OnTextChanged_Lambda([this](const FText& T)
														{
															RetrievalMaxSnippetTokensStr = T.ToString();
															OnAnySettingsChanged(T);
														})
												]
												+ SGridPanel::Slot(0, 4).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalAutoIdxLbl", "Auto-index on project open"))
												]
												+ SGridPanel::Slot(1, 4).Padding(4.f)
												[
													SNew(SCheckBox).Style(&FUnrealAiEditorStyle::GetCheckboxStyle())
														.IsChecked_Lambda([this]() { return bRetrievalAutoIndexOnProjectOpen ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
														.OnCheckStateChanged_Lambda([this](ECheckBoxState S)
														{
															bRetrievalAutoIndexOnProjectOpen = (S == ECheckBoxState::Checked);
															OnAnySettingsChanged(FText::GetEmpty());
														})
												]
												+ SGridPanel::Slot(0, 5).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalScrubLbl", "Periodic scrub minutes"))
												]
												+ SGridPanel::Slot(1, 5).Padding(4.f)
												[
													SNew(SEditableTextBox)
														.Text_Lambda([this]() { return FText::FromString(RetrievalPeriodicScrubMinutesStr); })
														.OnTextChanged_Lambda([this](const FText& T)
														{
															RetrievalPeriodicScrubMinutesStr = T.ToString();
															OnAnySettingsChanged(T);
														})
												]
												+ SGridPanel::Slot(0, 6).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalMixedCompatLbl", "Allow mixed model compatibility"))
												]
												+ SGridPanel::Slot(1, 6).Padding(4.f)
												[
													SNew(SCheckBox).Style(&FUnrealAiEditorStyle::GetCheckboxStyle())
														.IsChecked_Lambda([this]() { return bRetrievalAllowMixedModelCompatibility ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
														.OnCheckStateChanged_Lambda([this](ECheckBoxState S)
														{
															bRetrievalAllowMixedModelCompatibility = (S == ECheckBoxState::Checked);
															OnAnySettingsChanged(FText::GetEmpty());
														})
												]
												+ SGridPanel::Slot(0, 7).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalRootPresetLbl", "Index root preset"))
												]
												+ SGridPanel::Slot(1, 7).Padding(4.f)
												[
													SAssignNew(RetrievalRootPresetCombo, SComboBox<TSharedPtr<FString>>)
														.OptionsSource(&RetrievalRootPresetOptions)
														.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
														{
															return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : FString()));
														})
														.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Sel, ESelectInfo::Type)
														{
															if (Sel.IsValid())
															{
																RetrievalRootPresetStr = *Sel;
																OnAnySettingsChanged(FText::GetEmpty());
															}
														})
														.Content()
														[
															SNew(STextBlock)
																.Text_Lambda([this]()
																{
																	return FText::FromString(RetrievalRootPresetStr);
																})
														]
												]
												+ SGridPanel::Slot(0, 8).Padding(4.f).VAlign(VAlign_Top)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalIndexedExtLbl", "Indexed extensions (whitelist)"))
												]
												+ SGridPanel::Slot(1, 8).Padding(4.f)
												[
													SNew(SBox).MinDesiredHeight(72.f)
													[
														SAssignNew(RetrievalIndexedExtensionsBox, SMultiLineEditableTextBox)
															.AutoWrapText(true)
															.OnTextChanged_Lambda([this](const FText& T)
															{
																RetrievalIndexedExtensionsText = T.ToString();
																OnAnySettingsChanged(T);
															})
													]
												]
												+ SGridPanel::Slot(0, 9).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalMaxFilesLbl", "Max files per rebuild (0=unlimited)"))
												]
												+ SGridPanel::Slot(1, 9).Padding(4.f)
												[
													SNew(SEditableTextBox)
														.Text_Lambda([this]() { return FText::FromString(RetrievalMaxFilesPerRebuildStr); })
														.OnTextChanged_Lambda([this](const FText& T)
														{
															RetrievalMaxFilesPerRebuildStr = T.ToString();
															OnAnySettingsChanged(T);
														})
												]
												+ SGridPanel::Slot(0, 10).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalMaxChunksLbl", "Max chunks per rebuild (0=unlimited)"))
												]
												+ SGridPanel::Slot(1, 10).Padding(4.f)
												[
													SNew(SEditableTextBox)
														.Text_Lambda([this]() { return FText::FromString(RetrievalMaxTotalChunksPerRebuildStr); })
														.OnTextChanged_Lambda([this](const FText& T)
														{
															RetrievalMaxTotalChunksPerRebuildStr = T.ToString();
															OnAnySettingsChanged(T);
														})
												]
												+ SGridPanel::Slot(0, 11).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalMaxEmbLbl", "Max embedding calls per rebuild (0=unlimited)"))
												]
												+ SGridPanel::Slot(1, 11).Padding(4.f)
												[
													SNew(SEditableTextBox)
														.Text_Lambda([this]() { return FText::FromString(RetrievalMaxEmbeddingCallsPerRebuildStr); })
														.OnTextChanged_Lambda([this](const FText& T)
														{
															RetrievalMaxEmbeddingCallsPerRebuildStr = T.ToString();
															OnAnySettingsChanged(T);
														})
												]
												+ SGridPanel::Slot(0, 12).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalChunkCharsLbl", "Chunk chars / overlap"))
												]
												+ SGridPanel::Slot(1, 12).Padding(4.f)
												[
													SNew(SHorizontalBox)
													+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
													[
														SNew(SEditableTextBox)
															.HintText(LOCTEXT("RetrievalChunkCharsHint", "Chars"))
															.Text_Lambda([this]() { return FText::FromString(RetrievalChunkCharsStr); })
															.OnTextChanged_Lambda([this](const FText& T)
															{
																RetrievalChunkCharsStr = T.ToString();
																OnAnySettingsChanged(T);
															})
													]
													+ SHorizontalBox::Slot().FillWidth(1.f)
													[
														SNew(SEditableTextBox)
															.HintText(LOCTEXT("RetrievalChunkOvHint", "Overlap"))
															.Text_Lambda([this]() { return FText::FromString(RetrievalChunkOverlapStr); })
															.OnTextChanged_Lambda([this](const FText& T)
															{
																RetrievalChunkOverlapStr = T.ToString();
																OnAnySettingsChanged(T);
															})
													]
												]
												+ SGridPanel::Slot(0, 13).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalArMaxLbl", "Asset registry max assets (0=off)"))
												]
												+ SGridPanel::Slot(1, 13).Padding(4.f)
												[
													SNew(SHorizontalBox)
													+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
													[
														SNew(SEditableTextBox)
															.Text_Lambda([this]() { return FText::FromString(RetrievalAssetRegistryMaxAssetsStr); })
															.OnTextChanged_Lambda([this](const FText& T)
															{
																RetrievalAssetRegistryMaxAssetsStr = T.ToString();
																OnAnySettingsChanged(T);
															})
													]
													+ SHorizontalBox::Slot().AutoWidth()
													[
														SNew(SCheckBox).Style(&FUnrealAiEditorStyle::GetCheckboxStyle())
															.Content()
															[
																SNew(STextBlock).Text(LOCTEXT("RetrievalArEngineLbl", "Include /Engine"))
															]
															.IsChecked_Lambda([this]()
															{
																return bRetrievalAssetRegistryIncludeEngineAssets ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
															})
															.OnCheckStateChanged_Lambda([this](ECheckBoxState S)
															{
																bRetrievalAssetRegistryIncludeEngineAssets = (S == ECheckBoxState::Checked);
																OnAnySettingsChanged(FText::GetEmpty());
															})
													]
												]
												+ SGridPanel::Slot(0, 14).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalEmbedThrottleLbl", "Embed batch size / delay ms"))
												]
												+ SGridPanel::Slot(1, 14).Padding(4.f)
												[
													SNew(SHorizontalBox)
													+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
													[
														SNew(SEditableTextBox)
															.HintText(LOCTEXT("RetrievalBatchHint", "Batch"))
															.Text_Lambda([this]() { return FText::FromString(RetrievalEmbeddingBatchSizeStr); })
															.OnTextChanged_Lambda([this](const FText& T)
															{
																RetrievalEmbeddingBatchSizeStr = T.ToString();
																OnAnySettingsChanged(T);
															})
													]
													+ SHorizontalBox::Slot().FillWidth(1.f)
													[
														SNew(SEditableTextBox)
															.HintText(LOCTEXT("RetrievalDelayHint", "Delay ms"))
															.Text_Lambda([this]() { return FText::FromString(RetrievalMinDelayMsBetweenBatchesStr); })
															.OnTextChanged_Lambda([this](const FText& T)
															{
																RetrievalMinDelayMsBetweenBatchesStr = T.ToString();
																OnAnySettingsChanged(T);
															})
													]
												]
												+ SGridPanel::Slot(0, 15).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalMemVecLbl", "Index memory records in vector store"))
												]
												+ SGridPanel::Slot(1, 15).Padding(4.f)
												[
													SNew(SCheckBox).Style(&FUnrealAiEditorStyle::GetCheckboxStyle())
														.IsChecked_Lambda([this]()
														{
															return bRetrievalIndexMemoryRecordsInVectorStore ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
														})
														.OnCheckStateChanged_Lambda([this](ECheckBoxState S)
														{
															bRetrievalIndexMemoryRecordsInVectorStore = (S == ECheckBoxState::Checked);
															OnAnySettingsChanged(FText::GetEmpty());
														})
												]
												+ SGridPanel::Slot(0, 16).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("RetrievalBpMaxLbl", "Blueprint feature cap (0=default)"))
												]
												+ SGridPanel::Slot(1, 16).Padding(4.f)
												[
													SNew(SEditableTextBox)
														.Text_Lambda([this]() { return FText::FromString(RetrievalBlueprintMaxFeatureRecordsStr); })
														.OnTextChanged_Lambda([this](const FText& T)
														{
															RetrievalBlueprintMaxFeatureRecordsStr = T.ToString();
															OnAnySettingsChanged(T);
														})
												]
											]
											+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 12.f))
											[
												SNew(SButton)
													.Text(LOCTEXT("RetrievalRebuildNowBtn", "Rebuild retrieval index now"))
													.OnClicked(this, &SUnrealAiEditorSettingsTab::OnRetrievalRebuildNowClicked)
											]
											+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
											[
												SNew(SHorizontalBox)
												+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
												[
													SNew(STextBlock)
														.Font(FUnrealAiEditorStyle::FontSectionHeading())
														.Text(LOCTEXT("SecSections", "API key sections"))
												]
												+ SHorizontalBox::Slot().AutoWidth()
												[
													SNew(SButton)
														.Text(LOCTEXT("AddSection", "Add section"))
														.OnClicked(this, &SUnrealAiEditorSettingsTab::OnAddSectionClicked)
												]
											]
											+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 12.f))
											[
												SAssignNew(SectionsVBox, SVerticalBox)
											]
										]
									]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 4.f))
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
								[
									SNew(SButton)
										.Text(LOCTEXT("Test", "Test connection"))
										.OnClicked(this, &SUnrealAiEditorSettingsTab::OnTestConnectionClicked)
								]
								+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
								[
									SNew(SButton)
										.Text(LOCTEXT("SaveSettings", "Save and apply"))
										.OnClicked(this, &SUnrealAiEditorSettingsTab::OnSaveClicked)
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock)
									.AutoWrapText(true)
									.Text_Lambda([this]() { return StatusText; })
							]
						]
						+ SWidgetSwitcher::Slot()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
							[
								SNew(STextBlock)
									.AutoWrapText(true)
									.Text(LOCTEXT(
										"ChatHistoryHelp",
										"Open a past chat in a new Agent Chat tab. Threads are listed from your local thread index."))
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
							[
								SNew(SButton)
									.Text(LOCTEXT("ChatHistoryRefresh", "Refresh list"))
									.OnClicked(this, &SUnrealAiEditorSettingsTab::OnChatHistoryRefreshClicked)
							]
							+ SVerticalBox::Slot().FillHeight(1.f)
							[
								SNew(SSplitter)
								.Orientation(Orient_Horizontal)
								+ SSplitter::Slot()
								.Value(0.35f)
								[
									SNew(SScrollBox)
									+ SScrollBox::Slot()
									[
										SAssignNew(ChatHistoryListVBox, SVerticalBox)
									]
								]
								+ SSplitter::Slot()
								.Value(0.65f)
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
									[
										SNew(STextBlock)
											.Font(FUnrealAiEditorStyle::FontSectionHeading())
											.Text(LOCTEXT("ChatHistoryInspectorLbl", "Selected thread details"))
									]
									+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
										[
											SNew(SButton)
												.Text(LOCTEXT("ChatLoadCtxBtn", "Load context.json"))
												.OnClicked_Lambda([this]()
												{
													LoadSelectedChatHistoryContextJson();
													return FReply::Handled();
												})
										]
										+ SHorizontalBox::Slot().AutoWidth()
										[
											SNew(SButton)
												.Text(LOCTEXT("ChatLoadConvBtn", "Load conversation.json"))
												.OnClicked_Lambda([this]()
												{
													LoadSelectedChatHistoryConversationJson();
													return FReply::Handled();
												})
										]
									]
									+ SVerticalBox::Slot().FillHeight(1.f)
									[
										SAssignNew(ChatHistoryInspectorPanel, SUnrealAiLocalJsonInspectorPanel)
									]
								]
							]
						]
						+ SWidgetSwitcher::Slot()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
							[
								SNew(STextBlock)
									.AutoWrapText(true)
									.Text(LOCTEXT(
										"MemoriesHelp",
										"Memories are local-first durable lessons. Use this panel to review, inspect, and delete records."))
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
							[
								SAssignNew(MemoryGenerationStatusBlock, STextBlock)
									.AutoWrapText(true)
									.Text(LOCTEXT("MemoryGenStatusPlaceholder", "Memory generation status: loading..."))
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
							[
								SNew(SGridPanel).FillColumn(1, 1.f)
								+ SGridPanel::Slot(0, 0).Padding(4.f)
								[
									SNew(STextBlock).Text(LOCTEXT("MemoryEnabledLbl", "Enable memories"))
								]
								+ SGridPanel::Slot(1, 0).Padding(4.f)
								[
									SNew(SCheckBox).Style(&FUnrealAiEditorStyle::GetCheckboxStyle())
										.IsChecked_Lambda([this]() { return bMemoryEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
										.OnCheckStateChanged_Lambda([this](ECheckBoxState S)
										{
											bMemoryEnabled = (S == ECheckBoxState::Checked);
											OnAnySettingsChanged(FText::GetEmpty());
										})
								]
								+ SGridPanel::Slot(0, 1).Padding(4.f)
								[
									SNew(STextBlock).Text(LOCTEXT("MemoryAutoExtractLbl", "Auto extract"))
								]
								+ SGridPanel::Slot(1, 1).Padding(4.f)
								[
									SNew(SCheckBox).Style(&FUnrealAiEditorStyle::GetCheckboxStyle())
										.IsChecked_Lambda([this]() { return bMemoryAutoExtract ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
										.OnCheckStateChanged_Lambda([this](ECheckBoxState S)
										{
											bMemoryAutoExtract = (S == ECheckBoxState::Checked);
											OnAnySettingsChanged(FText::GetEmpty());
										})
								]
								+ SGridPanel::Slot(0, 2).Padding(4.f)
								[
									SNew(STextBlock).Text(LOCTEXT("MemoryMaxItemsLbl", "Max items"))
								]
								+ SGridPanel::Slot(1, 2).Padding(4.f)
								[
									SAssignNew(MemoryMaxItemsBox, SEditableTextBox)
										.Text_Lambda([this]() { return FText::FromString(MemoryMaxItemsStr); })
										.OnTextChanged_Lambda([this](const FText& T)
										{
											MemoryMaxItemsStr = T.ToString();
											OnAnySettingsChanged(T);
										})
								]
								+ SGridPanel::Slot(0, 3).Padding(4.f)
								[
									SNew(STextBlock).Text(LOCTEXT("MemoryMinConfidenceLbl", "Min confidence"))
								]
								+ SGridPanel::Slot(1, 3).Padding(4.f)
								[
									SAssignNew(MemoryMinConfidenceBox, SEditableTextBox)
										.Text_Lambda([this]() { return FText::FromString(MemoryMinConfidenceStr); })
										.OnTextChanged_Lambda([this](const FText& T)
										{
											MemoryMinConfidenceStr = T.ToString();
											OnAnySettingsChanged(T);
										})
								]
								+ SGridPanel::Slot(0, 4).Padding(4.f)
								[
									SNew(STextBlock).Text(LOCTEXT("MemoryRetentionLbl", "Retention days"))
								]
								+ SGridPanel::Slot(1, 4).Padding(4.f)
								[
									SAssignNew(MemoryRetentionDaysBox, SEditableTextBox)
										.Text_Lambda([this]() { return FText::FromString(MemoryRetentionDaysStr); })
										.OnTextChanged_Lambda([this](const FText& T)
										{
											MemoryRetentionDaysStr = T.ToString();
											OnAnySettingsChanged(T);
										})
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 8.f))
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
								[
									SAssignNew(MemorySearchBox, SEditableTextBox)
										.HintText(LOCTEXT("MemorySearchHint", "Search memory title/description/tags"))
										.OnTextChanged_Lambda([this](const FText&)
										{
											RebuildMemoryListUi();
										})
								]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(SButton)
										.Text(LOCTEXT("MemoriesRefresh", "Refresh"))
										.OnClicked(this, &SUnrealAiEditorSettingsTab::OnMemoriesRefreshClicked)
								]
							]
							+ SVerticalBox::Slot().FillHeight(1.f)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
								[
									SNew(SScrollBox)
									+ SScrollBox::Slot()
									[
										SAssignNew(MemoryListVBox, SVerticalBox)
									]
								]
								+ SHorizontalBox::Slot().FillWidth(1.f)
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
									[
										SAssignNew(MemorySelectedTitleBox, SEditableTextBox).IsReadOnly(true)
									]
									+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
									[
										SAssignNew(MemorySelectedDescriptionBox, SEditableTextBox).IsReadOnly(true)
									]
									+ SVerticalBox::Slot().FillHeight(1.f)
									[
										SAssignNew(MemorySelectedBodyBox, SEditableTextBox).IsReadOnly(true)
									]
								]
							]
						]
						+ SWidgetSwitcher::Slot()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
							[
								SNew(STextBlock)
									.AutoWrapText(true)
									.Text(LOCTEXT(
										"VectorDbOverviewHelp",
										"Read-only snapshot of the local vector index: what the indexer is configured to ingest, on-disk paths, "
										"chunk/source counts, integrity, and top indexed sources. Tune ingestion under Configuration → Retrieval."))
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
								[
									SNew(SButton)
										.Text(LOCTEXT("VectorDbOverviewRefresh", "Refresh overview"))
										.OnClicked(this, &SUnrealAiEditorSettingsTab::OnVectorDbOverviewRefreshClicked)
								]
							]
							+ SVerticalBox::Slot().FillHeight(1.f)
							[
								SNew(SSplitter)
								.Orientation(Orient_Horizontal)
								+ SSplitter::Slot()
								.Value(0.38f)
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
									[
										SAssignNew(VectorDbGraphSearchBox, SEditableTextBox)
											.HintText(LOCTEXT("VectorDbGraphSearchHint", "Search source paths (top-N nodes)…"))
											.Font(FUnrealAiEditorStyle::FontBodySmall())
											.OnTextChanged_Lambda([this](const FText& T)
											{
												RebuildVectorDbTopGraphNodesUi(T.ToString());
											})
									]
									+ SVerticalBox::Slot().FillHeight(1.f)
									[
										SNew(SScrollBox)
										+ SScrollBox::Slot()
										[
											SAssignNew(VectorDbTopSourcesVBox, SVerticalBox)
										]
									]
								]
								+ SSplitter::Slot()
								.Value(0.62f)
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
									[
										SNew(STextBlock)
											.AutoWrapText(true)
											.Font(FUnrealAiEditorStyle::FontSectionHeading())
											.Text(LOCTEXT("VectorDbDrilldownLbl", "Selected source drill-down"))
									]
									+ SVerticalBox::Slot().FillHeight(1.f)
									[
										SAssignNew(VectorDbSourceInspectorPanel, SUnrealAiLocalJsonInspectorPanel)
									]
								]
							]
						]
					+ SWidgetSwitcher::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
						[
							SNew(STextBlock)
								.AutoWrapText(true)
								.Text(LOCTEXT(
									"ContextHelp",
									"Inspect persisted per-chat context.json / conversation.json artifacts. Threads are listed from your local thread index; open chats are highlighted."))
									.Font(FUnrealAiEditorStyle::FontBodySmall())
									.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
						]
						+ SVerticalBox::Slot().FillHeight(1.f)
						[
							SNew(SSplitter)
							.Orientation(Orient_Horizontal)
							+ SSplitter::Slot()
							.Value(0.35f)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
								[
									SNew(STextBlock)
										.Font(FUnrealAiEditorStyle::FontSectionHeading())
										.Text(LOCTEXT("ContextThreadsLbl", "Threads in project"))
								]
								+ SVerticalBox::Slot().FillHeight(1.f)
								[
									SNew(SScrollBox)
									+ SScrollBox::Slot()
									[
										SAssignNew(ContextThreadListVBox, SVerticalBox)
									]
								]
							]
							+ SSplitter::Slot()
							.Value(0.65f)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
								[
									SNew(STextBlock)
										.Font(FUnrealAiEditorStyle::FontSectionHeading())
										.Text(LOCTEXT("ContextInspectorLbl", "Context inspector"))
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
									[
										SNew(SButton)
											.Text(LOCTEXT("LoadCtxBtn", "Load context.json"))
											.OnClicked_Lambda([this]() { return OnContextLoadClicked(0); })
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
									[
										SNew(SButton)
											.Text(LOCTEXT("LoadConvBtn", "Load conversation.json"))
											.OnClicked_Lambda([this]() { return OnContextLoadClicked(1); })
									]
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(SButton)
											.Text(LOCTEXT("LoadPlanBtn", "Load plan draft"))
											.OnClicked_Lambda([this]() { return OnContextLoadClicked(2); })
									]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
								[
								SNew(STextBlock)
									.Text(LOCTEXT("ContextLocalFilesLbl", "Local thread files (for selected thread)"))
									.Font(FUnrealAiEditorStyle::FontBodySmall())
									.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
								]
								+ SVerticalBox::Slot().AutoHeight()
								[
									SAssignNew(ContextLocalFilesVBox, SVerticalBox)
								]
								+ SVerticalBox::Slot().FillHeight(1.f).Padding(FMargin(0.f, 8.f, 0.f, 0.f))
								[
									SAssignNew(ContextInspectorPanel, SUnrealAiLocalJsonInspectorPanel)
								]
							]
						]
					]
					+ SWidgetSwitcher::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
						[
							SNew(STextBlock)
								.AutoWrapText(true)
								.Text(LOCTEXT(
									"ProjectIndexHelp",
									"Display threads_index.json and drill into a selected thread’s persisted context artifacts. Open chat tabs are highlighted."))
								.Font(FUnrealAiEditorStyle::FontBodySmall())
								.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
						]
						+ SVerticalBox::Slot().FillHeight(1.f)
						[
							SNew(SSplitter)
							.Orientation(Orient_Horizontal)
							+ SSplitter::Slot()
							.Value(0.35f)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
								[
									SNew(STextBlock)
										.Font(FUnrealAiEditorStyle::FontSectionHeading())
										.Text(LOCTEXT("ProjIndexThreadsLbl", "Threads in index"))
								]
								+ SVerticalBox::Slot().FillHeight(1.f)
								[
									SNew(SScrollBox)
									+ SScrollBox::Slot()
									[
										SAssignNew(ProjectIndexThreadListVBox, SVerticalBox)
									]
								]
							]
							+ SSplitter::Slot()
							.Value(0.65f)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
								[
									SNew(STextBlock)
										.Font(FUnrealAiEditorStyle::FontSectionHeading())
										.Text(LOCTEXT("ProjIndexInspectorLbl", "threads_index.json + selected thread artifacts"))
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
									[
										SNew(SButton)
											.Text(LOCTEXT("LoadIndexBtn", "Load threads_index.json"))
											.OnClicked_Lambda([this]()
											{
												LoadThreadsIndexJson();
												return FReply::Handled();
											})
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
									[
										SNew(SButton)
											.Text(LOCTEXT("ProjLoadCtxBtn", "Load context.json"))
											.OnClicked_Lambda([this]() { return OnProjectIndexLoadClicked(0); })
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
									[
										SNew(SButton)
											.Text(LOCTEXT("ProjLoadConvBtn", "Load conversation.json"))
											.OnClicked_Lambda([this]() { return OnProjectIndexLoadClicked(1); })
									]
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(SButton)
											.Text(LOCTEXT("ProjLoadPlanBtn", "Load plan draft"))
											.OnClicked_Lambda([this]() { return OnProjectIndexLoadClicked(2); })
									]
								]
								+ SVerticalBox::Slot().FillHeight(1.f).Padding(FMargin(0.f, 0.f, 0.f, 0.f))
								[
									SAssignNew(ProjectIndexInspectorPanel, SUnrealAiLocalJsonInspectorPanel)
								]
							]
						]
					]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 16.f, 0.f, 0.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SButton)
								.Text(LOCTEXT("ViewMyData", "View my data"))
								.ToolTipText(LOCTEXT(
									"ViewMyDataTip",
									"Open this folder in the system file explorer. It holds local settings, chats, thread context, and other Unreal AI Editor data."))
								.OnClicked(this, &SUnrealAiEditorSettingsTab::OnViewMyDataClicked)
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(12.f, 0.f, 0.f, 0.f)).VAlign(VAlign_Center)
						[
							SNew(STextBlock)
								.AutoWrapText(true)
								.Font(FUnrealAiEditorStyle::FontCaption())
								.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextFooter())
								.Text_Lambda([this]()
								{
									if (!BackendRegistry.IsValid())
									{
										return FText::GetEmpty();
									}
									if (IUnrealAiPersistence* P = BackendRegistry->GetPersistence())
									{
										return FText::FromString(P->GetDataRootDirectory());
									}
									return FText::GetEmpty();
								})
						]
					]
				]
			];

	LoadSettingsIntoUi();
	RefreshUsageHeaderText();

	TWeakPtr<SUnrealAiEditorSettingsTab> WeakTab(StaticCastSharedRef<SUnrealAiEditorSettingsTab>(AsShared()));
	UsageTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakTab](float) -> bool
		{
			if (TSharedPtr<SUnrealAiEditorSettingsTab> Tab = WeakTab.Pin())
			{
				Tab->TickSlowUiRefresh();
			}
			return true;
		}),
		1.5f);
}

void SUnrealAiEditorSettingsTab::TickSlowUiRefresh()
{
	RefreshUsageHeaderText();
	if (ActiveSettingsSegmentIndex == 3)
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - LastVectorDbOverviewPollSeconds >= 3.0)
		{
			LastVectorDbOverviewPollSeconds = Now;
			RefreshVectorDbTopGraphUi();
			RefreshVectorDbOverviewUi();
		}
	}
}

FReply SUnrealAiEditorSettingsTab::OnSettingsSegmentClicked(const int32 Index)
{
	ActiveSettingsSegmentIndex = Index;
	if (SettingsMainSwitcher.IsValid())
	{
		SettingsMainSwitcher->SetActiveWidgetIndex(Index);
	}
	if (Index == 1)
	{
		RebuildChatHistoryListUi();
	}
	else if (Index == 2)
	{
		RebuildMemoryListUi();
	}
	else if (Index == 3)
	{
		LastVectorDbOverviewPollSeconds = 0.0;
		RefreshVectorDbTopGraphUi();
		RefreshVectorDbOverviewUi();
	}
	else if (Index == 4)
	{
		RebuildContextThreadListUi();
		RebuildContextLocalFilesUi();
		if (!SelectedContextThreadIdDigitsWithHyphens.IsEmpty())
		{
			LoadSelectedContextJson();
		}
	}
	else if (Index == 5)
	{
		RebuildProjectIndexThreadListUi();
		if (!SelectedProjectIndexThreadIdDigitsWithHyphens.IsEmpty())
		{
			LoadThreadsIndexJson();
		}
	}
	return FReply::Handled();
}

FReply SUnrealAiEditorSettingsTab::OnContextLoadClicked(const int32 Which)
{
	if (Which == 0)
	{
		LoadSelectedContextJson();
	}
	else if (Which == 1)
	{
		LoadSelectedConversationJson();
	}
	else if (Which == 2)
	{
		LoadSelectedPlanDraftJson();
	}
	return FReply::Handled();
}

void SUnrealAiEditorSettingsTab::SelectContextThread(const FString& ThreadIdDigitsWithHyphens)
{
	SelectedContextThreadIdDigitsWithHyphens = ThreadIdDigitsWithHyphens;
	RebuildContextLocalFilesUi();
	LoadSelectedContextJson();
}

void SUnrealAiEditorSettingsTab::RebuildContextThreadListUi()
{
	if (!ContextThreadListVBox.IsValid())
	{
		return;
	}
	ContextThreadListVBox->ClearChildren();

	if (!BackendRegistry.IsValid())
	{
		ContextThreadListVBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
				.Font(FUnrealAiEditorStyle::FontBodySmall())
				.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
				.Text(LOCTEXT("CtxBackendMissing", "Backend not ready."))
		];
		return;
	}

	IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence();
	if (!Persist)
	{
		ContextThreadListVBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
				.Font(FUnrealAiEditorStyle::FontBodySmall())
				.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
				.Text(LOCTEXT("CtxPersistMissing", "Persistence not available."))
		];
		return;
	}

	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	TArray<FString> ThreadIds;
	TArray<FString> DisplayNames;
	Persist->ListPersistedThreadsForHistory(ProjectId, ThreadIds, DisplayNames);
	if (ThreadIds.Num() == 0)
	{
		ContextThreadListVBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
				.Font(FUnrealAiEditorStyle::FontBodySmall())
				.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
				.Text(LOCTEXT("CtxThreadsEmpty", "No saved chats indexed yet for this project."))
		];
		return;
	}

	TArray<FGuid> Open;
	FUnrealAiEditorModule::GetOpenAgentChatThreadIds(Open);
	TSet<FString> OpenThreadIds;
	for (const FGuid& G : Open)
	{
		if (!G.IsValid())
		{
			continue;
		}
		const FString Tid = G.ToString(EGuidFormats::DigitsWithHyphens);
		OpenThreadIds.Add(Tid);
	}

	for (int32 i = 0; i < ThreadIds.Num(); ++i)
	{
		const FString& ThreadId = ThreadIds[i];
		const FString Label = DisplayNames.IsValidIndex(i) ? DisplayNames[i] : ThreadId;
		const bool bIsOpen = OpenThreadIds.Contains(ThreadId);

		ContextThreadListVBox->AddSlot().AutoHeight().Padding(FMargin(0.f, 2.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(STextBlock)
					.Font(FUnrealAiEditorStyle::FontBodySmall())
					.AutoWrapText(true)
					.Text(FText::FromString(bIsOpen ? (Label + TEXT("  [OPEN]")) : Label))
					.ColorAndOpacity(bIsOpen ? FUnrealAiEditorStyle::ColorAccent() : FUnrealAiEditorStyle::ColorTextPrimary())
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
					.Text(LOCTEXT("CtxInspectBtn", "Inspect"))
					.OnClicked_Lambda([this, ThreadId]()
					{
						SelectContextThread(ThreadId);
						return FReply::Handled();
					})
			]
		];
	}

	if (SelectedContextThreadIdDigitsWithHyphens.IsEmpty())
	{
		SelectedContextThreadIdDigitsWithHyphens = ThreadIds[0];
	}
}

void SUnrealAiEditorSettingsTab::RebuildContextLocalFilesUi()
{
	if (!ContextLocalFilesVBox.IsValid())
	{
		return;
	}
	ContextLocalFilesVBox->ClearChildren();

	if (!BackendRegistry.IsValid())
	{
		return;
	}
	IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence();
	if (!Persist)
	{
		return;
	}
	if (SelectedContextThreadIdDigitsWithHyphens.IsEmpty())
	{
		ContextLocalFilesVBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
				.Font(FUnrealAiEditorStyle::FontBodySmall())
				.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
				.Text(LOCTEXT("CtxPickThreadFirst", "Pick a thread to view local files."))
		];
		return;
	}

	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	const FString ThreadsRoot = FPaths::Combine(Persist->GetDataRootDirectory(), TEXT("chats"), ProjectId, TEXT("threads"));
	const FString Slug = Persist->GetThreadStorageSlug(ProjectId, SelectedContextThreadIdDigitsWithHyphens);

	if (Slug.IsEmpty())
	{
		ContextLocalFilesVBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
				.Font(FUnrealAiEditorStyle::FontBodySmall())
				.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
				.Text(LOCTEXT("CtxNoSlug", "Could not resolve thread storage slug on disk."))
		];
		return;
	}

	auto AddInspectRow = [this](const FString& Kind, const FString& Path, const bool bExists)
	{
		ContextLocalFilesVBox->AddSlot().AutoHeight().Padding(FMargin(0.f, 2.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
			[
				SNew(STextBlock)
					.AutoWrapText(true)
					.Font(FUnrealAiEditorStyle::FontBodySmall())
					.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextPrimary())
					.Text(bExists ? FText::FromString(Kind + TEXT(": ") + Path) : FText::FromString(Kind + TEXT(": (missing)")))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
					.Text(LOCTEXT("CtxInspectFile", "Inspect file"))
					.Visibility(bExists ? EVisibility::Visible : EVisibility::Collapsed)
					.OnClicked_Lambda([this, Path]()
					{
						if (ContextInspectorPanel.IsValid())
						{
							ContextInspectorPanel->InspectFilePath(Path);
						}
						return FReply::Handled();
					})
			]
		];
	};

	// Flat (current) filenames.
	const FString FlatCtx = FPaths::Combine(ThreadsRoot, Slug + TEXT("-context.json"));
	const FString FlatConv = FPaths::Combine(ThreadsRoot, Slug + TEXT("-conversation.json"));
	const FString FlatPlan = FPaths::Combine(ThreadsRoot, Slug + TEXT("-plan-draft.json"));

	// Legacy filenames are inside a subdirectory named by the slug.
	const FString LegacyDir = FPaths::Combine(ThreadsRoot, Slug);
	const FString LegacyCtx = FPaths::Combine(LegacyDir, TEXT("context.json"));
	const FString LegacyConv = FPaths::Combine(LegacyDir, TEXT("conversation.json"));
	const FString LegacyPlan = FPaths::Combine(LegacyDir, TEXT("plan-draft.json"));

	const bool bCtxExists = FPaths::FileExists(FlatCtx) || FPaths::FileExists(LegacyCtx);
	const bool bConvExists = FPaths::FileExists(FlatConv) || FPaths::FileExists(LegacyConv);
	const bool bPlanExists = FPaths::FileExists(FlatPlan) || FPaths::FileExists(LegacyPlan);

	// Prefer flat if both exist (should be rare but keep deterministic).
	AddInspectRow(
		TEXT("context.json"),
		FPaths::FileExists(FlatCtx) ? FlatCtx : LegacyCtx,
		bCtxExists);
	AddInspectRow(
		TEXT("conversation.json"),
		FPaths::FileExists(FlatConv) ? FlatConv : LegacyConv,
		bConvExists);
	AddInspectRow(
		TEXT("plan-draft"),
		FPaths::FileExists(FlatPlan) ? FlatPlan : LegacyPlan,
		bPlanExists);
}

void SUnrealAiEditorSettingsTab::LoadSelectedContextJson()
{
	if (!ContextInspectorPanel.IsValid() || !BackendRegistry.IsValid())
	{
		return;
	}
	IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence();
	if (!Persist || SelectedContextThreadIdDigitsWithHyphens.IsEmpty())
	{
		ContextInspectorPanel->SetInspectorText(TEXT("(no thread selected)"));
		return;
	}
	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	FString Json;
	if (!Persist->LoadThreadContextJson(ProjectId, SelectedContextThreadIdDigitsWithHyphens, Json))
	{
		ContextInspectorPanel->SetInspectorText(LOCTEXT("CtxMissing", "No context.json on disk for this thread yet.").ToString());
		return;
	}
	ContextInspectorPanel->SetInspectorText(Json);
}

void SUnrealAiEditorSettingsTab::LoadSelectedConversationJson()
{
	if (!ContextInspectorPanel.IsValid() || !BackendRegistry.IsValid())
	{
		return;
	}
	IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence();
	if (!Persist || SelectedContextThreadIdDigitsWithHyphens.IsEmpty())
	{
		ContextInspectorPanel->SetInspectorText(TEXT("(no thread selected)"));
		return;
	}
	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	FString Json;
	if (!Persist->LoadThreadConversationJson(ProjectId, SelectedContextThreadIdDigitsWithHyphens, Json))
	{
		ContextInspectorPanel->SetInspectorText(LOCTEXT("ConvMissing", "No conversation.json on disk for this thread yet.").ToString());
		return;
	}
	ContextInspectorPanel->SetInspectorText(Json);
}

void SUnrealAiEditorSettingsTab::LoadSelectedPlanDraftJson()
{
	if (!ContextInspectorPanel.IsValid() || !BackendRegistry.IsValid())
	{
		return;
	}
	IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence();
	if (!Persist || SelectedContextThreadIdDigitsWithHyphens.IsEmpty())
	{
		ContextInspectorPanel->SetInspectorText(TEXT("(no thread selected)"));
		return;
	}
	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	FString Json;
	if (!Persist->LoadThreadPlanDraftJson(ProjectId, SelectedContextThreadIdDigitsWithHyphens, Json))
	{
		ContextInspectorPanel->SetInspectorText(LOCTEXT("PlanMissing", "No plan draft JSON on disk for this thread yet.").ToString());
		return;
	}
	ContextInspectorPanel->SetInspectorText(Json);
}

FReply SUnrealAiEditorSettingsTab::OnProjectIndexLoadClicked(const int32 Which)
{
	if (Which == 0)
	{
		LoadSelectedProjectIndexContextJson();
	}
	else if (Which == 1)
	{
		LoadSelectedProjectIndexConversationJson();
	}
	else if (Which == 2)
	{
		LoadSelectedProjectIndexPlanDraftJson();
	}
	return FReply::Handled();
}

void SUnrealAiEditorSettingsTab::SelectProjectIndexThread(const FString& ThreadIdDigitsWithHyphens)
{
	SelectedProjectIndexThreadIdDigitsWithHyphens = ThreadIdDigitsWithHyphens;
	LoadSelectedProjectIndexContextJson();
}

void SUnrealAiEditorSettingsTab::RebuildProjectIndexThreadListUi()
{
	if (!ProjectIndexThreadListVBox.IsValid())
	{
		return;
	}
	ProjectIndexThreadListVBox->ClearChildren();

	if (!BackendRegistry.IsValid())
	{
		ProjectIndexThreadListVBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
				.Font(FUnrealAiEditorStyle::FontBodySmall())
				.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
				.Text(LOCTEXT("ProjIdxBackendMissing", "Backend not ready."))
		];
		return;
	}

	IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence();
	if (!Persist)
	{
		ProjectIndexThreadListVBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
				.Font(FUnrealAiEditorStyle::FontBodySmall())
				.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
				.Text(LOCTEXT("ProjIdxPersistMissing", "Persistence not available."))
		];
		return;
	}

	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	TArray<FString> ThreadIds;
	TArray<FString> DisplayNames;
	Persist->ListPersistedThreadsForHistory(ProjectId, ThreadIds, DisplayNames);
	if (ThreadIds.Num() == 0)
	{
		ProjectIndexThreadListVBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
				.Font(FUnrealAiEditorStyle::FontBodySmall())
				.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
				.Text(LOCTEXT("ProjIdxEmpty", "No saved chats indexed yet for this project."))
		];
		return;
	}

	TArray<FGuid> Open;
	FUnrealAiEditorModule::GetOpenAgentChatThreadIds(Open);
	TSet<FString> OpenThreadIds;
	for (const FGuid& G : Open)
	{
		if (!G.IsValid())
		{
			continue;
		}
		OpenThreadIds.Add(G.ToString(EGuidFormats::DigitsWithHyphens));
	}

	for (int32 i = 0; i < ThreadIds.Num(); ++i)
	{
		const FString& ThreadId = ThreadIds[i];
		const FString Label = DisplayNames.IsValidIndex(i) ? DisplayNames[i] : ThreadId;
		const bool bIsOpen = OpenThreadIds.Contains(ThreadId);

		ProjectIndexThreadListVBox->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(STextBlock)
					.AutoWrapText(true)
					.Font(FUnrealAiEditorStyle::FontBodySmall())
					.Text(FText::FromString(bIsOpen ? (Label + TEXT("  [OPEN]")) : Label))
					.ColorAndOpacity(bIsOpen ? FUnrealAiEditorStyle::ColorAccent() : FUnrealAiEditorStyle::ColorTextPrimary())
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
					.Text(LOCTEXT("ProjIdxInspectBtn", "Select"))
					.OnClicked_Lambda([this, ThreadId]()
					{
						SelectProjectIndexThread(ThreadId);
						return FReply::Handled();
					})
			]
		];
	}

	if (SelectedProjectIndexThreadIdDigitsWithHyphens.IsEmpty())
	{
		SelectedProjectIndexThreadIdDigitsWithHyphens = ThreadIds[0];
	}
}

void SUnrealAiEditorSettingsTab::LoadThreadsIndexJson()
{
	if (!ProjectIndexInspectorPanel.IsValid() || !BackendRegistry.IsValid())
	{
		return;
	}
	IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence();
	if (!Persist)
	{
		ProjectIndexInspectorPanel->SetInspectorText(TEXT("(persistence not available)"));
		return;
	}

	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	const FString IndexPath = FPaths::Combine(Persist->GetDataRootDirectory(), TEXT("chats"), ProjectId, TEXT("threads_index.json"));
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *IndexPath))
	{
		ProjectIndexInspectorPanel->SetInspectorText(FString::Printf(TEXT("Could not read threads_index.json at %s"), *IndexPath));
		return;
	}
	ProjectIndexInspectorPanel->SetInspectorText(Json);
}

void SUnrealAiEditorSettingsTab::LoadSelectedProjectIndexContextJson()
{
	if (!ProjectIndexInspectorPanel.IsValid() || !BackendRegistry.IsValid())
	{
		return;
	}
	IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence();
	if (!Persist || SelectedProjectIndexThreadIdDigitsWithHyphens.IsEmpty())
	{
		ProjectIndexInspectorPanel->SetInspectorText(TEXT("(no thread selected)"));
		return;
	}

	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	FString Json;
	if (!Persist->LoadThreadContextJson(ProjectId, SelectedProjectIndexThreadIdDigitsWithHyphens, Json))
	{
		ProjectIndexInspectorPanel->SetInspectorText(LOCTEXT("ProjIdxCtxMissing", "No context.json on disk for this thread yet.").ToString());
		return;
	}
	ProjectIndexInspectorPanel->SetInspectorText(Json);
}

void SUnrealAiEditorSettingsTab::LoadSelectedProjectIndexConversationJson()
{
	if (!ProjectIndexInspectorPanel.IsValid() || !BackendRegistry.IsValid())
	{
		return;
	}
	IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence();
	if (!Persist || SelectedProjectIndexThreadIdDigitsWithHyphens.IsEmpty())
	{
		ProjectIndexInspectorPanel->SetInspectorText(TEXT("(no thread selected)"));
		return;
	}

	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	FString Json;
	if (!Persist->LoadThreadConversationJson(ProjectId, SelectedProjectIndexThreadIdDigitsWithHyphens, Json))
	{
		ProjectIndexInspectorPanel->SetInspectorText(LOCTEXT("ProjIdxConvMissing", "No conversation.json on disk for this thread yet.").ToString());
		return;
	}
	ProjectIndexInspectorPanel->SetInspectorText(Json);
}

void SUnrealAiEditorSettingsTab::LoadSelectedProjectIndexPlanDraftJson()
{
	if (!ProjectIndexInspectorPanel.IsValid() || !BackendRegistry.IsValid())
	{
		return;
	}
	IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence();
	if (!Persist || SelectedProjectIndexThreadIdDigitsWithHyphens.IsEmpty())
	{
		ProjectIndexInspectorPanel->SetInspectorText(TEXT("(no thread selected)"));
		return;
	}

	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	FString Json;
	if (!Persist->LoadThreadPlanDraftJson(ProjectId, SelectedProjectIndexThreadIdDigitsWithHyphens, Json))
	{
		ProjectIndexInspectorPanel->SetInspectorText(LOCTEXT("ProjIdxPlanMissing", "No plan-draft JSON on disk for this thread yet.").ToString());
		return;
	}
	ProjectIndexInspectorPanel->SetInspectorText(Json);
}

FReply SUnrealAiEditorSettingsTab::OnVectorDbOverviewRefreshClicked()
{
	RefreshVectorDbOverviewUi();
	RefreshVectorDbTopGraphUi();
	return FReply::Handled();
}

void SUnrealAiEditorSettingsTab::RefreshVectorDbOverviewUi()
{
	if (!VectorDbOverviewBlock.IsValid())
	{
		return;
	}
	if (!BackendRegistry.IsValid() || !BackendRegistry->GetRetrievalService())
	{
		VectorDbOverviewBlock->SetText(LOCTEXT("VectorDbOverviewNoService", "Retrieval service is not available."));
		return;
	}
	FUnrealAiRetrievalVectorDbOverview Overview;
	FString Err;
	if (!BackendRegistry->GetRetrievalService()->GetVectorDbOverview(UnrealAiProjectId::GetCurrentProjectId(), Overview, Err))
	{
		VectorDbOverviewBlock->SetText(FText::FromString(FString::Printf(TEXT("Could not load overview: %s"), *Err)));
		return;
	}
	VectorDbOverviewBlock->SetText(FText::FromString(UnrealAiVectorDbOverviewUi::FormatOverviewText(Overview)));
}

void SUnrealAiEditorSettingsTab::RefreshVectorDbTopGraphUi()
{
	VectorDbTopSources.Reset();
	if (!BackendRegistry.IsValid() || !BackendRegistry->GetRetrievalService())
	{
		if (VectorDbTopSourcesVBox.IsValid())
		{
			VectorDbTopSourcesVBox->ClearChildren();
			VectorDbTopSourcesVBox->AddSlot().AutoHeight()
			[
				SNew(STextBlock)
					.Font(FUnrealAiEditorStyle::FontBodySmall())
					.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
					.Text(LOCTEXT("VectorDbTopGraphNoService", "Retrieval service is not available."))
			];
		}
		return;
	}

	FString Err;
	const int32 TopN = 16;
	const int32 SamplePerSource = 3;
	if (!BackendRegistry->GetRetrievalService()->GetVectorDbTopGraphData(
			UnrealAiProjectId::GetCurrentProjectId(),
			TopN,
			SamplePerSource,
			VectorDbTopSources,
			Err))
	{
		if (VectorDbTopSourcesVBox.IsValid())
		{
			VectorDbTopSourcesVBox->ClearChildren();
			VectorDbTopSourcesVBox->AddSlot().AutoHeight()
			[
				SNew(STextBlock)
					.Font(FUnrealAiEditorStyle::FontBodySmall())
					.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
					.Text(FText::FromString(FString::Printf(TEXT("Could not load vector top graph: %s"), *Err)))
			];
		}
		return;
	}

	RebuildVectorDbTopGraphNodesUi(VectorDbLastGraphSearch);
}

void SUnrealAiEditorSettingsTab::RebuildVectorDbTopGraphNodesUi(const FString& SearchText)
{
	VectorDbLastGraphSearch = SearchText;
	if (!VectorDbTopSourcesVBox.IsValid())
	{
		return;
	}
	VectorDbTopSourcesVBox->ClearChildren();

	const FString Needle = SearchText.ToLower();

	TArray<const FUnrealAiVectorDbTopSourceRow*> Filtered;
	for (const FUnrealAiVectorDbTopSourceRow& Row : VectorDbTopSources)
	{
		if (Needle.IsEmpty())
		{
			Filtered.Add(&Row);
			continue;
		}
		const FString SourceLower = Row.SourcePath.ToLower();
		const bool bMatchPath = SourceLower.Contains(Needle);
		const bool bMatchThread = !Row.ThreadIdHint.IsEmpty() && Row.ThreadIdHint.ToLower().Contains(Needle);
		if (bMatchPath || bMatchThread)
		{
			Filtered.Add(&Row);
		}
	}

	bool bSelectedFound = false;
	for (const FUnrealAiVectorDbTopSourceRow* RowPtr : Filtered)
	{
		if (RowPtr->SourcePath.Equals(VectorDbSelectedSourcePath, ESearchCase::CaseSensitive))
		{
			bSelectedFound = true;
			break;
		}
	}
	if (!bSelectedFound)
	{
		VectorDbSelectedSourcePath = Filtered.Num() > 0 ? Filtered[0]->SourcePath : FString();
	}

	for (const FUnrealAiVectorDbTopSourceRow* RowPtr : Filtered)
	{
		const FUnrealAiVectorDbTopSourceRow& Row = *RowPtr;
		const bool bSelected = Row.SourcePath.Equals(VectorDbSelectedSourcePath, ESearchCase::CaseSensitive);
		const FString Short = Row.SourcePath.Len() > 70 ? (Row.SourcePath.Left(67) + TEXT("…")) : Row.SourcePath;
		const FString ThreadSuffix = Row.ThreadIdHint.IsEmpty() ? TEXT("") : FString::Printf(TEXT("  [thread:%s]"), *Row.ThreadIdHint);

		VectorDbTopSourcesVBox->AddSlot().AutoHeight().Padding(0.f, 2.f)
		[
			SNew(SButton)
				.Text(FText::FromString(Short + TEXT("  (") + FString::FromInt(Row.ChunkCount) + TEXT(")") + ThreadSuffix))
				.OnClicked_Lambda([this, SourcePath = Row.SourcePath]()
					{
						VectorDbSelectedSourcePath = SourcePath;
						UpdateVectorDbSourceInspectorPanel();
						return FReply::Handled();
					})
				.ButtonColorAndOpacity(bSelected ? FUnrealAiEditorStyle::ColorAccent() : FUnrealAiEditorStyle::ColorTextPrimary())
		];
	}

	UpdateVectorDbSourceInspectorPanel();
}

void SUnrealAiEditorSettingsTab::UpdateVectorDbSourceInspectorPanel()
{
	if (!VectorDbSourceInspectorPanel.IsValid())
	{
		return;
	}

	if (VectorDbSelectedSourcePath.IsEmpty() || VectorDbTopSources.Num() == 0)
	{
		VectorDbSourceInspectorPanel->SetInspectorText(TEXT("(no source selected)"));
		return;
	}

	const FUnrealAiVectorDbTopSourceRow* Selected = nullptr;
	for (const FUnrealAiVectorDbTopSourceRow& Row : VectorDbTopSources)
	{
		if (Row.SourcePath.Equals(VectorDbSelectedSourcePath, ESearchCase::CaseSensitive))
		{
			Selected = &Row;
			break;
		}
	}
	if (!Selected)
	{
		VectorDbSourceInspectorPanel->SetInspectorText(TEXT("(selected source not found)"));
		return;
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("source_path"), Selected->SourcePath);
	Root->SetNumberField(TEXT("chunk_count"), Selected->ChunkCount);
	if (!Selected->ThreadIdHint.IsEmpty())
	{
		Root->SetStringField(TEXT("thread_id_hint"), Selected->ThreadIdHint);
	}
	TArray<TSharedPtr<FJsonValue>> Samples;
	for (const FUnrealAiVectorDbTopChunkRow& C : Selected->ChunkSamples)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("chunk_id"), C.ChunkId);
		Obj->SetStringField(TEXT("chunk_text"), C.ChunkText);
		Samples.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Root->SetArrayField(TEXT("chunk_samples"), Samples);

	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
	VectorDbSourceInspectorPanel->SetInspectorText(Out);
}

FReply SUnrealAiEditorSettingsTab::OnChatHistoryRefreshClicked()
{
	RebuildChatHistoryListUi();
	return FReply::Handled();
}

void SUnrealAiEditorSettingsTab::RebuildChatHistoryListUi()
{
	if (!ChatHistoryListVBox.IsValid())
	{
		return;
	}
	ChatHistoryListVBox->ClearChildren();
	if (!BackendRegistry.IsValid())
	{
		ChatHistoryListVBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
				.Font(FUnrealAiEditorStyle::FontBodySmall())
				.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
				.Text(LOCTEXT("ChatHistoryNoBackend", "Backend not ready."))
		];
		return;
	}
	IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence();
	if (!Persist)
	{
		ChatHistoryListVBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
				.Font(FUnrealAiEditorStyle::FontBodySmall())
				.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
				.Text(LOCTEXT("ChatHistoryNoPersist", "Persistence not available."))
		];
		return;
	}
	TArray<FString> ThreadIds;
	TArray<FString> DisplayNames;
	Persist->ListPersistedThreadsForHistory(UnrealAiProjectId::GetCurrentProjectId(), ThreadIds, DisplayNames);
	if (ThreadIds.Num() == 0)
	{
		ChatHistoryListVBox->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
				.Font(FUnrealAiEditorStyle::FontBodySmall())
				.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
				.Text(LOCTEXT("ChatHistoryEmpty", "No saved chats indexed yet for this project."))
		];
		return;
	}

	TArray<FGuid> Open;
	FUnrealAiEditorModule::GetOpenAgentChatThreadIds(Open);
	TSet<FString> OpenThreadIds;
	for (const FGuid& G : Open)
	{
		if (!G.IsValid())
		{
			continue;
		}
		OpenThreadIds.Add(G.ToString(EGuidFormats::DigitsWithHyphens));
	}
	for (int32 i = 0; i < ThreadIds.Num(); ++i)
	{
		const FString& ThreadId = ThreadIds[i];
		const FString Label = DisplayNames.IsValidIndex(i) ? DisplayNames[i] : ThreadId;
		const bool bIsOpen = OpenThreadIds.Contains(ThreadId);
		ChatHistoryListVBox->AddSlot().AutoHeight().Padding(FMargin(0.f, 2.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
			[
				SNew(STextBlock)
					.Font(FUnrealAiEditorStyle::FontBodySmall())
					.AutoWrapText(true)
					.Text(FText::FromString(bIsOpen ? (Label + TEXT("  [OPEN]")) : Label))
					.ToolTipText(FText::FromString(ThreadId))
					.ColorAndOpacity(bIsOpen ? FUnrealAiEditorStyle::ColorAccent() : FUnrealAiEditorStyle::ColorTextPrimary())
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
					.Text(LOCTEXT("ChatHistoryInspect", "Inspect"))
					.OnClicked_Lambda([this, ThreadId]()
					{
						SelectChatHistoryThread(ThreadId);
						return FReply::Handled();
					})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(8.f, 0.f, 0.f, 0.f))
			[
				SNew(SButton)
					.Text(LOCTEXT("ChatHistoryOpen", "Open"))
					.OnClicked_Lambda([this, ThreadId]()
					{
						SelectChatHistoryThread(ThreadId);
						FUnrealAiEditorModule::OpenAgentChatTabWithPersistedThread(ThreadId);
						return FReply::Handled();
					})
			]
		];
	}

	if (SelectedChatHistoryThreadIdDigitsWithHyphens.IsEmpty() && ThreadIds.Num() > 0)
	{
		SelectedChatHistoryThreadIdDigitsWithHyphens = ThreadIds[0];
	}
	if (ChatHistoryInspectorPanel.IsValid() && !SelectedChatHistoryThreadIdDigitsWithHyphens.IsEmpty())
	{
		LoadSelectedChatHistoryContextJson();
	}
}

void SUnrealAiEditorSettingsTab::SelectChatHistoryThread(const FString& ThreadIdDigitsWithHyphens)
{
	SelectedChatHistoryThreadIdDigitsWithHyphens = ThreadIdDigitsWithHyphens;
	LoadSelectedChatHistoryContextJson();
}

void SUnrealAiEditorSettingsTab::LoadSelectedChatHistoryContextJson()
{
	if (!ChatHistoryInspectorPanel.IsValid() || !BackendRegistry.IsValid())
	{
		return;
	}

	IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence();
	if (!Persist || SelectedChatHistoryThreadIdDigitsWithHyphens.IsEmpty())
	{
		ChatHistoryInspectorPanel->SetInspectorText(TEXT("(no thread selected)"));
		return;
	}

	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	FString Json;
	if (!Persist->LoadThreadContextJson(ProjectId, SelectedChatHistoryThreadIdDigitsWithHyphens, Json))
	{
		ChatHistoryInspectorPanel->SetInspectorText(
			LOCTEXT("ChatHistoryCtxMissing", "No context.json on disk for this thread yet.").ToString());
		return;
	}
	ChatHistoryInspectorPanel->SetInspectorText(Json);
}

void SUnrealAiEditorSettingsTab::LoadSelectedChatHistoryConversationJson()
{
	if (!ChatHistoryInspectorPanel.IsValid() || !BackendRegistry.IsValid())
	{
		return;
	}

	IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence();
	if (!Persist || SelectedChatHistoryThreadIdDigitsWithHyphens.IsEmpty())
	{
		ChatHistoryInspectorPanel->SetInspectorText(TEXT("(no thread selected)"));
		return;
	}

	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	FString Json;
	if (!Persist->LoadThreadConversationJson(ProjectId, SelectedChatHistoryThreadIdDigitsWithHyphens, Json))
	{
		ChatHistoryInspectorPanel->SetInspectorText(
			LOCTEXT("ChatHistoryConvMissing", "No conversation.json on disk for this thread yet.").ToString());
		return;
	}
	ChatHistoryInspectorPanel->SetInspectorText(Json);
}

bool SUnrealAiEditorSettingsTab::LoadMemorySettingsFromRoot(const TSharedPtr<FJsonObject>& Root)
{
	bMemoryEnabled = true;
	bMemoryAutoExtract = true;
	MemoryMaxItemsStr = TEXT("500");
	MemoryMinConfidenceStr = TEXT("0.55");
	MemoryRetentionDaysStr = TEXT("30");
	if (!Root.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* MemoryObj = nullptr;
	if (!Root->TryGetObjectField(TEXT("memory"), MemoryObj) || !MemoryObj || !(*MemoryObj).IsValid())
	{
		return false;
	}
	(*MemoryObj)->TryGetBoolField(TEXT("enabled"), bMemoryEnabled);
	(*MemoryObj)->TryGetBoolField(TEXT("autoExtract"), bMemoryAutoExtract);
	double Num = 0;
	if ((*MemoryObj)->TryGetNumberField(TEXT("maxItems"), Num))
	{
		MemoryMaxItemsStr = FString::FromInt(FMath::Max(0, static_cast<int32>(Num)));
	}
	if ((*MemoryObj)->TryGetNumberField(TEXT("minConfidence"), Num))
	{
		MemoryMinConfidenceStr = FString::Printf(TEXT("%.2f"), Num);
	}
	if ((*MemoryObj)->TryGetNumberField(TEXT("retentionDays"), Num))
	{
		MemoryRetentionDaysStr = FString::FromInt(FMath::Max(0, static_cast<int32>(Num)));
	}
	return true;
}

bool SUnrealAiEditorSettingsTab::LoadRetrievalSettingsFromRoot(const TSharedPtr<FJsonObject>& Root)
{
	bRetrievalEnabled = false;
	RetrievalEmbeddingModel = TEXT("text-embedding-3-small");
	RetrievalMaxSnippetsPerTurnStr = TEXT("6");
	RetrievalMaxSnippetTokensStr = TEXT("256");
	bRetrievalAutoIndexOnProjectOpen = true;
	RetrievalPeriodicScrubMinutesStr = TEXT("30");
	bRetrievalAllowMixedModelCompatibility = false;
	RetrievalRootPresetStr = TEXT("minimal");
	RetrievalIndexedExtensionsText.Reset();
	RetrievalMaxFilesPerRebuildStr = TEXT("0");
	RetrievalMaxTotalChunksPerRebuildStr = TEXT("0");
	RetrievalMaxEmbeddingCallsPerRebuildStr = TEXT("0");
	RetrievalChunkCharsStr = TEXT("1200");
	RetrievalChunkOverlapStr = TEXT("200");
	RetrievalAssetRegistryMaxAssetsStr = TEXT("0");
	bRetrievalAssetRegistryIncludeEngineAssets = false;
	RetrievalEmbeddingBatchSizeStr = TEXT("8");
	RetrievalMinDelayMsBetweenBatchesStr = TEXT("0");
	bRetrievalIndexMemoryRecordsInVectorStore = false;
	RetrievalBlueprintMaxFeatureRecordsStr = TEXT("0");
	if (!Root.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* RetrievalObj = nullptr;
	if (!Root->TryGetObjectField(TEXT("retrieval"), RetrievalObj) || !RetrievalObj || !(*RetrievalObj).IsValid())
	{
		return false;
	}
	(*RetrievalObj)->TryGetBoolField(TEXT("enabled"), bRetrievalEnabled);
	(*RetrievalObj)->TryGetStringField(TEXT("embeddingModel"), RetrievalEmbeddingModel);
	double Number = 0.0;
	if ((*RetrievalObj)->TryGetNumberField(TEXT("maxSnippetsPerTurn"), Number))
	{
		RetrievalMaxSnippetsPerTurnStr = FString::FromInt(FMath::Max(0, static_cast<int32>(Number)));
	}
	if ((*RetrievalObj)->TryGetNumberField(TEXT("maxSnippetTokens"), Number))
	{
		RetrievalMaxSnippetTokensStr = FString::FromInt(FMath::Max(0, static_cast<int32>(Number)));
	}
	(*RetrievalObj)->TryGetBoolField(TEXT("autoIndexOnProjectOpen"), bRetrievalAutoIndexOnProjectOpen);
	(*RetrievalObj)->TryGetBoolField(TEXT("allowMixedModelCompatibility"), bRetrievalAllowMixedModelCompatibility);
	if ((*RetrievalObj)->TryGetNumberField(TEXT("periodicScrubMinutes"), Number))
	{
		RetrievalPeriodicScrubMinutesStr = FString::FromInt(FMath::Max(0, static_cast<int32>(Number)));
	}

	FString PresetStr;
	if ((*RetrievalObj)->TryGetStringField(TEXT("rootPreset"), PresetStr) && !PresetStr.IsEmpty())
	{
		RetrievalRootPresetStr = PresetStr.ToLower();
	}

	const TArray<TSharedPtr<FJsonValue>>* ExtArr = nullptr;
	if ((*RetrievalObj)->TryGetArrayField(TEXT("indexedExtensions"), ExtArr) && ExtArr)
	{
		RetrievalIndexedExtensionsText.Reset();
		for (int32 i = 0; i < ExtArr->Num(); ++i)
		{
			const TSharedPtr<FJsonValue>& V = (*ExtArr)[i];
			if (V.IsValid() && V->Type == EJson::String)
			{
				if (!RetrievalIndexedExtensionsText.IsEmpty())
				{
					RetrievalIndexedExtensionsText += LINE_TERMINATOR;
				}
				RetrievalIndexedExtensionsText += V->AsString();
			}
		}
	}

	if ((*RetrievalObj)->TryGetNumberField(TEXT("maxFilesPerRebuild"), Number))
	{
		RetrievalMaxFilesPerRebuildStr = FString::FromInt(FMath::Max(0, static_cast<int32>(Number)));
	}
	if ((*RetrievalObj)->TryGetNumberField(TEXT("maxTotalChunksPerRebuild"), Number))
	{
		RetrievalMaxTotalChunksPerRebuildStr = FString::FromInt(FMath::Max(0, static_cast<int32>(Number)));
	}
	if ((*RetrievalObj)->TryGetNumberField(TEXT("maxEmbeddingCallsPerRebuild"), Number))
	{
		RetrievalMaxEmbeddingCallsPerRebuildStr = FString::FromInt(FMath::Max(0, static_cast<int32>(Number)));
	}
	if ((*RetrievalObj)->TryGetNumberField(TEXT("chunkChars"), Number))
	{
		RetrievalChunkCharsStr = FString::FromInt(static_cast<int32>(Number));
	}
	if ((*RetrievalObj)->TryGetNumberField(TEXT("chunkOverlap"), Number))
	{
		RetrievalChunkOverlapStr = FString::FromInt(static_cast<int32>(Number));
	}
	if ((*RetrievalObj)->TryGetNumberField(TEXT("assetRegistryMaxAssets"), Number))
	{
		RetrievalAssetRegistryMaxAssetsStr = FString::FromInt(FMath::Max(0, static_cast<int32>(Number)));
	}
	(*RetrievalObj)->TryGetBoolField(TEXT("assetRegistryIncludeEngineAssets"), bRetrievalAssetRegistryIncludeEngineAssets);
	if ((*RetrievalObj)->TryGetNumberField(TEXT("embeddingBatchSize"), Number))
	{
		RetrievalEmbeddingBatchSizeStr = FString::FromInt(FMath::Max(1, static_cast<int32>(Number)));
	}
	if ((*RetrievalObj)->TryGetNumberField(TEXT("minDelayMsBetweenEmbeddingBatches"), Number))
	{
		RetrievalMinDelayMsBetweenBatchesStr = FString::FromInt(FMath::Max(0, static_cast<int32>(Number)));
	}
	(*RetrievalObj)->TryGetBoolField(TEXT("indexMemoryRecordsInVectorStore"), bRetrievalIndexMemoryRecordsInVectorStore);
	if ((*RetrievalObj)->TryGetNumberField(TEXT("blueprintMaxFeatureRecords"), Number))
	{
		RetrievalBlueprintMaxFeatureRecordsStr = FString::FromInt(FMath::Max(0, static_cast<int32>(Number)));
	}
	return true;
}

void SUnrealAiEditorSettingsTab::WriteMemorySettingsToRoot(TSharedPtr<FJsonObject>& Root) const
{
	if (!Root.IsValid())
	{
		return;
	}
	TSharedPtr<FJsonObject> MemoryObj = MakeShared<FJsonObject>();
	MemoryObj->SetBoolField(TEXT("enabled"), bMemoryEnabled);
	MemoryObj->SetBoolField(TEXT("autoExtract"), bMemoryAutoExtract);
	MemoryObj->SetNumberField(TEXT("maxItems"), FMath::Max(0, FCString::Atoi(*MemoryMaxItemsStr)));
	MemoryObj->SetNumberField(TEXT("minConfidence"), FCString::Atof(*MemoryMinConfidenceStr));
	MemoryObj->SetNumberField(TEXT("retentionDays"), FMath::Max(0, FCString::Atoi(*MemoryRetentionDaysStr)));
	Root->SetObjectField(TEXT("memory"), MemoryObj);
}

void SUnrealAiEditorSettingsTab::WriteRetrievalSettingsToRoot(TSharedPtr<FJsonObject>& Root) const
{
	if (!Root.IsValid())
	{
		return;
	}
	TSharedPtr<FJsonObject> RetrievalObj = MakeShared<FJsonObject>();
	RetrievalObj->SetBoolField(TEXT("enabled"), bRetrievalEnabled);
	RetrievalObj->SetStringField(TEXT("embeddingModel"), RetrievalEmbeddingModel);
	RetrievalObj->SetNumberField(TEXT("maxSnippetsPerTurn"), FMath::Max(0, FCString::Atoi(*RetrievalMaxSnippetsPerTurnStr)));
	RetrievalObj->SetNumberField(TEXT("maxSnippetTokens"), FMath::Max(0, FCString::Atoi(*RetrievalMaxSnippetTokensStr)));
	RetrievalObj->SetBoolField(TEXT("autoIndexOnProjectOpen"), bRetrievalAutoIndexOnProjectOpen);
	RetrievalObj->SetBoolField(TEXT("allowMixedModelCompatibility"), bRetrievalAllowMixedModelCompatibility);
	RetrievalObj->SetNumberField(TEXT("periodicScrubMinutes"), FMath::Max(0, FCString::Atoi(*RetrievalPeriodicScrubMinutesStr)));

	FString PresetOut = RetrievalRootPresetStr.ToLower();
	if (!PresetOut.Equals(TEXT("minimal")) && !PresetOut.Equals(TEXT("standard")) && !PresetOut.Equals(TEXT("extended")))
	{
		PresetOut = TEXT("minimal");
	}
	RetrievalObj->SetStringField(TEXT("rootPreset"), PresetOut);

	TArray<TSharedPtr<FJsonValue>> ExtJson;
	{
		FString Tmp = RetrievalIndexedExtensionsText;
		Tmp.ReplaceInline(TEXT("\r"), TEXT(""));
		TArray<FString> Lines;
		Tmp.ParseIntoArrayLines(Lines, false);
		for (FString& Line : Lines)
		{
			TArray<FString> Parts;
			Line.ParseIntoArray(Parts, TEXT(","), true);
			for (FString& P : Parts)
			{
				P.TrimStartAndEndInline();
				if (!P.IsEmpty())
				{
					ExtJson.Add(MakeShared<FJsonValueString>(P));
				}
			}
		}
	}
	RetrievalObj->SetArrayField(TEXT("indexedExtensions"), ExtJson);

	RetrievalObj->SetNumberField(TEXT("maxFilesPerRebuild"), FMath::Max(0, FCString::Atoi(*RetrievalMaxFilesPerRebuildStr)));
	RetrievalObj->SetNumberField(TEXT("maxTotalChunksPerRebuild"), FMath::Max(0, FCString::Atoi(*RetrievalMaxTotalChunksPerRebuildStr)));
	RetrievalObj->SetNumberField(TEXT("maxEmbeddingCallsPerRebuild"), FMath::Max(0, FCString::Atoi(*RetrievalMaxEmbeddingCallsPerRebuildStr)));
	RetrievalObj->SetNumberField(TEXT("chunkChars"), FCString::Atoi(*RetrievalChunkCharsStr));
	RetrievalObj->SetNumberField(TEXT("chunkOverlap"), FCString::Atoi(*RetrievalChunkOverlapStr));
	RetrievalObj->SetNumberField(TEXT("assetRegistryMaxAssets"), FMath::Max(0, FCString::Atoi(*RetrievalAssetRegistryMaxAssetsStr)));
	RetrievalObj->SetBoolField(TEXT("assetRegistryIncludeEngineAssets"), bRetrievalAssetRegistryIncludeEngineAssets);
	RetrievalObj->SetNumberField(TEXT("embeddingBatchSize"), FMath::Max(1, FCString::Atoi(*RetrievalEmbeddingBatchSizeStr)));
	RetrievalObj->SetNumberField(TEXT("minDelayMsBetweenEmbeddingBatches"), FMath::Max(0, FCString::Atoi(*RetrievalMinDelayMsBetweenBatchesStr)));
	RetrievalObj->SetBoolField(TEXT("indexMemoryRecordsInVectorStore"), bRetrievalIndexMemoryRecordsInVectorStore);
	RetrievalObj->SetNumberField(TEXT("blueprintMaxFeatureRecords"), FMath::Max(0, FCString::Atoi(*RetrievalBlueprintMaxFeatureRecordsStr)));

	Root->SetObjectField(TEXT("retrieval"), RetrievalObj);
}

FReply SUnrealAiEditorSettingsTab::OnMemoriesRefreshClicked()
{
	RebuildMemoryListUi();
	return FReply::Handled();
}

FReply SUnrealAiEditorSettingsTab::OnRetrievalRebuildNowClicked()
{
	if (!BackendRegistry.IsValid() || !BackendRegistry->GetRetrievalService())
	{
		StatusText = LOCTEXT("RetrievalRebuildUnavailable", "Retrieval service unavailable.");
		return FReply::Handled();
	}
	BackendRegistry->GetRetrievalService()->RequestRebuild(UnrealAiProjectId::GetCurrentProjectId());
	StatusText = LOCTEXT("RetrievalRebuildQueued", "Queued retrieval index rebuild.");
	if (ActiveSettingsSegmentIndex == 3)
	{
		RefreshVectorDbOverviewUi();
	}
	return FReply::Handled();
}

void SUnrealAiEditorSettingsTab::SelectMemoryById(const FString& MemoryId)
{
	SelectedMemoryId = MemoryId;
	if (!MemorySelectedTitleBox.IsValid() || !MemorySelectedDescriptionBox.IsValid() || !MemorySelectedBodyBox.IsValid())
	{
		return;
	}
	MemorySelectedTitleBox->SetText(FText::GetEmpty());
	MemorySelectedDescriptionBox->SetText(FText::GetEmpty());
	MemorySelectedBodyBox->SetText(FText::GetEmpty());
	if (!BackendRegistry.IsValid())
	{
		return;
	}
	IUnrealAiMemoryService* MemoryService = BackendRegistry->GetMemoryService();
	if (!MemoryService || MemoryId.IsEmpty())
	{
		return;
	}
	FUnrealAiMemoryRecord Record;
	if (!MemoryService->GetMemory(MemoryId, Record))
	{
		return;
	}
	MemorySelectedTitleBox->SetText(FText::FromString(Record.Title));
	MemorySelectedDescriptionBox->SetText(FText::FromString(Record.Description));
	MemorySelectedBodyBox->SetText(FText::FromString(Record.Body));
}

void SUnrealAiEditorSettingsTab::RebuildMemoryListUi()
{
	if (!MemoryListVBox.IsValid())
	{
		return;
	}
	MemoryListVBox->ClearChildren();
	if (!BackendRegistry.IsValid())
	{
		return;
	}
	IUnrealAiMemoryService* MemoryService = BackendRegistry->GetMemoryService();
	if (!MemoryService)
	{
		if (MemoryGenerationStatusBlock.IsValid())
		{
			MemoryGenerationStatusBlock->SetText(LOCTEXT("MemoryGenStatusNoService", "Memory generation status: unavailable."));
		}
		return;
	}
	if (MemoryGenerationStatusBlock.IsValid())
	{
		const FUnrealAiMemoryGenerationStatus S = MemoryService->GetGenerationStatus();
		const FString Attempt = S.LastAttemptAtUtc.GetTicks() > 0 ? S.LastAttemptAtUtc.ToString(TEXT("%Y-%m-%d %H:%M:%S UTC")) : TEXT("never");
		const FString Success = S.LastSuccessAtUtc.GetTicks() > 0 ? S.LastSuccessAtUtc.ToString(TEXT("%Y-%m-%d %H:%M:%S UTC")) : TEXT("never");
		FString Msg = FString::Printf(TEXT("Memory generation: %s (last attempt: %s, last success: %s, error: %s)"),
			*MemoryGenStateToString(S.State),
			*Attempt,
			*Success,
			*MemoryGenErrToString(S.ErrorCode));
		if (!S.ErrorMessage.IsEmpty())
		{
			Msg += FString::Printf(TEXT(" - %s"), *S.ErrorMessage);
		}
		MemoryGenerationStatusBlock->SetText(FText::FromString(Msg));
	}
	TArray<FUnrealAiMemoryIndexRow> Rows;
	MemoryService->ListMemories(Rows);
	const FString Search = MemorySearchBox.IsValid() ? MemorySearchBox->GetText().ToString().ToLower() : FString();
	for (const FUnrealAiMemoryIndexRow& Row : Rows)
	{
		const FString Hay = (Row.Title + TEXT(" ") + Row.Description + TEXT(" ") + FString::Join(Row.Tags, TEXT(" "))).ToLower();
		if (!Search.IsEmpty() && !Hay.Contains(Search))
		{
			continue;
		}
		const FString Id = Row.Id;
		const FString Label = FString::Printf(TEXT("%s  [%.2f]"), *Row.Title, Row.Confidence);
		MemoryListVBox->AddSlot().AutoHeight().Padding(FMargin(0.f, 2.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
			[
				SNew(STextBlock).Text(FText::FromString(Label))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
			[
				SNew(SButton)
					.Text(LOCTEXT("MemoryOpen", "Open"))
					.OnClicked_Lambda([this, Id]()
					{
						SelectMemoryById(Id);
						return FReply::Handled();
					})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
					.Text(LOCTEXT("MemoryDelete", "Delete"))
					.OnClicked_Lambda([this, Id]()
					{
						if (BackendRegistry.IsValid())
						{
							if (IUnrealAiMemoryService* Svc = BackendRegistry->GetMemoryService())
							{
								Svc->DeleteMemory(Id);
								if (SelectedMemoryId == Id)
								{
									SelectMemoryById(FString());
								}
								RebuildMemoryListUi();
							}
						}
						return FReply::Handled();
					})
			]
		];
	}
}

SUnrealAiEditorSettingsTab::~SUnrealAiEditorSettingsTab()
{
	CancelDeferredReload();
	if (UsageTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(UsageTickerHandle);
		UsageTickerHandle.Reset();
	}
}

void SUnrealAiEditorSettingsTab::RefreshUsageHeaderText()
{
	if (!UsageSummaryBlock.IsValid() || !BackendRegistry.IsValid())
	{
		return;
	}
	FUnrealAiUsageTracker* U = BackendRegistry->GetUsageTracker();
	FUnrealAiModelProfileRegistry* Reg = BackendRegistry->GetModelProfileRegistry();
	if (!U || !Reg)
	{
		UsageSummaryBlock->SetText(LOCTEXT("UsageNoData", "Usage: (tracker unavailable)"));
		return;
	}
	int64 Pt = 0, Ct = 0;
	double Usd = 0.0;
	U->AccumulateTotals(*Reg, Pt, Ct, Usd);
	const FText T = FText::Format(
		LOCTEXT(
			"UsageFmt",
			"Total usage (all models): {0} prompt tok, {1} completion tok · ~${2} (estimate where catalog matched your model ids)"),
		FText::AsNumber(Pt),
		FText::AsNumber(Ct),
		FText::FromString(FString::Printf(TEXT("%.4f"), Usd)));
	UsageSummaryBlock->SetText(T);
}

void SUnrealAiEditorSettingsTab::ApplyCompanyPreset(FDynSectionRow& Row, const FString& PresetId)
{
	Row.CompanyPreset = PresetId;
	if (PresetId == TEXT("openrouter"))
	{
		Row.BaseUrl = TEXT("https://openrouter.ai/api/v1");
	}
	else if (PresetId == TEXT("openai"))
	{
		Row.BaseUrl = TEXT("https://api.openai.com/v1");
	}
	else if (PresetId == TEXT("anthropic"))
	{
		Row.BaseUrl = TEXT("https://api.anthropic.com/v1");
	}
	else if (PresetId == TEXT("groq"))
	{
		Row.BaseUrl = TEXT("https://api.groq.com/openai/v1");
	}
	else if (PresetId == TEXT("deepseek"))
	{
		Row.BaseUrl = TEXT("https://api.deepseek.com/v1");
	}
	else if (PresetId == TEXT("xai"))
	{
		Row.BaseUrl = TEXT("https://api.x.ai/v1");
	}
	else if (PresetId == TEXT("azure"))
	{
		Row.BaseUrl = TEXT("https://YOUR_RESOURCE.openai.azure.com/openai/deployments");
	}
}

void SUnrealAiEditorSettingsTab::RebuildProviderOptions()
{
	ProviderIdOptions.Reset();
	for (const FDynSectionRow& Sec : SectionRows)
	{
		if (!Sec.Id.IsEmpty())
		{
			ProviderIdOptions.Add(MakeShared<FString>(Sec.Id));
		}
	}
	ProviderIdOptions.Sort([](const TSharedPtr<FString>& A, const TSharedPtr<FString>& B)
	{
		const FString AV = A.IsValid() ? *A : FString();
		const FString BV = B.IsValid() ? *B : FString();
		return AV < BV;
	});
	if (ApiDefaultProviderDropdown.IsValid())
	{
		ApiDefaultProviderDropdown->RefreshOptions();
	}
}

void SUnrealAiEditorSettingsTab::RefreshProviderDropdownSelection()
{
	if (!ApiDefaultProviderDropdown.IsValid() || !ApiDefaultProviderIdBox.IsValid())
	{
		return;
	}
	const FString Current = ApiDefaultProviderIdBox->GetText().ToString();
	for (const TSharedPtr<FString>& Opt : ProviderIdOptions)
	{
		if (Opt.IsValid() && *Opt == Current)
		{
			ApiDefaultProviderDropdown->SetSelectedItem(Opt);
			return;
		}
	}
	ApiDefaultProviderDropdown->SetSelectedItem(nullptr);
}

static FText CompanyHintText(const FString& PresetId)
{
	if (PresetId == TEXT("openrouter"))
	{
		return LOCTEXT("HintOR", "Paste an API key from openrouter.ai/keys. Requests use the OpenAI-compatible /v1/chat/completions endpoint.");
	}
	if (PresetId == TEXT("openai"))
	{
		return LOCTEXT("HintOAI", "Paste an API key from platform.openai.com/api-keys. Base URL should be https://api.openai.com/v1 for most accounts.");
	}
	if (PresetId == TEXT("anthropic"))
	{
		return LOCTEXT("HintAnt", "Direct Anthropic URLs differ from OpenAI-style proxies; double-check your provider. Many teams route Anthropic via OpenRouter.");
	}
	if (PresetId == TEXT("groq"))
	{
		return LOCTEXT("HintGroq", "Create a key at console.groq.com. Endpoint is typically https://api.groq.com/openai/v1.");
	}
	if (PresetId == TEXT("deepseek"))
	{
		return LOCTEXT("HintDS", "API keys from platform.deepseek.com; OpenAI-compatible base https://api.deepseek.com/v1.");
	}
	if (PresetId == TEXT("xai"))
	{
		return LOCTEXT("HintXai", "xAI console keys; base URL https://api.x.ai/v1 unless your account docs say otherwise.");
	}
	if (PresetId == TEXT("azure"))
	{
		return LOCTEXT("HintAzure", "Replace YOUR_RESOURCE in the base URL with your Azure OpenAI resource name; keys come from Azure Portal.");
	}
	return LOCTEXT("HintCustom", "Enter any OpenAI-compatible HTTPS base URL and bearer token.");
}

void SUnrealAiEditorSettingsTab::UpdateModelPricingHint(FDynModelRow& Row)
{
	Row.PricingHintCache.Reset();
	if (!BackendRegistry.IsValid())
	{
		return;
	}
	FUnrealAiModelPricingCatalog* Cat = BackendRegistry->GetPricingCatalog();
	if (!Cat)
	{
		return;
	}
	const FString Mid = Row.ModelIdForApi.IsEmpty() ? Row.ProfileKey : Row.ModelIdForApi;
	TArray<FString> Lines;
	Cat->GetRoughPriceHintLines(Mid, Lines);
	if (Lines.Num() > 0)
	{
		Row.PricingHintCache = FString::Join(Lines, TEXT(" · "));
	}
}

void SUnrealAiEditorSettingsTab::CancelDeferredReload()
{
	if (DeferredReloadHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DeferredReloadHandle);
		DeferredReloadHandle.Reset();
	}
}

void SUnrealAiEditorSettingsTab::ScheduleDeferredReload()
{
	if (!BackendRegistry.IsValid())
	{
		return;
	}
	CancelDeferredReload();
	TWeakPtr<SUnrealAiEditorSettingsTab> WeakSettingsTab(StaticCastSharedRef<SUnrealAiEditorSettingsTab>(AsShared()));
	DeferredReloadHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda(
			[WeakSettingsTab](float) -> bool
			{
				if (TSharedPtr<SUnrealAiEditorSettingsTab> Self = WeakSettingsTab.Pin())
				{
					if (Self->BackendRegistry.IsValid())
					{
						Self->BackendRegistry->ReloadLlmConfiguration();
					}
					Self->DeferredReloadHandle.Reset();
				}
				return false;
			}),
		0.25f);
}

void SUnrealAiEditorSettingsTab::PersistSettingsToDisk()
{
	if (bSuppressAutoSave || !BackendRegistry.IsValid())
	{
		return;
	}
	FString Json;
	FString Err;
	if (!BuildJsonFromUi(Json, Err))
	{
		StatusText = FText::FromString(FString::Printf(TEXT("Error: %s"), *Err));
		Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
		return;
	}
	if (IUnrealAiPersistence* P = BackendRegistry->GetPersistence())
	{
		P->SaveSettingsJson(Json);
	}
	TSharedPtr<FJsonObject> Reloaded;
	if (UnrealAiSettingsTabUtil::DeserializeRoot(Json, Reloaded) && Reloaded.IsValid())
	{
		CachedSettingsRoot = UnrealAiSettingsTabUtil::CloneJsonObject(Reloaded);
	}
}

void SUnrealAiEditorSettingsTab::OnAnySettingsChanged(const FText&)
{
	PersistSettingsToDisk();
	ScheduleDeferredReload();
}

void SUnrealAiEditorSettingsTab::LoadSettingsIntoUi()
{
	bSuppressAutoSave = true;
	FString Json;
	IUnrealAiPersistence* Persist = nullptr;
	if (BackendRegistry.IsValid())
	{
		Persist = BackendRegistry->GetPersistence();
		if (Persist && (!Persist->LoadSettingsJson(Json) || Json.IsEmpty()))
		{
			Json = UnrealAiSettingsTemplate::DefaultSettingsJson;
		}
		else if (!Persist)
		{
			Json = UnrealAiSettingsTemplate::DefaultSettingsJson;
		}
	}
	else
	{
		Json = UnrealAiSettingsTemplate::DefaultSettingsJson;
	}

	TSharedPtr<FJsonObject> Root;
	if (!UnrealAiSettingsTabUtil::DeserializeRoot(Json, Root) || !Root.IsValid())
	{
		Json = UnrealAiSettingsTemplate::DefaultSettingsJson;
		UnrealAiSettingsTabUtil::DeserializeRoot(Json, Root);
	}
	if (!Root.IsValid())
	{
		Root = MakeShared<FJsonObject>();
	}

	if (Persist)
	{
		UnrealAiSettingsTabUtil::MigrateSettingsToV4IfNeeded(Root, Persist);
	}

	CachedSettingsRoot = UnrealAiSettingsTabUtil::CloneJsonObject(Root);
	FUnrealAiEditorModule::HydrateEditorFocusFromJsonRoot(Root);
	FUnrealAiEditorModule::HydrateSubagentsFromJsonRoot(Root);
	FUnrealAiEditorModule::HydrateAgentCodeTypePreferenceFromJsonRoot(Root);
	FUnrealAiEditorModule::HydrateAutoConfirmDestructiveFromJsonRoot(Root);
	LoadMemorySettingsFromRoot(Root);
	LoadRetrievalSettingsFromRoot(Root);

	if (RetrievalIndexedExtensionsBox.IsValid())
	{
		RetrievalIndexedExtensionsBox->SetText(FText::FromString(RetrievalIndexedExtensionsText));
	}
	if (RetrievalRootPresetCombo.IsValid())
	{
		TSharedPtr<FString> Match;
		for (const TSharedPtr<FString>& Opt : RetrievalRootPresetOptions)
		{
			if (Opt.IsValid() && Opt->Equals(RetrievalRootPresetStr, ESearchCase::IgnoreCase))
			{
				Match = Opt;
				break;
			}
		}
		if (Match.IsValid())
		{
			RetrievalRootPresetCombo->SetSelectedItem(Match);
		}
	}

	if (CodeTypePreferenceCombo.IsValid())
	{
		const FString Pref = FUnrealAiEditorModule::GetAgentCodeTypePreference();
		TSharedPtr<FString> PrefMatch;
		for (const TSharedPtr<FString>& Opt : CodeTypePreferenceOptions)
		{
			if (Opt.IsValid() && *Opt == Pref)
			{
				PrefMatch = Opt;
				break;
			}
		}
		if (PrefMatch.IsValid())
		{
			CodeTypePreferenceCombo->SetSelectedItem(PrefMatch);
		}
	}

	const TSharedPtr<FJsonObject>* ApiObj = nullptr;
	if (Root->TryGetObjectField(TEXT("api"), ApiObj) && ApiObj->IsValid())
	{
		FString S;
		if ((*ApiObj)->TryGetStringField(TEXT("baseUrl"), S) && ApiBaseUrlBox.IsValid())
		{
			ApiBaseUrlBox->SetText(FText::FromString(S));
		}
		if ((*ApiObj)->TryGetStringField(TEXT("apiKey"), S) && ApiKeyBox.IsValid())
		{
			ApiKeyBox->SetText(FText::FromString(S));
		}
		if ((*ApiObj)->TryGetStringField(TEXT("defaultModel"), S) && ApiDefaultModelBox.IsValid())
		{
			ApiDefaultModelBox->SetText(FText::FromString(S));
		}
		if ((*ApiObj)->TryGetStringField(TEXT("defaultProviderId"), S) && ApiDefaultProviderIdBox.IsValid())
		{
			ApiDefaultProviderIdBox->SetText(FText::FromString(S));
		}
	}

	SectionRows.Reset();
	const TArray<TSharedPtr<FJsonValue>>* SecArr = nullptr;
	if (Root->TryGetArrayField(TEXT("sections"), SecArr) && SecArr)
	{
		for (const TSharedPtr<FJsonValue>& Sv : *SecArr)
		{
			const TSharedPtr<FJsonObject>* So = nullptr;
			if (!Sv.IsValid() || !Sv->TryGetObject(So) || !So->IsValid())
			{
				continue;
			}
			FDynSectionRow R;
			(*So)->TryGetStringField(TEXT("id"), R.Id);
			(*So)->TryGetStringField(TEXT("label"), R.Label);
			(*So)->TryGetStringField(TEXT("companyPreset"), R.CompanyPreset);
			(*So)->TryGetStringField(TEXT("baseUrl"), R.BaseUrl);
			(*So)->TryGetStringField(TEXT("apiKey"), R.ApiKey);
			if (R.CompanyPreset.IsEmpty())
			{
				R.CompanyPreset = TEXT("custom");
			}
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
					FDynModelRow Mr;
					(*Mo)->TryGetStringField(TEXT("profileKey"), Mr.ProfileKey);
					(*Mo)->TryGetStringField(TEXT("modelIdForApi"), Mr.ModelIdForApi);
					double Mct = 0;
					if ((*Mo)->TryGetNumberField(TEXT("maxContextTokens"), Mct))
					{
						Mr.MaxContextStr = FString::FromInt(static_cast<int32>(Mct));
					}
					else
					{
						Mr.MaxContextStr = TEXT("128000");
					}
					double Mot = 0;
					if ((*Mo)->TryGetNumberField(TEXT("maxOutputTokens"), Mot))
					{
						Mr.MaxOutputStr = FString::FromInt(static_cast<int32>(Mot));
					}
					else
					{
						Mr.MaxOutputStr = TEXT("4096");
					}
					double Mar = 0;
					if ((*Mo)->TryGetNumberField(TEXT("maxAgentLlmRounds"), Mar))
					{
						Mr.MaxAgentLlmRoundsStr = FString::FromInt(FMath::Clamp(static_cast<int32>(Mar), 1, 512));
					}
					else
					{
						Mr.MaxAgentLlmRoundsStr = TEXT("512");
					}
					(*Mo)->TryGetBoolField(TEXT("supportsNativeTools"), Mr.bSupportsNativeTools);
					(*Mo)->TryGetBoolField(TEXT("supportsParallelToolCalls"), Mr.bSupportsParallelToolCalls);
					{
						bool B = true;
						if ((*Mo)->TryGetBoolField(TEXT("supportsImages"), B))
						{
							Mr.bSupportsImages = B;
						}
					}
					UpdateModelPricingHint(Mr);
					R.Models.Add(MoveTemp(Mr));
				}
			}
			SectionRows.Add(MoveTemp(R));
		}
	}

	if (SectionRows.Num() == 0)
	{
		FDynSectionRow R;
		R.Id = TEXT("openrouter");
		R.Label = TEXT("OpenRouter");
		R.CompanyPreset = TEXT("openrouter");
		R.BaseUrl = TEXT("https://openrouter.ai/api/v1");
		FDynModelRow Mr;
		Mr.ProfileKey = TEXT("openai/gpt-4o-mini");
		Mr.MaxContextStr = TEXT("128000");
		Mr.MaxOutputStr = TEXT("4096");
		Mr.MaxAgentLlmRoundsStr = TEXT("512");
		UpdateModelPricingHint(Mr);
		R.Models.Add(Mr);
		SectionRows.Add(MoveTemp(R));
	}

	RebuildDynamicRows();
	RebuildProviderOptions();
	RefreshProviderDropdownSelection();
	bSuppressAutoSave = false;
}

void SUnrealAiEditorSettingsTab::SyncDynamicRowsFromWidgets()
{
	for (FDynSectionRow& Sec : SectionRows)
	{
		if (Sec.IdBox.IsValid())
		{
			Sec.Id = Sec.IdBox->GetText().ToString();
		}
		if (Sec.LabelBox.IsValid())
		{
			Sec.Label = Sec.LabelBox->GetText().ToString();
		}
		if (Sec.BaseUrlBox.IsValid())
		{
			Sec.BaseUrl = Sec.BaseUrlBox->GetText().ToString();
		}
		if (Sec.ApiKeyBox.IsValid())
		{
			Sec.ApiKey = Sec.ApiKeyBox->GetText().ToString();
		}
		for (FDynModelRow& Mr : Sec.Models)
		{
			if (Mr.ProfileKeyBox.IsValid())
			{
				Mr.ProfileKey = Mr.ProfileKeyBox->GetText().ToString();
			}
			if (Mr.ModelIdForApiBox.IsValid())
			{
				Mr.ModelIdForApi = Mr.ModelIdForApiBox->GetText().ToString();
			}
			if (Mr.ModelSearchBox.IsValid())
			{
				Mr.ModelSearchText = Mr.ModelSearchBox->GetText().ToString();
			}
			if (Mr.MaxContextBox.IsValid())
			{
				Mr.MaxContextStr = Mr.MaxContextBox->GetText().ToString();
			}
			if (Mr.MaxOutputBox.IsValid())
			{
				Mr.MaxOutputStr = Mr.MaxOutputBox->GetText().ToString();
			}
			if (Mr.MaxAgentLlmRoundsBox.IsValid())
			{
				Mr.MaxAgentLlmRoundsStr = Mr.MaxAgentLlmRoundsBox->GetText().ToString();
			}
			UpdateModelPricingHint(Mr);
		}
	}
}

void SUnrealAiEditorSettingsTab::RebuildDynamicRows()
{
	if (!SectionsVBox.IsValid())
	{
		return;
	}
	SectionsVBox->ClearChildren();
	for (int32 Si = 0; Si < SectionRows.Num(); ++Si)
	{
		const int32 SectionIdx = Si;
		FDynSectionRow& Sec = SectionRows[SectionIdx];
		Sec.IdBox.Reset();
		Sec.LabelBox.Reset();
		Sec.BaseUrlBox.Reset();
		Sec.ApiKeyBox.Reset();
		Sec.CompanyCombo.Reset();
		for (FDynModelRow& Mr : Sec.Models)
		{
			Mr.ProfileKeyBox.Reset();
			Mr.ModelIdForApiBox.Reset();
			Mr.ModelIdDropdown.Reset();
			Mr.ModelSearchBox.Reset();
			Mr.ModelIdOptions.Reset();
			Mr.MaxContextBox.Reset();
			Mr.MaxOutputBox.Reset();
			Mr.MaxAgentLlmRoundsBox.Reset();
		}

		TSharedPtr<FString> InitialPreset = CompanyPresetOptions[0];
		for (const TSharedPtr<FString>& Opt : CompanyPresetOptions)
		{
			if (Opt.IsValid() && *Opt == Sec.CompanyPreset)
			{
				InitialPreset = Opt;
				break;
			}
		}

		TSharedPtr<SVerticalBox> ModelsVBox;

		SectionsVBox->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 0.f, 0.f, 12.f))
			[
				SNew(SBorder)
					.Padding(FMargin(8.f))
					.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
							[
								SNew(STextBlock).Font(FUnrealAiEditorStyle::FontComposerBadge()).Text(FText::Format(LOCTEXT("SecTitle", "Section: {0}"), FText::FromString(Sec.Label.IsEmpty() ? Sec.Id : Sec.Label)))
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
							[
								SNew(STextBlock).Text(LOCTEXT("CompanyLbl", "Company"))
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SAssignNew(Sec.CompanyCombo, SComboBox<TSharedPtr<FString>>)
									.OptionsSource(&CompanyPresetOptions)
									.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
									{
										return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? **Item : FString()));
									})
									.InitiallySelectedItem(InitialPreset)
									.OnSelectionChanged_Lambda([this, SectionIdx](TSharedPtr<FString> Sel, ESelectInfo::Type)
									{
										if (!SectionRows.IsValidIndex(SectionIdx) || !Sel.IsValid())
										{
											return;
										}
										ApplyCompanyPreset(SectionRows[SectionIdx], *Sel);
										if (SectionRows[SectionIdx].BaseUrlBox.IsValid())
										{
											SectionRows[SectionIdx].BaseUrlBox->SetText(FText::FromString(SectionRows[SectionIdx].BaseUrl));
										}
										OnAnySettingsChanged(FText::GetEmpty());
										RebuildDynamicRows();
									})
									.Content()
									[
										SNew(STextBlock)
											.Text_Lambda([this, SectionIdx]()
											{
												return FText::FromString(
													SectionRows.IsValidIndex(SectionIdx) ? SectionRows[SectionIdx].CompanyPreset : FString());
											})
									]
							]
							+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(12.f, 0.f, 0.f, 0.f))
							[
								SNew(SButton)
									.Text(LOCTEXT("RemoveSec", "Remove section"))
									.OnClicked_Lambda([this, SectionIdx]() { return OnRemoveSectionClicked(SectionIdx); })
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(4.f, 0.f, 4.f, 8.f))
						[
							SNew(STextBlock)
								.AutoWrapText(true)
								.Font(FUnrealAiEditorStyle::FontBodySmall())
								.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
								.Text_Lambda([this, SectionIdx]()
								{
									const FString P = SectionRows.IsValidIndex(SectionIdx) ? SectionRows[SectionIdx].CompanyPreset : FString();
									return CompanyHintText(P);
								})
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SGridPanel).FillColumn(1, 1.f)
							+ SGridPanel::Slot(0, 0).Padding(4.f)
							[
								SNew(STextBlock).Text(LOCTEXT("SecId", "Section id"))
							]
							+ SGridPanel::Slot(1, 0).Padding(4.f)
							[
								SAssignNew(Sec.IdBox, SEditableTextBox)
									.Text(FText::FromString(Sec.Id))
									.OnTextChanged(this, &SUnrealAiEditorSettingsTab::OnAnySettingsChanged)
							]
							+ SGridPanel::Slot(0, 1).Padding(4.f)
							[
								SNew(STextBlock).Text(LOCTEXT("SecLabel", "Label"))
							]
							+ SGridPanel::Slot(1, 1).Padding(4.f)
							[
								SAssignNew(Sec.LabelBox, SEditableTextBox)
									.Text(FText::FromString(Sec.Label))
									.OnTextChanged(this, &SUnrealAiEditorSettingsTab::OnAnySettingsChanged)
							]
							+ SGridPanel::Slot(0, 2).Padding(4.f)
							[
								SNew(STextBlock).Text(LOCTEXT("SecBase", "Base URL"))
							]
							+ SGridPanel::Slot(1, 2).Padding(4.f)
							[
								SAssignNew(Sec.BaseUrlBox, SEditableTextBox)
									.Text(FText::FromString(Sec.BaseUrl))
									.OnTextChanged(this, &SUnrealAiEditorSettingsTab::OnAnySettingsChanged)
							]
							+ SGridPanel::Slot(0, 3).Padding(4.f)
							[
								SNew(STextBlock).Text(LOCTEXT("SecKey", "API key"))
							]
							+ SGridPanel::Slot(1, 3).Padding(4.f)
							[
								SAssignNew(Sec.ApiKeyBox, SEditableTextBox)
									.Text(FText::FromString(Sec.ApiKey))
									.OnTextChanged(this, &SUnrealAiEditorSettingsTab::OnAnySettingsChanged)
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 4.f))
						[
							SNew(SButton)
								.Text(LOCTEXT("AddModelSec", "Add model to this section"))
								.OnClicked_Lambda([this, SectionIdx]() { return OnAddModelClicked(SectionIdx); })
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SAssignNew(ModelsVBox, SVerticalBox)
						]
					]
			];

		if (ModelsVBox.IsValid())
		{
			for (int32 Mi = 0; Mi < Sec.Models.Num(); ++Mi)
			{
				const int32 ModelIdx = Mi;
				FDynModelRow& Mr = Sec.Models[ModelIdx];
				UnrealAiSettingsTabUtil::BuildModelOptionsForPreset(Sec.CompanyPreset, Mr.ModelSearchText, Mr.ModelIdOptions);
				UpdateModelPricingHint(Mr);
				ModelsVBox->AddSlot()
					.AutoHeight()
					.Padding(FMargin(0.f, 0.f, 0.f, 8.f))
					[
						SNew(SBorder)
							.Padding(FMargin(6.f))
							.BorderImage(FCoreStyle::Get().GetBrush("ToolPanel.GroupBorder"))
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								[
									SNew(SGridPanel).FillColumn(1, 1.f)
									+ SGridPanel::Slot(0, 0).Padding(4.f)
									[
										SNew(STextBlock).Text(LOCTEXT("ProfKey", "Profile key"))
									]
									+ SGridPanel::Slot(1, 0).Padding(4.f)
									[
										SAssignNew(Mr.ProfileKeyBox, SEditableTextBox)
											.Text(FText::FromString(Mr.ProfileKey))
											.OnTextChanged(this, &SUnrealAiEditorSettingsTab::OnAnySettingsChanged)
									]
									+ SGridPanel::Slot(0, 1).Padding(4.f)
									[
										SNew(STextBlock).Text(LOCTEXT("ModelIdApi2", "Model id for API"))
									]
									+ SGridPanel::Slot(1, 1).Padding(4.f)
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
										[
											SAssignNew(Mr.ModelIdForApiBox, SEditableTextBox)
												.Text(FText::FromString(Mr.ModelIdForApi))
												.OnTextChanged(this, &SUnrealAiEditorSettingsTab::OnAnySettingsChanged)
										]
										+ SHorizontalBox::Slot().FillWidth(1.f)
										[
											SNew(SVerticalBox)
											+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
											[
												SAssignNew(Mr.ModelSearchBox, SEditableTextBox)
													.Text(FText::FromString(Mr.ModelSearchText))
													.HintText(LOCTEXT("ModelSearchHint", "Search known models for this provider..."))
													.OnTextChanged_Lambda([this, &Mr](const FText& T)
													{
														Mr.ModelSearchText = T.ToString();
														RebuildDynamicRows();
														OnAnySettingsChanged(T);
													})
											]
											+ SVerticalBox::Slot().AutoHeight()
											[
												SAssignNew(Mr.ModelIdDropdown, SComboBox<TSharedPtr<FString>>)
													.OptionsSource(&Mr.ModelIdOptions)
													.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
													{
														return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : FString()));
													})
													.OnSelectionChanged_Lambda([this, &Mr](TSharedPtr<FString> NewSelection, ESelectInfo::Type)
													{
														if (!NewSelection.IsValid() || !Mr.ModelIdForApiBox.IsValid())
														{
															return;
														}
														const FString SelectedModel = *NewSelection;
														Mr.ModelIdForApiBox->SetText(FText::FromString(SelectedModel));
														if (Mr.ProfileKeyBox.IsValid() && Mr.ProfileKeyBox->GetText().ToString().IsEmpty())
														{
															Mr.ProfileKeyBox->SetText(FText::FromString(SelectedModel));
														}
														OnAnySettingsChanged(FText::FromString(SelectedModel));
													})
													.Content()
													[
														SNew(STextBlock)
															.Text_Lambda([&Mr]()
															{
																const FString Current = Mr.ModelIdForApiBox.IsValid()
																	? Mr.ModelIdForApiBox->GetText().ToString()
																	: Mr.ModelIdForApi;
																if (!Current.IsEmpty())
																{
																	return FText::FromString(Current);
																}
																return FText::FromString(
																	Mr.ModelIdOptions.Num() > 0
																		? FString::Printf(TEXT("Pick known model (%d matches)..."), Mr.ModelIdOptions.Num())
																		: TEXT("No matches for filter"));
															})
													]
											]
										]
									]
									+ SGridPanel::Slot(0, 2).Padding(4.f)
									[
										SNew(STextBlock).Text(LOCTEXT("MaxCtx2", "Max context tokens"))
									]
									+ SGridPanel::Slot(1, 2).Padding(4.f)
									[
										SAssignNew(Mr.MaxContextBox, SEditableTextBox)
											.Text(FText::FromString(Mr.MaxContextStr))
											.OnTextChanged(this, &SUnrealAiEditorSettingsTab::OnAnySettingsChanged)
									]
									+ SGridPanel::Slot(0, 3).Padding(4.f)
									[
										SNew(STextBlock).Text(LOCTEXT("MaxOut2", "Max output tokens"))
									]
									+ SGridPanel::Slot(1, 3).Padding(4.f)
									[
										SAssignNew(Mr.MaxOutputBox, SEditableTextBox)
											.Text(FText::FromString(Mr.MaxOutputStr))
											.OnTextChanged(this, &SUnrealAiEditorSettingsTab::OnAnySettingsChanged)
									]
									+ SGridPanel::Slot(0, 4).Padding(4.f)
									[
										SNew(STextBlock)
											.Text(LOCTEXT("MaxAgentRoundsLbl", "Agent LLM rounds"))
											.ToolTipText(LOCTEXT(
												"MaxAgentRoundsTip",
												"Maximum number of model completions (assistant messages) per chat send, "
												"including tool rounds. Raise this to allow longer multi-step agent runs; "
												"higher values usually consume more API tokens."))
									]
									+ SGridPanel::Slot(1, 4).Padding(4.f)
									[
										SAssignNew(Mr.MaxAgentLlmRoundsBox, SEditableTextBox)
											.Text(FText::FromString(Mr.MaxAgentLlmRoundsStr))
											.OnTextChanged(this, &SUnrealAiEditorSettingsTab::OnAnySettingsChanged)
											.ToolTipText(LOCTEXT(
												"MaxAgentRoundsTipEdit",
												"Maximum completions per send (tool↔LLM iterations). Default 32. "
												"Higher values allow more agent steps but typically use more tokens."))
									]
									+ SGridPanel::Slot(0, 5).Padding(4.f)
									[
										SNew(STextBlock).Text(LOCTEXT("UsageDropLbl", "Usage (session)"))
									]
									+ SGridPanel::Slot(1, 5).Padding(4.f)
									[
										SNew(SComboButton)
											.ButtonContent()
											[
												SNew(STextBlock)
													.Font(FUnrealAiEditorStyle::FontCaption())
													.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
													.Text_Lambda([this, SectionIdx, ModelIdx]()
													{
														if (!BackendRegistry.IsValid())
														{
															return FText::GetEmpty();
														}
														if (!SectionRows.IsValidIndex(SectionIdx))
														{
															return FText::GetEmpty();
														}
														const FDynSectionRow& SR = SectionRows[SectionIdx];
														if (!SR.Models.IsValidIndex(ModelIdx))
														{
															return FText::GetEmpty();
														}
														const SUnrealAiEditorSettingsTab::FDynModelRow& MR = SR.Models[ModelIdx];
														const FString Pk = MR.ProfileKeyBox.IsValid()
															? MR.ProfileKeyBox->GetText().ToString()
															: MR.ProfileKey;
														FString Line;
														bool bHasPrice = false;
														if (FUnrealAiUsageTracker* U = BackendRegistry->GetUsageTracker())
														{
															U->PerModelSummary(
																Pk,
																*BackendRegistry->GetModelProfileRegistry(),
																Line,
																bHasPrice);
														}
														return FText::FromString(Line);
													})
											]
											.OnGetMenuContent_Lambda([this, SectionIdx, ModelIdx]()
											{
												SyncDynamicRowsFromWidgets();
												FString Line;
												bool bHasPrice = false;
												if (BackendRegistry.IsValid())
												{
													if (FUnrealAiUsageTracker* U = BackendRegistry->GetUsageTracker())
													{
														if (SectionRows.IsValidIndex(SectionIdx)
															&& SectionRows[SectionIdx].Models.IsValidIndex(ModelIdx))
														{
															const FString Pk = SectionRows[SectionIdx].Models[ModelIdx].ProfileKey;
															U->PerModelSummary(
																Pk,
																*BackendRegistry->GetModelProfileRegistry(),
																Line,
																bHasPrice);
														}
													}
												}
												return SNew(SBorder)
													.Padding(FMargin(10.f))
													[
														SNew(STextBlock)
															.Font(FUnrealAiEditorStyle::FontBodySmall())
															.WrapTextAt(280.f)
															.Text(FText::FromString(Line.IsEmpty() ? TEXT("No usage recorded yet for this profile key.") : Line))
													];
											})
									]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(6.f, 2.f))
								[
									SNew(STextBlock)
										.Font(FUnrealAiEditorStyle::FontCaption())
										.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextFooter())
										.Text_Lambda([this, SectionIdx, ModelIdx]()
										{
											if (!SectionRows.IsValidIndex(SectionIdx)
												|| !SectionRows[SectionIdx].Models.IsValidIndex(ModelIdx))
											{
												return FText::GetEmpty();
											}
											const FString H = SectionRows[SectionIdx].Models[ModelIdx].PricingHintCache;
											return H.IsEmpty() ? LOCTEXT("NoPriceHint", "No bundled price row for this id — estimates unavailable.") : FText::FromString(H);
										})
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(4.f, 0.f))
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 16.f, 0.f))
									[
										SNew(SCheckBox).Style(&FUnrealAiEditorStyle::GetCheckboxStyle())
											.IsChecked_Lambda([this, SectionIdx, ModelIdx]()
											{
												return SectionRows.IsValidIndex(SectionIdx)
														&& SectionRows[SectionIdx].Models.IsValidIndex(ModelIdx)
														&& SectionRows[SectionIdx].Models[ModelIdx].bSupportsNativeTools
													? ECheckBoxState::Checked
													: ECheckBoxState::Unchecked;
											})
											.OnCheckStateChanged_Lambda([this, SectionIdx, ModelIdx](ECheckBoxState S)
											{
												if (SectionRows.IsValidIndex(SectionIdx)
													&& SectionRows[SectionIdx].Models.IsValidIndex(ModelIdx))
												{
													SectionRows[SectionIdx].Models[ModelIdx].bSupportsNativeTools = (S == ECheckBoxState::Checked);
												}
												OnAnySettingsChanged(FText::GetEmpty());
											})
											[
												SNew(STextBlock).Text(LOCTEXT("NatTools2", "Native tools"))
											]
									]
									+ SHorizontalBox::Slot().AutoWidth()
									[
										SNew(SCheckBox).Style(&FUnrealAiEditorStyle::GetCheckboxStyle())
											.IsChecked_Lambda([this, SectionIdx, ModelIdx]()
											{
												return SectionRows.IsValidIndex(SectionIdx)
														&& SectionRows[SectionIdx].Models.IsValidIndex(ModelIdx)
														&& SectionRows[SectionIdx].Models[ModelIdx].bSupportsParallelToolCalls
													? ECheckBoxState::Checked
													: ECheckBoxState::Unchecked;
											})
											.OnCheckStateChanged_Lambda([this, SectionIdx, ModelIdx](ECheckBoxState S)
											{
												if (SectionRows.IsValidIndex(SectionIdx)
													&& SectionRows[SectionIdx].Models.IsValidIndex(ModelIdx))
												{
													SectionRows[SectionIdx].Models[ModelIdx].bSupportsParallelToolCalls = (S == ECheckBoxState::Checked);
												}
												OnAnySettingsChanged(FText::GetEmpty());
											})
											[
												SNew(STextBlock).Text(LOCTEXT("ParTools2", "Parallel tool calls"))
											]
									]
									+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(16.f, 0.f, 0.f, 0.f))
									[
										SNew(SCheckBox).Style(&FUnrealAiEditorStyle::GetCheckboxStyle())
											.IsChecked_Lambda([this, SectionIdx, ModelIdx]()
											{
												return SectionRows.IsValidIndex(SectionIdx)
														&& SectionRows[SectionIdx].Models.IsValidIndex(ModelIdx)
														&& SectionRows[SectionIdx].Models[ModelIdx].bSupportsImages
													? ECheckBoxState::Checked
													: ECheckBoxState::Unchecked;
											})
											.OnCheckStateChanged_Lambda([this, SectionIdx, ModelIdx](ECheckBoxState S)
											{
												if (SectionRows.IsValidIndex(SectionIdx)
													&& SectionRows[SectionIdx].Models.IsValidIndex(ModelIdx))
												{
													SectionRows[SectionIdx].Models[ModelIdx].bSupportsImages = (S == ECheckBoxState::Checked);
												}
												OnAnySettingsChanged(FText::GetEmpty());
											})
											[
												SNew(STextBlock).Text(LOCTEXT("SuppImages2", "Support images"))
											]
									]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
								[
									SNew(SButton)
										.Text(LOCTEXT("RemoveModel2", "Remove model"))
										.OnClicked_Lambda([this, SectionIdx, ModelIdx]()
										{
											return OnRemoveModelClicked(SectionIdx, ModelIdx);
										})
								]
							]
					];
			}
		}
	}
	RebuildProviderOptions();
	RefreshProviderDropdownSelection();
}

bool SUnrealAiEditorSettingsTab::BuildJsonFromUi(FString& OutJson, FString& OutError)
{
	SyncDynamicRowsFromWidgets();

	TSharedPtr<FJsonObject> Root = UnrealAiSettingsTabUtil::CloneJsonObject(CachedSettingsRoot);
	if (!Root.IsValid())
	{
		Root = MakeShared<FJsonObject>();
	}

	Root->SetNumberField(TEXT("version"), 4);

	TSharedPtr<FJsonObject> Api = MakeShared<FJsonObject>();
	if (ApiBaseUrlBox.IsValid())
	{
		Api->SetStringField(TEXT("baseUrl"), ApiBaseUrlBox->GetText().ToString());
	}
	if (ApiKeyBox.IsValid())
	{
		Api->SetStringField(TEXT("apiKey"), ApiKeyBox->GetText().ToString());
	}
	if (ApiDefaultModelBox.IsValid())
	{
		Api->SetStringField(TEXT("defaultModel"), ApiDefaultModelBox->GetText().ToString());
	}
	if (ApiDefaultProviderIdBox.IsValid())
	{
		Api->SetStringField(TEXT("defaultProviderId"), ApiDefaultProviderIdBox->GetText().ToString());
	}
	Root->SetObjectField(TEXT("api"), Api);
	WriteMemorySettingsToRoot(Root);
	WriteRetrievalSettingsToRoot(Root);

	{
		TSharedPtr<FJsonObject> AgentObj = MakeShared<FJsonObject>();
		const TSharedPtr<FJsonObject>* ExistingAgent = nullptr;
		if (Root->TryGetObjectField(TEXT("agent"), ExistingAgent) && ExistingAgent && ExistingAgent->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ExistingAgent)->Values)
			{
				AgentObj->SetField(Pair.Key, Pair.Value);
			}
		}
		AgentObj->SetBoolField(TEXT("useSubagents"), FUnrealAiEditorModule::IsSubagentsEnabled());
		AgentObj->SetStringField(TEXT("codeTypePreference"), FUnrealAiEditorModule::GetAgentCodeTypePreference());
		AgentObj->SetBoolField(TEXT("autoConfirmDestructive"), FUnrealAiEditorModule::IsAutoConfirmDestructiveEnabled());
		Root->SetObjectField(TEXT("agent"), AgentObj);
	}

	{
		TSharedPtr<FJsonObject> UiObj = MakeShared<FJsonObject>();
		const TSharedPtr<FJsonObject>* ExistingUi = nullptr;
		if (Root->TryGetObjectField(TEXT("ui"), ExistingUi) && ExistingUi && ExistingUi->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ExistingUi)->Values)
			{
				UiObj->SetField(Pair.Key, Pair.Value);
			}
		}
		UiObj->SetBoolField(TEXT("editorFocus"), FUnrealAiEditorModule::IsEditorFocusEnabled());
		Root->SetObjectField(TEXT("ui"), UiObj);
	}

	TArray<TSharedPtr<FJsonValue>> SecArr;
	for (const FDynSectionRow& Sec : SectionRows)
	{
		if (Sec.Id.IsEmpty())
		{
			continue;
		}
		TSharedPtr<FJsonObject> So = MakeShared<FJsonObject>();
		So->SetStringField(TEXT("id"), Sec.Id);
		So->SetStringField(TEXT("label"), Sec.Label.IsEmpty() ? Sec.Id : Sec.Label);
		So->SetStringField(TEXT("companyPreset"), Sec.CompanyPreset.IsEmpty() ? TEXT("custom") : Sec.CompanyPreset);
		So->SetStringField(TEXT("baseUrl"), Sec.BaseUrl);
		So->SetStringField(TEXT("apiKey"), Sec.ApiKey);

		TArray<TSharedPtr<FJsonValue>> ModelArr;
		for (const FDynModelRow& Mr : Sec.Models)
		{
			if (Mr.ProfileKey.IsEmpty())
			{
				continue;
			}
			TSharedPtr<FJsonObject> Mo = MakeShared<FJsonObject>();
			Mo->SetStringField(TEXT("profileKey"), Mr.ProfileKey);
			if (!Mr.ModelIdForApi.IsEmpty())
			{
				Mo->SetStringField(TEXT("modelIdForApi"), Mr.ModelIdForApi);
			}
			const int32 Ctx = UnrealAiSettingsTabUtil::ParsePositiveInt(Mr.MaxContextStr, 128000);
			const int32 MaxOutTok = UnrealAiSettingsTabUtil::ParsePositiveInt(Mr.MaxOutputStr, 4096);
			int32 MaxRounds = UnrealAiSettingsTabUtil::ParsePositiveInt(Mr.MaxAgentLlmRoundsStr, 512);
			if (MaxRounds <= 0)
			{
				MaxRounds = 512;
			}
			MaxRounds = FMath::Clamp(MaxRounds, 1, 512);
			Mo->SetNumberField(TEXT("maxContextTokens"), Ctx);
			Mo->SetNumberField(TEXT("maxOutputTokens"), MaxOutTok);
			Mo->SetNumberField(TEXT("maxAgentLlmRounds"), MaxRounds);
			Mo->SetBoolField(TEXT("supportsNativeTools"), Mr.bSupportsNativeTools);
			Mo->SetBoolField(TEXT("supportsParallelToolCalls"), Mr.bSupportsParallelToolCalls);
			Mo->SetBoolField(TEXT("supportsImages"), Mr.bSupportsImages);
			ModelArr.Add(MakeShared<FJsonValueObject>(Mo.ToSharedRef()));
		}
		So->SetArrayField(TEXT("models"), ModelArr);
		SecArr.Add(MakeShared<FJsonValueObject>(So.ToSharedRef()));
	}
	Root->SetArrayField(TEXT("sections"), SecArr);
	Root->RemoveField(TEXT("providers"));
	Root->RemoveField(TEXT("models"));

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), W))
	{
		OutError = TEXT("Failed to serialize settings.");
		return false;
	}
	OutJson = MoveTemp(Out);
	return true;
}

FReply SUnrealAiEditorSettingsTab::OnSaveClicked()
{
	if (!BackendRegistry.IsValid())
	{
		return FReply::Handled();
	}
	FString Json;
	FString Err;
	if (!BuildJsonFromUi(Json, Err))
	{
		StatusText = FText::FromString(FString::Printf(TEXT("Error: %s"), *Err));
		Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
		return FReply::Handled();
	}
	if (IUnrealAiPersistence* P = BackendRegistry->GetPersistence())
	{
		P->SaveSettingsJson(Json);
	}
	TSharedPtr<FJsonObject> Reloaded;
	if (UnrealAiSettingsTabUtil::DeserializeRoot(Json, Reloaded) && Reloaded.IsValid())
	{
		CachedSettingsRoot = UnrealAiSettingsTabUtil::CloneJsonObject(Reloaded);
	}
	BackendRegistry->ReloadLlmConfiguration();
	StatusText = LOCTEXT("SavedReloaded", "Saved — LLM stack reloaded.");
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	RefreshUsageHeaderText();
	return FReply::Handled();
}

FReply SUnrealAiEditorSettingsTab::OnTestConnectionClicked()
{
	StatusText = LOCTEXT("StatusTesting", "Testing…");
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	if (BackendRegistry.IsValid())
	{
		PersistSettingsToDisk();
		CancelDeferredReload();
		BackendRegistry->ReloadLlmConfiguration();
		if (IUnrealAiModelConnector* M = BackendRegistry->GetModelConnector())
		{
			M->TestConnection(FUnrealAiModelTestResultDelegate::CreateLambda(
				[this](bool bOk)
				{
					StatusText = bOk ? LOCTEXT("Ok", "Connection OK (GET /models).") : LOCTEXT("Fail", "Connection failed — check URL, keys, and network.");
					Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
				}));
		}
	}
	return FReply::Handled();
}

FReply SUnrealAiEditorSettingsTab::OnViewMyDataClicked()
{
	if (!BackendRegistry.IsValid())
	{
		return FReply::Handled();
	}
	IUnrealAiPersistence* P = BackendRegistry->GetPersistence();
	if (!P)
	{
		return FReply::Handled();
	}
	const FString Root = FPaths::ConvertRelativePathToFull(P->GetDataRootDirectory());
	IFileManager::Get().MakeDirectory(*Root, true);
	FPlatformProcess::ExploreFolder(*Root);
	return FReply::Handled();
}

FReply SUnrealAiEditorSettingsTab::OnAddSectionClicked()
{
	SyncDynamicRowsFromWidgets();
	FDynSectionRow Row;
	Row.Id = FGuid::NewGuid().ToString();
	Row.Label = LOCTEXT("NewSecLabel", "New section").ToString();
	Row.CompanyPreset = TEXT("custom");
	Row.BaseUrl = ApiBaseUrlBox.IsValid() ? ApiBaseUrlBox->GetText().ToString() : FString();
	SectionRows.Add(MoveTemp(Row));
	RebuildDynamicRows();
	OnAnySettingsChanged(FText::GetEmpty());
	return FReply::Handled();
}

FReply SUnrealAiEditorSettingsTab::OnRemoveSectionClicked(int32 SectionIndex)
{
	SyncDynamicRowsFromWidgets();
	if (SectionRows.IsValidIndex(SectionIndex))
	{
		SectionRows.RemoveAt(SectionIndex);
		RebuildDynamicRows();
		OnAnySettingsChanged(FText::GetEmpty());
	}
	return FReply::Handled();
}

FReply SUnrealAiEditorSettingsTab::OnAddModelClicked(int32 SectionIndex)
{
	SyncDynamicRowsFromWidgets();
	if (!SectionRows.IsValidIndex(SectionIndex))
	{
		return FReply::Handled();
	}
	FDynModelRow Mr;
	Mr.ProfileKey = TEXT("openai/gpt-4o-mini");
	Mr.MaxContextStr = TEXT("128000");
	Mr.MaxOutputStr = TEXT("4096");
	Mr.MaxAgentLlmRoundsStr = TEXT("512");
	UpdateModelPricingHint(Mr);
	SectionRows[SectionIndex].Models.Add(MoveTemp(Mr));
	RebuildDynamicRows();
	OnAnySettingsChanged(FText::GetEmpty());
	return FReply::Handled();
}

FReply SUnrealAiEditorSettingsTab::OnRemoveModelClicked(int32 SectionIndex, int32 ModelIndex)
{
	SyncDynamicRowsFromWidgets();
	if (SectionRows.IsValidIndex(SectionIndex) && SectionRows[SectionIndex].Models.IsValidIndex(ModelIndex))
	{
		SectionRows[SectionIndex].Models.RemoveAt(ModelIndex);
		RebuildDynamicRows();
		OnAnySettingsChanged(FText::GetEmpty());
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
