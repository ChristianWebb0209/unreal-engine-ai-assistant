#include "Tools/UnrealAiToolDispatch_ArgRepair.h"

#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"

namespace UnrealAiToolDispatchArgRepair
{
	namespace
	{
		void NormalizeK2ClassPathForGraphPatch(FString& S)
		{
			SanitizeUnrealPathString(S);
			if (S.IsEmpty())
			{
				return;
			}

			auto StripUK2PrefixInScriptPath = [&S](const TCHAR* ModulePrefix)
			{
				const FString WithU = FString::Printf(TEXT("%sUK2Node_"), ModulePrefix);
				if (S.StartsWith(WithU, ESearchCase::IgnoreCase))
				{
					S = FString::Printf(TEXT("%sK2Node_"), ModulePrefix) + S.Mid(WithU.Len());
				}
			};
			StripUK2PrefixInScriptPath(TEXT("/Script/BlueprintGraph."));
			StripUK2PrefixInScriptPath(TEXT("/Script/InputBlueprintNodes."));

			if (!S.StartsWith(TEXT("/Script/")))
			{
				FString Leaf = S;
				Leaf.TrimStartAndEndInline();
				if (Leaf.StartsWith(TEXT("UK2Node_"), ESearchCase::IgnoreCase))
				{
					Leaf = FString(TEXT("K2Node_")) + Leaf.Mid(8);
				}
				if (Leaf.StartsWith(TEXT("K2Node_"), ESearchCase::IgnoreCase))
				{
					if (Leaf.StartsWith(TEXT("K2Node_Enhanced"), ESearchCase::IgnoreCase))
					{
						S = FString(TEXT("/Script/InputBlueprintNodes.")) + Leaf;
					}
					else
					{
						S = FString(TEXT("/Script/BlueprintGraph.")) + Leaf;
					}
				}
			}
		}
	} // namespace

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
			Args->TryGetStringField(TEXT("path"), Path);
		}
		if (Path.IsEmpty())
		{
			Args->TryGetStringField(TEXT("blueprint"), Path);
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

	void RepairBlueprintGraphPatchToolArgs(const TSharedPtr<FJsonObject>& Args)
	{
		if (!Args.IsValid())
		{
			return;
		}
		const TArray<TSharedPtr<FJsonValue>>* Ops = nullptr;
		if (!Args->TryGetArrayField(TEXT("ops"), Ops) || !Ops)
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& V : *Ops)
		{
			TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
			{
				continue;
			}
			TSharedPtr<FJsonObject> Op = *Obj;
			FString OpName;
			Op->TryGetStringField(TEXT("op"), OpName);
			OpName.TrimStartAndEndInline();
			if (!OpName.Equals(TEXT("create_node"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			// Breaks JSON schema additionalProperties:false; C++ never consumed this.
			Op->RemoveField(TEXT("node_guid"));
			Op->RemoveField(TEXT("class_name_unreal"));
			FString K2;
			Op->TryGetStringField(TEXT("k2_class"), K2);
			if (K2.IsEmpty())
			{
				FString CN;
				if (Op->TryGetStringField(TEXT("class_name"), CN) && !CN.IsEmpty())
				{
					Op->SetStringField(TEXT("k2_class"), CN);
					Op->RemoveField(TEXT("class_name"));
				}
			}
			if (Op->TryGetStringField(TEXT("k2_class"), K2) && !K2.IsEmpty())
			{
				NormalizeK2ClassPathForGraphPatch(K2);
				Op->SetStringField(TEXT("k2_class"), K2);
			}
			Op->RemoveField(TEXT("class_name"));
		}
	}
} // namespace UnrealAiToolDispatchArgRepair
