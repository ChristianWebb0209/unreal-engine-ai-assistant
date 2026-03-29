#include "Prompt/UnrealAiPromptChunkUtils.h"

#include "Context/AgentContextFormat.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Prompt/UnrealAiPromptAssembleParams.h"

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
}
