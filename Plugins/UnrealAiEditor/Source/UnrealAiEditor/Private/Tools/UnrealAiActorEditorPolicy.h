#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "GameFramework/Actor.h"

namespace UnrealAiActorEditorPolicy
{
	/** Built-in excludes: streaming / HLOD / partition helpers — not user blockout geometry. */
	inline bool ClassOrPathMatchesBuiltInExclude(const AActor* A)
	{
		if (!A)
		{
			return false;
		}
		const FString CN = A->GetClass()->GetName();
		const FString PN = A->GetPathName();
		if (CN.Contains(TEXT("HLOD"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (CN.Contains(TEXT("WorldPartitionHLOD"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (PN.Contains(TEXT("HLOD0_Instancing"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (CN.Contains(TEXT("WPRuntime"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		return false;
	}

	/**
	 * When bIncludeEngineInternals is false, apply built-in excludes plus optional class substring excludes.
	 * When true, only ExtraClassSubstrings are applied (power-user / diagnostics).
	 */
	inline bool ShouldExcludeFromSceneFuzzySearch(
		const AActor* A,
		const bool bIncludeEngineInternals,
		const TArray<FString>& ExtraClassSubstrings)
	{
		if (!A)
		{
			return false;
		}
		const FString ClassName = A->GetClass()->GetName();
		auto MatchesExtra = [&]() -> bool
		{
			for (const FString& S : ExtraClassSubstrings)
			{
				if (!S.IsEmpty() && ClassName.Contains(S, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
			return false;
		};
		if (bIncludeEngineInternals)
		{
			return MatchesExtra();
		}
		if (ClassOrPathMatchesBuiltInExclude(A))
		{
			return true;
		}
		return MatchesExtra();
	}

	/** Refuse actor_destroy on the same built-in infrastructure actors. */
	inline bool IsProtectedFromAiDestruction(const AActor* A)
	{
		return A && ClassOrPathMatchesBuiltInExclude(A);
	}
} // namespace UnrealAiActorEditorPolicy
