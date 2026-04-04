#include "Tools/UnrealAiToolDispatch_MaterialGraph.h"

#include "Tools/UnrealAiToolDispatch_ArgRepair.h"
#include "Tools/UnrealAiToolJson.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "MaterialEditingLibrary.h"
#include "Misc/Guid.h"
#include "ScopedTransaction.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "RHIShaderPlatform.h"
#endif

namespace UnrealAiMaterialGraphPriv
{
	static UMaterial* LoadBaseMaterial(const FString& MaterialPath, FString& OutError)
	{
		OutError.Reset();
		UMaterial* Mat = LoadObject<UMaterial>(nullptr, *MaterialPath);
		if (!Mat)
		{
			OutError = FString::Printf(TEXT("Could not load UMaterial at %s (base Material required, not Material Instance)."), *MaterialPath);
			return nullptr;
		}
		return Mat;
	}

	static UMaterialExpression* FindExpressionByGuid(UMaterial* Mat, const FGuid& G)
	{
		if (!Mat)
		{
			return nullptr;
		}
		for (const TObjectPtr<UMaterialExpression>& Ptr : Mat->GetExpressionCollection().Expressions)
		{
			UMaterialExpression* E = Ptr.Get();
			if (E && E->MaterialExpressionGuid == G)
			{
				return E;
			}
		}
		return nullptr;
	}

	static bool TryParseGuidString(const FString& S, FGuid& OutG)
	{
		FString T = S;
		T.TrimStartAndEndInline();
		return FGuid::Parse(T, OutG);
	}

	static bool ResolveMaterialRootSlot(UMaterialEditorOnlyData* D, const FString& SlotIn, FExpressionInput*& OutRoot)
	{
		OutRoot = nullptr;
		if (!D)
		{
			return false;
		}
		FString Slot = SlotIn;
		Slot.TrimStartAndEndInline();
		auto UC = [](const FString& A, const TCHAR* B) { return A.Equals(B, ESearchCase::IgnoreCase); };

		if (UC(Slot, TEXT("MaterialAttributes")))
		{
			OutRoot = &D->MaterialAttributes;
			return true;
		}

		if (Slot.StartsWith(TEXT("CustomizedUVs_"), ESearchCase::IgnoreCase))
		{
			const int32 Idx = FCString::Atoi(*Slot.Mid(14));
			if (Idx >= 0 && Idx < 8)
			{
				OutRoot = reinterpret_cast<FExpressionInput*>(&D->CustomizedUVs[Idx]);
				return true;
			}
			return false;
		}

#define UAI_MAT_SLOT(Name, Member) \
	if (UC(Slot, TEXT(Name))) \
	{ \
		OutRoot = reinterpret_cast<FExpressionInput*>(&D->Member); \
		return true; \
	}
		UAI_MAT_SLOT("BaseColor", BaseColor)
		UAI_MAT_SLOT("Metallic", Metallic)
		UAI_MAT_SLOT("Specular", Specular)
		UAI_MAT_SLOT("Roughness", Roughness)
		UAI_MAT_SLOT("Anisotropy", Anisotropy)
		UAI_MAT_SLOT("Normal", Normal)
		UAI_MAT_SLOT("Tangent", Tangent)
		UAI_MAT_SLOT("EmissiveColor", EmissiveColor)
		UAI_MAT_SLOT("Opacity", Opacity)
		UAI_MAT_SLOT("OpacityMask", OpacityMask)
		UAI_MAT_SLOT("WorldPositionOffset", WorldPositionOffset)
		UAI_MAT_SLOT("Displacement", Displacement)
		UAI_MAT_SLOT("SubsurfaceColor", SubsurfaceColor)
		UAI_MAT_SLOT("ClearCoat", ClearCoat)
		UAI_MAT_SLOT("ClearCoatRoughness", ClearCoatRoughness)
		UAI_MAT_SLOT("AmbientOcclusion", AmbientOcclusion)
		UAI_MAT_SLOT("Refraction", Refraction)
		UAI_MAT_SLOT("PixelDepthOffset", PixelDepthOffset)
		UAI_MAT_SLOT("SurfaceThickness", SurfaceThickness)
		UAI_MAT_SLOT("ShadingModelFromMaterialExpression", ShadingModelFromMaterialExpression)
		UAI_MAT_SLOT("FrontMaterial", FrontMaterial)
#undef UAI_MAT_SLOT
		return false;
	}

