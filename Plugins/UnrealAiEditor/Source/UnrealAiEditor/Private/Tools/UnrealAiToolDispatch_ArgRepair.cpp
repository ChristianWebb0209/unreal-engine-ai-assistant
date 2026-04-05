#include "Tools/UnrealAiToolDispatch_ArgRepair.h"

#include "Tools/UnrealAiBlueprintFunctionResolve.h"
#include "Tools/UnrealAiBlueprintGraphNodeGuid.h"
#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "Misc/Guid.h"
#include "UObject/UObjectGlobals.h"
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

		struct FPendingOpsInsert
		{
			int32 AfterOpIndex = INDEX_NONE;
			TSharedPtr<FJsonObject> OpToInsert;
		};

		static void ApplyPendingOpsInsertsDescending(const TSharedPtr<FJsonObject>& Args, TArray<FPendingOpsInsert>& Pending)
		{
			if (!Args.IsValid() || Pending.Num() == 0)
			{
				return;
			}
			const TArray<TSharedPtr<FJsonValue>>* SrcOps = nullptr;
			if (!Args->TryGetArrayField(TEXT("ops"), SrcOps) || !SrcOps)
			{
				return;
			}
			TArray<TSharedPtr<FJsonValue>> OpsCopy = *SrcOps;
			Pending.Sort([](const FPendingOpsInsert& A, const FPendingOpsInsert& B) { return A.AfterOpIndex > B.AfterOpIndex; });
			for (const FPendingOpsInsert& P : Pending)
			{
				if (!P.OpToInsert.IsValid() || P.AfterOpIndex < 0 || P.AfterOpIndex >= OpsCopy.Num())
				{
					continue;
				}
				OpsCopy.Insert(MakeShared<FJsonValueObject>(P.OpToInsert.ToSharedRef()), P.AfterOpIndex + 1);
			}
			Args->SetArrayField(TEXT("ops"), OpsCopy);
			Pending.Reset();
		}

		/** Models invent semantic_kind literal_int etc.; map to KismetSystemLibrary MakeLiteral* + optional set_pin_default. */
		static void TryRepairLiteralSemanticKindCreateNode(
			const TSharedPtr<FJsonObject>& Op,
			const TSharedPtr<FJsonObject>& Audit,
			int32 OpIdx,
			TArray<FPendingOpsInsert>& OutPending)
		{
			if (!Op.IsValid())
			{
				return;
			}
			FString Sem;
			if (!Op->TryGetStringField(TEXT("semantic_kind"), Sem))
			{
				return;
			}
			Sem.TrimStartAndEndInline();
			const FString L = Sem.ToLower();
			const TCHAR* FuncName = nullptr;
			if (L == TEXT("literal_int") || L == TEXT("int_literal") || L == TEXT("integer_literal") || L == TEXT("lit_int"))
			{
				FuncName = TEXT("MakeLiteralInt");
			}
			else if (L == TEXT("literal_float") || L == TEXT("float_literal"))
			{
				FuncName = TEXT("MakeLiteralFloat");
			}
			else if (L == TEXT("literal_bool") || L == TEXT("bool_literal"))
			{
				FuncName = TEXT("MakeLiteralBool");
			}
			else
			{
				return;
			}
			const FString FuncStr(FuncName);
			FString PatchId;
			if (!Op->TryGetStringField(TEXT("patch_id"), PatchId) || PatchId.IsEmpty())
			{
				return;
			}
			const FString OldSem = Sem;
			Op->RemoveField(TEXT("k2_class"));
			Op->SetStringField(TEXT("semantic_kind"), TEXT("call_library_function"));
			Op->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.KismetSystemLibrary"));
			Op->SetStringField(TEXT("function_name"), FuncStr);
			AppendGraphPatchRepair(Audit, OpIdx, TEXT("semantic_kind"), OldSem, TEXT("call_library_function+KismetSystemLibrary literal"));

			FString ValStr;
			bool bEmitDefault = false;
			if (FuncStr == TEXT("MakeLiteralBool"))
			{
				bool bBool = false;
				if (Op->TryGetBoolField(TEXT("value"), bBool))
				{
					ValStr = bBool ? TEXT("true") : TEXT("false");
					bEmitDefault = true;
					Op->RemoveField(TEXT("value"));
				}
			}
			if (!bEmitDefault)
			{
				const TSharedPtr<FJsonValue> V = Op->TryGetField(TEXT("value"));
				if (V.IsValid())
				{
					if (V->Type == EJson::Number)
					{
						const double N = V->AsNumber();
						if (FuncStr == TEXT("MakeLiteralInt"))
						{
							ValStr = FString::FromInt((int32)N);
						}
						else if (FuncStr == TEXT("MakeLiteralFloat"))
						{
							ValStr = FString::SanitizeFloat(N);
						}
						else
						{
							ValStr = (N != 0.0) ? TEXT("true") : TEXT("false");
						}
						bEmitDefault = true;
					}
					else if (V->Type == EJson::Boolean && FuncStr == TEXT("MakeLiteralBool"))
					{
						ValStr = V->AsBool() ? TEXT("true") : TEXT("false");
						bEmitDefault = true;
					}
					else if (V->Type == EJson::String)
					{
						ValStr = V->AsString();
						ValStr.TrimStartAndEndInline();
						bEmitDefault = !ValStr.IsEmpty();
					}
					if (bEmitDefault)
					{
						Op->RemoveField(TEXT("value"));
					}
				}
			}
			if (bEmitDefault && !ValStr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Sp = MakeShared<FJsonObject>();
				Sp->SetStringField(TEXT("op"), TEXT("set_pin_default"));
				Sp->SetStringField(TEXT("ref"), PatchId + TEXT(".Value"));
				Sp->SetStringField(TEXT("value"), ValStr);
				OutPending.Add(FPendingOpsInsert{OpIdx, Sp});
			}
		}

		/** Older prompts used KismetMathLibrary for MakeLiteral*; those UFunctions live on UKismetSystemLibrary. */
		static void TryRepairMakeLiteralKismetClassPath(
			const TSharedPtr<FJsonObject>& Op,
			const TSharedPtr<FJsonObject>& Audit,
			int32 OpIdx)
		{
			if (!Op.IsValid())
			{
				return;
			}
			FString Fn;
			if (!Op->TryGetStringField(TEXT("function_name"), Fn) || Fn.IsEmpty())
			{
				return;
			}
			if (!Fn.StartsWith(TEXT("MakeLiteral"), ESearchCase::IgnoreCase))
			{
				return;
			}
			FString Cp;
			if (!Op->TryGetStringField(TEXT("class_path"), Cp) || Cp.IsEmpty())
			{
				return;
			}
			if (!Cp.Contains(TEXT("KismetMathLibrary"), ESearchCase::IgnoreCase))
			{
				return;
			}
			const FString Old = Cp;
			Op->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.KismetSystemLibrary"));
			AppendGraphPatchRepair(
				Audit,
				OpIdx,
				TEXT("class_path"),
				Old,
				TEXT("/Script/Engine.KismetSystemLibrary (MakeLiteral* is on KismetSystemLibrary)"));
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
			const FString Tail = Ref.Mid(Dot);
			FGuid G;
			FString Canon;
			if (!UnrealAiTryParseBlueprintGraphNodeGuid(Head, G, &Canon))
			{
				return;
			}
			const FString Old = Ref;
			Ref = FString(TEXT("guid:")) + Canon + Tail;
			AppendGraphPatchRepair(Audit, OpIdx, FieldLabel, Old, Ref);
		}

		static void RepairConnectExecNodeRefs(const TSharedPtr<FJsonObject>& OpMut, const TSharedPtr<FJsonObject>& Audit, int32 OpIdx)
		{
			if (!OpMut.IsValid())
			{
				return;
			}
			FJsonObject* Op = OpMut.Get();
			auto NormalizeNodeRef = [&](FString& S, const TCHAR* FieldLabel)
			{
				S.TrimStartAndEndInline();
				SanitizeUnrealPathString(S);
				if (S.StartsWith(TEXT("guid:"), ESearchCase::IgnoreCase))
				{
					return;
				}
				FGuid G;
				FString Canon;
				if (UnrealAiTryParseBlueprintGraphNodeGuid(S, G, &Canon))
				{
					const FString Old = S;
					S = FString(TEXT("guid:")) + Canon;
					AppendGraphPatchRepair(Audit, OpIdx, FieldLabel, Old, S);
				}
			};
			FString FromN, ToN;
			Op->TryGetStringField(TEXT("from_node"), FromN);
			Op->TryGetStringField(TEXT("to_node"), ToN);
			if (FromN.IsEmpty())
			{
				Op->TryGetStringField(TEXT("from"), FromN);
			}
			if (ToN.IsEmpty())
			{
				Op->TryGetStringField(TEXT("to"), ToN);
			}
			NormalizeNodeRef(FromN, TEXT("from_node"));
			NormalizeNodeRef(ToN, TEXT("to_node"));
			if (!FromN.IsEmpty())
			{
				Op->SetStringField(TEXT("from_node"), FromN);
			}
			if (!ToN.IsEmpty())
			{
				Op->SetStringField(TEXT("to_node"), ToN);
			}
			Op->RemoveField(TEXT("from"));
			Op->RemoveField(TEXT("to"));
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

		static void RepairSemanticKindCallFunctionTypo(const TSharedPtr<FJsonObject>& Op, const TSharedPtr<FJsonObject>& Audit, int32 OpIdx)
		{
			FString Sem;
			if (!Op->TryGetStringField(TEXT("semantic_kind"), Sem))
			{
				return;
			}
			Sem.TrimStartAndEndInline();
			if (!Sem.Equals(TEXT("call_function"), ESearchCase::IgnoreCase))
			{
				return;
			}
			Op->SetStringField(TEXT("semantic_kind"), TEXT("call_library_function"));
			AppendGraphPatchRepair(Audit, OpIdx, TEXT("semantic_kind"), TEXT("call_function"), TEXT("call_library_function"));
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

		static void MaybeFillMissingCharacterJumpClassPath(
			const TSharedPtr<FJsonObject>& Op,
			const TSharedPtr<FJsonObject>& RootArgs,
			const TSharedPtr<FJsonObject>& Audit,
			int32 OpIdx)
		{
			FString Fn;
			Op->TryGetStringField(TEXT("function_name"), Fn);
			Fn.TrimStartAndEndInline();
			if (!Fn.Equals(TEXT("Jump"), ESearchCase::IgnoreCase))
			{
				return;
			}
			FString Cp;
			Op->TryGetStringField(TEXT("class_path"), Cp);
			Cp.TrimStartAndEndInline();
			if (!Cp.IsEmpty())
			{
				return;
			}
			FString Sem, K2;
			Op->TryGetStringField(TEXT("semantic_kind"), Sem);
			Op->TryGetStringField(TEXT("k2_class"), K2);
			Sem.TrimStartAndEndInline();
			K2.TrimStartAndEndInline();
			const FString SemL = Sem.ToLower();
			const bool bCallish = SemL == TEXT("call_library_function") || SemL == TEXT("call_function") || K2.Contains(TEXT("K2Node_CallFunction"));
			if (!bCallish)
			{
				return;
			}
			FString BpPath;
			if (!RootArgs.IsValid() || !RootArgs->TryGetStringField(TEXT("blueprint_path"), BpPath) || BpPath.IsEmpty())
			{
				return;
			}
			if (!BlueprintParentIsCharacter(BpPath))
			{
				return;
			}
			Op->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.Character"));
			AppendGraphPatchRepair(Audit, OpIdx, TEXT("class_path"), TEXT("<empty Jump>"), TEXT("/Script/Engine.Character"));
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
			if (!Outer.IsEmpty())
			{
				UClass* OC = LoadObject<UClass>(nullptr, *Outer);
				if (!OC)
				{
					AppendGraphPatchRepair(Audit, OpIdx, TEXT("event_override.outer_class_path"), Outer, TEXT("<cleared: invalid class path>"));
					Outer.Reset();
					Ev->RemoveField(TEXT("outer_class_path"));
				}
			}
			if (Outer.IsEmpty() && !Fn.IsEmpty())
			{
				FString DefOuter;
				if (UnrealAiBlueprintFunctionResolve::TryDefaultOuterClassPathForK2Event(Fn, DefOuter) && !DefOuter.IsEmpty())
				{
					AppendGraphPatchRepair(Audit, OpIdx, TEXT("event_override.outer_class_path"), TEXT("<empty>"), DefOuter);
					Outer = DefOuter;
					Ev->SetStringField(TEXT("outer_class_path"), Outer);
				}
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

		static bool SemanticKindBlocksGameplayClassK2Coercion(const FString& SemLower)
		{
			return SemLower == TEXT("event_override") || SemLower == TEXT("event") || SemLower == TEXT("custom_event")
				|| SemLower == TEXT("branch") || SemLower == TEXT("execution_sequence") || SemLower == TEXT("variable_get")
				|| SemLower == TEXT("variable_set") || SemLower == TEXT("dynamic_cast");
		}

		static bool FunctionNameLooksLikeCharacterEventGraphEntry(const FString& FnLower)
		{
			return FnLower == TEXT("landed") || FnLower == TEXT("receivebeginplay") || FnLower == TEXT("tick")
				|| FnLower == TEXT("endplay") || FnLower == TEXT("receiveendplay") || FnLower == TEXT("receivepossessed")
				|| FnLower == TEXT("receiveunpossessed") || FnLower == TEXT("restarted");
		}

		/** k2_class sometimes holds a gameplay UClass (e.g. /Script/Engine.Character); k2_class must be a UK2Node subclass. */
		static void MaybePromoteNonUk2NodeK2ClassToCallFunction(
			const TSharedPtr<FJsonObject>& Op,
			const TSharedPtr<FJsonObject>& RootArgs,
			const TSharedPtr<FJsonObject>& Audit,
			int32 OpIdx)
		{
			const TSharedPtr<FJsonObject>* EvObj = nullptr;
			if (Op->TryGetObjectField(TEXT("event_override"), EvObj) && EvObj && (*EvObj).IsValid())
			{
				return;
			}
			FString Sem;
			Op->TryGetStringField(TEXT("semantic_kind"), Sem);
			Sem.TrimStartAndEndInline();
			const FString SemLow = Sem.ToLower();
			if (SemanticKindBlocksGameplayClassK2Coercion(SemLow))
			{
				return;
			}
			FString K2;
			if (!Op->TryGetStringField(TEXT("k2_class"), K2) || K2.IsEmpty())
			{
				return;
			}
			FString K2Norm = K2;
			NormalizeK2ClassPathForGraphPatch(K2Norm);
			if (K2Norm.Contains(TEXT("K2Node_"), ESearchCase::IgnoreCase))
			{
				return;
			}
			UClass* Cls = LoadObject<UClass>(nullptr, *K2Norm);
			if (!Cls || Cls->IsChildOf(UK2Node::StaticClass()))
			{
				return;
			}
			FString Fn;
			Op->TryGetStringField(TEXT("function_name"), Fn);
			Fn.TrimStartAndEndInline();
			const TSharedPtr<FJsonObject>* CfObj = nullptr;
			const bool bHasCallNest = Op->TryGetObjectField(TEXT("call_function"), CfObj) && CfObj && (*CfObj).IsValid();
			if (Fn.IsEmpty() && bHasCallNest)
			{
				(*CfObj)->TryGetStringField(TEXT("function_name"), Fn);
				Fn.TrimStartAndEndInline();
			}
			const FString FnLow = Fn.ToLower();
			if (!FnLow.IsEmpty() && FunctionNameLooksLikeCharacterEventGraphEntry(FnLow) && !bHasCallNest
				&& (SemLow.IsEmpty() || SemLow == TEXT("call_library_function") || SemLow == TEXT("call_function")))
			{
				return;
			}
			FString ClassPathExisting;
			Op->TryGetStringField(TEXT("class_path"), ClassPathExisting);
			ClassPathExisting.TrimStartAndEndInline();
			if (ClassPathExisting.IsEmpty())
			{
				if (Fn.IsEmpty() || !Cls->FindFunctionByName(FName(*Fn)))
				{
					return;
				}
				Op->SetStringField(TEXT("class_path"), K2Norm);
			}
			const FString OldK2 = K2;
			Op->RemoveField(TEXT("k2_class"));
			if (SemLow == TEXT("call_function"))
			{
				Op->SetStringField(TEXT("semantic_kind"), TEXT("call_library_function"));
				AppendGraphPatchRepair(Audit, OpIdx, TEXT("semantic_kind"), TEXT("call_function"), TEXT("call_library_function"));
			}
			else if (SemLow.IsEmpty() || SemLow == TEXT("call_library_function"))
			{
				Op->SetStringField(TEXT("semantic_kind"), TEXT("call_library_function"));
				if (SemLow.IsEmpty())
				{
					AppendGraphPatchRepair(Audit, OpIdx, TEXT("semantic_kind"), TEXT("<empty>"), TEXT("call_library_function"));
				}
			}
			AppendGraphPatchRepair(Audit, OpIdx, TEXT("k2_class"), OldK2, TEXT("<promoted to class_path>"));
			RepairCreateNodeCallFunctionPaths(Op, RootArgs, Audit, OpIdx);
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

		/** Models sometimes emit create_node with semantic_kind set_pin_default; that is a separate op, not a node kind. */
		static void TryPromoteCreateNodeMisusedSetPinDefaultSemanticToOp(
			const TSharedPtr<FJsonObject>& Op,
			const TSharedPtr<FJsonObject>& Audit,
			int32 OpIdx,
			FString& InOutOpName)
		{
			if (!Op.IsValid() || !InOutOpName.Equals(TEXT("create_node"), ESearchCase::IgnoreCase))
			{
				return;
			}
			FString Sk;
			Op->TryGetStringField(TEXT("semantic_kind"), Sk);
			Sk.TrimStartAndEndInline();
			if (!Sk.Equals(TEXT("set_pin_default"), ESearchCase::IgnoreCase))
			{
				return;
			}
			Op->SetStringField(TEXT("op"), TEXT("set_pin_default"));
			Op->RemoveField(TEXT("semantic_kind"));
			Op->RemoveField(TEXT("k2_class"));
			InOutOpName = TEXT("set_pin_default");
			AppendGraphPatchRepair(
				Audit,
				OpIdx,
				TEXT("op"),
				TEXT("create_node"),
				TEXT("set_pin_default (semantic_kind set_pin_default is not a UK2Node kind)"));

			FString Ref;
			Op->TryGetStringField(TEXT("ref"), Ref);
			Ref.TrimStartAndEndInline();
			if (Ref.IsEmpty())
			{
				Op->TryGetStringField(TEXT("node_ref"), Ref);
				Ref.TrimStartAndEndInline();
			}
			if (Ref.IsEmpty())
			{
				FString PinName;
				Op->TryGetStringField(TEXT("pin"), PinName);
				if (PinName.IsEmpty())
				{
					Op->TryGetStringField(TEXT("pin_name"), PinName);
				}
				PinName.TrimStartAndEndInline();
				FString Pid;
				Op->TryGetStringField(TEXT("patch_id"), Pid);
				Pid.TrimStartAndEndInline();
				if (!Pid.IsEmpty())
				{
					if (PinName.IsEmpty())
					{
						PinName = TEXT("Value");
						AppendGraphPatchRepair(Audit, OpIdx, TEXT("pin"), TEXT("<inferred>"), PinName);
					}
					Ref = Pid + TEXT(".") + PinName;
				}
			}
			if (!Ref.IsEmpty())
			{
				Op->SetStringField(TEXT("ref"), Ref);
			}
			Op->RemoveField(TEXT("node_ref"));
			Op->RemoveField(TEXT("patch_id"));
			Op->RemoveField(TEXT("pin"));
			Op->RemoveField(TEXT("pin_name"));
			Op->RemoveField(TEXT("x"));
			Op->RemoveField(TEXT("y"));
		}

		void RepairBlueprintGraphPatchToolArgsImpl(
			const TSharedPtr<FJsonObject>& Args,
			const TSharedPtr<FJsonObject>& Audit,
			const bool bRecurseAfterLiteralInserts)
		{
			if (!Args.IsValid())
			{
				return;
			}
			TArray<FPendingOpsInsert> PendingLiteralInserts;
			RepairBlueprintAssetPathArgs(Args);

			{
				FString Ls;
				if (Args->TryGetStringField(TEXT("layout_scope"), Ls))
				{
					FString T = Ls;
					T.TrimStartAndEndInline();
					FString Canon;
					if (T.Equals(TEXT("patched_plus_downstream"), ESearchCase::IgnoreCase)
						|| T.Equals(TEXT("downstream"), ESearchCase::IgnoreCase)
						|| T.Equals(TEXT("patch_and_tail"), ESearchCase::IgnoreCase))
					{
						Canon = TEXT("patched_nodes_and_downstream_exec");
					}
					if (!Canon.IsEmpty())
					{
						Args->SetStringField(TEXT("layout_scope"), Canon);
						AppendGraphPatchRepair(Audit, -1, TEXT("layout_scope"), Ls, Canon);
					}
				}
			}
			{
				FString La;
				if (Args->TryGetStringField(TEXT("layout_anchor"), La))
				{
					FString T = La;
					T.TrimStartAndEndInline();
					if (T.Equals(TEXT("below"), ESearchCase::IgnoreCase))
					{
						Args->SetStringField(TEXT("layout_anchor"), TEXT("below_existing"));
						AppendGraphPatchRepair(Audit, -1, TEXT("layout_anchor"), La, TEXT("below_existing"));
					}
				}
			}
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
				TryPromoteCreateNodeMisusedSetPinDefaultSemanticToOp(Op, Audit, OpIdx, OpName);
				if (OpName.Equals(TEXT("call_function"), ESearchCase::IgnoreCase))
				{
					FString PatchId;
					if (!Op->TryGetStringField(TEXT("patch_id"), PatchId) || PatchId.IsEmpty())
					{
						PatchId = FString::Printf(TEXT("auto_call_%d"), OpIdx);
						Op->SetStringField(TEXT("patch_id"), PatchId);
						AppendGraphPatchRepair(Audit, OpIdx, TEXT("patch_id"), TEXT("<generated>"), PatchId);
					}
					Op->SetStringField(TEXT("op"), TEXT("create_node"));
					OpName = TEXT("create_node");
					FString Sk;
					Op->TryGetStringField(TEXT("semantic_kind"), Sk);
					Sk.TrimStartAndEndInline();
					if (Sk.IsEmpty())
					{
						Op->SetStringField(TEXT("semantic_kind"), TEXT("call_library_function"));
						AppendGraphPatchRepair(Audit, OpIdx, TEXT("op"), TEXT("call_function"), TEXT("create_node+call_library_function"));
					}
					else
					{
						AppendGraphPatchRepair(Audit, OpIdx, TEXT("op"), TEXT("call_function"), TEXT("create_node"));
					}
				}
				if (OpName.Equals(TEXT("connect"), ESearchCase::IgnoreCase)
					|| OpName.Equals(TEXT("break_link"), ESearchCase::IgnoreCase))
				{
					RepairGraphPatchConnectLikeOp(Op, Audit, OpIdx);
				}
				else if (OpName.Equals(TEXT("splice_on_link"), ESearchCase::IgnoreCase))
				{
					RepairGraphPatchConnectLikeOp(Op, Audit, OpIdx);
				}
				else if (OpName.Equals(TEXT("connect_exec"), ESearchCase::IgnoreCase))
				{
					RepairConnectExecNodeRefs(Op, Audit, OpIdx);
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
					TryRepairLiteralSemanticKindCreateNode(Op, Audit, OpIdx, PendingLiteralInserts);
					TryRepairMakeLiteralKismetClassPath(Op, Audit, OpIdx);
					RepairSemanticKindCallFunctionTypo(Op, Audit, OpIdx);
					HoistAndRepairEventOverrideOnCreateNode(Op, Args, Audit, OpIdx);
					MaybePromoteNonUk2NodeK2ClassToCallFunction(Op, Args, Audit, OpIdx);
					RepairCreateNodeCallFunctionPaths(Op, Args, Audit, OpIdx);
					MaybeFillMissingCharacterJumpClassPath(Op, Args, Audit, OpIdx);
				}
			}
			const int32 LiteralInsertCount = PendingLiteralInserts.Num();
			ApplyPendingOpsInsertsDescending(Args, PendingLiteralInserts);
			if (bRecurseAfterLiteralInserts && LiteralInsertCount > 0)
			{
				RepairBlueprintGraphPatchToolArgsImpl(Args, Audit, false);
			}
		}
	} // namespace UnrealAiGraphPatchRepair

	void RepairBlueprintGraphPatchToolArgs(
		const TSharedPtr<FJsonObject>& Args,
		const TSharedPtr<FJsonObject>& Audit)
	{
		UnrealAiGraphPatchRepair::RepairBlueprintGraphPatchToolArgsImpl(Args, Audit, true);
	}
} // namespace UnrealAiToolDispatchArgRepair
