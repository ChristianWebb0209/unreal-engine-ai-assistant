#pragma once

#include "CoreMinimal.h"
#include "Context/AgentContextTypes.h"
#include "Harness/UnrealAiAgentTypes.h"

/** Encode image attachments as OpenAI-style multimodal user message content (data URLs). */
namespace UnrealAiVisionMessageContent
{
	/** Max PNG bytes read per attachment (raw file size). */
	inline constexpr int32 MaxImageFileBytes = 4 * 1024 * 1024;

	/** Max viewport/file images embedded per turn. */
	inline constexpr int32 MaxVisionImagesPerTurn = 4;

	bool TryEncodePngFileAsDataUrl(const FString& AbsolutePathPng, FString& OutDataUrl, FString& OutError);

	/**
	 * Loads image-like context attachments and attaches data URLs to the last user message in ApiMsgs.
	 * Appends warnings to OutUserVisibleMessages when files are missing or skipped.
	 */
	void ApplyContextImageAttachmentsToApiMessages(
		TArray<FUnrealAiConversationMessage>& ApiMsgs,
		const TArray<FContextAttachment>& Attachments,
		bool bModelSupportsImages,
		TArray<FString>& OutUserVisibleMessages);
}
