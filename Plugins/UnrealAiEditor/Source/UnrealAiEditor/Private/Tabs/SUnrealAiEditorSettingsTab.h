#pragma once

#include "Containers/Ticker.h"
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/SCompoundWidget.h"

class FUnrealAiBackendRegistry;
class SEditableTextBox;
class SMultiLineEditableTextBox;
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

	/** Usage line + optional Vector DB panel refresh (slow ticker). */
	void TickSlowUiRefresh();

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

	FReply OnSettingsSegmentClicked(int32 Index);
	FReply OnVectorDbOverviewRefreshClicked();
	void RefreshVectorDbOverviewUi();
	FReply OnChatHistoryRefreshClicked();
	void RebuildChatHistoryListUi();
	FReply OnMemoriesRefreshClicked();
	FReply OnRetrievalRebuildNowClicked();
	void RebuildMemoryListUi();
	void SelectMemoryById(const FString& MemoryId);
	bool LoadMemorySettingsFromRoot(const TSharedPtr<FJsonObject>& Root);
	void WriteMemorySettingsToRoot(TSharedPtr<FJsonObject>& Root) const;
	bool LoadRetrievalSettingsFromRoot(const TSharedPtr<FJsonObject>& Root);
	void WriteRetrievalSettingsToRoot(TSharedPtr<FJsonObject>& Root) const;

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
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ApiDefaultProviderDropdown;

	TSharedPtr<SVerticalBox> SectionsVBox;
	TSharedPtr<SWidgetSwitcher> SettingsMainSwitcher;
	TSharedPtr<SVerticalBox> ChatHistoryListVBox;
	TSharedPtr<SVerticalBox> MemoryListVBox;
	TSharedPtr<SEditableTextBox> MemorySearchBox;
	TSharedPtr<SEditableTextBox> MemorySelectedTitleBox;
	TSharedPtr<SEditableTextBox> MemorySelectedDescriptionBox;
	TSharedPtr<SEditableTextBox> MemorySelectedBodyBox;
	TSharedPtr<SEditableTextBox> MemoryMaxItemsBox;
	TSharedPtr<SEditableTextBox> MemoryMinConfidenceBox;
	TSharedPtr<SEditableTextBox> MemoryRetentionDaysBox;
	TSharedPtr<STextBlock> MemoryGenerationStatusBlock;
	TSharedPtr<STextBlock> UsageSummaryBlock;
	TSharedPtr<STextBlock> VectorDbOverviewBlock;

	int32 ActiveSettingsSegmentIndex = 0;
	double LastVectorDbOverviewPollSeconds = 0.0;

	TArray<TSharedPtr<FString>> CompanyPresetOptions;
	TArray<TSharedPtr<FString>> ProviderIdOptions;
	TArray<FDynSectionRow> SectionRows;
	FString SelectedMemoryId;
	bool bMemoryEnabled = true;
	bool bMemoryAutoExtract = true;
	FString MemoryMaxItemsStr = TEXT("500");
	FString MemoryMinConfidenceStr = TEXT("0.55");
	FString MemoryRetentionDaysStr = TEXT("30");
	bool bRetrievalEnabled = false;
	FString RetrievalEmbeddingModel = TEXT("text-embedding-3-small");
	FString RetrievalMaxSnippetsPerTurnStr = TEXT("6");
	FString RetrievalMaxSnippetTokensStr = TEXT("256");
	bool bRetrievalAutoIndexOnProjectOpen = true;
	FString RetrievalPeriodicScrubMinutesStr = TEXT("30");
	bool bRetrievalAllowMixedModelCompatibility = false;

	FString RetrievalRootPresetStr = TEXT("minimal");
	FString RetrievalIndexedExtensionsText;
	FString RetrievalMaxFilesPerRebuildStr = TEXT("0");
	FString RetrievalMaxTotalChunksPerRebuildStr = TEXT("0");
	FString RetrievalMaxEmbeddingCallsPerRebuildStr = TEXT("0");
	FString RetrievalChunkCharsStr = TEXT("1200");
	FString RetrievalChunkOverlapStr = TEXT("200");
	FString RetrievalAssetRegistryMaxAssetsStr = TEXT("0");
	bool bRetrievalAssetRegistryIncludeEngineAssets = false;
	FString RetrievalEmbeddingBatchSizeStr = TEXT("8");
	FString RetrievalMinDelayMsBetweenBatchesStr = TEXT("0");
	bool bRetrievalIndexMemoryRecordsInVectorStore = false;
	FString RetrievalBlueprintMaxFeatureRecordsStr = TEXT("0");

	TArray<TSharedPtr<FString>> RetrievalRootPresetOptions;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> RetrievalRootPresetCombo;
	TSharedPtr<SMultiLineEditableTextBox> RetrievalIndexedExtensionsBox;

	FText StatusText;
};
