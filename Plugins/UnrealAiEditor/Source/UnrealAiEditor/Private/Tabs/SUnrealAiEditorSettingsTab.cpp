#include "Tabs/SUnrealAiEditorSettingsTab.h"

#include "Backend/UnrealAiBackendRegistry.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "UnrealAiEditorTabIds.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

namespace UnrealAiSettingsTemplate
{
	static const TCHAR* DefaultSettingsJson = TEXT(
		"{\n"
		"\t\"version\": 3,\n"
		"\t\"api\": {\n"
		"\t\t\"baseUrl\": \"https://openrouter.ai/api/v1\",\n"
		"\t\t\"apiKey\": \"\",\n"
		"\t\t\"defaultModel\": \"openai/gpt-4o-mini\",\n"
		"\t\t\"defaultProviderId\": \"\"\n"
		"\t},\n"
		"\t\"providers\": [\n"
		"\t\t{\n"
		"\t\t\t\"id\": \"openrouter\",\n"
		"\t\t\t\"baseUrl\": \"https://openrouter.ai/api/v1\",\n"
		"\t\t\t\"apiKey\": \"\"\n"
		"\t\t}\n"
		"\t],\n"
		"\t\"models\": {\n"
		"\t\t\"openai/gpt-4o-mini\": {\n"
		"\t\t\t\"providerId\": \"openrouter\",\n"
		"\t\t\t\"maxContextTokens\": 128000,\n"
		"\t\t\t\"maxOutputTokens\": 4096,\n"
		"\t\t\t\"supportsNativeTools\": true,\n"
		"\t\t\t\"supportsParallelToolCalls\": true\n"
		"\t\t}\n"
		"\t}\n"
		"}\n");
}

void SUnrealAiEditorSettingsTab::Construct(const FArguments& InArgs)
{
	BackendRegistry = InArgs._BackendRegistry;

	ChildSlot
		[
			SNew(SBorder)
				.Padding(FMargin(12.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
					[
						SNew(STextBlock)
							.Text(LOCTEXT("SettingsTitle", "AI Settings — API & models (JSON)"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
						[
							SNew(SButton)
								.Text(LOCTEXT("OpenApiStructured", "Open API Keys & Models (structured)"))
								.OnClicked(this, &SUnrealAiEditorSettingsTab::OnOpenApiModelsTab)
						]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(STextBlock)
								.AutoWrapText(true)
								.Text(LOCTEXT(
									"QuickFormHint",
									"Use the structured tab for provider, URL, and key; edit raw JSON below for advanced fields (MCP, env, profiles)."))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
					[
						SNew(STextBlock)
							.AutoWrapText(true)
							.Text(LOCTEXT(
								"SettingsHelp",
								"Edit plugin_settings.json below. Use \"api\" for a single global key, and/or "
								"\"providers\" for multiple named endpoints (id, baseUrl, apiKey). "
								"Per-model \"providerId\" selects which provider to use. Save applies immediately "
								"and reloads the LLM stack (stub vs HTTP)."))
					]
					+ SVerticalBox::Slot().FillHeight(1.f).Padding(FMargin(0.f, 4.f))
					[
						SNew(SBox)
							.MinDesiredHeight(280.f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(SettingsJsonBox, SMultiLineEditableTextBox).AllowMultiLine(true)
								]
							]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
					[
						SNew(SButton)
							.Text(LOCTEXT("SaveSettings", "Save and apply"))
							.OnClicked(this, &SUnrealAiEditorSettingsTab::OnSaveClicked)
					]
				]
		];

	LoadSettingsTextIntoBox();
}

void SUnrealAiEditorSettingsTab::LoadSettingsTextIntoBox()
{
	if (!SettingsJsonBox.IsValid() || !BackendRegistry.IsValid())
	{
		return;
	}
	FString Json;
	if (IUnrealAiPersistence* P = BackendRegistry->GetPersistence())
	{
		if (!P->LoadSettingsJson(Json) || Json.IsEmpty())
		{
			Json = UnrealAiSettingsTemplate::DefaultSettingsJson;
		}
	}
	else
	{
		Json = UnrealAiSettingsTemplate::DefaultSettingsJson;
	}
	SettingsJsonBox->SetText(FText::FromString(Json));
}

FReply SUnrealAiEditorSettingsTab::OnSaveClicked()
{
	if (BackendRegistry.IsValid() && SettingsJsonBox.IsValid())
	{
		if (IUnrealAiPersistence* P = BackendRegistry->GetPersistence())
		{
			const FString Json = SettingsJsonBox->GetText().ToString();
			P->SaveSettingsJson(Json);
		}
		BackendRegistry->ReloadLlmConfiguration();
	}
	return FReply::Handled();
}

FReply SUnrealAiEditorSettingsTab::OnOpenApiModelsTab()
{
	FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::ApiModelsTab);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
