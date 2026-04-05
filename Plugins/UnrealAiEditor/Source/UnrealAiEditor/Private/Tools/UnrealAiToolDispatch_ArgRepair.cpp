#include "Tools/UnrealAiToolDispatch_ArgRepair.h"

#include "Tools/UnrealAiBlueprintFunctionResolve.h"
#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Character.h"

namespace UnrealAiToolDispatchArgRepair
{
	void SanitizeUnrealPathString(FString& S)
	{
		S.TrimStartAndEndInline();
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

	namespace UnrealAiGraphPatchRepair
	{
		static bool IsHexDigitChar(TCHAR C)
		{
			return (C >= TEXT('0') && C <= TEXT('9')) || (C >= TEXT('A') && C <= TEXT('F')) || (C >= TEXT('a') && C <= TEXT('f'));
		}

		static bool LooksLikeBareUuid(const FString& S)
		{
			if (S.Len() != 36)
			{
				return false;
			}
			for (int32 I = 0; I < 36; ++I)
			{
				const TCHAR C = S[I];
				if (I == 8 || I == 13 || I == 18 || I == 23)
				{
					if (C != TEXT('-'))
					{
						return false;
					}
				}
				else if (!IsHexDigitChar(C))
				{
					return false;
				}
			}
			return true;
		}

		static void AppendGraphPatchRepair(
			const TSharedPtr<FJsonObject>& Audit,
			int32 OpIndex,
			const TCHAR* Field,
			const FString& From,
			const FString& To)
		{
			if (!Audit.IsValid())
			{
				return;
			}
			const TCHAR* Key = TEXT("graph_patch_repairs");
			TArray<TSharedPtr<FJsonValue>> Repairs;
			const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
			if (Audit->TryGetArrayField(Key, Existing) && Existing)
			{
				Repairs = *Existing;
			}
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetNumberField(TEXT("op_index"), OpIndex);
			R->SetStringField(TEXT("field"), Field);
			if (!From.IsEmpty())
			{
				R->SetStringField(TEXT("from"), From);
			}
			if (!To.IsEmpty())
			{
				R->SetStringField(TEXT("to"), To);
			}
			Repairs.Add(MakeShareable(new FJsonValueObject(R.ToSharedRef())));
			Audit->SetArrayField(Key, Repairs);
		}

		static void MaybePrefixGuidOnNodePinRef(FString& Ref, const TSharedPtr<FJsonObject>& Audit, int32 OpIdx, const TCHAR* FieldLabel)
		{
			Ref.TrimStartAndEndInline();
			SanitizeUnrealPathString(Ref);
			if (Ref.StartsWith(TEXT("guid:"), ESearchCase::IgnoreCase))
			{
				return;
			}
			const int32 Dot = Ref.Find(TEXT("."));
			if (Dot <= 0 || Dot >= Ref.Len() - 1)
			{
				return;
			}
			const FString Head = Ref.Left(Dot);
			if (!LooksLikeBareUuid(Head))
			{
				return;
			}
			const FString Old = Ref;
			Ref = FString(TEXT("guid:")) + Ref;
			AppendGraphPatchRepair(Audit, OpIdx, FieldLabel, Old, Ref);
		}

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
			static const TCHAR* CallFunction = TEXT("/Script/BlueprintGraph.K2Node_CallFunction");
			if (Low == TEXT("/script/blueprintgraph.k2node_intless") || Low == TEXT("/script/blueprintgraph.k2node_intadd")
				|| Low == TEXT("/script/blueprintgraph.k2node_lessint") || Low == TEXT("/script/blueprintgraph.k2node_addint")
				|| Low == TEXT("/script/blueprintgraph.k2node_math"))
			{
				S = CallFunction;
			}
		}

		static void NormalizeK2ClassPathForGraphPatch(FString& S)
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

		static bool BlueprintParentIsCharacter(const FString& BlueprintObjectPath)
		{
			FString Path = BlueprintObjectPath;
			NormalizeAssetLikeObjectPath(Path);
			if (Path.IsEmpty())
			{
				return false;
			}
			UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Path);
			return BP && BP->ParentClass && BP->ParentClass->IsChildOf(ACharacter::StaticClass());
		}

