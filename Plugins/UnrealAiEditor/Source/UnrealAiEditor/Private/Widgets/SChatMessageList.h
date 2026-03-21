#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"

class FUnrealAiChatTranscript;
class FUnrealAiBackendRegistry;
struct FUnrealAiChatUiSession;
class SAssistantStreamBlock;
class SThinkingBlock;

class SChatMessageList final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatMessageList) {}
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiBackendRegistry>, BackendRegistry)
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiChatUiSession>, Session)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	TSharedPtr<FUnrealAiChatTranscript> GetTranscript() const { return Transcript; }

	void AddUserMessage(const FString& Text);
	void ClearTranscript();

	/** @deprecated Prefer AddUserMessage; kept for call sites. */
	void ResetAssistant();

private:
	void RebuildTranscript();
	void OnAssistantDeltaUi(const FString& Chunk);
	void OnThinkingDeltaUi(const FString& Chunk, bool bFirstChunk);
	void RequestScrollToEnd();

	TSharedPtr<FUnrealAiChatTranscript> Transcript;
	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<FUnrealAiChatUiSession> Session;
	TSharedPtr<SScrollBox> ScrollBox;
	TSharedPtr<SVerticalBox> MessageBox;

	TSharedPtr<SAssistantStreamBlock> ActiveAssistantWidget;
	TSharedPtr<SThinkingBlock> ActiveThinkingWidget;
};
