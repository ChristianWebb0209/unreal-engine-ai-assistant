#pragma once

#include "CoreMinimal.h"

class UClass;
class UFactory;

/**
 * Non-interactive factory resolution for asset_create.
 *
 * Goals:
 * - Deterministic selection (same inputs -> same factory).
 * - Avoid UI/modal ConfigureProperties().
 * - Prefer a tiny explicit mapping for common asset classes, then fall back to generic scoring.
 */
class FUnrealAiAssetFactoryResolver
{
public:
	struct FResolveResult
	{
		/** Instantiated transient factory (may be null on failure). */
		UFactory* Factory = nullptr;
		/** Resolved factory class path if known (useful for suggested_correct_call). */
		FString FactoryClassPath;
		/** Human-readable notes about decisions/filters. */
		TArray<FString> Notes;
		/** Candidate class paths considered (bounded). */
		TArray<FString> Candidates;
	};

	/** Resolve either a provided factory class path or infer one for AssetClass. */
	static FResolveResult Resolve(UClass* AssetClass, const FString& OptionalFactoryClassPath);

private:
	static FResolveResult ResolveExplicit(UClass* AssetClass, const FString& FactoryClassPath);
	static FResolveResult ResolveMapped(UClass* AssetClass);
	static FResolveResult ResolveGenericScored(UClass* AssetClass);
};

