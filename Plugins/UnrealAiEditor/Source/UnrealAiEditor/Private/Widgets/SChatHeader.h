#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FUnrealAiBackendRegistry;
struct FUnrealAiChatUiSession;

class SChatHeader final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatHeader) {}
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiBackendRegistry>, BackendRegistry)
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiChatUiSession>, Session)
	SLATE_EVENT(FSimpleDelegate, OnOpenSettings)
	SLATE_EVENT(FSimpleDelegate, OnNewChat)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SChatHeader() override;

private:
	FReply OnNewChatPressed();
	FText GetChatTitleText() const;
	void OnChatNameChanged();

	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<FUnrealAiChatUiSession> Session;
	FSimpleDelegate OnOpenSettings;
	FSimpleDelegate OnNewChatDelegate;
};
