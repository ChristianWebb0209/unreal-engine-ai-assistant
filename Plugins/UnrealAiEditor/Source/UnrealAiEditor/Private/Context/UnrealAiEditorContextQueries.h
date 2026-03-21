#pragma once

#include "Context/AgentContextTypes.h"

class IAgentContextService;
class AActor;
struct FAssetData;

namespace UnrealAiEditorContextQueries
{
	inline constexpr int32 MaxContentBrowserSelectedAssets = 24;
	inline constexpr int32 MaxOpenEditorAssets = 12;

	void PopulateContentBrowserAndSelection(FEditorContextSnapshot& Snap);
	void PopulateOpenEditorAssets(FEditorContextSnapshot& Snap);

	/** Adds up to MaxContentBrowserSelectedAssets selected assets as attachments (active context session). */
	void AddContentBrowserSelectionAsAttachments(IAgentContextService* Ctx);

	/** Detailed attachment body for the LLM (beyond raw payload). */
	FString BuildRichAttachmentPayload(const FContextAttachment& Attachment);

	FContextAttachment AttachmentFromAssetData(const FAssetData& AssetData);
	FContextAttachment AttachmentFromActor(AActor* Actor);
	/** PNG written by FScreenshotRequest (editor viewport); payload is absolute path on disk. */
	FContextAttachment AttachmentFromViewportScreenshotFile(const FString& FullPathPng);

	/** File paths (image extensions, viewport screenshots) and texture-like assets. */
	bool IsImageLikeAttachment(const FContextAttachment& Attachment);
}