		static void RepairCallFunctionClassPathForCharacterApi(
			FString& ClassPath,
			FString& FunctionName,
			const TSharedPtr<FJsonObject>& RootArgs,
			const TSharedPtr<FJsonObject>& Audit,
			int32 OpIdx,
			const TCHAR* AuditField)
		{
			FunctionName.TrimStartAndEndInline();
			ClassPath.TrimStartAndEndInline();
			UnrealAiBlueprintFunctionResolve::SplitCombinedClassPathAndFunctionName(ClassPath, FunctionName);
			if (FunctionName.IsEmpty() || ClassPath.IsEmpty())
			{
				return;
			}
			const FString FnLow = FunctionName.ToLower();
			if (FnLow != TEXT("jump") && FnLow != TEXT("landed"))
			{
				return;
			}
			const FString ClLow = ClassPath.ToLower();
			if (!ClLow.Contains(TEXT("charactermovement")) && !ClLow.Contains(TEXT("movementcomponent")))
			{
				return;
			}
			FString BpPath;
			if (!RootArgs.IsValid() || !RootArgs->TryGetStringField(TEXT("blueprint_path"), BpPath) || !BlueprintParentIsCharacter(BpPath))
			{
				return;
			}
			const FString NewCls = TEXT("/Script/Engine.Character");
			if (!ClassPath.Equals(NewCls, ESearchCase::IgnoreCase))
			{
				AppendGraphPatchRepair(Audit, OpIdx, AuditField, ClassPath, NewCls);
				ClassPath = NewCls;
			}
		}

		static void RepairEventOverrideObject(
			const TSharedPtr<FJsonObject>& Ev,
			const TSharedPtr<FJsonObject>& Audit,
			int32 OpIdx)
		{
			if (!Ev.IsValid())
			{
				return;
			}
			FString Fn, Outer;
			Ev->TryGetStringField(TEXT("function_name"), Fn);
			Ev->TryGetStringField(TEXT("outer_class_path"), Outer);
			Fn.TrimStartAndEndInline();
			Outer.TrimStartAndEndInline();
			UClass* Ch = ACharacter::StaticClass();
			const FString Lf = Fn.ToLower();
			auto TryCanon = [&](const TCHAR* Canon) -> bool
			{
				if (!Ch->FindFunctionByName(FName(Canon)))
				{
					return false;
				}
				if (!Fn.Equals(Canon, ESearchCase::CaseSensitive))
				{
					const FString Old = Fn;
					Fn = Canon;
					Ev->SetStringField(TEXT("function_name"), Fn);
					AppendGraphPatchRepair(Audit, OpIdx, TEXT("event_override.function_name"), Old, Fn);
				}
				return true;
			};
			if (Lf == TEXT("onjumped") || Lf == TEXT("jumpevent") || Lf == TEXT("onjump"))
			{
				TryCanon(TEXT("Jump"));
			}
			else if (Lf == TEXT("onlanded") || Lf == TEXT("beginland") || Lf == TEXT("landedevent"))
			{
				TryCanon(TEXT("Landed"));
			}
			if (Outer.StartsWith(TEXT("/Game/")) && !Fn.IsEmpty() && Ch->FindFunctionByName(FName(*Fn)))
			{
				const FString NewOuter = TEXT("/Script/Engine.Character");
				if (!Outer.Equals(NewOuter, ESearchCase::IgnoreCase))
				{
					AppendGraphPatchRepair(Audit, OpIdx, TEXT("event_override.outer_class_path"), Outer, NewOuter);
					Outer = NewOuter;
					Ev->SetStringField(TEXT("outer_class_path"), Outer);
				}
			}
		}

