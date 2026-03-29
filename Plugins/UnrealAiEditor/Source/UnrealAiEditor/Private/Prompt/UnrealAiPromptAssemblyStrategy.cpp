#include "Prompt/UnrealAiPromptAssemblyStrategy.h"

#include "Prompt/UnrealAiPromptChunkUtils.h"

FString FUnrealAiLinearPromptAssemblyStrategy::BuildSystemDeveloperContent(const FUnrealAiPromptAssembleParams& Params) const
{
	if (!Params.Built)
	{
		return FString();
	}
	const FAgentContextBuildResult& B = *Params.Built;
	FString Acc;

	auto AppendChunk = [&Acc, this](const TCHAR* File)
	{
		FString C;
		if (UnrealAiPromptChunkUtils::LoadChunk(ChunkSubdir, File, C))
		{
			if (!Acc.IsEmpty())
			{
				Acc += TEXT("\n\n---\n\n");
			}
			Acc += C;
		}
	};

	AppendChunk(TEXT("01-identity.md"));

	{
		FString C2;
		if (UnrealAiPromptChunkUtils::LoadChunk(ChunkSubdir, TEXT("02-operating-modes.md"), C2))
		{
			C2 = UnrealAiPromptChunkUtils::ExtractOperatingModeSection(C2, Params.Mode);
			if (!Acc.IsEmpty())
			{
				Acc += TEXT("\n\n---\n\n");
			}
			Acc += C2;
		}
	}

	AppendChunk(TEXT("03-complexity-and-todo-plan.md"));
	AppendChunk(TEXT("04-tool-calling-contract.md"));
	AppendChunk(TEXT("05-context-and-editor.md"));
	AppendChunk(TEXT("10-mvp-gameplay-and-tooling.md"));
	if (Params.bIncludePlanNodeExecutionChunk)
	{
		AppendChunk(TEXT("11-plan-node-execution.md"));
	}
	if (Params.bIncludeExecutionSubturnChunk)
	{
		AppendChunk(TEXT("06-execution-subturn.md"));
	}
	AppendChunk(TEXT("07-safety-banned.md"));
	AppendChunk(TEXT("08-output-style.md"));
	if (Params.bIncludePlanDagChunk)
	{
		AppendChunk(TEXT("09-plan-dag.md"));
	}

	UnrealAiPromptChunkUtils::ApplyTemplateTokens(Acc, Params, B);

	if (!B.SystemOrDeveloperBlock.IsEmpty())
	{
		Acc = B.SystemOrDeveloperBlock + TEXT("\n\n---\n\n") + Acc;
	}

	if (Acc.IsEmpty())
	{
		return TEXT(
			"You are an AI assistant embedded in the Unreal Editor. Follow the user's instructions, respect tool and safety policies, and prefer read-only tools before writes.");
	}

	return Acc;
}

EUnrealAiPromptAssemblyKind UnrealAiPromptAssembly::GetEffectiveAssemblyKind()
{
	return EUnrealAiPromptAssemblyKind::LegacyChunks;
}

const IUnrealAiPromptAssemblyStrategy& UnrealAiPromptAssembly::GetStrategyForKind(const EUnrealAiPromptAssemblyKind Kind)
{
	static FUnrealAiLinearPromptAssemblyStrategy GLegacy(TEXT("chunks"));
	switch (Kind)
	{
	case EUnrealAiPromptAssemblyKind::LegacyChunks:
	default:
		return GLegacy;
	}
}

FString UnrealAiPromptAssembly::KindToTelemetryString(const EUnrealAiPromptAssemblyKind Kind)
{
	switch (Kind)
	{
	case EUnrealAiPromptAssemblyKind::LegacyChunks:
	default:
		return TEXT("legacy_chunks");
	}
}
