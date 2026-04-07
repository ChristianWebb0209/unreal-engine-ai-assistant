#include "Tools/UnrealAiEnvironmentBuilderToolSurface.h"

#include "Misc/UnrealAiRuntimeDefaults.h"
#include "Tools/UnrealAiToolCatalog.h"

bool UnrealAiEnvironmentBuilderToolSurface::TryComputeAppendixBudgetFromModelContext(
	int32 MaxContextTokens,
	int32 CharPerTokenApprox,
	int32& OutAppendixBudgetChars,
	FString* OutFailureReason)
{
	int32 Tok = MaxContextTokens;
	if (Tok <= 0)
	{
		Tok = UnrealAiRuntimeDefaults::BlueprintBuilderFallbackContextTokensWhenUnset;
	}
	const int32 Cpt = FMath::Max(1, CharPerTokenApprox);
	const int32 TotalWindowChars = FMath::Max(4096, Tok * Cpt);
	const float Frac = UnrealAiRuntimeDefaults::BlueprintBuilderToolSurfaceMaxContextFraction;
	const int32 Proposed = static_cast<int32>(static_cast<float>(TotalWindowChars) * Frac);
	const int32 Capped = FMath::Min(Proposed, UnrealAiRuntimeDefaults::BlueprintBuilderToolSurfaceBudgetChars);
	const int32 MinB = UnrealAiRuntimeDefaults::BlueprintBuilderToolSurfaceMinBudgetChars;
	if (Capped < MinB)
	{
		if (OutFailureReason)
		{
			*OutFailureReason = FString::Printf(
				TEXT("Environment Builder needs a larger model context: tool appendix budget is ~%d chars (%.0f%% of ~%d context tokens × %d chars/token), ")
				TEXT("but at least ~%d chars is required. Raise **Max context tokens** in the model profile, or use a model with a bigger window."),
				Capped,
				static_cast<double>(Frac * 100.0),
				MaxContextTokens > 0 ? MaxContextTokens : Tok,
				Cpt,
				MinB);
		}
		return false;
	}
	OutAppendixBudgetChars = Capped;
	return true;
}

void UnrealAiEnvironmentBuilderToolSurface::AugmentHybridRetrievalQuery(FString& InOutHybrid)
{
	InOutHybrid += TEXT(
		" pcg_generate landscape foliage heightmap terrain procedural_content_generation PCGComponent landscape_foliage_pcg ");
}

FString UnrealAiEnvironmentBuilderToolSurface::SurfaceProfileTelemetryId()
{
	return TEXT("environment_builder_verbose");
}

void UnrealAiEnvironmentBuilderToolSurface::MergeEnvironmentCoreToolsAfterGuardrails(
	const FUnrealAiToolCatalog& Catalog,
	EUnrealAiAgentMode Mode,
	const FUnrealAiModelCapabilities& Caps,
	const FUnrealAiToolPackOptions* PackOptions,
	TFunctionRef<bool(const FString& ToolId)> ToolFilter,
	const TSet<FString>& GuardrailIds,
	TArray<FString>& InOutOrdered)
{
	TArray<FString> CoreInsert;
	Catalog.ForEachEnabledToolForMode(
		Mode,
		Caps,
		PackOptions,
		ToolFilter,
		[&](const FString& Tid, const TSharedPtr<FJsonObject>& Def)
		{
			bool bCore = false;
			const TSharedPtr<FJsonObject>* Ctx = nullptr;
			if (Def->TryGetObjectField(TEXT("context_selector"), Ctx) && Ctx && (*Ctx).IsValid())
			{
				(*Ctx)->TryGetBoolField(TEXT("environment_builder_core"), bCore);
			}
			if (bCore)
			{
				CoreInsert.Add(Tid);
			}
		});
	CoreInsert.Sort();
	TSet<FString> SeenOrdered(InOutOrdered);
	int32 SplitIdx = 0;
	for (; SplitIdx < InOutOrdered.Num(); ++SplitIdx)
	{
		if (!GuardrailIds.Contains(InOutOrdered[SplitIdx]))
		{
			break;
		}
	}
	TArray<FString> Prefix;
	for (int32 I = 0; I < SplitIdx; ++I)
	{
		Prefix.Add(InOutOrdered[I]);
	}
	TArray<FString> Tail;
	for (int32 I = SplitIdx; I < InOutOrdered.Num(); ++I)
	{
		Tail.Add(InOutOrdered[I]);
	}
	InOutOrdered.Reset();
	InOutOrdered.Append(Prefix);
	for (const FString& Cid : CoreInsert)
	{
		if (!SeenOrdered.Contains(Cid))
		{
			InOutOrdered.Add(Cid);
			SeenOrdered.Add(Cid);
		}
	}
	for (const FString& T : Tail)
	{
		if (!SeenOrdered.Contains(T))
		{
			InOutOrdered.Add(T);
			SeenOrdered.Add(T);
		}
	}
}

void UnrealAiEnvironmentBuilderToolSurface::WidenKeffectiveToFullEligibleRoster(int32 NonGuardrailPoolCount, int32& InOutKeffective)
{
	InOutKeffective = FMath::Max(InOutKeffective, NonGuardrailPoolCount);
}

void UnrealAiEnvironmentBuilderToolSurface::GetVerboseAppendixSettings(
	int32 OrderedToolCount,
	int32 MaxAppendixBudgetChars,
	int32& OutExpandedCount,
	int32& OutBudgetChars,
	int32& OutParametersExcerptMaxChars)
{
	OutExpandedCount = FMath::Max(OrderedToolCount, 1);
	OutBudgetChars = MaxAppendixBudgetChars;
	OutParametersExcerptMaxChars = UnrealAiRuntimeDefaults::BlueprintBuilderToolParamsExcerptMaxChars;
}

FString UnrealAiEnvironmentBuilderToolSurface::BuildAutomatedSubturnHarnessPreamble(
	const EUnrealAiEnvironmentBuilderTargetKind TargetKind)
{
	const FString Domain = UnrealAiEnvironmentBuilderTargetKind::ToDomainString(TargetKind);
	FString Header = FString::Printf(
		TEXT("[Environment Builder — automated sub-turn]\n")
		TEXT("The main agent delegated work using `<unreal_ai_build_environment>` with **target_kind: %s**. ")
		TEXT("Use discovery tools to resolve real `/Game/...` asset paths and world actor paths before mutating. ")
		TEXT("Catalog tools `pcg_generate`, `foliage_paint_instances`, and `landscape_import_heightmap` are **implemented** in-editor; they may still return structured errors when prerequisites are missing (e.g. no PCG component, missing foliage type, heightmap file path).\n")
		TEXT("\n")
		TEXT("When finished or blocked, emit `<unreal_ai_environment_builder_result>...</unreal_ai_environment_builder_result>` for the main agent.\n")
		TEXT("\n")
		TEXT("--- Delegated specification (verbatim from main agent) ---\n"),
		*Domain);
	return Header;
}
