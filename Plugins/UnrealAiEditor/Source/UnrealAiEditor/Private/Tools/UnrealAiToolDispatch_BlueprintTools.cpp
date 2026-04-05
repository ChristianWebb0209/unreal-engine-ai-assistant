#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"

#include "Tools/UnrealAiToolDispatch_ArgRepair.h"
#include "Tools/UnrealAiBlueprintFunctionResolve.h"
#include "BlueprintFormat/BlueprintGraphCommentReflow.h"
#include "BlueprintFormat/BlueprintGraphFormatOptions.h"
#include "BlueprintFormat/BlueprintGraphLayoutAnalysis.h"
#include "BlueprintFormat/UnrealAiBlueprintFormatterBridge.h"
#include "BlueprintFormat/UnrealAiBlueprintGraphSelectionLayout.h"
#include "Tools/UnrealAiToolDispatch_MoreAssets.h"
#include "Tools/UnrealAiToolJson.h"
#include "Tools/Presentation/UnrealAiToolEditorNoteBuilders.h"

#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/ActorComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/KismetSystemLibrary.h"
#include "K2Node_CustomEvent.h"
#include "EdGraphNode_Comment.h"
#include "Logging/TokenizedMessage.h"
#include "UnrealAiEditorSettings.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ScopedTransaction.h"
#include "Misc/PackageName.h"
#include "Tools/Presentation/UnrealAiEditorNavigation.h"
#include "Misc/UnrealAiRecentUiTracker.h"
#include "UObject/Class.h"
#include "UObject/Script.h"
#include "UObject/Field.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "AssetRegistry/AssetRegistryModule.h"

