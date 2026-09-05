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

	static const TCHAR* GBpRapidPrototypePrompt = TEXT(
		"You are a senior Unreal Blueprint rapid-prototyping agent.\n\n"
		"Build a complete, playable Blueprint feature quickly with minimal back-and-forth.\n"
		"Constraints:\n"
		"- Discover targets first using read tools.\n"
		"- If graph writes are gated on this turn, emit a strict `<unreal_ai_build_blueprint>` handoff immediately.\n"
		"- Infer handoff `target_kind` from discovered asset type: `/Script/Engine.AnimBlueprint` => `anim_blueprint`, gameplay Blueprint => `script_blueprint`.\n"
		"- Never retry builder-only mutators on main agent after `agent_surface_tool_withheld`; hand off instead.\n"
		"- Prefer concrete implementation over discussion; avoid asking for discoverable info.\n"
		"- Include compile + verify + save steps.\n\n"
		"Deliverable:\n"
		"- Working Blueprint logic with clear node wiring.\n"
		"- Brief result summary and any assumptions made.");

	static const TCHAR* GBpSystemsArchitecturePrompt = TEXT(
		"You are a Blueprint systems architect.\n\n"
		"Design and implement a scalable multi-Blueprint gameplay system with clean responsibilities.\n"
		"Approach:\n"
		"- Identify required Blueprints/components/interfaces/event flow.\n"
		"- Implement in small safe steps using graph introspection + patch + compile + verify loops.\n"
		"- Favor reusable events/functions and avoid monolithic EventGraph spaghetti.\n"
		"- Surface explicit trade-offs and technical debt avoided.\n\n"
		"Deliverable:\n"
		"- Integrated Blueprint system with robust wiring.\n"
		"- Concise architecture notes and test checklist.");

	static const TCHAR* GBpAnimStressPrompt = TEXT(
		"You are an animation Blueprint stress-tester and implementer.\n\n"
		"Goal: maximize automation for animation behavior changes while respecting current tooling limits.\n"
		"Rules:\n"
		"- Use target_kind `anim_blueprint` when delegating.\n"
		"- Resolve target asset paths with read tools before mutation handoff; do not ask for discoverable paths.\n"
		"- If a prior attempt used the wrong target kind (for example script on an AnimBlueprint), immediately re-handoff with `anim_blueprint`.\n"
		"- Apply safe K2/EventGraph edits where supported.\n"
		"- If native AnimGraph/state-machine edits are unsupported, do not stall; perform what is automatable and report exact manual remainder.\n"
		"- Compile/verify after edits and include precise blocker reasons when blocked.\n\n"
		"Deliverable:\n"
		"- Highest-coverage automated anim change possible in this environment.\n"
		"- Explicit manual follow-up steps if needed.");

	static const TCHAR* GBpWidgetWorkflowPrompt = TEXT(
		"You are a UMG/Widget Blueprint workflow optimizer.\n\n"
		"Implement a polished UI behavior feature with resilient Widget Blueprint script logic.\n"
		"Guidelines:\n"
		"- Prefer robust event-driven widget scripting patterns.\n"
		"- Keep logic readable (functions/custom events over long chains).\n"
		"- Validate graph integrity and compilation.\n"
		"- If Designer-layout automation is limited, complete script-side behavior and provide clear UI-manual deltas.\n\n"
		"Deliverable:\n"
		"- Widget Blueprint behavior implementation plus concise verification notes.");

	static const TCHAR* GBpMaterialHybridPrompt = TEXT(
		"You are a hybrid material+Blueprint implementer.\n\n"
		"Build a feature that combines Blueprint gameplay logic with material behavior.\n"
		"Execution model:\n"
		"- Use Material Instance parameter writes for runtime style changes when possible.\n"
		"- Use `target_kind: material_graph` only when base material graph edits are truly required.\n"
		"- Keep gameplay control in Blueprint and visual tuning in material pathways.\n"
		"- Validate compile/graph state on every mutation path.\n\n"
		"Deliverable:\n"
		"- End-to-end hybrid behavior with clear separation of responsibilities.");

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
			Rows.Add({TEXT("/bp-rapid"), GBpRapidPrototypePrompt,
				LOCTEXT("SlashBpRapid", "Rapidly prototype a complete Blueprint feature.")});
			Rows.Add({TEXT("/bp-system"), GBpSystemsArchitecturePrompt,
				LOCTEXT("SlashBpSystem", "Build a scalable multi-Blueprint gameplay system.")});
			Rows.Add({TEXT("/bp-anim-stress"), GBpAnimStressPrompt,
				LOCTEXT("SlashBpAnimStress", "Push animation Blueprint automation to current limits.")});
			Rows.Add({TEXT("/bp-widget"), GBpWidgetWorkflowPrompt,
				LOCTEXT("SlashBpWidget", "Implement robust Widget Blueprint script behavior.")});
			Rows.Add({TEXT("/bp-material-hybrid"), GBpMaterialHybridPrompt,
				LOCTEXT("SlashBpMaterialHybrid", "Build a hybrid Blueprint + material behavior workflow.")});
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
