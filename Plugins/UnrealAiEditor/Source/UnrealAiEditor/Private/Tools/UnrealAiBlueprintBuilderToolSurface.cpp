#include "Tools/UnrealAiBlueprintBuilderToolSurface.h"

#include "Misc/UnrealAiRuntimeDefaults.h"
#include "Tools/UnrealAiToolCatalog.h"
#include "UnrealAiBlueprintBuilderTargetKind.h"

namespace UnrealAiBlueprintBuilderToolSurfacePriv
{
	static bool DefMatchesBuilderDomain(const TSharedPtr<FJsonObject>& Def, EUnrealAiBlueprintBuilderTargetKind Kind)
	{
		const FString Want = UnrealAiBlueprintBuilderTargetKind::ToDomainString(Kind);
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Def->TryGetArrayField(TEXT("builder_domains"), Arr) && Arr && Arr->Num() > 0)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S))
				{
					S.TrimStartAndEndInline();
					if (S.Equals(Want, ESearchCase::IgnoreCase))
					{
						return true;
					}
				}
			}
			return false;
		}
		// Legacy catalog entries: no builder_domains => script_blueprint (K2 stack) only.
		return Kind == EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint;
	}
} // namespace UnrealAiBlueprintBuilderToolSurfacePriv

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
		" blueprint blueprint_graph blueprint_compile blueprint_export blueprint_import t3d k2 node graph pin guid patch apply_ir verify compile asset "
		"material_graph material_graph_export material_graph_patch material_graph_compile material_graph_validate UMaterial expression ");
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
	EUnrealAiBlueprintBuilderTargetKind BuilderTargetKind,
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
			if (bCore && UnrealAiBlueprintBuilderToolSurfacePriv::DefMatchesBuilderDomain(Def, BuilderTargetKind))
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

FString UnrealAiBlueprintBuilderToolSurface::BuildAutomatedSubturnHarnessPreamble(
	const EUnrealAiBlueprintBuilderTargetKind BuilderTargetKind)
{
	const FString Domain = UnrealAiBlueprintBuilderTargetKind::ToDomainString(BuilderTargetKind);
	FString Header = FString::Printf(
		TEXT("[Blueprint Builder — automated sub-turn]\n")
		TEXT("The main agent delegated work using `<unreal_ai_build_blueprint>` with **target_kind: %s**. ")
		TEXT("Target assets MUST already exist at the paths referenced below. Follow the **domain chunk** in the system prompt for this kind — Kismet/T3D loops apply to **script_blueprint** (and compatible graphs); other domains use different tool subsets.\n")
		TEXT("\n"),
		*Domain);
	Header += TEXT(
		"You are in a **verbose tool context**: the system message includes an **expanded** tool roster (summaries + parameter schema excerpts). "
		"The roster is filled **up to a budget** derived from this model profile's **context window** (see plugin defaults: fraction of window, min budget); "
		"if the catalog is larger than that budget, trailing tools may be omitted from the appendix — prefer IDs you see listed. "
		"Prefer tools marked for this `target_kind`; use other tools (assets, search, project files, etc.) when the spec requires them.\n"
		"\n");

	if (BuilderTargetKind == EUnrealAiBlueprintBuilderTargetKind::MaterialGraph)
	{
		Header += TEXT(
			"**Preferred graph loop (base Material / `material_graph` — not K2):**\n"
			"1. `material_graph_export` on the **UMaterial** object path (use `material_graph_summarize` only for a lightweight node list).\n"
			"2. Plan `material_graph_patch` **ops[]** batches: `add_expression` (`class_path` e.g. `/Script/Engine.MaterialExpressionConstant3Vector`), "
			"`connect` / `connect_material_input` (root slots: `BaseColor`, `Normal`, `MaterialAttributes`, `CustomizedUVs_0`, …), `set_constant3vector`, "
			"`disconnect_*`, `delete_expression`, `set_editor_position`. Use **expression_guid** from export / add_expression results — not Blueprint T3D.\n"
			"3. `material_graph_compile` (or `recompile: true` on the last patch) then `material_graph_validate` (orphans + compile flag).\n"
			"\n"
			"**Do not** use `blueprint_graph_patch`, `blueprint_apply_ir`, `blueprint_graph_import_t3d`, or other K2 Blueprint graph mutators on Materials.\n"
			"\n");
	}
	else
	{
		Header += TEXT(
			"**Preferred graph loop (Kismet / script Blueprints — skip if your domain chunk says otherwise):**\n"
			"1. Read: `blueprint_graph_introspect` (pins include `linked_to` peers), `blueprint_export_graph_t3d`, `blueprint_export_ir`, or `blueprint_get_graph_summary`.\n"
			"2. **Wiring is explicit:** `blueprint_apply_ir` **`links[]`** and/or `blueprint_graph_patch` **`connect`** in the **same** batch as new nodes; **T3D** must keep export-style pin connection lines — create-only payloads yield **floating nodes**.\n"
			"3. T3D path: placeholders **`__UAI_G_NNNNNN__`** (six digits) — do **not** invent raw `FGuid` literals; `blueprint_t3d_preflight_validate` → `blueprint_graph_import_t3d`.\n"
			"4. `blueprint_compile` then `blueprint_verify_graph` e.g. `[\"links\",\"orphan_pins\",\"duplicate_node_guids\",\"dead_exec_outputs\",\"pin_type_mismatch\"]` — fix and retry until clean when possible.\n"
			"5. Layout: `blueprint_format_graph` / `blueprint_format_selection` / IR `auto_layout` — spacing, knots, preserve-existing, and comments follow **Editor Preferences → Plugins → Unreal AI Editor → Blueprint Formatting** (toolbar combo next to AI format buttons). Prefer `layout_scope: ir_nodes` / `patched_nodes` unless a full-graph reformat is intended; large IR applies may auto-promote to full-graph layout.\n"
			"\n"
			"**Fallback** when T3D is a poor fit: `blueprint_apply_ir` / `blueprint_graph_patch` (still require explicit links/connect ops).\n"
			"\n");
	}

	Header += TEXT(
		"When finished or blocked, emit `<unreal_ai_blueprint_builder_result>...</unreal_ai_blueprint_builder_result>` for the main agent.\n"
		"\n"
		"--- Delegated specification (verbatim from main agent) ---\n");
	return Header;
}
