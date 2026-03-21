#pragma once

#include "Context/AgentContextTypes.h"

class FDragDropEvent;
class FUnrealAiBackendRegistry;
struct FUnrealAiChatUiSession;

/** Drag/drop from editor surfaces + applying to the active Agent Chat thread. */
namespace UnrealAiContextDragDrop
{
	/** Max items added in one drop or menu action (matches Content Browser selection cap). */
	inline constexpr int32 MaxAttachmentsPerAction = 24;

	bool TryParseDragDrop(const FDragDropEvent& DragDropEvent, TArray<FContextAttachment>& OutAttachments);

	void AddAttachmentsToActiveChat(
		TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry,
		TSharedPtr<FUnrealAiChatUiSession> Session,
		const TArray<FContextAttachment>& Attachments);
}
