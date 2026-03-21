#pragma once

#include "CoreMinimal.h"
#include "Widgets/Input/SComboBox.h"
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

private:
	FReply OnStopPressed();
	FReply OnConnectPressed();
	FReply OnNewChatPressed();
	void OnModelSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);

	FText GetConnectButtonText() const;
	FText GetConnectionStatusText() const;
	FText GetComplexityText() const;
	FText GetSelectedModelText() const;
	bool IsModelComboEnabled() const;

	EActiveTimerReturnType OnComplexityTimerTick(double InCurrentTime, float InDeltaTime);

	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<FUnrealAiChatUiSession> Session;
	FSimpleDelegate OnOpenSettings;
	FSimpleDelegate OnNewChatDelegate;

	TArray<TSharedPtr<FString>> ModelOptions;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> ModelCombo;

	bool bStubConnected = false;
	FString CachedComplexityLabel;
};