		static void HoistAndRepairEventOverrideOnCreateNode(
			const TSharedPtr<FJsonObject>& Op,
			const TSharedPtr<FJsonObject>& RootArgs,
			const TSharedPtr<FJsonObject>& Audit,
			int32 OpIdx)
		{
			FString ClassPathProbe;
			Op->TryGetStringField(TEXT("class_path"), ClassPathProbe);
			ClassPathProbe.TrimStartAndEndInline();
			if (!ClassPathProbe.IsEmpty() && !Op->HasField(TEXT("outer_class_path")))
			{
				const TSharedPtr<FJsonObject>* EvProbe = nullptr;
				if (!Op->TryGetObjectField(TEXT("event_override"), EvProbe) || !EvProbe || !(*EvProbe).IsValid())
				{
					// Typical CallFunction shape: class_path + function_name without outer_class_path.
					return;
				}
			}
			const TSharedPtr<FJsonObject>* CfNest = nullptr;
			const bool bHasCallFunctionNest =
				Op->TryGetObjectField(TEXT("call_function"), CfNest) && CfNest && (*CfNest).IsValid();
			FString Sem, K2;
			Op->TryGetStringField(TEXT("semantic_kind"), Sem);
			Op->TryGetStringField(TEXT("k2_class"), K2);
			Sem.TrimStartAndEndInline();
			K2.TrimStartAndEndInline();
			NormalizeK2ClassPathForGraphPatch(K2);
			const FString SemLow = Sem.ToLower();
			const bool bCallish = SemLow == TEXT("call_library_function") || K2.Contains(TEXT("K2Node_CallFunction"));
			const bool bCustom = K2.Contains(TEXT("K2Node_CustomEvent")) || SemLow == TEXT("custom_event");
			if (bCallish || bCustom || bHasCallFunctionNest)
			{
				return;
			}
			const bool bEventish = SemLow == TEXT("event_override") || SemLow == TEXT("event") || K2.Contains(TEXT("K2Node_Event"))
				|| (K2.IsEmpty() && SemLow.IsEmpty() && Op->HasField(TEXT("outer_class_path")));
			if (!bEventish)
			{
				return;
			}
			const TSharedPtr<FJsonObject>* EvExisting = nullptr;
			if (Op->TryGetObjectField(TEXT("event_override"), EvExisting) && EvExisting && (*EvExisting).IsValid())
			{
				RepairEventOverrideObject(*EvExisting, Audit, OpIdx);
				return;
			}
			FString RootFn, RootOuter;
			Op->TryGetStringField(TEXT("function_name"), RootFn);
			Op->TryGetStringField(TEXT("outer_class_path"), RootOuter);
			RootFn.TrimStartAndEndInline();
			RootOuter.TrimStartAndEndInline();
			if (RootFn.IsEmpty() && RootOuter.IsEmpty())
			{
				return;
			}
			TSharedPtr<FJsonObject> Ev = MakeShared<FJsonObject>();
			if (!RootFn.IsEmpty())
			{
				Ev->SetStringField(TEXT("function_name"), RootFn);
			}
			if (!RootOuter.IsEmpty())
			{
				Ev->SetStringField(TEXT("outer_class_path"), RootOuter);
			}
			Op->SetObjectField(TEXT("event_override"), Ev);
			Op->RemoveField(TEXT("function_name"));
			Op->RemoveField(TEXT("outer_class_path"));
			AppendGraphPatchRepair(Audit, OpIdx, TEXT("event_override"), TEXT("<hoisted from op root>"), TEXT("nested object"));
			RepairEventOverrideObject(Ev, Audit, OpIdx);
		}

