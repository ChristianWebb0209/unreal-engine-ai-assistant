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
class SWidgetSwitcher;

class SUnrealAiEditorSettingsTab final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUnrealAiEditorSettingsTab) {}
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiBackendRegistry>, BackendRegistry)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SUnrealAiEditorSettingsTab() override;

private:
	struct FDynModelRow
	{
		FString ProfileKey;
		FString ModelIdForApi;
		FString MaxContextStr;
		FString MaxOutputStr;
		/** Max tool↔LLM iterations per send (plugin_settings `maxAgentLlmRounds`). */
		FString MaxAgentLlmRoundsStr;
		bool bSupportsNativeTools = true;
		bool bSupportsParallelToolCalls = true;
		bool bSupportsImages = true;
		FString PricingHintCache;
		TSharedPtr<SEditableTextBox> ProfileKeyBox;
		TSharedPtr<SEditableTextBox> ModelIdForApiBox;
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

	FReply OnSettingsSegmentClicked(int32 Index);
	FReply OnChatHistoryRefreshClicked();
	void RebuildChatHistoryListUi();

	void SyncChatAppearanceWidgetsFromSettings();
	void CommitUserChatBubbleComponent(int32 ChannelIndex, float Value);
	void CommitAgentChatBubbleComponent(int32 ChannelIndex, float Value);
	FReply OnResetChatBubbleColorsClicked();

	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<FJsonObject> CachedSettingsRoot;

	bool bSuppressAutoSave = false;
	FTSTicker::FDelegateHandle DeferredReloadHandle;
	FTSTicker::FDelegateHandle UsageTickerHandle;

	TSharedPtr<SEditableTextBox> ApiBaseUrlBox;
	TSharedPtr<SEditableTextBox> ApiKeyBox;
	TSharedPtr<SEditableTextBox> ApiDefaultModelBox;
	TSharedPtr<SEditableTextBox> ApiDefaultProviderIdBox;

	TSharedPtr<SVerticalBox> SectionsVBox;
	TSharedPtr<SWidgetSwitcher> SettingsMainSwitcher;
	TSharedPtr<SVerticalBox> ChatHistoryListVBox;
	TSharedPtr<STextBlock> UsageSummaryBlock;

	TArray<TSharedPtr<FString>> CompanyPresetOptions;
	TArray<FDynSectionRow> SectionRows;

	FText StatusText;
};
