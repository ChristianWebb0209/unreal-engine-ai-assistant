#pragma once

#include "CoreMinimal.h"
#include "Harness/UnrealAiAgentTypes.h"

namespace UnrealAiConversationJson
{
	bool MessagesToJson(const TArray<FUnrealAiConversationMessage>& Messages, FString& OutJson);
	bool JsonToMessages(const FString& Json, TArray<FUnrealAiConversationMessage>& OutMessages, TArray<FString>& OutWarnings);

	/** Build `messages` JSON array for chat-completions-style HTTP bodies. */
	bool MessagesToChatCompletionsJsonArray(const TArray<FUnrealAiConversationMessage>& Messages, FString& OutJsonArray);
}