		static void RepairCreateNodeCallFunctionPaths(
			const TSharedPtr<FJsonObject>& Op,
			const TSharedPtr<FJsonObject>& RootArgs,
			const TSharedPtr<FJsonObject>& Audit,
			int32 OpIdx)
		{
			FString K2;
			Op->TryGetStringField(TEXT("k2_class"), K2);
			NormalizeK2ClassPathForGraphPatch(K2);
			FString Sem;
			Op->TryGetStringField(TEXT("semantic_kind"), Sem);
			Sem.TrimStartAndEndInline();
			const FString SemLow = Sem.ToLower();
			const bool bCallish = SemLow == TEXT("call_library_function") || K2.Contains(TEXT("K2Node_CallFunction"));
			if (!bCallish)
			{
				return;
			}
			FString ClsPath, FnName;
			Op->TryGetStringField(TEXT("class_path"), ClsPath);
			Op->TryGetStringField(TEXT("function_name"), FnName);
			UnrealAiBlueprintFunctionResolve::SplitCombinedClassPathAndFunctionName(ClsPath, FnName);
			if (!ClsPath.IsEmpty())
			{
				Op->SetStringField(TEXT("class_path"), ClsPath);
			}
			if (!FnName.IsEmpty())
			{
				Op->SetStringField(TEXT("function_name"), FnName);
			}
			RepairCallFunctionClassPathForCharacterApi(ClsPath, FnName, RootArgs, Audit, OpIdx, TEXT("create_node.class_path"));
			Op->SetStringField(TEXT("class_path"), ClsPath);
			Op->SetStringField(TEXT("function_name"), FnName);

			const TSharedPtr<FJsonObject>* CfObj = nullptr;
			if (Op->TryGetObjectField(TEXT("call_function"), CfObj) && CfObj && (*CfObj).IsValid())
			{
				FString C2, F2;
				(*CfObj)->TryGetStringField(TEXT("class_path"), C2);
				(*CfObj)->TryGetStringField(TEXT("function_name"), F2);
				RepairCallFunctionClassPathForCharacterApi(C2, F2, RootArgs, Audit, OpIdx, TEXT("call_function.class_path"));
				(*CfObj)->SetStringField(TEXT("class_path"), C2);
				(*CfObj)->SetStringField(TEXT("function_name"), F2);
			}
		}

		static void NormalizeAddVariableTypeString(FString& T, const TSharedPtr<FJsonObject>& Audit, int32 OpIdx)
		{
			if (T.IsEmpty())
			{
				return;
			}
			const FString Low = T.ToLower();
			const FString Old = T;
			if (Low == TEXT("integer") || Low == TEXT("int32") || Low == TEXT("int64"))
			{
				T = TEXT("int");
			}
			else if (Low == TEXT("boolean") || Low == TEXT("bool"))
			{
				T = TEXT("bool");
			}
			else if (Low == TEXT("double") || Low == TEXT("number"))
			{
				T = TEXT("float");
			}
			else if (Low == TEXT("string"))
			{
				T = TEXT("text");
			}
			if (!Old.Equals(T, ESearchCase::CaseSensitive))
			{
				AppendGraphPatchRepair(Audit, OpIdx, TEXT("add_variable.type"), Old, T);
			}
		}

		static void RepairGraphPatchConnectLikeOp(
			const TSharedPtr<FJsonObject>& OpMut,
			const TSharedPtr<FJsonObject>& Audit,
			int32 OpIdx)
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
			MaybePrefixGuidOnNodePinRef(From, Audit, OpIdx, TEXT("from"));
			MaybePrefixGuidOnNodePinRef(To, Audit, OpIdx, TEXT("to"));
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

