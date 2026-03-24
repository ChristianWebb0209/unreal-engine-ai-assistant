#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"
#include "Misc/Guid.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Docking/SDockTab.h"

class FUnrealAiBackendRegistry;
struct FUnrealAiChatUiSession;
class SChatComposer;
class SChatMessageList;

class SUnrealAiEditorChatTab final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUnrealAiEditorChatTab)
		: _bUseExplicitThreadId(false)
	{}
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiBackendRegistry>, BackendRegistry)
	SLATE_ARGUMENT(bool, bUseExplicitThreadId)
	SLATE_ARGUMENT(FGuid, ExplicitThreadId)
	SLATE_END_ARGS()

	TSharedPtr<FUnrealAiChatUiSession> GetSession() const { return Session; }

	/** Registered by the dock tab spawner so tab title updates do not rely on parent traversal. */
	void SetHostDockTab(const TSharedRef<SDockTab>& InHostTab);

	void Construct(const FArguments& InArgs);
	virtual ~SUnrealAiEditorChatTab() override;

	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

	/** Agent Chat tab header context menu (see SDockTab::OnExtendContextMenu). */
	void MenuNewChat();
	void MenuExportChat();
	void MenuCopyChatToClipboard();
	void MenuDeleteChat();

private:
	void OpenSettingsTab() const;
	void OnUnifiedNewChat();
	void RefreshChatChrome();

	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<FUnrealAiChatUiSession> Session;
	TSharedPtr<SChatMessageList> MessageListWidget;
	TSharedPtr<SChatComposer> ComposerWidget;
	TWeakPtr<SDockTab> HostDockTab;
};