namespace UnrealAiBlueprintToolsPriv
{
	/**
	 * Models often pass /Game/Folder/Asset without the required .Asset suffix.
	 * Canonical form for Unreal object paths is /Game/.../Name.Name (package path + exported object name).
	 */
	void NormalizeBlueprintObjectPath(FString& Path)
	{
		UnrealAiToolDispatchArgRepair::SanitizeUnrealPathString(Path);
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty() || Path.Contains(TEXT("..")))
		{
			return;
		}
		if (Path.EndsWith(TEXT(".uasset"), ESearchCase::IgnoreCase))
		{
			Path.LeftChopInline(7);
			Path.TrimEndInline();
		}
		int32 LastSlash = INDEX_NONE;
		if (!Path.FindLastChar(TEXT('/'), LastSlash))
		{
			return;
		}
		const FString Leaf = Path.Mid(LastSlash + 1);
		if (Leaf.IsEmpty() || Leaf.Contains(TEXT(".")))
		{
			return;
		}
		Path.Reserve(Path.Len() + 1 + Leaf.Len());
		Path.Append(TEXT(".")).Append(Leaf);
	}

	static bool IsGameWritableBlueprintObjectPath(const FString& BlueprintObjectPath)
	{
		if (BlueprintObjectPath.IsEmpty() || BlueprintObjectPath.Contains(TEXT("..")))
		{
			return false;
		}
		FString Tmp = BlueprintObjectPath;
		NormalizeBlueprintObjectPath(Tmp);
		const FString Pkg = FPackageName::ObjectPathToPackageName(Tmp);
		return Pkg.StartsWith(TEXT("/Game/")) || Pkg == TEXT("/Game");
	}

	static UBlueprint* LoadBlueprint(const FString& PathIn, FString* OutFailureDetail = nullptr)
	{
		FString Path = PathIn;
		NormalizeBlueprintObjectPath(Path);
		if (Path.IsEmpty())
		{
			if (OutFailureDetail)
			{
				*OutFailureDetail = TEXT("blueprint_path is empty after normalization.");
			}
			return nullptr;
		}
		if (Path.Contains(TEXT("..")))
		{
			if (OutFailureDetail)
			{
				*OutFailureDetail = TEXT("blueprint_path must not contain '..'.");
			}
			return nullptr;
		}

		// Registry hints only: tags can be stale or incomplete while the asset still loads.
		enum class ERegistryBlueprintHint : uint8
		{
			Ok,
			NotInRegistry,
			BadGeneratedClassTag,
		};
		ERegistryBlueprintHint RegHint = ERegistryBlueprintHint::Ok;
		{
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			const FAssetData AD = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(Path));
			if (!AD.IsValid())
			{
				RegHint = ERegistryBlueprintHint::NotInRegistry;
			}
			else
			{
				FString GeneratedClassTag;
				if (!AD.GetTagValue(FName(TEXT("GeneratedClass")), GeneratedClassTag)
					|| GeneratedClassTag.IsEmpty()
					|| GeneratedClassTag.Equals(TEXT("None"), ESearchCase::IgnoreCase)
					|| GeneratedClassTag.Equals(TEXT("null"), ESearchCase::IgnoreCase))
				{
					RegHint = ERegistryBlueprintHint::BadGeneratedClassTag;
				}
			}
		}

		if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Path))
		{
			return BP;
		}
		if (UBlueprint* BP = Cast<UBlueprint>(FSoftObjectPath(Path).TryLoad()))
		{
			return BP;
		}

		if (OutFailureDetail)
		{
			switch (RegHint)
			{
			case ERegistryBlueprintHint::NotInRegistry:
				*OutFailureDetail = FString::Printf(
					TEXT("No asset registry entry for '%s'. Run asset_index_fuzzy_search or asset_registry_query and use matches[].object_path / assets[].object_path exactly."),
					*Path);
				break;
			case ERegistryBlueprintHint::BadGeneratedClassTag:
				*OutFailureDetail = FString::Printf(
					TEXT("Could not load '%s' as UBlueprint. Registry shows missing or invalid GeneratedClass (save/recompile the Blueprint in-editor, or confirm the object path points at the Blueprint asset, not a different type)."),
					*Path);
				break;
			default:
				*OutFailureDetail = FString::Printf(
					TEXT("LoadObject/TryLoad failed for '%s' as UBlueprint (wrong asset type, unloadable package, or path typo)."),
					*Path);
				break;
			}
		}
		return nullptr;
	}

	static UEdGraph* FindGraphByName(UBlueprint* BP, const FString& GraphName)
	{
		if (!BP || GraphName.IsEmpty())
		{
			return nullptr;
		}
		const FName GN(*GraphName);
		auto TryList = [&GN, &GraphName](const TArray<UEdGraph*>& List) -> UEdGraph*
		{
			for (UEdGraph* G : List)
			{
				if (G && (G->GetFName() == GN || G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)))
				{
					return G;
				}
			}
			return nullptr;
		};
		if (UEdGraph* G = TryList(BP->UbergraphPages))
		{
			return G;
		}
		if (UEdGraph* G = TryList(BP->FunctionGraphs))
		{
			return G;
		}
		if (UEdGraph* G = TryList(BP->MacroGraphs))
		{
			return G;
		}
		return nullptr;
	}

	static bool TryParsePinType(const FString& TypeStr, FEdGraphPinType& P)
	{
		FString Trimmed = TypeStr;
		Trimmed.TrimStartAndEndInline();
		if (Trimmed.IsEmpty())
		{
			return false;
		}
		const FString T = Trimmed.ToLower();
		if (T == TEXT("bool") || T == TEXT("boolean"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			return true;
		}
		if (T == TEXT("int") || T == TEXT("integer") || T == TEXT("int32"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Int;
			return true;
		}
		if (T == TEXT("int64") || T == TEXT("long"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Int64;
			return true;
		}
		if (T == TEXT("byte") || T == TEXT("uint8"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Byte;
			return true;
		}
		if (T == TEXT("float"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Real;
			P.PinSubCategory = UEdGraphSchema_K2::PC_Float;
			return true;
		}
		if (T == TEXT("double"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Real;
			P.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			return true;
		}
		if (T == TEXT("string"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_String;
			return true;
		}
		if (T == TEXT("name") || T == TEXT("fname"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Name;
			return true;
		}
		if (T == TEXT("text") || T == TEXT("ftext"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Text;
			return true;
		}
		if (T == TEXT("vector"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Struct;
			P.PinSubCategoryObject = TBaseStructure<FVector>::Get();
			return true;
		}
		if (T == TEXT("rotator"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Struct;
			P.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
			return true;
		}
		if (Trimmed.StartsWith(TEXT("/")))
		{
			if (UClass* C = LoadObject<UClass>(nullptr, *Trimmed))
			{
				P.PinCategory = UEdGraphSchema_K2::PC_Object;
				P.PinSubCategoryObject = C;
				return true;
			}
			if (UScriptStruct* S = Cast<UScriptStruct>(StaticLoadObject(UScriptStruct::StaticClass(), nullptr, *Trimmed)))
			{
				P.PinCategory = UEdGraphSchema_K2::PC_Struct;
				P.PinSubCategoryObject = S;
				return true;
			}
		}
		return false;
	}

	static FEdGraphPinType ParsePinTypeWithBooleanFallback(const FString& TypeStr)
	{
		FEdGraphPinType P;
		if (TryParsePinType(TypeStr, P))
		{
			return P;
		}
		P.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		return P;
	}
}

void UnrealAiNormalizeBlueprintObjectPath(FString& BlueprintObjectPath)
{
	UnrealAiBlueprintToolsPriv::NormalizeBlueprintObjectPath(BlueprintObjectPath);
}

FUnrealBlueprintGraphFormatOptions UnrealAiBlueprintTools_MakeFormatOptionsFromSettings(const UUnrealAiEditorSettings* Settings)
{
	const UUnrealAiEditorSettings* S = Settings ? Settings : GetDefault<UUnrealAiEditorSettings>();
	FUnrealBlueprintGraphFormatOptions O;
	O.CommentsMode = S->BlueprintCommentsMode;
	O.bPreserveExistingPositions = S->bBlueprintFormatPreserveExistingPositions;
	O.bReflowCommentsByGeometry = S->bBlueprintFormatReflowCommentsByGeometry;
	switch (S->BlueprintFormatSpacingDensity)
	{
	case EUnrealAiBlueprintFormatSpacingDensity::Sparse:
		O.SpacingX = 480;
		O.SpacingY = 240;
		O.BranchVerticalGap = 64;
		break;
	case EUnrealAiBlueprintFormatSpacingDensity::Dense:
		O.SpacingX = 320;
		O.SpacingY = 160;
		O.BranchVerticalGap = 32;
		break;
	default:
		O.SpacingX = 400;
		O.SpacingY = 200;
		O.BranchVerticalGap = 48;
		break;
	}
	if (S->bBlueprintFormatUseWireKnots)
	{
		O.WireKnotAggression = EUnrealBlueprintWireKnotAggression::Light;
		O.MaxWireLengthBeforeReroute = 420;
		O.MaxCrossingsPerSegment = 3;
	}
	else
	{
		O.WireKnotAggression = EUnrealBlueprintWireKnotAggression::Off;
		O.MaxWireLengthBeforeReroute = 0;
		O.MaxCrossingsPerSegment = 0;
	}
	return O;
}

static FString UnrealAiBlueprintLoadHint(const FString& BlueprintPath)
{
	return FString::Printf(
		TEXT("Could not load Blueprint '%s'. Expected a Blueprint asset object path under /Game (for example /Game/Blueprints/MyBP or /Game/Blueprints/MyBP.MyBP). If this is a new asset, create it first with asset_create, then edit graphs with blueprint_graph_patch."),
		*BlueprintPath);
}

namespace UnrealAiBlueprintComponentDefaultPriv
{
	static bool PropertyIsEditableForComponentDefault(const FProperty* Prop)
	{
		return Prop && !Prop->HasAnyPropertyFlags(CPF_Transient | CPF_SkipSerialization) && Prop->HasAnyPropertyFlags(CPF_Edit);
	}

	static bool TryApplyJsonToEditableProperty(
		const FProperty* Prop,
		void* Addr,
		UObject* Owner,
		const TSharedPtr<FJsonValue>& Val,
		FString& OutErr)
	{
		if (!Prop || !Addr || !Owner || !Val.IsValid())
		{
			OutErr = TEXT("internal: invalid property apply args");
			return false;
		}
		const FString FieldName = Prop->GetName();
		if (const FBoolProperty* BP = CastField<FBoolProperty>(Prop))
		{
			bool B = false;
			if (Val->TryGetBool(B))
			{
				const_cast<FBoolProperty*>(BP)->SetPropertyValue(Addr, B);
				return true;
			}
			OutErr = FString::Printf(TEXT("%s: expected bool"), *FieldName);
			return false;
		}
		if (const FNumericProperty* NP = CastField<FNumericProperty>(Prop))
		{
			double Num = 0;
			if (!Val->TryGetNumber(Num))
			{
				OutErr = FString::Printf(TEXT("%s: expected number"), *FieldName);
				return false;
			}
			FNumericProperty* NPM = const_cast<FNumericProperty*>(NP);
			if (NPM->IsFloatingPoint())
			{
				NPM->SetFloatingPointPropertyValue(Addr, Num);
				return true;
			}
			if (NPM->IsInteger())
			{
				NPM->SetIntPropertyValue(Addr, static_cast<int64>(FMath::RoundToDouble(Num)));
				return true;
			}
			OutErr = FString::Printf(TEXT("%s: unsupported numeric storage"), *FieldName);
			return false;
		}
		if (const FStrProperty* SP = CastField<FStrProperty>(Prop))
		{
			FString S;
			if (Val->TryGetString(S))
			{
				const_cast<FStrProperty*>(SP)->SetPropertyValue(Addr, MoveTemp(S));
				return true;
			}
			OutErr = FString::Printf(TEXT("%s: expected string"), *FieldName);
			return false;
		}
		if (const FNameProperty* NP = CastField<FNameProperty>(Prop))
		{
			FString S;
			if (Val->TryGetString(S))
			{
				const_cast<FNameProperty*>(NP)->SetPropertyValue(Addr, FName(*S));
				return true;
			}
			OutErr = FString::Printf(TEXT("%s: expected string (name)"), *FieldName);
			return false;
		}
		if (const FTextProperty* TP = CastField<FTextProperty>(Prop))
		{
			FString S;
			if (Val->TryGetString(S))
			{
				const_cast<FTextProperty*>(TP)->SetPropertyValue(Addr, FText::FromString(S));
				return true;
			}
			OutErr = FString::Printf(TEXT("%s: expected string (text)"), *FieldName);
			return false;
		}
		if (const FEnumProperty* EP = CastField<FEnumProperty>(Prop))
		{
			FString S;
			if (!Val->TryGetString(S))
			{
				OutErr = FString::Printf(TEXT("%s: expected enum string"), *FieldName);
				return false;
			}
			if (UEnum* En = EP->GetEnum())
			{
				const int64 Ev = En->GetValueByName(FName(*S));
				if (Ev == INDEX_NONE)
				{
					OutErr = FString::Printf(TEXT("%s: unknown enum value %s"), *FieldName, *S);
					return false;
				}
				if (FNumericProperty* Under = EP->GetUnderlyingProperty())
				{
					Under->SetIntPropertyValue(Addr, Ev);
					return true;
				}
			}
			OutErr = FString::Printf(TEXT("%s: enum apply failed"), *FieldName);
			return false;
		}
		if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			FString S;
			if (!Val->TryGetString(S))
			{
				OutErr = FString::Printf(TEXT("%s: expected struct export-text string"), *FieldName);
				return false;
			}
			void* StructAddr = Addr;
			const TCHAR* Buf = *S;
			const TCHAR* NewBuf = StructProp->ImportText_Direct(Buf, StructAddr, Owner, PPF_None);
			if (NewBuf == nullptr)
			{
				OutErr = FString::Printf(TEXT("%s: struct ImportText failed"), *FieldName);
				return false;
			}
			return true;
		}

		FString S;
		if (!Val->TryGetString(S))
		{
			OutErr = FString::Printf(TEXT("%s: use string ExportText form for this property type"), *FieldName);
			return false;
		}
		const TCHAR* Buf = *S;
		void* PropAddr = Addr;
		const TCHAR* NewBuf = Prop->ImportText_Direct(Buf, PropAddr, Owner, PPF_None);
		if (NewBuf == nullptr)
		{
			OutErr = FString::Printf(TEXT("%s: ImportText failed"), *FieldName);
			return false;
		}
		return true;
	}

	static UActorComponent* FindNamedComponentOnActorCDO(AActor* ActorCDO, const FString& ComponentName)
	{
		if (!ActorCDO || ComponentName.IsEmpty())
		{
			return nullptr;
		}
		const FName Want(*ComponentName);

		if (ACharacter* Char = Cast<ACharacter>(ActorCDO))
		{
			if (Want == FName(TEXT("CharacterMovement")) || ComponentName.Equals(TEXT("CharacterMovement"), ESearchCase::IgnoreCase))
			{
				return Char->GetCharacterMovement();
			}
		}

		TInlineComponentArray<UActorComponent*> Components(ActorCDO);
		for (UActorComponent* C : Components)
		{
			if (!C)
			{
				continue;
			}
			if (C->GetFName() == Want || C->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				return C;
			}
		}
		return nullptr;
	}
} // namespace UnrealAiBlueprintComponentDefaultPriv

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintSetComponentDefault(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiBlueprintComponentDefaultPriv;
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	FString Path;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/MyBP.MyBP"));
		SuggestedArgs->SetStringField(TEXT("component"), TEXT("CharacterMovement"));
		SuggestedArgs->SetStringField(TEXT("property"), TEXT("JumpZVelocity"));
		SuggestedArgs->SetNumberField(TEXT("value"), 1200.0);
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("blueprint_path is required. Discover Blueprints with asset_index_fuzzy_search or asset_registry_query."),
			TEXT("blueprint_set_component_default"),
			SuggestedArgs);
	}
	FString ComponentName;
	if (!Args->TryGetStringField(TEXT("component"), ComponentName) || ComponentName.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("component is required (e.g. CharacterMovement)."));
	}
	FString PropertyName;
	if (!Args->TryGetStringField(TEXT("property"), PropertyName) || PropertyName.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("property is required (e.g. JumpZVelocity)."));
	}
	TSharedPtr<FJsonValue> ValueField = Args->TryGetField(TEXT("value"));
	if (!ValueField.IsValid())
	{
		return UnrealAiToolJson::Error(TEXT("value is required (JSON number, bool, or string per property type)."));
	}

	if (!UnrealAiBlueprintToolsPriv::IsGameWritableBlueprintObjectPath(Path))
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path must be under /Game."));
	}

	FString LoadErr;
	UBlueprint* BP = UnrealAiBlueprintToolsPriv::LoadBlueprint(Path, &LoadErr);
	if (!BP || !BP->GeneratedClass)
	{
		if (BP && !BP->GeneratedClass)
		{
			return UnrealAiToolJson::Error(FString::Printf(
				TEXT("Blueprint '%s' loaded but GeneratedClass is null — open and recompile in the editor."),
				*Path));
		}
		return UnrealAiToolJson::Error(LoadErr.IsEmpty() ? UnrealAiBlueprintLoadHint(Path) : LoadErr);
	}

	UObject* CDO = BP->GeneratedClass->GetDefaultObject();
	AActor* ActorCDO = Cast<AActor>(CDO);
	if (!ActorCDO)
	{
		return UnrealAiToolJson::Error(
			TEXT("This Blueprint's generated class default is not an Actor. Component defaults can only be set on Actor-based Blueprints (e.g. Character, Pawn)."));
	}

	UActorComponent* Comp = FindNamedComponentOnActorCDO(ActorCDO, ComponentName);
	if (!Comp)
	{
		return UnrealAiToolJson::Error(FString::Printf(
			TEXT("Component '%s' not found on the class default. Use the component name from the Blueprint (e.g. CharacterMovement)."),
			*ComponentName));
	}

	FProperty* Prop = FindFProperty<FProperty>(Comp->GetClass(), FName(*PropertyName));
	if (!Prop || !PropertyIsEditableForComponentDefault(Prop))
	{
		return UnrealAiToolJson::Error(FString::Printf(
			TEXT("Property '%s' is not editable on %s (must exist on the component instance with Edit flag)."),
			*PropertyName,
			*Comp->GetClass()->GetName()));
	}

	void* Addr = Prop->ContainerPtrToValuePtr<void>(Comp);
	FString ApplyErr;
	if (!TryApplyJsonToEditableProperty(Prop, Addr, Comp, ValueField, ApplyErr))
	{
		return UnrealAiToolJson::Error(ApplyErr);
	}

	const FScopedTransaction Txn(
		NSLOCTEXT("UnrealAiEditor", "TxnBpSetCompDefault", "Unreal AI: set component class default"));
	BP->Modify();
	Comp->Modify();
	Comp->PostEditChange();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, nullptr);

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), BP->Status != BS_Error);
	O->SetStringField(TEXT("blueprint_path"), Path);
	O->SetStringField(TEXT("component"), ComponentName);
	O->SetStringField(TEXT("property"), PropertyName);
	O->SetField(TEXT("value"), ValueField);
	O->SetStringField(TEXT("component_class"), Comp->GetClass()->GetPathName());
	O->SetNumberField(TEXT("blueprint_status"), static_cast<double>(static_cast<int32>(BP->Status)));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintCompile(const TSharedPtr<FJsonObject>& Args)
{
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	FString Path;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/MyBP.MyBP"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("blueprint_path is required. Discover Blueprints with asset_index_fuzzy_search (query + path_prefix) or asset_registry_query with path_filter/class_name."),
			TEXT("blueprint_compile"),
			SuggestedArgs);
	}
	FString LoadErr;
	UBlueprint* BP = UnrealAiBlueprintToolsPriv::LoadBlueprint(Path, &LoadErr);
	if (!BP)
	{
		return UnrealAiToolJson::Error(LoadErr.IsEmpty() ? UnrealAiBlueprintLoadHint(Path) : LoadErr);
	}
	bool bFormatGraphs = false;
	Args->TryGetBoolField(TEXT("format_graphs"), bFormatGraphs);
	const bool bFormatterLoaded = UnrealAiBlueprintFormatterBridge::EnsureFormatterModuleLoaded(nullptr);
	int32 GraphsFormatted = 0;
	if (bFormatGraphs && bFormatterLoaded)
	{
		const FScopedTransaction Txn(
			NSLOCTEXT("UnrealAiEditor", "TxnBpCompileFormat", "Unreal AI: format Blueprint graphs"));
		BP->Modify();
		const FUnrealBlueprintGraphFormatOptions CompileFmtOpts =
			UnrealAiBlueprintTools_MakeFormatOptionsFromSettings(GetDefault<UUnrealAiEditorSettings>());
		GraphsFormatted = UnrealAiBlueprintFormatterBridge::TryLayoutAllScriptGraphs(BP, CompileFmtOpts);
	}
	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Results);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), BP->Status != BS_Error);
	O->SetNumberField(TEXT("blueprint_status"), static_cast<double>(static_cast<int32>(BP->Status)));
	TArray<TSharedPtr<FJsonValue>> Msgs;
	int32 ErrCount = 0;
	int32 WarnCount = 0;
	for (const TSharedRef<FTokenizedMessage>& Msg : Results.Messages)
	{
		const EMessageSeverity::Type Sev = Msg->GetSeverity();
		if (Sev == EMessageSeverity::Error)
		{
			++ErrCount;
		}
		else if (Sev == EMessageSeverity::Warning || Sev == EMessageSeverity::PerformanceWarning)
		{
			++WarnCount;
		}
		TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("text"), Msg->ToText().ToString());
		M->SetNumberField(TEXT("severity"), static_cast<double>(static_cast<int32>(Sev)));
		Msgs.Add(MakeShareable(new FJsonValueObject(M.ToSharedRef())));
	}
	O->SetArrayField(TEXT("compiler_messages"), Msgs);
	O->SetNumberField(TEXT("compiler_error_count"), static_cast<double>(ErrCount));
	O->SetNumberField(TEXT("compiler_warning_count"), static_cast<double>(WarnCount));
	O->SetBoolField(TEXT("format_graphs_requested"), bFormatGraphs);
	O->SetNumberField(TEXT("graphs_auto_formatted"), static_cast<double>(GraphsFormatted));
	O->SetBoolField(TEXT("formatter_available"), bFormatterLoaded);
	if (bFormatGraphs && !bFormatterLoaded)
	{
		O->SetStringField(TEXT("formatter_hint"), UnrealAiBlueprintFormatterBridge::FormatterInstallHint());
	}

	const FString ToolMarkdown = FString::Printf(
		TEXT("### Blueprint compile\n- Errors: %d\n- Warnings: %d\n- Blueprint status: %d\n"),
		ErrCount,
		WarnCount,
		static_cast<int32>(BP->Status));
	return UnrealAiToolJson::OkWithEditorPresentation(
		O,
		UnrealAiToolEditorNoteBuilders::MakeBlueprintToolNote(Path, FString(), ToolMarkdown));
}