	static bool StructExtendsExpressionInput(UScriptStruct* S)
	{
		for (UStruct* Cur = S; Cur; Cur = Cur->GetSuperStruct())
		{
			UScriptStruct* SS = Cast<UScriptStruct>(Cur);
			if (!SS)
			{
				break;
			}
			if (SS->GetStructCPPName() == TEXT("FExpressionInput"))
			{
				return true;
			}
		}
		return false;
	}

	static void CollectExpressionInputLinks(UMaterialExpression* E, TArray<TSharedPtr<FJsonValue>>& OutLinks)
	{
		if (!E)
		{
			return;
		}
		for (int32 Idx = 0;; ++Idx)
		{
			FExpressionInput* Input = E->GetInput(Idx);
			if (!Input)
			{
				break;
			}
			if (!Input->Expression)
			{
				continue;
			}
			const FName N = E->GetInputName(Idx);
			TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
			L->SetStringField(TEXT("to_input"), N.IsNone() ? FString::Printf(TEXT("input_%d"), Idx) : N.ToString());
			L->SetNumberField(TEXT("to_input_index"), static_cast<double>(Idx));
			L->SetStringField(TEXT("from_expression_guid"), Input->Expression->MaterialExpressionGuid.ToString());
			L->SetNumberField(TEXT("from_output_index"), static_cast<double>(Input->OutputIndex));
			OutLinks.Add(MakeShared<FJsonValueObject>(L));
		}
	}

	static void AppendMaterialRootConnections(UMaterial* Mat, TArray<TSharedPtr<FJsonValue>>& OutLinks)
	{
		UMaterialEditorOnlyData* D = Mat->GetEditorOnlyData();
		if (!D)
		{
			return;
		}

		static const TCHAR* const SlotNames[] = {
			TEXT("BaseColor"),
			TEXT("Metallic"),
			TEXT("Specular"),
			TEXT("Roughness"),
			TEXT("Anisotropy"),
			TEXT("Normal"),
			TEXT("Tangent"),
			TEXT("EmissiveColor"),
			TEXT("Opacity"),
			TEXT("OpacityMask"),
			TEXT("WorldPositionOffset"),
			TEXT("Displacement"),
			TEXT("SubsurfaceColor"),
			TEXT("ClearCoat"),
			TEXT("ClearCoatRoughness"),
			TEXT("AmbientOcclusion"),
			TEXT("Refraction"),
			TEXT("PixelDepthOffset"),
			TEXT("SurfaceThickness"),
			TEXT("ShadingModelFromMaterialExpression"),
			TEXT("FrontMaterial"),
			TEXT("MaterialAttributes"),
		};
		for (const TCHAR* Slot : SlotNames)
		{
			FExpressionInput* Root = nullptr;
			if (!ResolveMaterialRootSlot(D, FString(Slot), Root) || !Root)
			{
				continue;
			}
			UMaterialExpression* Src = Root->Expression;
			if (!Src)
			{
				continue;
			}
			const int32 OutIdx = Root->OutputIndex;
			TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
			L->SetStringField(TEXT("material_input"), FString(Slot));
			L->SetStringField(TEXT("from_expression_guid"), Src->MaterialExpressionGuid.ToString());
			L->SetNumberField(TEXT("from_output_index"), static_cast<double>(OutIdx));
			OutLinks.Add(MakeShared<FJsonValueObject>(L));
		}
		for (int32 Uv = 0; Uv < 8; ++Uv)
		{
			const FExpressionInput& Root = *reinterpret_cast<FExpressionInput*>(&D->CustomizedUVs[Uv]);
			if (!Root.Expression)
			{
				continue;
			}
			TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
			L->SetStringField(TEXT("material_input"), FString::Printf(TEXT("CustomizedUVs_%d"), Uv));
			L->SetStringField(TEXT("from_expression_guid"), Root.Expression->MaterialExpressionGuid.ToString());
			L->SetNumberField(TEXT("from_output_index"), static_cast<double>(Root.OutputIndex));
			OutLinks.Add(MakeShared<FJsonValueObject>(L));
		}
	}

