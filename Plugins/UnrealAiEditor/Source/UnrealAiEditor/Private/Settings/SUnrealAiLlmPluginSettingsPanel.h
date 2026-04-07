#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SCompoundWidget.h"

class FUnrealAiBackendRegistry;
class SEditableTextBox;
class SVerticalBox;
class STextBlock;

/** LLM / plugin_settings.json editor shown inside Project Settings (details customization). */
class SUnrealAiLlmPluginSettingsPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUnrealAiLlmPluginSettingsPanel) {}
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiBackendRegistry>, BackendRegistry)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SUnrealAiLlmPluginSettingsPanel() override;

	void TickSlowUiRefresh();

private:
	struct FDynModelRow
	{
		FString ProfileKey;
		FString ModelIdForApi;
		FString MaxContextStr;
		FString MaxOutputStr;
		FString MaxAgentLlmRoundsStr;
		bool bSupportsNativeTools = true;
		bool bSupportsParallelToolCalls = true;
		bool bSupportsImages = true;
		FString PricingHintCache;
		FString ModelSearchText;
		TSharedPtr<SEditableTextBox> ProfileKeyBox;
		TSharedPtr<SEditableTextBox> ModelIdForApiBox;
		TSharedPtr<SComboBox<TSharedPtr<FString>>> ModelIdDropdown;
		TSharedPtr<SEditableTextBox> ModelSearchBox;
		TArray<TSharedPtr<FString>> ModelIdOptions;
		TSharedPtr<SEditableTextBox> MaxContextBox;
		TSharedPtr<SEditableTextBox> MaxOutputBox;
		TSharedPtr<SEditableTextBox> MaxAgentLlmRoundsBox;
	};

	struct FDynSectionRow
	{
		FString Id;
		FString Label;
		FString CompanyPreset;
		FString BaseUrl;
		FString ApiKey;
		TArray<FDynModelRow> Models;
		TSharedPtr<SEditableTextBox> IdBox;
		TSharedPtr<SEditableTextBox> LabelBox;
		TSharedPtr<SEditableTextBox> BaseUrlBox;
		TSharedPtr<SEditableTextBox> ApiKeyBox;
		TSharedPtr<SComboBox<TSharedPtr<FString>>> CompanyCombo;
	};

	FReply OnSaveClicked();
	FReply OnTestConnectionClicked();
	FReply OnViewMyDataClicked();
	FReply OnAddSectionClicked();
	FReply OnRemoveSectionClicked(int32 SectionIndex);
	FReply OnAddModelClicked(int32 SectionIndex);
	FReply OnRemoveModelClicked(int32 SectionIndex, int32 ModelIndex);

	void OnAnySettingsChanged(const FText& NewText);
	void PersistSettingsToDisk();
	void ScheduleDeferredReload();
	void CancelDeferredReload();

	void LoadSettingsIntoUi();
	void SyncDynamicRowsFromWidgets();
	void RebuildDynamicRows();
	bool BuildJsonFromUi(FString& OutJson, FString& OutError);

	void RefreshUsageHeaderText();
	void UpdateModelPricingHint(FDynModelRow& Row);
	void ApplyCompanyPreset(FDynSectionRow& Row, const FString& PresetId);
	void RebuildProviderOptions();
	void RefreshProviderDropdownSelection();

	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<FJsonObject> CachedSettingsRoot;

	bool bSuppressAutoSave = false;
	FTSTicker::FDelegateHandle DeferredReloadHandle;
	FTSTicker::FDelegateHandle UsageTickerHandle;

	TSharedPtr<SEditableTextBox> ApiBaseUrlBox;
	TSharedPtr<SEditableTextBox> ApiKeyBox;
	TSharedPtr<SEditableTextBox> ApiDefaultModelBox;
	TSharedPtr<SEditableTextBox> ApiDefaultProviderIdBox;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ApiDefaultProviderDropdown;

	TSharedPtr<SVerticalBox> SectionsVBox;
	TSharedPtr<STextBlock> UsageSummaryBlock;

	TArray<TSharedPtr<FString>> CompanyPresetOptions;
	TArray<TSharedPtr<FString>> CodeTypePreferenceOptions;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> CodeTypePreferenceCombo;
	TArray<TSharedPtr<FString>> ProviderIdOptions;
	TArray<FDynSectionRow> SectionRows;

	FText StatusText;
};