static FUnrealAiToolInvocationResult DispatchBlueprintFormatLayout(
	const TSharedPtr<FJsonObject>& Args,
	bool bForceSelectionScope)
{
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	FString Path;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path is required"));
	}
	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);
	UnrealAiBlueprintToolsPriv::NormalizeBlueprintObjectPath(Path);
	FString LoadErr;
	UBlueprint* BP = UnrealAiBlueprintToolsPriv::LoadBlueprint(Path, &LoadErr);
	if (!BP)
	{
		return UnrealAiToolJson::Error(LoadErr.IsEmpty() ? UnrealAiBlueprintLoadHint(Path) : LoadErr);
	}
	UEdGraph* Graph = nullptr;
	if (!GraphName.IsEmpty())
	{
		Graph = UnrealAiBlueprintToolsPriv::FindGraphByName(BP, GraphName);
	}
	else
	{
		Graph = BP->UbergraphPages.Num() > 0 ? BP->UbergraphPages[0].Get() : nullptr;
	}
	if (!Graph)
	{
		return UnrealAiToolJson::Error(TEXT("Graph not found"));
	}

	FString FormatScope;
	if (bForceSelectionScope)
	{
		FormatScope = TEXT("selection");
	}
	else
	{
		Args->TryGetStringField(TEXT("format_scope"), FormatScope);
		FormatScope.TrimStartAndEndInline();
		if (FormatScope.IsEmpty())
		{
			FormatScope = TEXT("full_graph");
		}
	}
	if (!FormatScope.Equals(TEXT("full_graph"), ESearchCase::IgnoreCase)
		&& !FormatScope.Equals(TEXT("selection"), ESearchCase::IgnoreCase))
	{
		return UnrealAiToolJson::Error(TEXT("format_scope must be full_graph or selection"));
	}

	const UUnrealAiEditorSettings* AiSettings = GetDefault<UUnrealAiEditorSettings>();
	const FUnrealBlueprintGraphFormatOptions FmtOpts = UnrealAiBlueprintTools_MakeFormatOptionsFromSettings(AiSettings);

	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnBpFormatGraph", "Unreal AI: format Blueprint graph"));
	BP->Modify();
	Graph->Modify();

	UnrealAiBlueprintFormatterBridge::EnsureFormatterModuleLoaded(nullptr);
	const bool bFmt = UnrealAiBlueprintFormatterBridge::IsFormatterModuleReady();
	FUnrealBlueprintGraphFormatResult FormatResult;
	if (FormatScope.Equals(TEXT("selection"), ESearchCase::IgnoreCase))
	{
		TArray<UEdGraphNode*> Sel;
		FString SelErr;
		if (!UnrealAiTryGetBlueprintGraphSelectedNodes(BP, Graph, Sel, &SelErr))
		{
			return UnrealAiToolJson::Error(SelErr);
		}
		FormatResult = UnrealAiBlueprintFormatterBridge::TryLayoutSelectedNodes(Graph, Sel, true, FmtOpts);
	}
	else
	{
		FormatResult = UnrealAiBlueprintFormatterBridge::TryLayoutEntireGraph(Graph, true, FmtOpts);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("blueprint_path"), Path);
	O->SetStringField(TEXT("graph_name"), Graph->GetName());
	O->SetStringField(TEXT("format_scope"), FormatScope);
	O->SetBoolField(TEXT("formatter_available"), bFmt);
	O->SetBoolField(TEXT("layout_applied"), FormatResult.NodesPositioned > 0 || FormatResult.NodesMoved > 0);
	O->SetNumberField(TEXT("layout_nodes_positioned"), static_cast<double>(FormatResult.NodesPositioned));
	O->SetNumberField(TEXT("layout_nodes_moved"), static_cast<double>(FormatResult.NodesMoved));
	O->SetNumberField(TEXT("layout_nodes_skipped_preserve"), static_cast<double>(FormatResult.NodesSkippedPreserve));
	O->SetNumberField(TEXT("layout_entry_subgraphs"), static_cast<double>(FormatResult.EntrySubgraphs));
	O->SetNumberField(TEXT("layout_disconnected_nodes"), static_cast<double>(FormatResult.DisconnectedNodes));
	O->SetNumberField(TEXT("layout_data_only_nodes_placed"), static_cast<double>(FormatResult.DataOnlyNodesPlaced));
	O->SetNumberField(TEXT("layout_knots_inserted"), static_cast<double>(FormatResult.KnotsInserted));
	O->SetNumberField(TEXT("layout_comments_adjusted"), static_cast<double>(FormatResult.CommentsAdjusted));
	if (!bFmt)
	{
		O->SetStringField(TEXT("formatter_hint"), UnrealAiBlueprintFormatterBridge::FormatterInstallHint());
	}
	if (FormatResult.Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarnArr;
		for (const FString& W : FormatResult.Warnings)
		{
			WarnArr.Add(MakeShareable(new FJsonValueString(W)));
		}
		O->SetArrayField(TEXT("layout_warnings"), WarnArr);
	}
	const FString ToolMarkdown = FString::Printf(TEXT("### Blueprint format graph\n- Graph: **%s**\n"), *Graph->GetName());
	return UnrealAiToolJson::OkWithEditorPresentation(
		O,
		UnrealAiToolEditorNoteBuilders::MakeBlueprintToolNote(Path, Graph->GetName(), ToolMarkdown));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintFormatGraph(const TSharedPtr<FJsonObject>& Args)
{
	return DispatchBlueprintFormatLayout(Args, false);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintFormatSelection(const TSharedPtr<FJsonObject>& Args)
{
	return DispatchBlueprintFormatLayout(Args, true);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintGetGraphSummary(const TSharedPtr<FJsonObject>& Args)
{
	UnrealAiToolDispatchArgRepair::RepairBlueprintAssetPathArgs(Args);
	FString Path;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/MyBP.MyBP"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("blueprint_path is required. Discover Blueprints with asset_index_fuzzy_search (query + path_prefix) or asset_registry_query with path_filter/class_name."),
			TEXT("blueprint_get_graph_summary"),
			SuggestedArgs);
	}
	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);
	bool bIncludeLayoutAnalysis = false;
	Args->TryGetBoolField(TEXT("include_layout_analysis"), bIncludeLayoutAnalysis);
	FString LoadErr;
	UBlueprint* BP = UnrealAiBlueprintToolsPriv::LoadBlueprint(Path, &LoadErr);
	if (!BP)
	{
		return UnrealAiToolJson::Error(LoadErr.IsEmpty() ? UnrealAiBlueprintLoadHint(Path) : LoadErr);
	}
	TArray<UEdGraph*> Graphs;
	if (!GraphName.IsEmpty())
	{
		if (UEdGraph* G = UnrealAiBlueprintToolsPriv::FindGraphByName(BP, GraphName))
		{
			Graphs.Add(G);
		}
	}
	else
	{
		Graphs.Append(BP->UbergraphPages);
		Graphs.Append(BP->FunctionGraphs);
		Graphs.Append(BP->MacroGraphs);
	}
	TArray<TSharedPtr<FJsonValue>> GraphArr;
	for (UEdGraph* G : Graphs)
	{
		if (!G)
		{
			continue;
		}
		TSharedPtr<FJsonObject> Go = MakeShared<FJsonObject>();
		Go->SetStringField(TEXT("name"), G->GetName());
		int32 Nodes = 0;
		for (UEdGraphNode* N : G->Nodes)
		{
			if (N)
			{
				++Nodes;
			}
		}
		Go->SetNumberField(TEXT("node_count"), static_cast<double>(Nodes));
		if (bIncludeLayoutAnalysis)
		{
			UnrealBlueprintGraphLayoutAnalysis::AppendToJsonObject(G, Go);
		}
		GraphArr.Add(MakeShareable(new FJsonValueObject(Go.ToSharedRef())));
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetArrayField(TEXT("graphs"), GraphArr);
	O->SetNumberField(TEXT("graph_count"), static_cast<double>(GraphArr.Num()));

	TArray<FString> FirstNames;
	for (const TSharedPtr<FJsonValue>& V : GraphArr)
	{
		if (!V.IsValid())
		{
			continue;
		}
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!V->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
		{
			continue;
		}
		FString Name;
		if ((*Obj)->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
		{
			FirstNames.Add(Name);
		}
		if (FirstNames.Num() >= 8)
		{
			break;
		}
	}

	const FString GraphScope = GraphName.IsEmpty() ? TEXT("(all)") : GraphName;
	FString ToolMarkdown = FString::Printf(
		TEXT("### Blueprint graphs\n- Scope: %s\n- Returned graphs: %d\n"),
		*GraphScope,
		GraphArr.Num());
	if (FirstNames.Num() > 0)
	{
		ToolMarkdown += TEXT("- First graphs: ");
		ToolMarkdown += FString::Join(FirstNames, TEXT(", "));
		ToolMarkdown += TEXT("\n");
	}

	return UnrealAiToolJson::OkWithEditorPresentation(
		O,
		UnrealAiToolEditorNoteBuilders::MakeBlueprintToolNote(Path, GraphName, ToolMarkdown));
}


FUnrealAiToolInvocationResult UnrealAiDispatch_AnimBlueprintGetGraphSummary(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	if (!Args->TryGetStringField(TEXT("anim_blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("anim_blueprint_path is required"));
	}
	if (!LoadObject<UAnimBlueprint>(nullptr, *Path))
	{
		return UnrealAiToolJson::Error(TEXT("Could not load AnimBlueprint"));
	}
	TSharedPtr<FJsonObject> Fake = MakeShared<FJsonObject>();
	Fake->SetStringField(TEXT("blueprint_path"), Path);
	return UnrealAiDispatch_BlueprintGetGraphSummary(Fake);
}

UBlueprint* UnrealAiBlueprintTools_LoadBlueprintGame(const FString& BlueprintObjectPath, FString* OutFailureDetail)
{
	return UnrealAiBlueprintToolsPriv::LoadBlueprint(BlueprintObjectPath, OutFailureDetail);
}

bool UnrealAiBlueprintTools_IsGameWritableBlueprintPath(const FString& BlueprintObjectPath)
{
	return UnrealAiBlueprintToolsPriv::IsGameWritableBlueprintObjectPath(BlueprintObjectPath);
}

UEdGraph* UnrealAiBlueprintTools_FindGraphByName(UBlueprint* BP, const FString& GraphName)
{
	return UnrealAiBlueprintToolsPriv::FindGraphByName(BP, GraphName);
}

bool UnrealAiBlueprintTools_TryParsePinTypeFromString(const FString& TypeStr, FEdGraphPinType& OutType)
{
	return UnrealAiBlueprintToolsPriv::TryParsePinType(TypeStr, OutType);
}

FEdGraphPinType UnrealAiBlueprintTools_ParsePinTypeFromString(const FString& TypeStr)
{
	return UnrealAiBlueprintToolsPriv::ParsePinTypeWithBooleanFallback(TypeStr);
}

void UnrealAiFocusBlueprintEditor(const FString& BlueprintObjectPath, const FString& GraphName)
{
	using namespace UnrealAiBlueprintToolsPriv;
	if (BlueprintObjectPath.IsEmpty() || !GEditor)
	{
		return;
	}
	UBlueprint* BP = LoadBlueprint(BlueprintObjectPath);
	if (!BP)
	{
		return;
	}
	UnrealAiEditorNavigation::OpenAssetEditorPreferDocked(BP);
	FUnrealAiRecentUiTracker::RecordAgentToolNavigationToAssetPath(BlueprintObjectPath);

	FString FocusGraph = GraphName.TrimStartAndEnd();
	if (FocusGraph.IsEmpty())
	{
		FocusGraph = TEXT("EventGraph");
	}
	if (UEdGraph* G = FindGraphByName(BP, FocusGraph))
	{
		FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(G);
		return;
	}
	if (BP->UbergraphPages.Num() > 0 && BP->UbergraphPages[0])
	{
		FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(BP->UbergraphPages[0]);
	}
}
