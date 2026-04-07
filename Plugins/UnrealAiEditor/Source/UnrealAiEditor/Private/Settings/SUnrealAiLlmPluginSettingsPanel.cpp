#include "Settings/SUnrealAiLlmPluginSettingsPanel.h"

#include "Settings/UnrealAiSettingsJsonUtil.h"
#include "UnrealAiEditorModule.h"
#include "Style/UnrealAiEditorStyle.h"
#include "UnrealAiEditorSettings.h"
#include "Backend/FUnrealAiUsageTracker.h"
#include "Context/UnrealAiProjectId.h"
#include "Context/UnrealAiStartupOpsStatus.h"
#include "Retrieval/IUnrealAiRetrievalService.h"
#include "Retrieval/UnrealAiRetrievalTypes.h"
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
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void SUnrealAiLlmPluginSettingsPanel::Construct(const FArguments& InArgs)
{
	BackendRegistry = InArgs._BackendRegistry;
	StatusText = FText::FromString(UnrealAiStartupOpsStatus::BuildCompactLine(
		BackendRegistry,
		UnrealAiProjectId::GetCurrentProjectId()));

	CompanyPresetOptions.Add(MakeShared<FString>(TEXT("custom")));
	CompanyPresetOptions.Add(MakeShared<FString>(TEXT("openai")));
	CompanyPresetOptions.Add(MakeShared<FString>(TEXT("anthropic")));
	CompanyPresetOptions.Add(MakeShared<FString>(TEXT("groq")));
	CompanyPresetOptions.Add(MakeShared<FString>(TEXT("deepseek")));
	CompanyPresetOptions.Add(MakeShared<FString>(TEXT("xai")));
	CompanyPresetOptions.Add(MakeShared<FString>(TEXT("azure")));

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
							.Text(LOCTEXT("SettingsTitle", "LLM & API"))
							.Font(FUnrealAiEditorStyle::FontWindowTitle())
					]
					+ SVerticalBox::Slot().FillHeight(1.f).Padding(FMargin(0.f, 4.f))
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
												SNew(SExpandableArea)
													.InitiallyCollapsed(true)
													.AreaTitle(LOCTEXT("SecGlobal", "Global API defaults"))
													.HeaderContent()
													[
														SNew(STextBlock)
															.Font(FUnrealAiEditorStyle::FontSectionHeading())
															.Text(LOCTEXT("SecGlobal", "Global API defaults"))
													]
													.BodyContent()
													[
														SNew(SGridPanel).FillColumn(1, 1.f)
												+ SGridPanel::Slot(0, 0).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("BaseUrl", "Base URL"))
												]
												+ SGridPanel::Slot(1, 0).Padding(4.f)
												[
													SAssignNew(ApiBaseUrlBox, SEditableTextBox)
														.Text(FText::FromString(TEXT("https://api.openai.com/v1")))
														.OnTextChanged(this, &SUnrealAiLlmPluginSettingsPanel::OnAnySettingsChanged)
												]
												+ SGridPanel::Slot(0, 1).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("ApiKeyGlobal", "Global API key (optional)"))
												]
												+ SGridPanel::Slot(1, 1).Padding(4.f)
												[
													SAssignNew(ApiKeyBox, SEditableTextBox)
														.OnTextChanged(this, &SUnrealAiLlmPluginSettingsPanel::OnAnySettingsChanged)
												]
												+ SGridPanel::Slot(0, 2).Padding(4.f)
												[
													SNew(STextBlock).Text(LOCTEXT("DefaultModel", "Default model profile key"))
												]
												+ SGridPanel::Slot(1, 2).Padding(4.f)
												[
													SAssignNew(ApiDefaultModelBox, SEditableTextBox)
														.Text(FText::FromString(TEXT("gpt-4o-mini")))
														.OnTextChanged(this, &SUnrealAiLlmPluginSettingsPanel::OnAnySettingsChanged)
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
															.OnTextChanged(this, &SUnrealAiLlmPluginSettingsPanel::OnAnySettingsChanged)
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
											+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 12.f))
											[
												SNew(SHorizontalBox)
												+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
												[
													SNew(SCheckBox).Style(&FUnrealAiEditorStyle::GetCheckboxStyle())
														.IsChecked_Lambda([]()
														{
															return FUnrealAiEditorModule::IsPieToolsEnabled()
																? ECheckBoxState::Checked
																: ECheckBoxState::Unchecked;
														})
														.OnCheckStateChanged_Lambda([](ECheckBoxState S)
														{
															FUnrealAiEditorModule::SetPieToolsEnabled(S == ECheckBoxState::Checked);
														})
												]
												+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
												[
													SNew(STextBlock)
														.AutoWrapText(true)
														.WrapTextAt(520.f)
														.Text(LOCTEXT(
															"EnablePieToolsHelp",
															"Runtime / PIE tools: when off (default), the agent avoids starting/stopping PIE and related runtime diagnostics, and PIE tool results are not fed back into context. Turn on only when you want the agent to perform runtime checks in a live PIE session."))
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
															"Editor focus: when on, only the primary chat agent may steer the editor (opens or foregrounds editors after tools, content browser sync, viewport framing). Plan-mode steps, plan node workers, and Blueprint/Environment builder sub-turns never take focus. Off by default. Pass focused:false on a tool call to skip once."))
												]
											]
											+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 12.f))
											[
												SNew(SExpandableArea)
													.InitiallyCollapsed(true)
													.AreaTitle(LOCTEXT("SecSections", "API key sections"))
													.HeaderContent()
													[
														SNew(STextBlock)
															.Font(FUnrealAiEditorStyle::FontSectionHeading())
															.Text(LOCTEXT("SecSections", "API key sections"))
													]
													.BodyContent()
													[
														SNew(SVerticalBox)
														+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
														[
															SNew(SButton)
																.Text(LOCTEXT("AddSection", "Add section"))
																.OnClicked(this, &SUnrealAiLlmPluginSettingsPanel::OnAddSectionClicked)
														]
														+ SVerticalBox::Slot().AutoHeight()
														[
															SAssignNew(SectionsVBox, SVerticalBox)
														]
													]
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
										.OnClicked(this, &SUnrealAiLlmPluginSettingsPanel::OnTestConnectionClicked)
								]
								+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
								[
									SNew(SButton)
										.Text(LOCTEXT("SaveSettings", "Save and apply"))
										.OnClicked(this, &SUnrealAiLlmPluginSettingsPanel::OnSaveClicked)
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(STextBlock)
									.AutoWrapText(true)
									.Text_Lambda([this]() { return StatusText; })
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
								.OnClicked(this, &SUnrealAiLlmPluginSettingsPanel::OnViewMyDataClicked)
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

	TWeakPtr<SUnrealAiLlmPluginSettingsPanel> WeakTab(StaticCastSharedRef<SUnrealAiLlmPluginSettingsPanel>(AsShared()));
	UsageTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakTab](float) -> bool
		{
			if (TSharedPtr<SUnrealAiLlmPluginSettingsPanel> Tab = WeakTab.Pin())
			{
				Tab->TickSlowUiRefresh();
			}
			return true;
		}),
		1.5f);
}

