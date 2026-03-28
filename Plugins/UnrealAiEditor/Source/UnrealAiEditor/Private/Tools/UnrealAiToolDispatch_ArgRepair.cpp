#include "Tools/UnrealAiToolDispatch_ArgRepair.h"

#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"

namespace UnrealAiToolDispatchArgRepair
{
	void SanitizeUnrealPathString(FString& S)
	{
		S.TrimStartAndEndInline();
		// Strip repeated outer quote pairs often emitted as SQL-style literals: '/Game/Foo'
		for (int32 Guard = 0; Guard < 8 && S.Len() >= 2; ++Guard)
		{
			const TCHAR A = S[0];
			const TCHAR B = S[S.Len() - 1];
			const bool bPair = (A == TEXT('\'') && B == TEXT('\'')) || (A == TEXT('"') && B == TEXT('"'));
			if (!bPair)
			{
				break;
			}
			S = S.Mid(1, S.Len() - 2);
			S.TrimStartAndEndInline();
		}
	}

	void NormalizeAssetLikeObjectPath(FString& Path)
	{
		SanitizeUnrealPathString(Path);
		if (Path.IsEmpty() || Path.Contains(TEXT("..")))
		{
			return;
		}
		if (Path.EndsWith(TEXT(".uasset"), ESearchCase::IgnoreCase))
		{
			Path.LeftChopInline(7);
			Path.TrimEndInline();
		}
		UnrealAiNormalizeBlueprintObjectPath(Path);
	}

	void RepairBlueprintAssetPathArgs(const TSharedPtr<FJsonObject>& Args)
	{
		if (!Args.IsValid())
		{
			return;
		}
		FString Path;
		Args->TryGetStringField(TEXT("blueprint_path"), Path);
		if (Path.IsEmpty())
		{
			Args->TryGetStringField(TEXT("object_path"), Path);
		}
		if (Path.IsEmpty())
		{
			Args->TryGetStringField(TEXT("asset_path"), Path);
		}
		if (Path.IsEmpty())
		{
			return;
		}
		NormalizeAssetLikeObjectPath(Path);
		if (!Path.IsEmpty())
		{
			Args->SetStringField(TEXT("blueprint_path"), Path);
		}
	}
} // namespace UnrealAiToolDispatchArgRepair
