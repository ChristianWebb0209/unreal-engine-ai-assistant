#pragma once

#include "CoreMinimal.h"

class IAgentContextService;

namespace UnrealAiContextMentionParser
{
	/** Parse @tokens from Prompt and add resolved asset paths as attachments. */
	void ApplyMentionsFromPrompt(IAgentContextService* Ctx, const FString& Prompt);
}
