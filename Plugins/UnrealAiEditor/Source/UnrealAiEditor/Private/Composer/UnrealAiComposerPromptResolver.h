#pragma once

#include "Containers/UnrealString.h"
#include "Internationalization/Text.h"

struct FUnrealAiComposerResolveResult
{
	FString ResolvedText;
	bool bWasModified = false;
	/** If true, send pipeline skips the LLM (caller handles locally, e.g. /view-context). */
	bool bLocalOnlyNoLlm = false;
};

struct FUnrealAiSlashCommand
{
	FString Command;
	FText Description;
};

namespace UnrealAiComposerPromptResolver
{
	/** Runs when the user hits Send, before context and agent turn. May rewrite text (slash commands, etc.). */
	FUnrealAiComposerResolveResult ResolveBeforeSend(const FString& RawUserText);

	/** All registered slash commands (for autocomplete UI). */
	void GetSlashCommands(TArray<FUnrealAiSlashCommand>& OutCommands);

	/**
	 * If the user is typing an active slash command (last `/` in the buffer is a command starter),
	 * returns true and the filter token after `/` (may be empty = show all commands).
	 */
	bool TryParseSlashAutocomplete(const FString& FullText, int32& OutSlashPos, FString& OutTokenAfterSlash);
}
