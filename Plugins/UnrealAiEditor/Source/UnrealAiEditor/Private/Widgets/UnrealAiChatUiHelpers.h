#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Text.h"

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

/** Load context + conversation.json into UI for an explicit thread id (session must already use this id). */
void UnrealAiChatUi_LoadPersistedThreadIntoUi(
	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry,
	TSharedPtr<FUnrealAiChatUiSession> Session,
	TSharedPtr<SChatMessageList> MessageList);

/**
 * True when plugin_settings.json has a non-empty api.apiKey (trimmed).
 * Does not perform network validation.
 */
bool UnrealAiChatUi_IsPersistedApiKeyConfigured(TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry);

/**
 * If the transcript is empty and no API key is persisted, adds an informational notice pointing users to AI Settings.
 */
void UnrealAiChatUi_MaybeInjectLlmSetupNotice(
	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry,
	TSharedPtr<SChatMessageList> MessageList);

/** Plugin version + engine label for chat chrome (e.g. "Unreal AI Editor 1.0 · 5.7..."). */
FText UnrealAiChatUi_GetComposerFooterVersionText();
