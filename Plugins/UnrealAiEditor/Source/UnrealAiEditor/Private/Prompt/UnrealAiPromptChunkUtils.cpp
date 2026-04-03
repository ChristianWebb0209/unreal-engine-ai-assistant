#include "Prompt/UnrealAiPromptChunkUtils.h"

#include "Context/AgentContextFormat.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Prompt/UnrealAiPromptAssembleParams.h"
#include "UnrealAiEditorModule.h"
#include "UnrealAiEditorSettings.h"

namespace UnrealAiPromptChunkUtilsPriv
{
	static FString AgentModeString(EUnrealAiAgentMode M)
	{
		switch (M)
		{
		case EUnrealAiAgentMode::Ask:
			return TEXT("ask");
		case EUnrealAiAgentMode::Agent:
			return TEXT("agent");
		case EUnrealAiAgentMode::Plan:
			return TEXT("plan");
		default:
			return TEXT("ask");
		}
	}

	static void ReplaceAll(FString& S, const FString& From, const FString& To)
	{
		int32 Pos = 0;
		while ((Pos = S.Find(From, ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos)) != INDEX_NONE)
		{
			S = S.Left(Pos) + To + S.Mid(Pos + From.Len());
			Pos += To.Len();
		}
	}

	static FString BlueprintCommentsPolicyText(const EUnrealAiBlueprintCommentsMode Mode)
	{
		switch (Mode)
		{
		case EUnrealAiBlueprintCommentsMode::Off:
			return TEXT(
				"Off — Do not add Blueprint graph comment boxes or extra narrative/section labels in IR unless the user explicitly asks for comments.");
		case EUnrealAiBlueprintCommentsMode::Verbose:
			return TEXT(
				"Verbose — Prefer clear sectioning for Blueprint work: describe intent, use Sequence/structure where it helps readability, and add short readable labels when patching graphs (within blueprint_apply_ir capabilities).");
		case EUnrealAiBlueprintCommentsMode::Minimal:
		default:
			return TEXT(
				"Minimal — Use brief section labels or sparse annotations only where they materially aid navigation; avoid long prose.");
		}
	}

	static FString AgentCodeTypePreferenceParagraph()
	{
		const FString P = FUnrealAiEditorModule::GetAgentCodeTypePreference();
		if (P == TEXT("blueprint_only"))
		{
			return TEXT(
				"**Code-type preference: blueprint_only** — Prefer Blueprint graph work (`blueprint_export_ir`, `blueprint_graph_patch`, `blueprint_apply_ir`) and gameplay assets. Avoid introducing or editing C++ under `Source/` unless the user explicitly requires native code. After graph edits, run `blueprint_compile` and fix all reported errors before finishing. Use `asset_rename` for `/Game` assets; use `project_file_move` for on-disk files outside Content if needed.");
		}
		if (P == TEXT("cpp_only"))
		{
			return TEXT(
				"**Code-type preference: cpp_only** — Prefer C++ changes via `project_file_read_text` / `project_file_write_text` (drafts under `Saved/UnrealAiEditorAgent/` by default; `confirm_project_critical:true` for `Source/`, `Config/`, `.uproject`). Do not call `blueprint_apply_ir`. After native edits, prefer a closed-editor build (`build-editor.ps1`); `cpp_project_compile` in an open editor requires `confirm_external_rebuild:true` plus compiler output until clean.");
		}
		if (P == TEXT("blueprint_first"))
		{
			return TEXT(
				"**Code-type preference: blueprint_first** — Default to Blueprint graphs and `/Game` assets when either approach could work; fall back to C++ when Blueprint or tools are insufficient. Use `blueprint_graph_patch` when arbitrary K2 nodes matter; use `blueprint_apply_ir` when the compact IR ops fit. Always compile: `blueprint_compile` for Blueprints; after substantive C++ use a closed-editor build when possible, or `cpp_project_compile` with `confirm_external_rebuild:true` if the editor is interactive.");
		}
		if (P == TEXT("cpp_first"))
		{
			return TEXT(
				"**Code-type preference: cpp_first** — Default to C++ in `Source/` when either approach could work; use Blueprints where clearly better (UMG layout, rapid iteration the user asked for in BP). After C++ edits, prefer closed-editor rebuilds; interactive `cpp_project_compile` needs `confirm_external_rebuild:true`.");
		}
		return TEXT(
			"**Code-type preference: auto** — Choose Blueprint vs C++ from task fit and project signals. After Blueprint graph edits run `blueprint_compile`. After meaningful C++ edits prefer closed-editor builds or `cpp_project_compile` with `confirm_external_rebuild:true` when the editor is open. Prefer drafts in `Saved/UnrealAiEditorAgent/`; use `confirm_project_critical:true` for other paths. Prefer `project_file_move` with the same rules; use `asset_rename` for `/Game` object paths.");
	}
} // namespace UnrealAiPromptChunkUtilsPriv

