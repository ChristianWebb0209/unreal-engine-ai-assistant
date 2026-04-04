#include "Tools/UnrealAiToolDispatch_ArgRepair.h"

#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"

namespace UnrealAiToolDispatchArgRepair
{
	namespace
	{
		/**
		 * Map common hallucinated UK2Node paths to real engine classes (small table; extend from telemetry).
		 * Invoked after path normalization so both bare K2Node_* and /Script/... forms are covered.
		 */
		static void ApplyK2ClassAliasesForGraphPatch(FString& S)
		{
			if (S.IsEmpty())
			{
				return;
			}
			const FString Low = S.ToLower();
			static const TCHAR* ExecutionSequence = TEXT("/Script/BlueprintGraph.K2Node_ExecutionSequence");
			if (Low == TEXT("/script/blueprintgraph.k2node_sequence")
				|| Low == TEXT("/script/blueprintgraph.uk2node_sequence"))
			{
				S = ExecutionSequence;
				return;
			}
			// Wrong "math node" type names: still not valid UK2Node classes; point to CallFunction so LoadObject can succeed.
			static const TCHAR* CallFunction = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
			if (Low == TEXT("/script/blueprintgraph.k2node_intless") || Low == TEXT("/script/blueprintgraph.k2node_intadd")
				|| Low == TEXT("/script/blueprintgraph.k2node_lessint") || Low == TEXT("/script/blueprintgraph.k2node_addint")
				|| Low == TEXT("/script/blueprintgraph.k2node_math") || Low.Contains(TEXT("k2node_intless"))
				|| Low.Contains(TEXT("k2node_intadd")))
			{
				S = CallFunction;
			}
		}

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

			// Bare tokens models use instead of K2Node_* class names.
			{
				FString Bare = S;
				Bare.TrimStartAndEndInline();
				const FString BareLow = Bare.ToLower();
				if (BareLow == TEXT("sequence") || BareLow == TEXT("executionsequence") || BareLow == TEXT("execsequence"))
				{
					S = TEXT("/Script/BlueprintGraph.K2Node_ExecutionSequence");
				}
			}

			ApplyK2ClassAliasesForGraphPatch(S);
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

	static void RepairGraphPatchConnectLikeOp(const TSharedPtr<FJsonObject>& OpMut)
	{
		if (!OpMut.IsValid())
		{
			return;
		}
		FJsonObject* Op = OpMut.Get();
		FString From, To;
		Op->TryGetStringField(TEXT("from"), From);
		Op->TryGetStringField(TEXT("to"), To);
		if (From.IsEmpty())
		{
			Op->TryGetStringField(TEXT("link_from"), From);
		}
		if (To.IsEmpty())
		{
			Op->TryGetStringField(TEXT("link_to"), To);
		}
		FString Fn, Fp, Tn, Tp;
		if (From.IsEmpty() && Op->TryGetStringField(TEXT("from_node"), Fn) && Op->TryGetStringField(TEXT("from_pin"), Fp)
			&& !Fn.IsEmpty() && !Fp.IsEmpty())
		{
			From = Fn + TEXT(".") + Fp;
		}
		if (To.IsEmpty() && Op->TryGetStringField(TEXT("to_node"), Tn) && Op->TryGetStringField(TEXT("to_pin"), Tp)
			&& !Tn.IsEmpty() && !Tp.IsEmpty())
		{
			To = Tn + TEXT(".") + Tp;
		}
		if (!From.IsEmpty())
		{
			Op->SetStringField(TEXT("from"), From);
		}
		if (!To.IsEmpty())
		{
			Op->SetStringField(TEXT("to"), To);
		}
		Op->RemoveField(TEXT("link_from"));
		Op->RemoveField(TEXT("link_to"));
		Op->RemoveField(TEXT("from_node"));
		Op->RemoveField(TEXT("from_pin"));
		Op->RemoveField(TEXT("to_node"));
		Op->RemoveField(TEXT("to_pin"));
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
			if (OpName.Equals(TEXT("connect"), ESearchCase::IgnoreCase)
				|| OpName.Equals(TEXT("break_link"), ESearchCase::IgnoreCase))
			{
				RepairGraphPatchConnectLikeOp(Op);
			}
			else if (OpName.Equals(TEXT("splice_on_link"), ESearchCase::IgnoreCase))
			{
				RepairGraphPatchConnectLikeOp(Op);
			}
			else if (OpName.Equals(TEXT("add_variable"), ESearchCase::IgnoreCase))
			{
				FString Nm;
				Op->TryGetStringField(TEXT("name"), Nm);
				Nm.TrimStartAndEndInline();
				if (Nm.IsEmpty())
				{
					if (Op->TryGetStringField(TEXT("variable_name"), Nm) && !Nm.IsEmpty())
					{
						Op->SetStringField(TEXT("name"), Nm);
					}
				}
				TArray<const TCHAR*> TypeAliases;
				TypeAliases.Add(TEXT("variable_type"));
				TypeAliases.Add(TEXT("typ"));
				TypeAliases.Add(TEXT("pin_type"));
				FString Typ;
				(void)TryGetStringFieldCanonical(Op, TEXT("type"), TypeAliases, Typ);
				Op->RemoveField(TEXT("typ"));
				Op->RemoveField(TEXT("pin_type"));
			}
			else if (OpName.Equals(TEXT("create_node"), ESearchCase::IgnoreCase))
			{
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
				if (K2.IsEmpty())
				{
					FString TmpNc;
					if (Op->TryGetStringField(TEXT("node_class"), TmpNc) && !TmpNc.IsEmpty())
					{
						Op->SetStringField(TEXT("k2_class"), TmpNc);
						Op->RemoveField(TEXT("node_class"));
					}
				}
				Op->TryGetStringField(TEXT("k2_class"), K2);
				if (K2.IsEmpty())
				{
					FString KindStr;
					if (Op->TryGetStringField(TEXT("kind"), KindStr) && !KindStr.IsEmpty())
					{
						Op->SetStringField(TEXT("k2_class"), KindStr);
						Op->RemoveField(TEXT("kind"));
					}
				}
				if (Op->TryGetStringField(TEXT("k2_class"), K2) && !K2.IsEmpty())
				{
					NormalizeK2ClassPathForGraphPatch(K2);
					Op->SetStringField(TEXT("k2_class"), K2);
				}
				Op->RemoveField(TEXT("class_name"));
				Op->RemoveField(TEXT("node_class"));
				Op->RemoveField(TEXT("kind"));
			}
		}
	}
} // namespace UnrealAiToolDispatchArgRepair