	static int32 GetExpressionOutputIndexByNameLocal(UMaterialExpression* Expression, FName OutputName)
	{
		if (!Expression)
		{
			return INDEX_NONE;
		}
		if (Expression->Outputs.Num() == 0)
		{
			return INDEX_NONE;
		}
		if (OutputName.IsNone())
		{
			return 0;
		}
		for (int32 OutIdx = 0; OutIdx < Expression->Outputs.Num(); ++OutIdx)
		{
			const FExpressionOutput& Output = Expression->Outputs[OutIdx];
			if (!Output.OutputName.IsNone())
			{
				if (OutputName == Output.OutputName)
				{
					return OutIdx;
				}
			}
			else
			{
				if (Output.MaskR && !Output.MaskG && !Output.MaskB && !Output.MaskA && OutputName == FName(TEXT("R")))
				{
					return OutIdx;
				}
				if (Output.MaskG && !Output.MaskR && !Output.MaskB && !Output.MaskA && OutputName == FName(TEXT("G")))
				{
					return OutIdx;
				}
				if (Output.MaskB && !Output.MaskR && !Output.MaskG && !Output.MaskA && OutputName == FName(TEXT("B")))
				{
					return OutIdx;
				}
				if (Output.MaskA && !Output.MaskR && !Output.MaskG && !Output.MaskB && OutputName == FName(TEXT("A")))
				{
					return OutIdx;
				}
				if (Output.MaskR && Output.MaskG && Output.MaskB && !Output.MaskA && OutputName == FName(TEXT("RGB")))
				{
					return OutIdx;
				}
				if (Output.MaskR && Output.MaskG && Output.MaskB && Output.MaskA && OutputName == FName(TEXT("RGBA")))
				{
					return OutIdx;
				}
			}
		}
		return INDEX_NONE;
	}

	static void GatherReachableExpressions(UMaterial* Mat, TSet<FGuid>& OutVisited)
	{
		OutVisited.Reset();
		if (!Mat)
		{
			return;
		}
		TArray<UMaterialExpression*> Queue;
		auto Enqueue = [&](UMaterialExpression* E)
		{
			if (!E)
			{
				return;
			}
			if (OutVisited.Contains(E->MaterialExpressionGuid))
			{
				return;
			}
			OutVisited.Add(E->MaterialExpressionGuid);
			Queue.Add(E);
		};

		if (UMaterialEditorOnlyData* D = Mat->GetEditorOnlyData())
		{
			static const TCHAR* const SlotNames[] = {
				TEXT("BaseColor"),
				TEXT("Metallic"),
				TEXT("Specular"),
				TEXT("Roughness"),
				TEXT("Anisotropy"),
				TEXT("Normal"),
				TEXT("Tangent"),
				TEXT("EmissiveColor"),
				TEXT("Opacity"),
				TEXT("OpacityMask"),
				TEXT("WorldPositionOffset"),
				TEXT("Displacement"),
				TEXT("SubsurfaceColor"),
				TEXT("ClearCoat"),
				TEXT("ClearCoatRoughness"),
				TEXT("AmbientOcclusion"),
				TEXT("Refraction"),
				TEXT("PixelDepthOffset"),
				TEXT("SurfaceThickness"),
				TEXT("ShadingModelFromMaterialExpression"),
				TEXT("FrontMaterial"),
				TEXT("MaterialAttributes"),
			};
			for (const TCHAR* Slot : SlotNames)
			{
				FExpressionInput* Root = nullptr;
				if (!ResolveMaterialRootSlot(D, FString(Slot), Root) || !Root || !Root->Expression)
				{
					continue;
				}
				Enqueue(Root->Expression);
			}
			for (int32 Uv = 0; Uv < 8; ++Uv)
			{
				const FExpressionInput& Root = *reinterpret_cast<FExpressionInput*>(&D->CustomizedUVs[Uv]);
				if (Root.Expression)
				{
					Enqueue(Root.Expression);
				}
			}
		}

		for (int32 Qi = 0; Qi < Queue.Num(); ++Qi)
		{
			UMaterialExpression* E = Queue[Qi];
			for (int32 Idx = 0;; ++Idx)
			{
				FExpressionInput* In = E->GetInput(Idx);
				if (!In)
				{
					break;
				}
				if (In->Expression)
				{
					Enqueue(In->Expression);
				}
			}
		}
	}
} // namespace UnrealAiMaterialGraphPriv

