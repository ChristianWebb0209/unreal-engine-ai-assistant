#pragma once

#include "CoreMinimal.h"
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

private:
	void OpenSettingsTab() const;
	void OnUnifiedNewChat();

	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<FUnrealAiChatUiSession> Session;
	TSharedPtr<SChatMessageList> MessageListWidget;
	TSharedPtr<SChatComposer> ComposerWidget;
};