		static void RepairSetPinDefaultValueAliases(
			const TSharedPtr<FJsonObject>& Op,
			const TSharedPtr<FJsonObject>& Audit,
			int32 OpIdx)
		{
			if (!Op.IsValid())
			{
				return;
			}
			FString Val;
			Op->TryGetStringField(TEXT("value"), Val);
			Val.TrimStartAndEndInline();
			if (!Val.IsEmpty())
			{
				return;
			}
			FString DvStr;
			if (Op->TryGetStringField(TEXT("default_value"), DvStr))
			{
				DvStr.TrimStartAndEndInline();
				if (!DvStr.IsEmpty())
				{
					Op->SetStringField(TEXT("value"), DvStr);
					Op->RemoveField(TEXT("default_value"));
					AppendGraphPatchRepair(Audit, OpIdx, TEXT("value"), TEXT("<default_value string>"), DvStr);
				}
				return;
			}
			const TSharedPtr<FJsonValue> Dv = Op->TryGetField(TEXT("default_value"));
			if (!Dv.IsValid())
			{
				return;
			}
			FString Coerced;
			if (Dv->Type == EJson::Boolean)
			{
				Coerced = Dv->AsBool() ? TEXT("true") : TEXT("false");
			}
			else if (Dv->Type == EJson::Number)
			{
				const double N = Dv->AsNumber();
				Coerced = (FMath::TruncToDouble(N) == N) ? FString::FromInt((int32)N) : FString::SanitizeFloat(N);
			}
			else if (Dv->Type == EJson::String)
			{
				Coerced = Dv->AsString();
				Coerced.TrimStartAndEndInline();
			}
			if (!Coerced.IsEmpty())
			{
				Op->SetStringField(TEXT("value"), Coerced);
				Op->RemoveField(TEXT("default_value"));
				AppendGraphPatchRepair(Audit, OpIdx, TEXT("value"), TEXT("<default_value coerced>"), Coerced);
			}
		}

		void RepairBlueprintGraphPatchToolArgsImpl(
			const TSharedPtr<FJsonObject>& Args,
			const TSharedPtr<FJsonObject>& Audit)
		{
			if (!Args.IsValid())
			{
				return;
			}
			RepairBlueprintAssetPathArgs(Args);
			const TArray<TSharedPtr<FJsonValue>>* Ops = nullptr;
			if (!Args->TryGetArrayField(TEXT("ops"), Ops) || !Ops)
			{
				return;
			}
			for (int32 OpIdx = 0; OpIdx < Ops->Num(); ++OpIdx)
			{
				const TSharedPtr<FJsonValue>& V = (*Ops)[OpIdx];
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
					RepairGraphPatchConnectLikeOp(Op, Audit, OpIdx);
				}
				else if (OpName.Equals(TEXT("splice_on_link"), ESearchCase::IgnoreCase))
				{
					RepairGraphPatchConnectLikeOp(Op, Audit, OpIdx);
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
					FString TypNorm;
					if (Op->TryGetStringField(TEXT("type"), TypNorm) && !TypNorm.IsEmpty())
					{
						NormalizeAddVariableTypeString(TypNorm, Audit, OpIdx);
						Op->SetStringField(TEXT("type"), TypNorm);
					}
				}
				else if (OpName.Equals(TEXT("set_pin_default"), ESearchCase::IgnoreCase))
				{
					RepairSetPinDefaultValueAliases(Op, Audit, OpIdx);
				}
				else if (OpName.Equals(TEXT("create_node"), ESearchCase::IgnoreCase))
				{
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
					if (Op->TryGetStringField(TEXT("k2_class"), K2) && !K2.IsEmpty())
					{
						NormalizeK2ClassPathForGraphPatch(K2);
						Op->SetStringField(TEXT("k2_class"), K2);
					}
					Op->RemoveField(TEXT("class_name"));
					Op->RemoveField(TEXT("node_class"));
					Op->RemoveField(TEXT("kind"));
					HoistAndRepairEventOverrideOnCreateNode(Op, Args, Audit, OpIdx);
					RepairCreateNodeCallFunctionPaths(Op, Args, Audit, OpIdx);
				}
			}
		}
	} // namespace UnrealAiGraphPatchRepair

	void RepairBlueprintGraphPatchToolArgs(
		const TSharedPtr<FJsonObject>& Args,
		const TSharedPtr<FJsonObject>& Audit)
	{
		UnrealAiGraphPatchRepair::RepairBlueprintGraphPatchToolArgsImpl(Args, Audit);
	}
} // namespace UnrealAiToolDispatchArgRepair
