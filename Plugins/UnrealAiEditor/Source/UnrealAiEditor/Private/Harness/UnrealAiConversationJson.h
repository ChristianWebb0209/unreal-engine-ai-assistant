#pragma once

#include "CoreMinimal.h"
#include "Harness/UnrealAiAgentTypes.h"

namespace UnrealAiConversationJson
{
	bool MessagesToJson(const TArray<FUnrealAiConversationMessage>& Messages, FString& OutJson);
	bool JsonToMessages(const FString& Json, TArray<FUnrealAiConversationMessage>& OutMessages, TArray<FString>& OutWarnings);

	/** Build OpenAI `messages` JSON array value (content for HTTP body). */
	bool MessagesToOpenAiJsonArray(const TArray<FUnrealAiConversationMessage>& Messages, FString& OutJsonArray);
}
