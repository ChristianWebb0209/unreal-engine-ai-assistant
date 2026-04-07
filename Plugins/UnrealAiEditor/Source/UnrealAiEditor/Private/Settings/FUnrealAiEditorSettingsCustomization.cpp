#include "Settings/FUnrealAiEditorSettingsCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "Settings/SUnrealAiLlmPluginSettingsPanel.h"
#include "UnrealAiEditorModule.h"
#include "UnrealAiEditorSettings.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

TSharedRef<IDetailCustomization> FUnrealAiEditorSettingsCustomization::MakeInstance()
{
	return MakeShareable(new FUnrealAiEditorSettingsCustomization);
}

void FUnrealAiEditorSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& LlmCat = DetailBuilder.EditCategory(
		TEXT("UnrealAiLlm"),
		LOCTEXT("UnrealAiLlmCat", "LLM & API (plugin_settings.json)"),
		ECategoryPriority::Important);

	LlmCat.AddCustomRow(LOCTEXT("UnrealAiLlmRow", "LLM"), false)
		.WholeRowContent()
		.MinDesiredWidth(560.f)
		[
			SNew(SBox)
				.MinDesiredHeight(420.f)
				[
					SNew(SUnrealAiLlmPluginSettingsPanel)
						.BackendRegistry(FUnrealAiEditorModule::GetBackendRegistry())
				]
		];
}

#undef LOCTEXT_NAMESPACE
