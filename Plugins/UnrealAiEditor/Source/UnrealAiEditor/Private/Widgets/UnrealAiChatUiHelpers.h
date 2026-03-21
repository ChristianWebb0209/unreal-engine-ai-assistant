#pragma once

#include "CoreMinimal.h"

class FUnrealAiBackendRegistry;
class SChatMessageList;
struct FUnrealAiChatUiSession;

/** Shared new-chat flow (header + composer). */
void UnrealAiChatUi_StartNewChat(
	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry,
	TSharedPtr<FUnrealAiChatUiSession> Session,
	TSharedPtr<SChatMessageList> MessageList);

/** Clear UI + in-memory context for the thread and delete persisted thread data on disk; then start a fresh thread. */
void UnrealAiChatUi_DeleteChatPermanently(
	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry,
	TSharedPtr<FUnrealAiChatUiSession> Session,
	TSharedPtr<SChatMessageList> MessageList);
