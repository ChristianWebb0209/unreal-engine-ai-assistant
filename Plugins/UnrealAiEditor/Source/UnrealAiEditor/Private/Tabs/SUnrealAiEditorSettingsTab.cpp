#include "Tabs/SUnrealAiEditorSettingsTab.h"

#include "Backend/FUnrealAiUsageTracker.h"
#include "Backend/IUnrealAiModelConnector.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Framework/Docking/TabManager.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Pricing/FUnrealAiModelPricingCatalog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

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
		"\t]\n"
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

	ChildSlot
		[SNew(SBorder)
				.Padding(FMargin(12.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
					[
						SAssignNew(UsageSummaryBlock, STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.58f, 1.f)))
							.WrapTextAt(560.f)
							.Text(LOCTEXT("UsagePlaceholder", "Usage: loading…"))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
					[
						SNew(STextBlock)
							.Text(LOCTEXT("SettingsTitle", "AI Settings"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
					[
						SNew(STextBlock)
							.AutoWrapText(true)
							.Text(LOCTEXT(
								"SettingsHelp",
								"Add API key sections (one provider account each) and attach any number of model profiles. "
								"Rough token pricing uses a bundled Litellm dataset when the model id matches; unknown models still work. "
								"Settings save automatically; use Save and apply for an immediate LLM reload."))
					]
					+ SVerticalBox::Slot().FillHeight(1.f).Padding(FMargin(0.f, 4.f))
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
											.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
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
											SAssignNew(ApiDefaultProviderIdBox, SEditableTextBox)
												.OnTextChanged(this, &SUnrealAiEditorSettingsTab::OnAnySettingsChanged)
										]
									]
									+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
										[
											SNew(STextBlock)
												.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
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
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.52f, 0.55f, 1.f)))
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
				]];

	LoadSettingsIntoUi();
	RefreshUsageHeaderText();

	TWeakPtr<SUnrealAiEditorSettingsTab> WeakTab(StaticCastSharedRef<SUnrealAiEditorSettingsTab>(AsShared()));
	UsageTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakTab](float) -> bool
		{
			if (TSharedPtr<SUnrealAiEditorSettingsTab> Tab = WeakTab.Pin())
			{
				Tab->RefreshUsageHeaderText();
			}
			return true;
		}),
		1.5f);
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
						Mr.MaxAgentLlmRoundsStr = FString::FromInt(FMath::Clamp(static_cast<int32>(Mar), 1, 256));
					}
					else
					{
						Mr.MaxAgentLlmRoundsStr = TEXT("16");
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
		Mr.MaxAgentLlmRoundsStr = TEXT("16");
		UpdateModelPricingHint(Mr);
		R.Models.Add(Mr);
		SectionRows.Add(MoveTemp(R));
	}

	RebuildDynamicRows();
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
								SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 11)).Text(FText::Format(LOCTEXT("SecTitle", "Section: {0}"), FText::FromString(Sec.Label.IsEmpty() ? Sec.Id : Sec.Label)))
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
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.58f, 1.f)))
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
										SNew(STextBlock).Text(LOCTEXT("ModelIdApi2", "Model id for API (optional)"))
									]
									+ SGridPanel::Slot(1, 1).Padding(4.f)
									[
										SAssignNew(Mr.ModelIdForApiBox, SEditableTextBox)
											.Text(FText::FromString(Mr.ModelIdForApi))
											.OnTextChanged(this, &SUnrealAiEditorSettingsTab::OnAnySettingsChanged)
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
												"Maximum completions per send (tool↔LLM iterations). Default 16. "
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
													.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
													.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.58f, 1.f)))
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
															.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
															.WrapTextAt(280.f)
															.Text(FText::FromString(Line.IsEmpty() ? TEXT("No usage recorded yet for this profile key.") : Line))
													];
											})
									]
								]
								+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(6.f, 2.f))
								[
									SNew(STextBlock)
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
										.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.52f, 0.55f, 1.f)))
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
										SNew(SCheckBox)
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
										SNew(SCheckBox)
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
										SNew(SCheckBox)
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
			int32 MaxRounds = UnrealAiSettingsTabUtil::ParsePositiveInt(Mr.MaxAgentLlmRoundsStr, 16);
			if (MaxRounds <= 0)
			{
				MaxRounds = 16;
			}
			MaxRounds = FMath::Clamp(MaxRounds, 1, 256);
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
	Mr.MaxAgentLlmRoundsStr = TEXT("16");
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