FUnrealAiToolInvocationResult UnrealAiDispatch_MaterialGraphExport(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiMaterialGraphPriv;

	FString MaterialPath;
	if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
	{
		Args->TryGetStringField(TEXT("object_path"), MaterialPath);
		if (MaterialPath.IsEmpty())
		{
			Args->TryGetStringField(TEXT("path"), MaterialPath);
		}
	}
	double MaxExprD = 500.0;
	Args->TryGetNumberField(TEXT("max_expressions"), MaxExprD);
	const int32 MaxExpressions = FMath::Clamp(static_cast<int32>(MaxExprD), 1, 4000);

	if (MaterialPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("material_path is required (aliases: object_path, path)."));
	}
	UnrealAiToolDispatchArgRepair::NormalizeAssetLikeObjectPath(MaterialPath);

	FString LoadErr;
	UMaterial* Mat = LoadBaseMaterial(MaterialPath, LoadErr);
	if (!Mat)
	{
		return UnrealAiToolJson::Error(LoadErr);
	}

	const FMaterialExpressionCollection& Coll = Mat->GetExpressionCollection();
	const int32 TotalExpressions = Coll.Expressions.Num();
	TArray<TSharedPtr<FJsonValue>> ExprArr;
	ExprArr.Reserve(FMath::Min(MaxExpressions, TotalExpressions));

	int32 Added = 0;
	for (const TObjectPtr<UMaterialExpression>& Ptr : Coll.Expressions)
	{
		if (Added >= MaxExpressions)
		{
			break;
		}
		UMaterialExpression* E = Ptr.Get();
		if (!E)
		{
			continue;
		}
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("class"), E->GetClass()->GetPathName());
		Row->SetStringField(TEXT("expression_guid"), E->MaterialExpressionGuid.ToString());
#if WITH_EDITOR
		Row->SetNumberField(TEXT("editor_x"), static_cast<double>(E->MaterialExpressionEditorX));
		Row->SetNumberField(TEXT("editor_y"), static_cast<double>(E->MaterialExpressionEditorY));
