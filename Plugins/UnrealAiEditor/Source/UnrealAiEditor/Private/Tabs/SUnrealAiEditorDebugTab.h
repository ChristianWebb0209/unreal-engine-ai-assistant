#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"

class FUnrealAiBackendRegistry;

/** One row in the debug navigator (file or directory under a rooted tree). */
struct FUnrealAiDebugListEntry
{
	FString DisplayName;
	FString FullPath;
	bool bIsDirectory = false;
	int32 Depth = 0;
};

class SUnrealAiEditorDebugTab final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUnrealAiEditorDebugTab) {}
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiBackendRegistry>, BackendRegistry)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SUnrealAiEditorDebugTab() override;

private:
	void RebuildFileList();
	void RefreshActiveSessionUi();
	void OnRefreshClicked();
	void OnAutoRefreshToggled(ECheckBoxState State);
	ECheckBoxState GetAutoRefreshCheckState() const;
	void OnSelectionChanged(TSharedPtr<FUnrealAiDebugListEntry> Item, ESelectInfo::Type SelectInfo);
	void OnLoadActiveContext();
	void OnLoadActiveConversation();
	void OnCopyInspectorClicked();
	FReply OnDeleteAllLocalChatDataClicked();

	void RebuildOpenChatHighlightCache();
	bool IsPathHighlighted(const FString& FullPathNormalized) const;

	FString PrettyOrRawJson(const FString& Raw) const;
	FString LoadFileCapped(const FString& Path, bool& bOutTruncated) const;
	void BuildTreeForRoot(const FString& RootPath, const FString& LabelForRoot, int32 MaxDepth);

	TSharedRef<class ITableRow> OnGenerateRow(TSharedPtr<FUnrealAiDebugListEntry> Item, const TSharedRef<class STableViewBase>& OwnerTable);

	bool OnAutoRefreshTick(float DeltaSeconds);

	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<class SListView<TSharedPtr<FUnrealAiDebugListEntry>>> FileList;
	TArray<TSharedPtr<FUnrealAiDebugListEntry>> FileEntries;
	TSharedPtr<FUnrealAiDebugListEntry> SelectedEntry;
	TSharedPtr<class SMultiLineEditableText> InspectorText;
	FText InspectorContent;
	FText SummaryLine;
	FText DataRootShort;
	FText DataRootTooltip;
	FText ProjectIdLabel;

	bool bAutoRefresh = false;
	float AutoRefreshAccumSeconds = 0.f;
	FTSTicker::FDelegateHandle TickerHandle;

	TSet<FString> OpenChatExactPathsNorm;
	TSet<FString> OpenChatDirectoryPrefixesNorm;
};