FString UnrealAiPromptChunkUtils::ResolvePromptSubdir(const TCHAR* Subdir)
{
	if (TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(TEXT("UnrealAiEditor")); P.IsValid())
	{
		return FPaths::Combine(P->GetBaseDir(), TEXT("prompts"), Subdir);
	}
	return FString();
}

bool UnrealAiPromptChunkUtils::LoadChunk(const TCHAR* Subdir, const TCHAR* FileName, FString& Out)
{
	const FString Base = ResolvePromptSubdir(Subdir);
	if (Base.IsEmpty())
	{
		return false;
	}
	const FString Path = FPaths::Combine(Base, FileName);
	return FFileHelper::LoadFileToString(Out, *Path);
}

FString UnrealAiPromptChunkUtils::ExtractOperatingModeSection(const FString& Chunk02, EUnrealAiAgentMode Mode)
{
	const TCHAR* Start = nullptr;
	const TCHAR* End = nullptr;
	switch (Mode)
	{
	case EUnrealAiAgentMode::Ask:
		Start = TEXT("## Mode: Ask (`ask`)");
		End = TEXT("## Mode: Agent (`agent`)");
		break;
	case EUnrealAiAgentMode::Agent:
		Start = TEXT("## Mode: Agent (`agent`)");
		End = TEXT("## Mode: Plan (`plan`)");
		break;
	case EUnrealAiAgentMode::Plan:
		Start = TEXT("## Mode: Plan (`plan`)");
		End = nullptr;
		break;
	default:
		Start = TEXT("## Mode: Ask (`ask`)");
		End = nullptr;
		break;
	}
	const int32 Is = Chunk02.Find(Start);
	if (Is == INDEX_NONE)
	{
		return Chunk02;
	}
	int32 Ie = Chunk02.Len();
	if (End)
	{
		const int32 Found = Chunk02.Find(End, ESearchCase::CaseSensitive, ESearchDir::FromStart, Is + 1);
		if (Found != INDEX_NONE)
		{
			Ie = Found;
		}
	}
	const int32 FirstModeHeader = Chunk02.Find(TEXT("## Mode:"));
	FString Preamble;
	if (FirstModeHeader != INDEX_NONE && FirstModeHeader <= Is)
	{
		Preamble = Chunk02.Left(FirstModeHeader);
	}
	else
	{
		Preamble = Chunk02.Left(Is);
	}
	const FString Section = Chunk02.Mid(Is, Ie - Is);
	return Preamble + Section;
}

void UnrealAiPromptChunkUtils::ApplyTemplateTokens(
	FString& Doc,
	const FUnrealAiPromptAssembleParams& P,
	const FAgentContextBuildResult& B)
{
	using namespace UnrealAiPromptChunkUtilsPriv;
	ReplaceAll(Doc, TEXT("{{CONTEXT_SERVICE_OUTPUT}}"), B.ContextBlock);
	ReplaceAll(Doc, TEXT("{{AGENT_MODE}}"), AgentModeString(P.Mode));
	ReplaceAll(Doc, TEXT("{{MAX_PLAN_STEPS}}"), TEXT("12"));
	const FString TodoSum = B.ActiveTodoSummaryText.IsEmpty()
		? FString(TEXT("(no active plan on disk)"))
		: B.ActiveTodoSummaryText;
	ReplaceAll(Doc, TEXT("{{ACTIVE_TODO_SUMMARY}}"), TodoSum);
	const FString RoundStr = FString::Printf(TEXT("%d / %d"), FMath::Max(1, P.LlmRound), FMath::Max(1, P.MaxLlmRounds));
	ReplaceAll(Doc, TEXT("{{CONTINUATION_ROUND}}"), RoundStr);
	const FString Pointer = FString::Printf(
		TEXT("threadId=%s; storage=context.json activeTodoPlan + todoStepsDone"),
		P.ThreadId.IsEmpty() ? TEXT("(unknown)") : *P.ThreadId);
	ReplaceAll(Doc, TEXT("{{PLAN_POINTER}}"), Pointer);
	ReplaceAll(Doc, TEXT("((project version))"), UnrealAiAgentContextFormat::GetProjectEngineVersionLabel());
	ReplaceAll(Doc, TEXT("{{CODE_TYPE_PREFERENCE}}"), UnrealAiPromptChunkUtilsPriv::AgentCodeTypePreferenceParagraph());
	if (const UUnrealAiEditorSettings* EdSet = GetDefault<UUnrealAiEditorSettings>())
	{
		ReplaceAll(
			Doc,
			TEXT("{{BLUEPRINT_COMMENTS_POLICY}}"),
			UnrealAiPromptChunkUtilsPriv::BlueprintCommentsPolicyText(EdSet->BlueprintCommentsMode));
	}
	else
	{
		ReplaceAll(Doc, TEXT("{{BLUEPRINT_COMMENTS_POLICY}}"), FString());
	}
}