#endif
		TArray<TSharedPtr<FJsonValue>> InLinks;
		CollectExpressionInputLinks(E, InLinks);
		Row->SetArrayField(TEXT("inputs"), InLinks);

		ExprArr.Add(MakeShared<FJsonValueObject>(Row));
		++Added;
	}

	TArray<TSharedPtr<FJsonValue>> RootLinks;
	AppendMaterialRootConnections(Mat, RootLinks);

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("material_path"), MaterialPath);
	O->SetNumberField(TEXT("expression_count_total"), static_cast<double>(TotalExpressions));
	O->SetNumberField(TEXT("expression_count_returned"), static_cast<double>(ExprArr.Num()));
	O->SetBoolField(TEXT("truncated"), TotalExpressions > ExprArr.Num());
	O->SetArrayField(TEXT("expressions"), ExprArr);
	O->SetArrayField(TEXT("material_inputs"), RootLinks);
	O->SetStringField(
		TEXT("note"),
		TEXT("Structured export: expression GUIDs, per-node inputs (links to other expressions), and material root connections. Use material_graph_patch for mutations; material_graph_compile after edits."));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_MaterialGraphPatch(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiMaterialGraphPriv;

	FString MaterialPath;
	if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
	{
		Args->TryGetStringField(TEXT("object_path"), MaterialPath);
	}
	if (MaterialPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("material_path is required."));
	}
	UnrealAiToolDispatchArgRepair::NormalizeAssetLikeObjectPath(MaterialPath);

	const TArray<TSharedPtr<FJsonValue>>* OpsArr = nullptr;
	if (!Args->TryGetArrayField(TEXT("ops"), OpsArr) || !OpsArr || OpsArr->Num() == 0)
	{
		return UnrealAiToolJson::Error(TEXT("ops array is required (non-empty)."));
	}

	bool bRecompile = false;
	Args->TryGetBoolField(TEXT("recompile"), bRecompile);

	FString LoadErr;
	UMaterial* Mat = LoadBaseMaterial(MaterialPath, LoadErr);
	if (!Mat)
	{
		return UnrealAiToolJson::Error(LoadErr);
	}

	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnMatGraphPatch", "Unreal AI: material_graph_patch"));

	TArray<TSharedPtr<FJsonValue>> OpResults;
	OpResults.Reserve(OpsArr->Num());
	int32 Applied = 0;

	for (const TSharedPtr<FJsonValue>& V : *OpsArr)
	{
		const TSharedPtr<FJsonObject>* OpObj = nullptr;
		if (!V.IsValid() || !V->TryGetObject(OpObj) || !OpObj->IsValid())
		{
			TSharedPtr<FJsonObject> One = MakeShared<FJsonObject>();
			One->SetBoolField(TEXT("ok"), false);
			One->SetStringField(TEXT("error"), TEXT("op must be an object"));
			OpResults.Add(MakeShared<FJsonValueObject>(One));
			continue;
		}
		const TSharedPtr<FJsonObject>& Op = *OpObj;

		FString OpName;
		Op->TryGetStringField(TEXT("op"), OpName);
		OpName.TrimStartAndEndInline();

		auto OkOne = [&](const TSharedPtr<FJsonObject>& Payload = nullptr)
		{
			TSharedPtr<FJsonObject> One = MakeShared<FJsonObject>();
			One->SetBoolField(TEXT("ok"), true);
			if (Payload)
			{
				for (const auto& Pair : Payload->Values)
				{
					One->SetField(Pair.Key, Pair.Value);
				}
			}
			OpResults.Add(MakeShared<FJsonValueObject>(One));
			++Applied;
		};
		auto FailOne = [&](const FString& Msg)
		{
			TSharedPtr<FJsonObject> One = MakeShared<FJsonObject>();
			One->SetBoolField(TEXT("ok"), false);
			One->SetStringField(TEXT("error"), Msg);
			OpResults.Add(MakeShared<FJsonValueObject>(One));
		};

		if (OpName.Equals(TEXT("add_expression"), ESearchCase::IgnoreCase))
		{
			FString ClassPath;
			if (!Op->TryGetStringField(TEXT("class_path"), ClassPath) || ClassPath.IsEmpty())
			{
				Op->TryGetStringField(TEXT("class"), ClassPath);
			}
			if (ClassPath.IsEmpty())
			{
				FailOne(TEXT("add_expression requires class_path (e.g. /Script/Engine.MaterialExpressionConstant3Vector)."));
				continue;
			}
			UClass* Cls = LoadClass<UMaterialExpression>(nullptr, *ClassPath);
			if (!Cls || !Cls->IsChildOf(UMaterialExpression::StaticClass()))
			{
				FailOne(FString::Printf(TEXT("Could not load UMaterialExpression class: %s"), *ClassPath));
				continue;
			}
			int32 X = 0, Y = 0;
			double Xd = 0, Yd = 0;
			if (Op->TryGetNumberField(TEXT("editor_x"), Xd))
			{
				X = static_cast<int32>(Xd);
			}
			if (Op->TryGetNumberField(TEXT("editor_y"), Yd))
			{
				Y = static_cast<int32>(Yd);
			}
			UMaterialExpression* NewEx = UMaterialEditingLibrary::CreateMaterialExpression(Mat, Cls, X, Y);
			if (!NewEx)
			{
				FailOne(TEXT("CreateMaterialExpression failed."));
				continue;
			}
			Mat->MarkPackageDirty();
			TSharedPtr<FJsonObject> Pay = MakeShared<FJsonObject>();
			Pay->SetStringField(TEXT("expression_guid"), NewEx->MaterialExpressionGuid.ToString());
			OkOne(Pay);
			continue;
		}

		if (OpName.Equals(TEXT("delete_expression"), ESearchCase::IgnoreCase))
		{
			FString Gs;
			if (!Op->TryGetStringField(TEXT("expression_guid"), Gs) || Gs.IsEmpty())
			{
				FailOne(TEXT("delete_expression requires expression_guid."));
				continue;
			}
			FGuid G;
			if (!TryParseGuidString(Gs, G))
			{
				FailOne(TEXT("expression_guid is not a valid GUID."));
				continue;
			}
			UMaterialExpression* E = FindExpressionByGuid(Mat, G);
			if (!E)
			{
				FailOne(TEXT("expression_guid not found in material."));
				continue;
			}
			UMaterialEditingLibrary::DeleteMaterialExpression(Mat, E);
			Mat->MarkPackageDirty();
			OkOne();
			continue;
		}

		if (OpName.Equals(TEXT("connect"), ESearchCase::IgnoreCase))
		{
			FString FromG, ToG, ToInput, FromOut;
			Op->TryGetStringField(TEXT("from_expression_guid"), FromG);
			Op->TryGetStringField(TEXT("to_expression_guid"), ToG);
			Op->TryGetStringField(TEXT("to_input"), ToInput);
			Op->TryGetStringField(TEXT("from_output"), FromOut);
			FromG.TrimStartAndEndInline();
			ToG.TrimStartAndEndInline();
			ToInput.TrimStartAndEndInline();
			FromOut.TrimStartAndEndInline();
			if (FromG.IsEmpty() || ToG.IsEmpty() || ToInput.IsEmpty())
			{
				FailOne(TEXT("connect requires from_expression_guid, to_expression_guid, to_input (from_output optional, default first output)."));
				continue;
			}
			FGuid GFrom, GTo;
			if (!TryParseGuidString(FromG, GFrom) || !TryParseGuidString(ToG, GTo))
			{
				FailOne(TEXT("Invalid GUID in connect."));
				continue;
			}
			UMaterialExpression* FromE = FindExpressionByGuid(Mat, GFrom);
			UMaterialExpression* ToE = FindExpressionByGuid(Mat, GTo);
			if (!FromE || !ToE)
			{
				FailOne(TEXT("connect: from or to expression not found."));
				continue;
			}
			const bool bOk = UMaterialEditingLibrary::ConnectMaterialExpressions(FromE, FromOut, ToE, ToInput);
			if (!bOk)
			{
				FailOne(TEXT("ConnectMaterialExpressions failed (pin names / types)."));
				continue;
			}
			Mat->MarkPackageDirty();
			OkOne();
			continue;
		}

		if (OpName.Equals(TEXT("connect_material_input"), ESearchCase::IgnoreCase))
		{
			FString FromG, Slot, FromOut;
			Op->TryGetStringField(TEXT("from_expression_guid"), FromG);
			Op->TryGetStringField(TEXT("material_input"), Slot);
			Op->TryGetStringField(TEXT("from_output"), FromOut);
			FromG.TrimStartAndEndInline();
			Slot.TrimStartAndEndInline();
			FromOut.TrimStartAndEndInline();
			if (FromG.IsEmpty() || Slot.IsEmpty())
			{
				FailOne(TEXT("connect_material_input requires from_expression_guid and material_input."));
				continue;
			}
			FGuid GFrom;
			if (!TryParseGuidString(FromG, GFrom))
			{
				FailOne(TEXT("Invalid from_expression_guid."));
				continue;
			}
			UMaterialExpression* FromE = FindExpressionByGuid(Mat, GFrom);
			if (!FromE)
			{
				FailOne(TEXT("from expression not found."));
				continue;
			}
			UMaterialEditorOnlyData* D = Mat->GetEditorOnlyData();
			if (!D)
			{
				FailOne(TEXT("Material has no editor-only data."));
				continue;
			}
			FExpressionInput* Root = nullptr;
			if (!ResolveMaterialRootSlot(D, Slot, Root) || !Root)
			{
				FailOne(FString::Printf(TEXT("Unknown material_input slot: %s"), *Slot));
				continue;
			}
			const int32 OutIdx = GetExpressionOutputIndexByNameLocal(FromE, FromOut.IsEmpty() ? NAME_None : FName(*FromOut));
			if (OutIdx == INDEX_NONE)
			{
				FailOne(TEXT("from_output does not match any expression output (try empty string for first output)."));
				continue;
			}
			Root->Connect(OutIdx, FromE);
			Mat->MarkPackageDirty();
			OkOne();
			continue;
		}

		if (OpName.Equals(TEXT("disconnect_material_input"), ESearchCase::IgnoreCase))
		{
			FString Slot;
			if (!Op->TryGetStringField(TEXT("material_input"), Slot) || Slot.IsEmpty())
			{
				FailOne(TEXT("disconnect_material_input requires material_input."));
				continue;
			}
			UMaterialEditorOnlyData* D = Mat->GetEditorOnlyData();
			if (!D)
			{
				FailOne(TEXT("Material has no editor-only data."));
				continue;
			}
			FExpressionInput* Root = nullptr;
			if (!ResolveMaterialRootSlot(D, Slot, Root) || !Root)
			{
				FailOne(FString::Printf(TEXT("Unknown material_input slot: %s"), *Slot));
				continue;
			}
			Root->Expression = nullptr;
			Root->OutputIndex = 0;
			Mat->MarkPackageDirty();
			OkOne();
			continue;
		}

		if (OpName.Equals(TEXT("disconnect_input"), ESearchCase::IgnoreCase))
		{
			FString Gs, InName;
			Op->TryGetStringField(TEXT("expression_guid"), Gs);
			Op->TryGetStringField(TEXT("input_name"), InName);
			Gs.TrimStartAndEndInline();
			InName.TrimStartAndEndInline();
			if (Gs.IsEmpty() || InName.IsEmpty())
			{
				FailOne(TEXT("disconnect_input requires expression_guid and input_name (property name, e.g. A)."));
				continue;
			}
			FGuid G;
			if (!TryParseGuidString(Gs, G))
			{
				FailOne(TEXT("Invalid expression_guid."));
				continue;
			}
			UMaterialExpression* E = FindExpressionByGuid(Mat, G);
			if (!E)
			{
				FailOne(TEXT("expression not found."));
				continue;
			}
			FProperty* Prop = E->GetClass()->FindPropertyByName(FName(*InName));
			const FStructProperty* Sp = CastField<FStructProperty>(Prop);
			if (!Sp || !StructExtendsExpressionInput(Sp->Struct))
			{
				FailOne(TEXT("input_name is not an FExpressionInput-derived pin on that expression."));
				continue;
			}
			FExpressionInput* In = Sp->ContainerPtrToValuePtr<FExpressionInput>(E);
			In->Expression = nullptr;
			In->OutputIndex = 0;
			Mat->MarkPackageDirty();
			OkOne();
			continue;
		}

		if (OpName.Equals(TEXT("set_editor_position"), ESearchCase::IgnoreCase))
		{
			FString Gs;
			Op->TryGetStringField(TEXT("expression_guid"), Gs);
			Gs.TrimStartAndEndInline();
			double Xd = 0, Yd = 0;
			Op->TryGetNumberField(TEXT("editor_x"), Xd);
			Op->TryGetNumberField(TEXT("editor_y"), Yd);
			FGuid G;
			if (!TryParseGuidString(Gs, G))
			{
				FailOne(TEXT("Invalid expression_guid."));
				continue;
			}
			UMaterialExpression* E = FindExpressionByGuid(Mat, G);
			if (!E)
			{
				FailOne(TEXT("expression not found."));
				continue;
			}
#if WITH_EDITOR
			E->MaterialExpressionEditorX = static_cast<int32>(Xd);
			E->MaterialExpressionEditorY = static_cast<int32>(Yd);
#endif
			Mat->MarkPackageDirty();
			OkOne();
			continue;
		}

		if (OpName.Equals(TEXT("set_constant3vector"), ESearchCase::IgnoreCase))
		{
			FString Gs;
			Op->TryGetStringField(TEXT("expression_guid"), Gs);
			Gs.TrimStartAndEndInline();
			FGuid G;
			if (!TryParseGuidString(Gs, G))
			{
				FailOne(TEXT("Invalid expression_guid."));
				continue;
			}
			UMaterialExpression* E = FindExpressionByGuid(Mat, G);
			UMaterialExpressionConstant3Vector* C = Cast<UMaterialExpressionConstant3Vector>(E);
			if (!C)
			{
				FailOne(TEXT("expression is not MaterialExpressionConstant3Vector."));
				continue;
			}
			double R = 1, Gc = 1, B = 1, A = 1;
			Op->TryGetNumberField(TEXT("r"), R);
			Op->TryGetNumberField(TEXT("g"), Gc);
			Op->TryGetNumberField(TEXT("b"), B);
			Op->TryGetNumberField(TEXT("a"), A);
			C->Constant = FLinearColor(static_cast<float>(R), static_cast<float>(Gc), static_cast<float>(B), static_cast<float>(A));
			Mat->MarkPackageDirty();
			OkOne();
			continue;
		}

		FailOne(FString::Printf(TEXT("Unknown op: %s"), *OpName));
	}

	if (bRecompile)
	{
		UMaterialEditingLibrary::RecompileMaterial(Mat);
	}

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("material_path"), MaterialPath);
	O->SetNumberField(TEXT("ops_requested"), static_cast<double>(OpsArr->Num()));
	O->SetNumberField(TEXT("ops_applied_ok"), static_cast<double>(Applied));
	O->SetArrayField(TEXT("op_results"), OpResults);
	O->SetBoolField(TEXT("recompile_requested"), bRecompile);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_MaterialGraphCompile(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiMaterialGraphPriv;

	FString MaterialPath;
	if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
	{
		Args->TryGetStringField(TEXT("object_path"), MaterialPath);
	}
	if (MaterialPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("material_path is required."));
	}
	UnrealAiToolDispatchArgRepair::NormalizeAssetLikeObjectPath(MaterialPath);

	FString LoadErr;
	UMaterial* Mat = LoadBaseMaterial(MaterialPath, LoadErr);
	if (!Mat)
	{
		return UnrealAiToolJson::Error(LoadErr);
	}

	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnMatGraphCompile", "Unreal AI: material_graph_compile"));
	UMaterialEditingLibrary::RecompileMaterial(Mat);

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("material_path"), MaterialPath);
#if WITH_EDITOR
	const bool bErr = Mat->IsCompilingOrHadCompileError(GMaxRHIShaderPlatform);
	O->SetBoolField(TEXT("compile_error_or_pending"), bErr);
	if (bErr)
	{
		O->SetStringField(
			TEXT("note"),
			TEXT("Material reports compiling or compile error on the current RHI shader platform; check the Material Editor message log and graph."));
	}
