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

