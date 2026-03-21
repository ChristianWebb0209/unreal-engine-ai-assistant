#pragma once

#include "CoreMinimal.h"

class IAgentContextService;

namespace UnrealAiContextMentionParser
{
	/** Parse @tokens from Prompt and add resolved asset paths as attachments (skips duplicates already in session). */
	void ApplyMentionsFromPrompt(
		IAgentContextService* Ctx,
		const FString& ProjectId,
		const FString& ThreadId,
		const FString& Prompt);
}