#else
	O->SetBoolField(TEXT("compile_error_or_pending"), false);
#endif
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_MaterialGraphValidate(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiMaterialGraphPriv;

	FString MaterialPath;
	if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
	{
		Args->TryGetStringField(TEXT("object_path"), MaterialPath);
	}
	if (MaterialPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("material_path is required."));
	}
	UnrealAiToolDispatchArgRepair::NormalizeAssetLikeObjectPath(MaterialPath);

	FString LoadErr;
	UMaterial* Mat = LoadBaseMaterial(MaterialPath, LoadErr);
	if (!Mat)
	{
		return UnrealAiToolJson::Error(LoadErr);
	}

	TSet<FGuid> Reachable;
	GatherReachableExpressions(Mat, Reachable);

	TArray<TSharedPtr<FJsonValue>> Orphans;
	const FMaterialExpressionCollection& Coll = Mat->GetExpressionCollection();
	for (const TObjectPtr<UMaterialExpression>& Ptr : Coll.Expressions)
	{
		UMaterialExpression* E = Ptr.Get();
		if (!E)
		{
			continue;
		}
		if (!Reachable.Contains(E->MaterialExpressionGuid))
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("expression_guid"), E->MaterialExpressionGuid.ToString());
			Row->SetStringField(TEXT("class"), E->GetClass()->GetPathName());
			Orphans.Add(MakeShared<FJsonValueObject>(Row));
		}
	}

#if WITH_EDITOR
	const bool bCompileErr = Mat->IsCompilingOrHadCompileError(GMaxRHIShaderPlatform);
#else
	const bool bCompileErr = false;
#endif

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("material_path"), MaterialPath);
	O->SetNumberField(TEXT("orphan_expression_count"), static_cast<double>(Orphans.Num()));
	O->SetArrayField(TEXT("orphan_expressions"), Orphans);
	O->SetBoolField(TEXT("compile_error_or_pending"), bCompileErr);
	return UnrealAiToolJson::Ok(O);
}
