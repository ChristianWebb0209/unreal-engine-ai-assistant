#include "Tools/UnrealAiToolDispatch_GenericAssets.h"

#include "Tools/UnrealAiToolJson.h"

#include "AssetRegistry/AssetIdentifier.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dom/JsonValue.h"
#include "Factories/Factory.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "UObject/Class.h"
#include "LevelSequence.h"

namespace UnrealAiGenericAssets
{
	static bool IsUnderGameContentPackage(const FString& InPackageOrObjectPath)
	{
		FString Pkg = InPackageOrObjectPath;
		Pkg.TrimStartAndEndInline();
		if (Pkg.IsEmpty())
		{
			return false;
		}
		if (Pkg.Contains(TEXT(".")))
		{
			Pkg = FPackageName::ObjectPathToPackageName(Pkg);
		}
		return Pkg.StartsWith(TEXT("/Game/"));
	}

	static UObject* LoadObjectInGame(const FString& ObjectPath, FString& OutErr)
	{
		if (!IsUnderGameContentPackage(ObjectPath))
		{
			OutErr = TEXT("object_path must be under /Game/ (writable content policy)");
			return nullptr;
		}
		UObject* Obj = LoadObject<UObject>(nullptr, *ObjectPath);
		if (!Obj)
		{
			OutErr = FString::Printf(TEXT("Could not load object: %s"), *ObjectPath);
		}
		return Obj;
	}

	static bool ShouldExportProperty(const FProperty* Prop)
	{
		if (!Prop || Prop->HasAnyPropertyFlags(CPF_Transient) || Prop->HasAnyPropertyFlags(CPF_SkipSerialization))
		{
			return false;
		}
		if (!Prop->HasAnyPropertyFlags(CPF_Edit))
		{
			return false;
		}
		return true;
	}

