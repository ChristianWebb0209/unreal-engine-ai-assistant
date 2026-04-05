#include "Prompt/UnrealAiPromptAssemblyStrategy.h"

#include "Prompt/UnrealAiPromptChunkUtils.h"
#include "UnrealAiBlueprintBuilderTargetKind.h"
#include "UnrealAiEnvironmentBuilderTargetKind.h"
#include "HAL/CriticalSection.h"

#include <initializer_list>

namespace UnrealAiMainAgentPromptDiskCache
{
	static FCriticalSection Mutex;
	static uint32 GCachedLayoutKey = 0;
	static uint32 GCachedDiskSig = 0;
	static FString GCachedPreTemplate;

	static uint32 MainPathLayoutKey(const FUnrealAiPromptAssembleParams& Params)
	{
		uint32 K = GetTypeHash(Params.Mode);
		K = HashCombine(K, Params.bInjectBlueprintBuilderResumeChunk ? 1u : 0u);
		K = HashCombine(K, Params.bInjectEnvironmentBuilderResumeChunk ? 1u : 0u);
		K = HashCombine(K, Params.bIncludePlanNodeExecutionChunk ? 1u : 0u);
		K = HashCombine(K, Params.bIncludeExecutionSubturnChunk ? 1u : 0u);
		K = HashCombine(K, Params.bIncludePlanDagChunk ? 1u : 0u);
		return K;
	}
}

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

	auto AppendBlueprintBuilderChunk = [&Acc](const TCHAR* FileUnderBlueprintBuilder)
	{
		FString C;
		if (UnrealAiPromptChunkUtils::LoadChunk(TEXT("chunks"), FileUnderBlueprintBuilder, C))
		{
			if (!Acc.IsEmpty())
			{
				Acc += TEXT("\n\n---\n\n");
			}
			Acc += C;
		}
	};

	auto AppendEnvironmentBuilderChunk = [&Acc](const TCHAR* FileUnderEnvironmentBuilder)
	{
		FString C;
		if (UnrealAiPromptChunkUtils::LoadChunk(TEXT("chunks"), FileUnderEnvironmentBuilder, C))
		{
			if (!Acc.IsEmpty())
			{
				Acc += TEXT("\n\n---\n\n");
			}
			Acc += C;
		}
	};

	auto AppendChunkList = [&Acc](const std::initializer_list<const TCHAR*> RelPathsUnderChunks)
	{
		for (const TCHAR* Rel : RelPathsUnderChunks)
		{
			FString C;
			if (UnrealAiPromptChunkUtils::LoadChunk(TEXT("chunks"), Rel, C))
			{
				if (!Acc.IsEmpty())
				{
					Acc += TEXT("\n\n---\n\n");
				}
				Acc += C;
			}
		}
	};

	/** Paths under `prompts/chunks/` (not `chunks/common/`) — builder handoffs, plan DAG, etc. */
	auto AppendUnderChunksTree = [&Acc](const TCHAR* RelFromChunksRoot)
	{
		FString C;
		if (UnrealAiPromptChunkUtils::LoadChunk(TEXT("chunks"), RelFromChunksRoot, C))
		{
			if (!Acc.IsEmpty())
			{
				Acc += TEXT("\n\n---\n\n");
			}
			Acc += C;
		}
	};

	if (Params.bBlueprintBuilderMode)
	{
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
		AppendBlueprintBuilderChunk(TEXT("blueprint-builder/00-overview.md"));
		AppendBlueprintBuilderChunk(TEXT("blueprint-builder/01-deterministic-loop.md"));
		AppendBlueprintBuilderChunk(TEXT("blueprint-builder/02-architecture-plain-language.md"));
		AppendBlueprintBuilderChunk(TEXT("blueprint-builder/03-fail-safe-handoff.md"));
		AppendBlueprintBuilderChunk(TEXT("blueprint-builder/05-verification-ladder.md"));
		AppendBlueprintBuilderChunk(TEXT("blueprint-builder/06-cross-tool-identity.md"));
		AppendBlueprintBuilderChunk(TEXT("blueprint-builder/07-graph-patch-canonical.md"));
		{
			const FString KindRel = UnrealAiBlueprintBuilderTargetKind::KindChunkFileName(Params.BlueprintBuilderTargetKind);
			FString KindChunk;
			if (UnrealAiPromptChunkUtils::LoadChunk(TEXT("chunks"), *KindRel, KindChunk) && !KindChunk.IsEmpty())
			{
				if (!Acc.IsEmpty())
				{
					Acc += TEXT("\n\n---\n\n");
				}
				Acc += KindChunk;
			}
		}
		AppendChunk(TEXT("05-context-and-editor.md"));
		AppendChunk(TEXT("07-safety-banned.md"));
		AppendChunk(TEXT("08-output-style.md"));
		UnrealAiPromptChunkUtils::ApplyTemplateTokens(Acc, Params, B);
		if (!B.SystemOrDeveloperBlock.IsEmpty())
		{
			Acc = B.SystemOrDeveloperBlock + TEXT("\n\n---\n\n") + Acc;
		}
		return Acc;
	}

	if (Params.bEnvironmentBuilderMode)
	{
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
		AppendEnvironmentBuilderChunk(TEXT("environment-builder/00-overview.md"));
		AppendEnvironmentBuilderChunk(TEXT("environment-builder/01-deterministic-loop.md"));
		AppendEnvironmentBuilderChunk(TEXT("environment-builder/02-unreal-pcg-model.md"));
		AppendEnvironmentBuilderChunk(TEXT("environment-builder/03-landscape-and-height.md"));
		AppendEnvironmentBuilderChunk(TEXT("environment-builder/04-foliage-and-instances.md"));
		AppendEnvironmentBuilderChunk(TEXT("environment-builder/05-scene-safety.md"));
		AppendEnvironmentBuilderChunk(TEXT("environment-builder/06-verification-ladder.md"));
		{
			const FString KindRel = UnrealAiEnvironmentBuilderTargetKind::KindChunkFileName(Params.EnvironmentBuilderTargetKind);
			FString KindChunk;
			if (UnrealAiPromptChunkUtils::LoadChunk(TEXT("chunks"), *KindRel, KindChunk) && !KindChunk.IsEmpty())
			{
				if (!Acc.IsEmpty())
				{
					Acc += TEXT("\n\n---\n\n");
				}
				Acc += KindChunk;
			}
		}
		AppendChunk(TEXT("05-context-and-editor.md"));
		AppendChunk(TEXT("07-safety-banned.md"));
		AppendChunk(TEXT("08-output-style.md"));
		UnrealAiPromptChunkUtils::ApplyTemplateTokens(Acc, Params, B);
		if (!B.SystemOrDeveloperBlock.IsEmpty())
		{
			Acc = B.SystemOrDeveloperBlock + TEXT("\n\n---\n\n") + Acc;
		}
		return Acc;
	}

	{
		const uint32 LayoutKey = UnrealAiMainAgentPromptDiskCache::MainPathLayoutKey(Params);
		const uint32 DiskSig = UnrealAiPromptChunkUtils::GetPromptsLooseSignatureThrottled();
		bool bFromCache = false;
		{
			FScopeLock Lock(&UnrealAiMainAgentPromptDiskCache::Mutex);
			if (LayoutKey == UnrealAiMainAgentPromptDiskCache::GCachedLayoutKey && DiskSig == UnrealAiMainAgentPromptDiskCache::GCachedDiskSig
				&& !UnrealAiMainAgentPromptDiskCache::GCachedPreTemplate.IsEmpty())
			{
				Acc = UnrealAiMainAgentPromptDiskCache::GCachedPreTemplate;
				bFromCache = true;
			}
		}
		if (!bFromCache)
		{
			Acc.Reset();
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
			AppendUnderChunksTree(TEXT("blueprint-builder/08-delegation-from-main-agent.md"));
			if (Params.bInjectBlueprintBuilderResumeChunk)
			{
				AppendUnderChunksTree(TEXT("blueprint-builder/09-resume-on-main-agent.md"));
			}
			AppendUnderChunksTree(TEXT("environment-builder/07-delegation-from-main-agent.md"));
			if (Params.bInjectEnvironmentBuilderResumeChunk)
			{
				AppendUnderChunksTree(TEXT("environment-builder/08-resume-on-main-agent.md"));
			}
			AppendChunk(TEXT("05-context-and-editor.md"));
			AppendChunk(TEXT("10-mvp-gameplay-and-tooling.md"));
			if (Params.bIncludePlanNodeExecutionChunk)
			{
				AppendChunkList({
					TEXT("plan-node/01-scope-and-context.md"),
					TEXT("plan-node/02-completion-and-tools.md"),
					TEXT("plan-node/03-retries-blockers-budget.md"),
				});
			}
			if (Params.bIncludeExecutionSubturnChunk)
			{
				AppendChunk(TEXT("06-execution-subturn.md"));
			}
			AppendChunk(TEXT("07-safety-banned.md"));
			AppendChunk(TEXT("08-output-style.md"));
			if (Params.bIncludePlanDagChunk)
			{
				AppendChunkList({
					TEXT("plan/01-overview-shape-next.md"),
					TEXT("plan/02-rules-and-selfchecks.md"),
					TEXT("plan/03-automatic-replan.md"),
					TEXT("plan/04-policies-and-canonical-shape.md"),
				});
			}

			FScopeLock Lock(&UnrealAiMainAgentPromptDiskCache::Mutex);
			UnrealAiMainAgentPromptDiskCache::GCachedLayoutKey = LayoutKey;
			UnrealAiMainAgentPromptDiskCache::GCachedDiskSig = DiskSig;
			UnrealAiMainAgentPromptDiskCache::GCachedPreTemplate = Acc;
		}
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
	static FUnrealAiLinearPromptAssemblyStrategy GLegacy(TEXT("chunks/common"));
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
