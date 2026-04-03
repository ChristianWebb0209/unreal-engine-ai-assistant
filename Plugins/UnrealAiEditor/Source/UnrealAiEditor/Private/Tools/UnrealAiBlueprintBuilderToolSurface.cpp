#include "Tools/UnrealAiBlueprintBuilderToolSurface.h"

#include "Misc/UnrealAiRuntimeDefaults.h"
#include "Tools/UnrealAiToolCatalog.h"

bool UnrealAiBlueprintBuilderToolSurface::TryComputeAppendixBudgetFromModelContext(
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
				TEXT("Blueprint Builder needs a larger model context: tool appendix budget is ~%d chars (%.0f%% of ~%d context tokens × %d chars/token), ")
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

void UnrealAiBlueprintBuilderToolSurface::AugmentHybridRetrievalQuery(FString& InOutHybrid)
{
	InOutHybrid += TEXT(
		" blueprint blueprint_graph blueprint_compile blueprint_export blueprint_import t3d k2 node graph pin guid patch apply_ir verify compile asset ");
}

FString UnrealAiBlueprintBuilderToolSurface::SurfaceProfileTelemetryId()
{
	return TEXT("blueprint_builder_verbose");
}

void UnrealAiBlueprintBuilderToolSurface::MergeBlueprintCoreToolsAfterGuardrails(
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
				(*Ctx)->TryGetBoolField(TEXT("blueprint_builder_core"), bCore);
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

void UnrealAiBlueprintBuilderToolSurface::WidenKeffectiveToFullEligibleRoster(int32 NonGuardrailPoolCount, int32& InOutKeffective)
{
	InOutKeffective = FMath::Max(InOutKeffective, NonGuardrailPoolCount);
}

void UnrealAiBlueprintBuilderToolSurface::GetVerboseAppendixSettings(
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

FString UnrealAiBlueprintBuilderToolSurface::BuildAutomatedSubturnHarnessPreamble()
{
	return TEXT(
		"[Blueprint Builder — automated sub-turn]\n"
		"The main agent delegated work using `<unreal_ai_build_blueprint>`. Target Blueprint assets MUST already exist at the paths referenced below.\n"
		"\n"
		"You are in a **verbose tool context**: the system message includes an **expanded** tool roster (summaries + parameter schema excerpts). "
		"The roster is filled **up to a budget** derived from this model profile's **context window** (see plugin defaults: fraction of window, min budget); "
		"if the catalog is larger than that budget, trailing tools may be omitted from the appendix — prefer IDs you see listed. "
		"Prefer the curated Blueprint/T3D tools; use other tools (assets, search, project files, etc.) when the spec requires them.\n"
		"\n"
		"**Preferred graph loop (deterministic):**\n"
		"1. `blueprint_graph_introspect` / `blueprint_export_graph_t3d` (or `blueprint_export_ir` / `blueprint_get_graph_summary`) to ground pins and structure.\n"
		"2. Author or edit **T3D** using placeholder node GUIDs **`__UAI_G_NNNNNN__`** (six digits) — do **not** invent raw Unreal `FGuid` literals.\n"
		"3. `blueprint_t3d_preflight_validate` → `blueprint_graph_import_t3d` (atomic `ImportNodesFromText`).\n"
		"4. `blueprint_compile` then `blueprint_verify_graph` with steps such as `[\"links\",\"orphan_pins\",\"duplicate_node_guids\"]`.\n"
		"5. Optional layout: `blueprint_format_graph` / `blueprint_format_selection`.\n"
		"\n"
		"**Fallback** when T3D is a poor fit: `blueprint_apply_ir` / `blueprint_graph_patch` in coherent batches.\n"
		"\n"
		"When finished or blocked, emit `<unreal_ai_blueprint_builder_result>...</unreal_ai_blueprint_builder_result>` for the main agent.\n"
		"\n"
		"--- Delegated specification (verbatim from main agent) ---\n");
}