void SUnrealAiLlmPluginSettingsPanel::TickSlowUiRefresh()
{
	RefreshUsageHeaderText();
}

SUnrealAiLlmPluginSettingsPanel::~SUnrealAiLlmPluginSettingsPanel()
{
	CancelDeferredReload();
	if (UsageTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(UsageTickerHandle);
		UsageTickerHandle.Reset();
	}
}

void SUnrealAiLlmPluginSettingsPanel::RefreshUsageHeaderText()
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

void SUnrealAiLlmPluginSettingsPanel::ApplyCompanyPreset(FDynSectionRow& Row, const FString& PresetId)
{
	Row.CompanyPreset = PresetId;
	// Keep section metadata aligned with the selected provider preset.
	Row.Id = UnrealAiSettingsJsonUtil::PresetToSectionId(PresetId);
	Row.Label = UnrealAiSettingsJsonUtil::PresetToSectionLabel(PresetId);
	if (PresetId == TEXT("openai"))
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

void SUnrealAiLlmPluginSettingsPanel::RebuildProviderOptions()
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

void SUnrealAiLlmPluginSettingsPanel::RefreshProviderDropdownSelection()
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
	if (PresetId == TEXT("openai"))
	{
		return LOCTEXT("HintOAI", "Paste an API key from platform.openai.com/api-keys. Base URL should be https://api.openai.com/v1 for most accounts.");
	}
	if (PresetId == TEXT("anthropic"))
	{
		return LOCTEXT("HintAnt", "Direct Anthropic URLs differ from OpenAI-style proxies; double-check your provider’s documentation for the correct base URL.");
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

void SUnrealAiLlmPluginSettingsPanel::UpdateModelPricingHint(FDynModelRow& Row)
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

void SUnrealAiLlmPluginSettingsPanel::CancelDeferredReload()
{
	if (DeferredReloadHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DeferredReloadHandle);
		DeferredReloadHandle.Reset();
	}
}

void SUnrealAiLlmPluginSettingsPanel::ScheduleDeferredReload()
{
	if (!BackendRegistry.IsValid())
	{
		return;
	}
	CancelDeferredReload();
	TWeakPtr<SUnrealAiLlmPluginSettingsPanel> WeakSettingsTab(StaticCastSharedRef<SUnrealAiLlmPluginSettingsPanel>(AsShared()));
	DeferredReloadHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda(
			[WeakSettingsTab](float) -> bool
			{
				if (TSharedPtr<SUnrealAiLlmPluginSettingsPanel> Self = WeakSettingsTab.Pin())
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

void SUnrealAiLlmPluginSettingsPanel::PersistSettingsToDisk()
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
	if (UnrealAiSettingsJsonUtil::DeserializeRoot(Json, Reloaded) && Reloaded.IsValid())
	{
		CachedSettingsRoot = UnrealAiSettingsJsonUtil::CloneJsonObject(Reloaded);
	}
}

void SUnrealAiLlmPluginSettingsPanel::OnAnySettingsChanged(const FText&)
{
	PersistSettingsToDisk();
	ScheduleDeferredReload();
}

void SUnrealAiLlmPluginSettingsPanel::LoadSettingsIntoUi()
{
	bSuppressAutoSave = true;
	FString Json;
	IUnrealAiPersistence* Persist = nullptr;
	if (BackendRegistry.IsValid())
	{
		Persist = BackendRegistry->GetPersistence();
		if (Persist && (!Persist->LoadSettingsJson(Json) || Json.IsEmpty()))
		{
			Json = UnrealAiSettingsJsonUtil::GetDefaultSettingsJson();
		}
		else if (!Persist)
		{
			Json = UnrealAiSettingsJsonUtil::GetDefaultSettingsJson();
		}
	}
	else
	{
		Json = UnrealAiSettingsJsonUtil::GetDefaultSettingsJson();
	}

	TSharedPtr<FJsonObject> Root;
	if (!UnrealAiSettingsJsonUtil::DeserializeRoot(Json, Root) || !Root.IsValid())
	{
		Json = UnrealAiSettingsJsonUtil::GetDefaultSettingsJson();
		UnrealAiSettingsJsonUtil::DeserializeRoot(Json, Root);
	}
	if (!Root.IsValid())
	{
		Root = MakeShared<FJsonObject>();
	}

	if (Persist)
	{
		UnrealAiSettingsJsonUtil::MigrateSettingsToV4IfNeeded(Root, Persist);
	}

	CachedSettingsRoot = UnrealAiSettingsJsonUtil::CloneJsonObject(Root);
	FUnrealAiEditorModule::HydrateEditorFocusFromJsonRoot(Root);
	FUnrealAiEditorModule::HydrateSubagentsFromJsonRoot(Root);
	FUnrealAiEditorModule::HydrateAgentCodeTypePreferenceFromJsonRoot(Root);
	FUnrealAiEditorModule::HydrateAutoConfirmDestructiveFromJsonRoot(Root);

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
		R.Id = TEXT("openai");
		R.Label = TEXT("OpenAI");
		R.CompanyPreset = TEXT("openai");
		R.BaseUrl = TEXT("https://api.openai.com/v1");
		FDynModelRow Mr;
		Mr.ProfileKey = TEXT("gpt-4o-mini");
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

void SUnrealAiLlmPluginSettingsPanel::SyncDynamicRowsFromWidgets()
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

void SUnrealAiLlmPluginSettingsPanel::RebuildDynamicRows()
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
										if (SectionRows[SectionIdx].IdBox.IsValid())
										{
											SectionRows[SectionIdx].IdBox->SetText(FText::FromString(SectionRows[SectionIdx].Id));
										}
										if (SectionRows[SectionIdx].LabelBox.IsValid())
										{
											SectionRows[SectionIdx].LabelBox->SetText(FText::FromString(SectionRows[SectionIdx].Label));
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
									.OnTextChanged(this, &SUnrealAiLlmPluginSettingsPanel::OnAnySettingsChanged)
							]
							+ SGridPanel::Slot(0, 1).Padding(4.f)
							[
								SNew(STextBlock).Text(LOCTEXT("SecLabel", "Label"))
							]
							+ SGridPanel::Slot(1, 1).Padding(4.f)
							[
								SAssignNew(Sec.LabelBox, SEditableTextBox)
									.Text(FText::FromString(Sec.Label))
									.OnTextChanged(this, &SUnrealAiLlmPluginSettingsPanel::OnAnySettingsChanged)
							]
							+ SGridPanel::Slot(0, 2).Padding(4.f)
							[
								SNew(STextBlock).Text(LOCTEXT("SecBase", "Base URL"))
							]
							+ SGridPanel::Slot(1, 2).Padding(4.f)
							[
								SAssignNew(Sec.BaseUrlBox, SEditableTextBox)
									.Text(FText::FromString(Sec.BaseUrl))
									.OnTextChanged(this, &SUnrealAiLlmPluginSettingsPanel::OnAnySettingsChanged)
							]
							+ SGridPanel::Slot(0, 3).Padding(4.f)
							[
								SNew(STextBlock).Text(LOCTEXT("SecKey", "API key"))
							]
							+ SGridPanel::Slot(1, 3).Padding(4.f)
							[
								SAssignNew(Sec.ApiKeyBox, SEditableTextBox)
									.Text(FText::FromString(Sec.ApiKey))
									.OnTextChanged(this, &SUnrealAiLlmPluginSettingsPanel::OnAnySettingsChanged)
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
				UnrealAiSettingsJsonUtil::BuildModelOptionsForPreset(Sec.CompanyPreset, Mr.ModelSearchText, Mr.ModelIdOptions);
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
											.OnTextChanged(this, &SUnrealAiLlmPluginSettingsPanel::OnAnySettingsChanged)
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
												.OnTextChanged(this, &SUnrealAiLlmPluginSettingsPanel::OnAnySettingsChanged)
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
											.OnTextChanged(this, &SUnrealAiLlmPluginSettingsPanel::OnAnySettingsChanged)
									]
									+ SGridPanel::Slot(0, 3).Padding(4.f)
									[
										SNew(STextBlock).Text(LOCTEXT("MaxOut2", "Max output tokens"))
									]
									+ SGridPanel::Slot(1, 3).Padding(4.f)
									[
										SAssignNew(Mr.MaxOutputBox, SEditableTextBox)
											.Text(FText::FromString(Mr.MaxOutputStr))
											.OnTextChanged(this, &SUnrealAiLlmPluginSettingsPanel::OnAnySettingsChanged)
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
											.OnTextChanged(this, &SUnrealAiLlmPluginSettingsPanel::OnAnySettingsChanged)
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
										SNew(STextBlock)
											.Font(FUnrealAiEditorStyle::FontCaption())
											.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
											.AutoWrapText(true)
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
												const SUnrealAiLlmPluginSettingsPanel::FDynModelRow& MR = SR.Models[ModelIdx];
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
												return FText::FromString(Line.IsEmpty() ? TEXT("No usage recorded yet for this profile key.") : Line);
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

bool SUnrealAiLlmPluginSettingsPanel::BuildJsonFromUi(FString& OutJson, FString& OutError)
{
	SyncDynamicRowsFromWidgets();

	TSharedPtr<FJsonObject> Root = UnrealAiSettingsJsonUtil::CloneJsonObject(CachedSettingsRoot);
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
		AgentObj->SetBoolField(TEXT("enablePieTools"), FUnrealAiEditorModule::IsPieToolsEnabled());
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
			const int32 Ctx = UnrealAiSettingsJsonUtil::ParsePositiveInt(Mr.MaxContextStr, 128000);
			const int32 MaxOutTok = UnrealAiSettingsJsonUtil::ParsePositiveInt(Mr.MaxOutputStr, 4096);
			int32 MaxRounds = UnrealAiSettingsJsonUtil::ParsePositiveInt(Mr.MaxAgentLlmRoundsStr, 512);
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

FReply SUnrealAiLlmPluginSettingsPanel::OnSaveClicked()
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
	if (UnrealAiSettingsJsonUtil::DeserializeRoot(Json, Reloaded) && Reloaded.IsValid())
	{
		CachedSettingsRoot = UnrealAiSettingsJsonUtil::CloneJsonObject(Reloaded);
	}
	BackendRegistry->ReloadLlmConfiguration();
	StatusText = LOCTEXT("SavedReloaded", "Saved — LLM stack reloaded.");
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	RefreshUsageHeaderText();

	// If retrieval is enabled but the project has no index yet, verify the saved API and queue a rebuild (same probe as Test connection).
	const TWeakPtr<SUnrealAiLlmPluginSettingsPanel> WeakSelf = StaticCastSharedRef<SUnrealAiLlmPluginSettingsPanel>(AsShared());
	const TWeakPtr<FUnrealAiBackendRegistry> WeakReg(BackendRegistry);
	if (IUnrealAiModelConnector* Connector = BackendRegistry->GetModelConnector())
	{
		Connector->TestConnection(FUnrealAiModelTestResultDelegate::CreateLambda(
			[WeakSelf, WeakReg](const bool bOk)
			{
				if (!bOk)
				{
					return;
				}
				const TSharedPtr<FUnrealAiBackendRegistry> Reg = WeakReg.Pin();
				if (!Reg.IsValid())
				{
					return;
				}
				IUnrealAiRetrievalService* const Retrieval = Reg->GetRetrievalService();
				if (!Retrieval)
				{
					return;
				}
				const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
				if (!Retrieval->IsEnabledForProject(ProjectId))
				{
					return;
				}
				const FUnrealAiRetrievalProjectStatus St = Retrieval->GetProjectStatus(ProjectId);
				const bool bHasIndex = St.StateText.Equals(TEXT("ready"), ESearchCase::IgnoreCase) && St.ChunksIndexed > 0;
				if (bHasIndex)
				{
					return;
				}
				Retrieval->RequestRebuild(ProjectId);
				const TSharedPtr<SUnrealAiLlmPluginSettingsPanel> Self = WeakSelf.Pin();
				if (Self.IsValid())
				{
					Self->StatusText =
						LOCTEXT("SavedReloadedAndQueuedIndex", "Saved — LLM reloaded. Connection OK — vector index rebuild queued.");
					Self->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
				}
			}));
	}

	return FReply::Handled();
}

FReply SUnrealAiLlmPluginSettingsPanel::OnTestConnectionClicked()
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

FReply SUnrealAiLlmPluginSettingsPanel::OnViewMyDataClicked()
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

FReply SUnrealAiLlmPluginSettingsPanel::OnAddSectionClicked()
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

FReply SUnrealAiLlmPluginSettingsPanel::OnRemoveSectionClicked(int32 SectionIndex)
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

FReply SUnrealAiLlmPluginSettingsPanel::OnAddModelClicked(int32 SectionIndex)
{
	SyncDynamicRowsFromWidgets();
	if (!SectionRows.IsValidIndex(SectionIndex))
	{
		return FReply::Handled();
	}
	FDynModelRow Mr;
	Mr.ProfileKey = TEXT("gpt-4o-mini");
	Mr.MaxContextStr = TEXT("128000");
	Mr.MaxOutputStr = TEXT("4096");
	Mr.MaxAgentLlmRoundsStr = TEXT("512");
	UpdateModelPricingHint(Mr);
	SectionRows[SectionIndex].Models.Add(MoveTemp(Mr));
	RebuildDynamicRows();
	OnAnySettingsChanged(FText::GetEmpty());
	return FReply::Handled();
}

FReply SUnrealAiLlmPluginSettingsPanel::OnRemoveModelClicked(int32 SectionIndex, int32 ModelIndex)
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