	static bool ExportPropertyJson(const FProperty* Prop, const void* Addr, UObject* Owner, TSharedPtr<FJsonValue>& Out)
	{
		if (const FBoolProperty* BP = CastField<FBoolProperty>(Prop))
		{
			Out = MakeShareable(new FJsonValueBoolean(BP->GetPropertyValue(Addr)));
			return true;
		}
		if (const FNumericProperty* NP = CastField<FNumericProperty>(Prop))
		{
			if (NP->IsFloatingPoint())
			{
				Out = MakeShareable(new FJsonValueNumber(NP->GetFloatingPointPropertyValue(Addr)));
				return true;
			}
			if (NP->IsInteger())
			{
				Out = MakeShareable(new FJsonValueNumber(static_cast<double>(NP->GetSignedIntPropertyValue(Addr))));
				return true;
			}
		}
		if (const FStrProperty* SP = CastField<FStrProperty>(Prop))
		{
			Out = MakeShareable(new FJsonValueString(SP->GetPropertyValue(Addr)));
			return true;
		}
		if (const FNameProperty* NP = CastField<FNameProperty>(Prop))
		{
			Out = MakeShareable(new FJsonValueString(NP->GetPropertyValue(Addr).ToString()));
			return true;
		}
		if (const FTextProperty* TP = CastField<FTextProperty>(Prop))
		{
			Out = MakeShareable(new FJsonValueString(TP->GetPropertyValue(Addr).ToString()));
			return true;
		}
		if (const FEnumProperty* EP = CastField<FEnumProperty>(Prop))
		{
			if (const FNumericProperty* Under = EP->GetUnderlyingProperty())
			{
				const int64 V = Under->GetSignedIntPropertyValue(Addr);
				if (UEnum* En = EP->GetEnum())
				{
					Out = MakeShareable(new FJsonValueString(En->GetNameStringByValue(V)));
					return true;
				}
			}
			return false;
		}
		if (const FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Prop))
		{
			UObject* ObjVal = OP->GetObjectPropertyValue(Addr);
			if (!ObjVal)
			{
				Out = MakeShareable(new FJsonValueNull());
				return true;
			}
			Out = MakeShareable(new FJsonValueString(ObjVal->GetPathName()));
			return true;
		}
		if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			const void* StructAddr = Addr;
			FString Exported;
			StructProp->ExportTextItem_Direct(Exported, StructAddr, StructAddr, Owner, PPF_None);
			Out = MakeShareable(new FJsonValueString(Exported));
			return true;
		}

		FString Exported;
		Prop->ExportTextItem_Direct(Exported, Addr, Addr, Owner, PPF_None);
		if (Exported.IsEmpty())
		{
			return false;
		}
		Out = MakeShareable(new FJsonValueString(Exported));
		return true;
	}

	static bool ApplyPropertyFromJson(const FProperty* Prop, void* Addr, UObject* Owner, const TSharedPtr<FJsonValue>& Val, TArray<FString>& OutErrors)
	{
		const FString FieldName = Prop->GetName();
		if (!Val.IsValid())
		{
			OutErrors.Add(FString::Printf(TEXT("%s: missing value"), *FieldName));
			return false;
		}
		if (const FBoolProperty* BP = CastField<FBoolProperty>(Prop))
		{
			bool B = false;
			if (Val->TryGetBool(B))
			{
				BP->SetPropertyValue(Addr, B);
				return true;
			}
			OutErrors.Add(FString::Printf(TEXT("%s: expected bool"), *FieldName));
			return false;
		}
		if (const FNumericProperty* NP = CastField<FNumericProperty>(Prop))
		{
			double Num = 0;
			if (!Val->TryGetNumber(Num))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: expected number"), *FieldName));
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
			OutErrors.Add(FString::Printf(TEXT("%s: unsupported number storage type"), *FieldName));
			return false;
		}
		if (const FStrProperty* SP = CastField<FStrProperty>(Prop))
		{
			FString S;
			if (Val->TryGetString(S))
			{
				SP->SetPropertyValue(Addr, MoveTemp(S));
				return true;
			}
			OutErrors.Add(FString::Printf(TEXT("%s: expected string"), *FieldName));
			return false;
		}
		if (const FNameProperty* NP = CastField<FNameProperty>(Prop))
		{
			FString S;
			if (Val->TryGetString(S))
			{
				NP->SetPropertyValue(Addr, FName(*S));
				return true;
			}
			OutErrors.Add(FString::Printf(TEXT("%s: expected string (name)"), *FieldName));
			return false;
		}
		if (const FTextProperty* TP = CastField<FTextProperty>(Prop))
		{
			FString S;
			if (Val->TryGetString(S))
			{
				TP->SetPropertyValue(Addr, FText::FromString(S));
				return true;
			}
			OutErrors.Add(FString::Printf(TEXT("%s: expected string (text)"), *FieldName));
			return false;
		}
		if (const FEnumProperty* EP = CastField<FEnumProperty>(Prop))
		{
			FString S;
			if (!Val->TryGetString(S))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: expected enum string"), *FieldName));
				return false;
			}
			if (UEnum* En = EP->GetEnum())
			{
				int64 Ev = En->GetValueByName(FName(*S));
				if (Ev == INDEX_NONE)
				{
					OutErrors.Add(FString::Printf(TEXT("%s: unknown enum value %s"), *FieldName, *S));
					return false;
				}
				if (FNumericProperty* Under = EP->GetUnderlyingProperty())
				{
					Under->SetIntPropertyValue(Addr, Ev);
					return true;
				}
			}
			return false;
		}
		if (const FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Prop))
		{
			FString S;
			if (Val->Type == EJson::Null)
			{
				const_cast<FObjectPropertyBase*>(OP)->SetObjectPropertyValue(Addr, nullptr);
				return true;
			}
			if (!Val->TryGetString(S) || S.IsEmpty())
			{
				OutErrors.Add(FString::Printf(TEXT("%s: expected object path string or null"), *FieldName));
				return false;
			}
			UObject* Resolved = LoadObject<UObject>(nullptr, *S);
			if (!Resolved)
			{
				OutErrors.Add(FString::Printf(TEXT("%s: could not load %s"), *FieldName, *S));
				return false;
			}
			if (OP->PropertyClass && !Resolved->IsA(OP->PropertyClass))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: type mismatch for %s"), *FieldName, *S));
				return false;
			}
			const_cast<FObjectPropertyBase*>(OP)->SetObjectPropertyValue(Addr, Resolved);
			return true;
		}
		if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			FString S;
			if (!Val->TryGetString(S))
			{
				OutErrors.Add(FString::Printf(TEXT("%s: expected struct export-text string"), *FieldName));
				return false;
			}
			void* StructAddr = Addr;
			const TCHAR* Buf = *S;
			const TCHAR* NewBuf = StructProp->ImportText_Direct(Buf, StructAddr, Owner, PPF_None);
			return NewBuf != nullptr;
		}

		FString S;
		if (!Val->TryGetString(S))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: use string ExportText form for this property type"), *FieldName));
			return false;
		}
		const TCHAR* Buf = *S;
		void* PropAddr = Addr;
		const TCHAR* NewBuf = Prop->ImportText_Direct(Buf, PropAddr, Owner, PPF_None);
		return NewBuf != nullptr;
	}
} // namespace UnrealAiGenericAssets

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetCreate(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiGenericAssets;
	FString PackagePath;
	FString AssetName;
	FString ClassPath;
	FString FactoryClassPath;
	if (!Args->TryGetStringField(TEXT("package_path"), PackagePath) || PackagePath.IsEmpty()
		|| !Args->TryGetStringField(TEXT("asset_name"), AssetName) || AssetName.IsEmpty()
		|| !Args->TryGetStringField(TEXT("asset_class"), ClassPath) || ClassPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("package_path, asset_name, and asset_class are required"));
	}
	if (!IsUnderGameContentPackage(PackagePath))
	{
		return UnrealAiToolJson::Error(TEXT("package_path must be under /Game/"));
	}
	Args->TryGetStringField(TEXT("factory_class"), FactoryClassPath);

	UClass* AssetClass = FindObject<UClass>(nullptr, *ClassPath);
	if (!AssetClass)
	{
		AssetClass = LoadObject<UClass>(nullptr, *ClassPath);
	}
	if (!AssetClass || !AssetClass->IsChildOf(UObject::StaticClass()))
	{
		return UnrealAiToolJson::Error(TEXT("Could not resolve asset_class"));
	}

	UFactory* Factory = nullptr;
	if (!FactoryClassPath.IsEmpty())
	{
		UClass* FC = LoadObject<UClass>(nullptr, *FactoryClassPath);
		if (!FC || !FC->IsChildOf(UFactory::StaticClass()))
		{
			return UnrealAiToolJson::Error(TEXT("factory_class must be a UFactory subclass"));
		}
		Factory = NewObject<UFactory>(GetTransientPackage(), FC);
	}

	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnAssetCreate", "Unreal AI: create asset"));
	UObject* NewObj = IAssetTools::Get().CreateAsset(AssetName, PackagePath, AssetClass, Factory);
	if (!NewObj)
	{
		return UnrealAiToolJson::Error(TEXT("CreateAsset failed (check package path, name, class, factory)"));
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("object_path"), NewObj->GetPathName());
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetExportProperties(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiGenericAssets;
	FString Path;
	if (!Args->TryGetStringField(TEXT("object_path"), Path) || Path.IsEmpty())
	{
		Args->TryGetStringField(TEXT("path"), Path);
	}
	if (Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("object_path is required"));
	}
	FString Err;
	UObject* Obj = LoadObjectInGame(Path, Err);
	if (!Obj)
	{
		return UnrealAiToolJson::Error(Err);
	}
	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
	int32 Exported = 0;
	for (TFieldIterator<FProperty> It(Obj->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		const FProperty* Prop = *It;
		if (!ShouldExportProperty(Prop))
		{
			continue;
		}
		const void* Addr = Prop->ContainerPtrToValuePtr<void>(Obj);
		TSharedPtr<FJsonValue> Jv;
		if (ExportPropertyJson(Prop, Addr, Obj, Jv))
		{
			Props->SetField(Prop->GetName(), Jv);
			++Exported;
		}
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("object_path"), Obj->GetPathName());
	O->SetObjectField(TEXT("properties"), Props);
	O->SetNumberField(TEXT("exported_count"), static_cast<double>(Exported));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetApplyProperties(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiGenericAssets;
	FString Path;
	if (!Args->TryGetStringField(TEXT("object_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("object_path is required"));
	}
	const TSharedPtr<FJsonObject>* Delta = nullptr;
	if (!Args->TryGetObjectField(TEXT("properties"), Delta) || !Delta || !(*Delta).IsValid())
	{
		return UnrealAiToolJson::Error(TEXT("properties object is required"));
	}
	bool bDryRun = false;
	Args->TryGetBoolField(TEXT("dry_run"), bDryRun);

	FString Err;
	UObject* Obj = LoadObjectInGame(Path, Err);
	if (!Obj)
	{
		return UnrealAiToolJson::Error(Err);
	}

	TArray<FString> ApplyErrors;
	FScopedTransaction Txn(
		NSLOCTEXT("UnrealAiEditor", "TxnApplyProps", "Unreal AI: apply properties"),
		!bDryRun);

	if (!bDryRun)
	{
		Obj->Modify();
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Delta)->Values)
	{
		FProperty* Prop = FindFProperty<FProperty>(Obj->GetClass(), FName(*Pair.Key));
		if (!Prop || !ShouldExportProperty(Prop))
		{
			ApplyErrors.Add(FString::Printf(TEXT("%s: not editable or unknown property"), *Pair.Key));
			continue;
		}
		void* Addr = Prop->ContainerPtrToValuePtr<void>(Obj);
		if (!ApplyPropertyFromJson(Prop, Addr, Obj, Pair.Value, ApplyErrors))
		{
			continue;
		}
	}

	if (ApplyErrors.Num() > 0)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), false);
		O->SetStringField(TEXT("status"), TEXT("apply_errors"));
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& E : ApplyErrors)
		{
			Arr.Add(MakeShareable(new FJsonValueString(E)));
		}
		O->SetArrayField(TEXT("errors"), Arr);
		FUnrealAiToolInvocationResult R;
		R.bOk = false;
		R.ErrorMessage = TEXT("One or more properties failed to apply");
		R.ContentForModel = UnrealAiToolJson::SerializeObject(O);
		return R;
	}

	if (!bDryRun)
	{
		Obj->MarkPackageDirty();
		Obj->PostEditChange();
	}

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("object_path"), Obj->GetPathName());
	O->SetBoolField(TEXT("dry_run"), bDryRun);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetFindReferencers(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	if (!Args->TryGetStringField(TEXT("object_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("object_path is required"));
	}
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AR = ARM.Get();
	const FSoftObjectPath SOP(Path);
	const FAssetData AD = AR.GetAssetByObjectPath(SOP);
	if (!AD.IsValid())
	{
		return UnrealAiToolJson::Error(TEXT("Asset not found"));
	}
	const FAssetIdentifier Id(AD.PackageName, AD.AssetName);
	TArray<FAssetIdentifier> Referencers;
	AR.GetReferencers(Id, Referencers);
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FAssetIdentifier& R : Referencers)
	{
		Arr.Add(MakeShareable(new FJsonValueString(R.ToString())));
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetArrayField(TEXT("referencers"), Arr);
	O->SetNumberField(TEXT("count"), static_cast<double>(Arr.Num()));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetGetDependencies(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	if (!Args->TryGetStringField(TEXT("object_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("object_path is required"));
	}
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AR = ARM.Get();
	const FSoftObjectPath SOP(Path);
	const FAssetData AD = AR.GetAssetByObjectPath(SOP);
	if (!AD.IsValid())
	{
		return UnrealAiToolJson::Error(TEXT("Asset not found"));
	}
	const FAssetIdentifier Id(AD.PackageName, AD.AssetName);
	TArray<FAssetIdentifier> Deps;
	AR.GetDependencies(Id, Deps);
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FAssetIdentifier& D : Deps)
	{
		Arr.Add(MakeShareable(new FJsonValueString(D.ToString())));
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetArrayField(TEXT("dependencies"), Arr);
	O->SetNumberField(TEXT("count"), static_cast<double>(Arr.Num()));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_LevelSequenceCreateAsset(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiGenericAssets;
	FString PackagePath;
	FString AssetName;
	if (!Args->TryGetStringField(TEXT("package_path"), PackagePath) || PackagePath.IsEmpty()
		|| !Args->TryGetStringField(TEXT("asset_name"), AssetName) || AssetName.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("package_path and asset_name are required"));
	}
	if (!IsUnderGameContentPackage(PackagePath))
	{
		return UnrealAiToolJson::Error(TEXT("package_path must be under /Game/"));
	}
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnLSCreate", "Unreal AI: create Level Sequence"));
	UObject* NewObj =
		IAssetTools::Get().CreateAsset(AssetName, PackagePath, ULevelSequence::StaticClass(), nullptr);
	if (!NewObj)
	{
		return UnrealAiToolJson::Error(TEXT("CreateAsset failed for LevelSequence"));
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("object_path"), NewObj->GetPathName());
	return UnrealAiToolJson::Ok(O);
}
