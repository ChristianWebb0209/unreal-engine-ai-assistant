#include "Tabs/SUnrealAiEditorApiModelsTab.h"

#include "Backend/UnrealAiBackendRegistry.h"
#include "Backend/IUnrealAiModelConnector.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Widgets/SBoxPanel.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void SUnrealAiEditorApiModelsTab::Construct(const FArguments& InArgs)
{
	BackendRegistry = InArgs._BackendRegistry;
	StatusText = LOCTEXT("StatusIdle", "Status: idle");

	ChildSlot
		[
			SNew(SBorder)
				.Padding(FMargin(12.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
					[
						SNew(STextBlock)
							.Text(LOCTEXT("ApiTitle", "API Keys & Models (BYOK)"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SGridPanel).FillColumn(1, 1.f)
						+ SGridPanel::Slot(0, 0).Padding(4.f)
						[
							SNew(STextBlock).Text(LOCTEXT("Provider", "Provider"))
						]
						+ SGridPanel::Slot(1, 0).Padding(4.f)
						[
							SAssignNew(ProviderBox, SEditableTextBox).Text(FText::FromString(TEXT("openrouter")))
						]
						+ SGridPanel::Slot(0, 1).Padding(4.f)
						[
							SNew(STextBlock).Text(LOCTEXT("BaseUrl", "Base URL"))
						]
						+ SGridPanel::Slot(1, 1).Padding(4.f)
						[
							SAssignNew(BaseUrlBox, SEditableTextBox).Text(FText::FromString(TEXT("https://openrouter.ai/api/v1")))
						]
						+ SGridPanel::Slot(0, 2).Padding(4.f)
						[
							SNew(STextBlock).Text(LOCTEXT("ApiKey", "API Key"))
						]
						+ SGridPanel::Slot(1, 2).Padding(4.f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
							[
								SAssignNew(ApiKeyBox, SEditableTextBox)
									.Text(FText::FromString(TEXT("sk-or-stub")))
									.IsReadOnly_Lambda([this]() { return bMaskKey; })
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f))
							[
								SNew(SCheckBox)
									.IsChecked_Lambda([this]() { return bMaskKey ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
									.OnCheckStateChanged(this, &SUnrealAiEditorApiModelsTab::OnMaskToggled)
									[
										SNew(STextBlock).Text(LOCTEXT("Mask", "Mask key in UI"))
									]
							]
						]
						+ SGridPanel::Slot(0, 3).Padding(4.f)
						[
							SNew(STextBlock).Text(LOCTEXT("Model", "Default model"))
						]
						+ SGridPanel::Slot(1, 3).Padding(4.f)
						[
							SAssignNew(ModelBox, SEditableTextBox).Text(FText::FromString(TEXT("anthropic/claude-3.5-sonnet")))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)
						[
							SNew(SButton)
								.Text(LOCTEXT("Test", "Test connection"))
								.OnClicked(this, &SUnrealAiEditorApiModelsTab::OnTestConnection)
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(4.f)
						[
							SNew(SButton)
								.Text(LOCTEXT("SaveBtn", "Save"))
								.OnClicked(this, &SUnrealAiEditorApiModelsTab::OnSave)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f))
					[
						SNew(STextBlock)
							.Text_Lambda([this]() { return StatusText; })
					]
				]
		];
}

void SUnrealAiEditorApiModelsTab::OnMaskToggled(ECheckBoxState NewState)
{
	bMaskKey = NewState == ECheckBoxState::Checked;
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

FReply SUnrealAiEditorApiModelsTab::OnTestConnection()
{
	StatusText = LOCTEXT("StatusTesting", "Status: testing…");
	if (BackendRegistry.IsValid())
	{
		if (IUnrealAiModelConnector* M = BackendRegistry->GetModelConnector())
		{
			M->TestConnection(
				FUnrealAiModelTestResultDelegate::CreateLambda(
					[this](bool bOk)
					{
						StatusText = bOk ? LOCTEXT("Ok", "Status: connection OK (GET /models)") : LOCTEXT("Fail", "Status: failed (check URL, key, and network)");
						Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
					}));
		}
	}
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	return FReply::Handled();
}

FReply SUnrealAiEditorApiModelsTab::OnSave()
{
	if (!BackendRegistry.IsValid())
	{
		return FReply::Handled();
	}
	FString ProviderId = ProviderBox.IsValid() ? ProviderBox->GetText().ToString() : FString();
	if (ProviderId.IsEmpty())
	{
		ProviderId = TEXT("default");
	}
	const FString BaseUrl = BaseUrlBox.IsValid() ? BaseUrlBox->GetText().ToString() : FString();
	const FString Key = ApiKeyBox.IsValid() ? ApiKeyBox->GetText().ToString() : FString();
	const FString Model = ModelBox.IsValid() ? ModelBox->GetText().ToString() : FString(TEXT("openai/gpt-4o-mini"));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), 3);
	TSharedPtr<FJsonObject> Api = MakeShared<FJsonObject>();
	Api->SetStringField(TEXT("baseUrl"), BaseUrl);
	Api->SetStringField(TEXT("apiKey"), TEXT(""));
	Api->SetStringField(TEXT("defaultModel"), Model);
	Api->SetStringField(TEXT("defaultProviderId"), ProviderId);
	Root->SetObjectField(TEXT("api"), Api);

	TArray<TSharedPtr<FJsonValue>> ProvArr;
	TSharedPtr<FJsonObject> Po = MakeShared<FJsonObject>();
	Po->SetStringField(TEXT("id"), ProviderId);
	Po->SetStringField(TEXT("baseUrl"), BaseUrl);
	Po->SetStringField(TEXT("apiKey"), Key);
	ProvArr.Add(MakeShared<FJsonValueObject>(Po.ToSharedRef()));
	Root->SetArrayField(TEXT("providers"), ProvArr);

	TSharedPtr<FJsonObject> ModelsObj = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> MCap = MakeShared<FJsonObject>();
	MCap->SetStringField(TEXT("providerId"), ProviderId);
	ModelsObj->SetObjectField(Model, MCap);
	Root->SetObjectField(TEXT("models"), ModelsObj);

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	if (FJsonSerializer::Serialize(Root.ToSharedRef(), W))
	{
		if (IUnrealAiPersistence* P = BackendRegistry->GetPersistence())
		{
			P->SaveSettingsJson(Out);
		}
		BackendRegistry->ReloadLlmConfiguration();
		StatusText = LOCTEXT("SavedReloaded", "Status: saved — LLM stack reloaded");
	}
	else
	{
		StatusText = LOCTEXT("SaveFail", "Status: failed to serialize settings");
	}
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
