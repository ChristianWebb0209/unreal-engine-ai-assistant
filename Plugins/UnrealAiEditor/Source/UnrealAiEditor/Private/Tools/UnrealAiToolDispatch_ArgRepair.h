#pragma once

#include "Dom/JsonObject.h"

/**
 * Small shared helpers for canonicalizing common argument aliases.
 *
 * Intent:
 * - Keep dispatchers tolerant to legacy/alias key drift.
 * - When a legacy key is used, canonicalize into the strict key so downstream
 *   validation/parsing and suggested_correct_call retries are deterministic.
 */
namespace UnrealAiToolDispatchArgRepair
{
	/** Strip SQL/JSON-style wrapping quotes and trim; safe to call on path-like strings. */
	void SanitizeUnrealPathString(FString& S);

	/**
	 * Best-effort repair for common hallucinated asset object paths:
	 * - outer quotes (`'/Game/...'`)
	 * - file-style `.uasset` suffix
	 * - `/Game/.../Leaf` without `.Leaf` object suffix (delegates to Blueprint normalizer).
	 */
	void NormalizeAssetLikeObjectPath(FString& Path);

	/**
	 * Canonicalize blueprint_path: merge object_path/asset_path aliases, strip quotes / .uasset,
	 * apply Blueprint path normalization, write back blueprint_path.
	 */
	void RepairBlueprintAssetPathArgs(const TSharedPtr<FJsonObject>& Args);

	/**
	 * Pre-schema fixes for blueprint_graph_patch: normalize k2_class (UK2Node_*, bare K2Node_*),
	 * strip node_guid on create_node, copy class_name/node_class -> k2_class (semantic_kind is not aliased here),
	 * fold connect/break_link/splice link_from|link_to and split from_node+from_pin / to_node+to_pin into from|to,
	 * fold typ|pin_type|variable_type -> type on add_variable,
	 * hoist event_override fields, repair Character Jump/Landed call targets, optional graph_patch_repairs on Audit.
	 */
	void RepairBlueprintGraphPatchToolArgs(
		const TSharedPtr<FJsonObject>& Args,
		const TSharedPtr<FJsonObject>& Audit = nullptr);

	static bool TryGetStringFieldCanonical(
		const TSharedPtr<FJsonObject>& Args,
		const TCHAR* CanonicalKey,
		const TArray<const TCHAR*>& Aliases,
		FString& OutValue)
	{
		if (!Args.IsValid())
		{
			return false;
		}

		if (Args->TryGetStringField(CanonicalKey, OutValue) && !OutValue.IsEmpty())
		{
			return true;
		}

		for (const TCHAR* AliasKey : Aliases)
		{
			if (!AliasKey)
			{
				continue;
			}
			FString Tmp;
			if (Args->TryGetStringField(AliasKey, Tmp) && !Tmp.IsEmpty())
			{
				Args->SetStringField(CanonicalKey, Tmp);
				OutValue = MoveTemp(Tmp);
				return true;
			}
		}
		return false;
	}
} // namespace UnrealAiToolDispatchArgRepair

