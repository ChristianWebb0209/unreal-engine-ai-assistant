#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"
#include "Widgets/SCompoundWidget.h"

class FUnrealAiBackendRegistry;
struct FUnrealAiChatUiSession;
class SChatComposer;
class SChatMessageList;

class SUnrealAiEditorChatTab final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUnrealAiEditorChatTab) {}
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiBackendRegistry>, BackendRegistry)
	SLATE_END_ARGS()

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

	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<FUnrealAiChatUiSession> Session;
	TSharedPtr<SChatMessageList> MessageListWidget;
	TSharedPtr<SChatComposer> ComposerWidget;
};
