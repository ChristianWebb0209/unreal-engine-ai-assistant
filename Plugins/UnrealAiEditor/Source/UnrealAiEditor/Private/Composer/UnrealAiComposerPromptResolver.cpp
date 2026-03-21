#include "Composer/UnrealAiComposerPromptResolver.h"

#include "CoreMinimal.h"

#define LOCTEXT_NAMESPACE "UnrealAiComposerResolver"

namespace
{
	static const TCHAR* GToolTestPrompt = TEXT(
		"You are running a structured tool smoke test in Unreal AI Editor.\n\n"
		"Goal: exercise as many distinct tool categories as are available in this session (read-only where possible).\n\n"
		"Please:\n"
		"1) List which tools or capabilities you believe are available to you for this project.\n"
		"2) For filesystem / project access tools: read a small, safe file (e.g. project README or a trivial text asset path) and quote one line or confirm read success.\n"
		"3) For search / discovery tools: run a minimal search (e.g. a symbol or path fragment) and summarize hits.\n"
		"4) For editor / world / actor / selection tools: query current editor or level state in a non-destructive way.\n"
		"5) For Blueprint or asset inspection tools: perform a read-only inspection on one asset if such a tool exists.\n"
		"6) For any MCP or external integration tools exposed here: invoke at most one trivial call if safe.\n\n"
		"After each attempt, note success, failure, or \"not available\". End with a short table: tool category | called? | outcome.\n"
		"Avoid destructive writes, mass deletes, or build steps unless the user explicitly asks; this pass is diagnostic only.");

	static const TCHAR* GTodoListPrompt = TEXT(
		"The user is testing todo-list rendering in the chat UI.\n\n"
		"Respond with exactly one markdown block that looks like a real todo list:\n"
		"- Start with a line: ## Todo list\n"
		"- Then at least 6 tasks, each on its own line, using GitHub-style checkboxes: '- [ ] ' followed by a short, concrete task.\n"
		"  Mix a few checked and unchecked items using '- [x] ' for completed and '- [ ] ' for pending.\n"
		"  Tasks should be plausible Unreal Editor / plugin workflow items (e.g. verify project settings, compile, run PIE, review a Blueprint, check logs).\n"
		"Do not add explanations, introductions, or closing remarks—only the heading and the checklist lines.");

	struct FCmdRow
	{
		const TCHAR* Command;
		/** If null, ResolveBeforeSend sets bLocalOnlyNoLlm (no LLM prompt substitution). */
		const TCHAR* Prompt;
		FText Description;
	};

	static const TArray<FCmdRow>& CommandRows()
	{
		static TArray<FCmdRow> Rows;
		if (Rows.Num() == 0)
		{
			Rows.Add({TEXT("/tool-test"), GToolTestPrompt,
				LOCTEXT("SlashToolTest", "Run a broad tool smoke test (read-only where possible).")});
			Rows.Add({TEXT("/todo-list"), GTodoListPrompt,
				LOCTEXT("SlashTodoList", "Ask for a sample markdown todo list (for UI testing).")});
			Rows.Add({TEXT("/view-context"), nullptr,
				LOCTEXT("SlashViewContext", "Show a readable summary of current thread context (no LLM call).")});
		}
		return Rows;
	}
}

void UnrealAiComposerPromptResolver::GetSlashCommands(TArray<FUnrealAiSlashCommand>& OutCommands)
{
	OutCommands.Reset();
	for (const FCmdRow& Row : CommandRows())
	{
		FUnrealAiSlashCommand C;
		C.Command = Row.Command;
		C.Description = Row.Description;
		OutCommands.Add(MoveTemp(C));
	}
}

bool UnrealAiComposerPromptResolver::TryParseSlashAutocomplete(
	const FString& FullText,
	int32& OutSlashPos,
	FString& OutTokenAfterSlash)
{
	OutSlashPos = INDEX_NONE;
	OutTokenAfterSlash.Empty();

	const int32 Slash = FullText.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (Slash == INDEX_NONE)
	{
		return false;
	}
	if (Slash > 0)
	{
		const TCHAR Prev = FullText[Slash - 1];
		if (!FChar::IsWhitespace(Prev) && Prev != TEXT('\n') && Prev != TEXT('\r'))
		{
			return false;
		}
	}

	FString Rest = FullText.Mid(Slash + 1);
	int32 Cut = INDEX_NONE;
	if (Rest.FindChar(TEXT(' '), Cut) || Rest.FindChar(TEXT('\n'), Cut))
	{
		Rest = Rest.Left(Cut);
	}
	OutSlashPos = Slash;
	OutTokenAfterSlash = Rest;
	return true;
}

FUnrealAiComposerResolveResult UnrealAiComposerPromptResolver::ResolveBeforeSend(const FString& RawUserText)
{
	FUnrealAiComposerResolveResult R;
	R.ResolvedText = RawUserText;

	const FString Trimmed = RawUserText.TrimStartAndEnd();
	for (const FCmdRow& Row : CommandRows())
	{
		if (Trimmed != Row.Command)
		{
			continue;
		}
		if (!Row.Prompt)
		{
			R.bLocalOnlyNoLlm = true;
			R.bWasModified = false;
			R.ResolvedText = RawUserText;
			return R;
		}
		R.bWasModified = true;
		R.ResolvedText = Row.Prompt;
		return R;
	}

	return R;
}

#undef LOCTEXT_NAMESPACE
