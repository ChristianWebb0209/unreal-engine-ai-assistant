#include "Tools/UnrealAiToolDispatch_GenericAssets.h"

#include "Context/UnrealAiProjectId.h"
#include "Context/UnrealAiProjectTreeSampler.h"
#include "Tools/UnrealAiToolJson.h"
#include "Tools/UnrealAiToolDispatch_ArgRepair.h"
#include "Tools/UnrealAiAssetFactoryResolver.h"

#include "AssetRegistry/AssetIdentifier.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Factories/Factory.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "FileHelpers.h"
#include "ScopedTransaction.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"
#include "UObject/Class.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/Engine.h"
#include "Editor.h"
#include "LevelSequence.h"
#include "Materials/MaterialInterface.h"

namespace UnrealAiGenericAssets
{
	static bool IsCommonTextOrWidgetPropertyMistakeOnMaterial(const FString& Key)
	{
		// Models often try to set Text/UMG fields on UMaterial — these are not material properties.
		if (Key.Equals(TEXT("Text"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("HorizontalAlignment"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("VerticalAlignment"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("Font"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("FontSize"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("WorldSize"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("TextWorldSize"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("ColorAndOpacity"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("Justification"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("LineBreakStrategy"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		return false;
	}

	static FUnrealAiToolInvocationResult BuildMaterialTextPropertyConfusionError(UObject* Obj, const TArray<FString>& BadKeys)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), false);
		O->SetStringField(TEXT("status"), TEXT("material_text_property_confusion"));
		O->SetStringField(
			TEXT("message"),
			TEXT("asset_apply_properties: property name(s) look like Text/UMG widget fields, not Material/MaterialInstance properties. ")
			TEXT("Use a TextRenderActor (or UMG) in the level for visible text; use asset_apply_properties only for material parameters (e.g. BaseColor, Roughness, scalar/vector params)."));
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& K : BadKeys)
		{
			Arr.Add(MakeShareable(new FJsonValueString(K)));
		}
		O->SetArrayField(TEXT("invalid_keys_for_material"), Arr);
		O->SetStringField(TEXT("object_path"), Obj->GetPathName());
		O->SetStringField(TEXT("object_class"), Obj->GetClass()->GetName());
		TSharedPtr<FJsonObject> SuggestedCall = MakeShared<FJsonObject>();
		SuggestedCall->SetStringField(TEXT("tool"), TEXT("actor_spawn_from_class"));
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.TextRenderActor"));
		SuggestedCall->SetObjectField(TEXT("args"), SuggestedArgs);
		O->SetObjectField(TEXT("suggested_correct_call"), SuggestedCall);
		FUnrealAiToolInvocationResult R;
		R.bOk = false;
		R.ErrorMessage = UnrealAiToolJson::SerializeObject(O);
		R.ContentForModel = R.ErrorMessage;
		return R;
	}

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
		// `/Game` is valid content root (CreateAsset accepts it); only `/Game/...` matched before.
		if (Pkg == TEXT("/Game"))
		{
			return true;
		}
		return Pkg.StartsWith(TEXT("/Game/"));
	}

	static FString BuildExpectedObjectPath(const FString& PackagePath, const FString& AssetName)
	{
		// Example: PackagePath=/Game/Foo, AssetName=Bar -> /Game/Foo/Bar.Bar
		return PackagePath + TEXT("/") + AssetName + TEXT(".") + AssetName;
	}

	static bool IsBlueprintAssetMissingGeneratedClass(const FAssetData& AD)
	{
		if (!AD.IsValid())
		{
			return false;
		}
		const auto BlueprintClassPath = UBlueprint::StaticClass()->GetClassPathName();
		if (AD.AssetClassPath != BlueprintClassPath)
		{
			return false;
		}
		FString GeneratedClassTag;
		if (!AD.GetTagValue(FName(TEXT("GeneratedClass")), GeneratedClassTag))
		{
			return true;
		}
		GeneratedClassTag.TrimStartAndEndInline();
		return GeneratedClassTag.IsEmpty() || GeneratedClassTag.Equals(TEXT("None"), ESearchCase::IgnoreCase)
			|| GeneratedClassTag.Equals(TEXT("null"), ESearchCase::IgnoreCase);
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
	const TArray<const TCHAR*> PackageAliases = { TEXT("path") };
	const TArray<const TCHAR*> NameAliases = { TEXT("name"), TEXT("object_name") };
	const TArray<const TCHAR*> ClassAliases = { TEXT("class_path") };
	const bool bHasPackagePath = UnrealAiToolDispatchArgRepair::TryGetStringFieldCanonical(
		Args,
		TEXT("package_path"),
		PackageAliases,
		PackagePath);
	const bool bHasAssetName = UnrealAiToolDispatchArgRepair::TryGetStringFieldCanonical(
		Args,
		TEXT("asset_name"),
		NameAliases,
		AssetName);
	const bool bHasAssetClass = UnrealAiToolDispatchArgRepair::TryGetStringFieldCanonical(
		Args,
		TEXT("asset_class"),
		ClassAliases,
		ClassPath);
	if ((PackagePath.IsEmpty() || !bHasPackagePath) && !ClassPath.IsEmpty() && ClassPath.Contains(TEXT("Blueprint"), ESearchCase::IgnoreCase))
	{
		const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
		const FString Preferred = UnrealAiProjectTreeSampler::GetPreferredPackagePathForProject(
			ProjectId,
			TEXT("blueprint"),
			TEXT("/Game/Blueprints"));
		if (!Preferred.IsEmpty())
		{
			PackagePath = Preferred;
			Args->SetStringField(TEXT("package_path"), PackagePath);
		}
	}
	if (!bHasPackagePath || PackagePath.IsEmpty()
		|| !bHasAssetName || AssetName.IsEmpty()
		|| !bHasAssetClass || ClassPath.IsEmpty())
	{
		if (PackagePath.IsEmpty() || AssetName.IsEmpty() || ClassPath.IsEmpty())
		{
			TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
			SuggestedArgs->SetStringField(TEXT("package_path"), TEXT("/Game/Blueprints"));
			SuggestedArgs->SetStringField(TEXT("asset_name"), TEXT("DoorBP"));
			SuggestedArgs->SetStringField(TEXT("asset_class"), TEXT("/Script/Engine.Blueprint"));
			return UnrealAiToolJson::ErrorWithSuggestedCall(
				TEXT("package_path, asset_name, and asset_class are required (alias: class_path)."),
				TEXT("asset_create"),
				SuggestedArgs);
		}
	}
	if (!IsUnderGameContentPackage(PackagePath))
	{
		return UnrealAiToolJson::Error(TEXT("package_path must be /Game or a path under /Game/ (e.g. /Game/Blueprints)"));
	}
	Args->TryGetStringField(TEXT("factory_class"), FactoryClassPath);

	UClass* AssetClass = FindObject<UClass>(nullptr, *ClassPath);
	if (!AssetClass)
	{
		AssetClass = LoadObject<UClass>(nullptr, *ClassPath);
	}
	if (!AssetClass || !AssetClass->IsChildOf(UObject::StaticClass()))
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("package_path"), PackagePath.IsEmpty() ? TEXT("/Game") : PackagePath);
		SuggestedArgs->SetStringField(TEXT("asset_name"), AssetName.IsEmpty() ? TEXT("NewAsset") : AssetName);
		SuggestedArgs->SetStringField(TEXT("asset_class"), TEXT("/Script/Engine.Blueprint"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("Could not resolve asset_class. Use a native UClass path like /Script/Engine.Blueprint, /Script/Engine.Material, /Script/Engine.World, /Script/LevelSequence.LevelSequence, or /Script/Engine.Actor."),
			TEXT("asset_create"),
			SuggestedArgs);
	}
	if (AssetClass->IsChildOf(AActor::StaticClass()) && AssetClass != UBlueprint::StaticClass())
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("package_path"), PackagePath.IsEmpty() ? TEXT("/Game/Blueprints") : PackagePath);
		SuggestedArgs->SetStringField(TEXT("asset_name"), AssetName.IsEmpty() ? TEXT("MyCharacterBP") : AssetName);
		SuggestedArgs->SetStringField(TEXT("asset_class"), TEXT("/Script/Engine.Blueprint"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("asset_create expects an asset class (UObject type), not an actor gameplay class. For Character/Pawn gameplay assets, create a Blueprint asset (/Script/Engine.Blueprint) and set parent_class in blueprint_apply_ir."),
			TEXT("asset_create"),
			SuggestedArgs);
	}
	if (AssetClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return UnrealAiToolJson::Error(FString::Printf(
			TEXT("asset_create: asset_class is abstract and cannot be instantiated (%s). Use a concrete subclass, or /Script/Engine.Blueprint with parent_class set to a concrete type."),
			*ClassPath));
	}

	// Map/world creation special-case.
	// This repo's non-interactive factory resolver often can't instantiate UWorld/ULevel,
	// but gameplay tasks need a fresh editor world so subsequent actor/spawn and actor search tools work.
	{
		const bool bWantsMap =
			ClassPath.Equals(TEXT("/Script/Engine.World"), ESearchCase::IgnoreCase)
			|| ClassPath.Equals(TEXT("/Script/Engine.Level"), ESearchCase::IgnoreCase);

		if (bWantsMap)
		{
			UClass* EffectiveWorldClass = UWorld::StaticClass();
			if (!EffectiveWorldClass)
			{
				return UnrealAiToolJson::Error(TEXT("asset_create: UWorld::StaticClass unavailable"));
			}

			// Ensure consistent object path for maps.
			const FString ExpectedObjPath = BuildExpectedObjectPath(PackagePath, AssetName);
			if (UObject* ExistingObj = LoadObject<UObject>(nullptr, *ExpectedObjPath))
			{
				if (ExistingObj && ExistingObj->IsA(EffectiveWorldClass))
				{
					// Best-effort open the map so this becomes the active editor world.
					if (GEngine && GEditor)
					{
						const FString MapLongPackageName = PackagePath + TEXT("/") + AssetName; // /Game/.../MapName
						(void)GEngine->Exec(nullptr, *FString::Printf(TEXT("open %s"), *MapLongPackageName), *GLog);
					}

					TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetBoolField(TEXT("ok"), true);
					O->SetStringField(TEXT("object_path"), ExistingObj->GetPathName());
					O->SetBoolField(TEXT("already_existed"), true);
					return UnrealAiToolJson::Ok(O);
				}

				return UnrealAiToolJson::Error(TEXT("asset_create: asset already exists with a different class"));
			}

			const FString PackageName = PackagePath + TEXT("/") + AssetName; // /Game/.../MapName (package, not object)
			UPackage* Pkg = CreatePackage(*PackageName);
			if (!Pkg)
			{
				return UnrealAiToolJson::Error(TEXT("asset_create: failed to CreatePackage for map/world"));
			}
			Pkg->FullyLoad();

			UWorld* NewWorld = NewObject<UWorld>(Pkg, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
			if (!NewWorld)
			{
				return UnrealAiToolJson::Error(TEXT("asset_create: failed to allocate new UWorld"));
			}

			// Initialize and ensure PersistentLevel exists.
			// We keep this minimal and best-effort across UE minor versions.
			{
				// Some UE versions provide InitializeNewWorld; if available, it typically creates PersistentLevel.
				// If the signature differs in your UE version, the build will surface it and we can adjust.
				NewWorld->InitializeNewWorld(UWorld::InitializationValues());
			}

			if (!NewWorld->PersistentLevel)
			{
				ULevel* Persistent = NewObject<ULevel>(NewWorld, NAME_PersistentLevel, RF_Public | RF_Standalone | RF_Transactional);
				if (!Persistent)
				{
					return UnrealAiToolJson::Error(TEXT("asset_create: failed to create PersistentLevel for blank map/world"));
				}
				Persistent->OwningWorld = NewWorld;
				NewWorld->PersistentLevel = Persistent;
				NewWorld->SetCurrentLevel(Persistent);
			}

			Pkg->MarkPackageDirty();
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			ARM.Get().AssetCreated(NewWorld);

			// Save packages so the editor can open the new map.
			{
				// In headless / automation scenarios we avoid prompts and save dirty packages.
				// This is best-effort: it may save more than just the newly created world.
				(void)FEditorFileUtils::SaveDirtyPackages(false, true, true, true, false, false);
			}

			// Open the created map (best-effort) so actor spawn/search tools target it.
			if (GEngine && GEditor)
			{
				const FString MapLongPackageName = PackagePath + TEXT("/") + AssetName; // /Game/.../MapName
				(void)GEngine->Exec(nullptr, *FString::Printf(TEXT("open %s"), *MapLongPackageName), *GLog);
			}

			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("ok"), true);
			O->SetStringField(TEXT("object_path"), NewWorld->GetPathName());
			return UnrealAiToolJson::Ok(O);
		}
	}

	// If the asset already exists, return success to prevent repeated CreateAsset failures
	// from consuming agent LLM rounds.
	{
		const FString ExpectedObjPath = BuildExpectedObjectPath(PackagePath, AssetName);
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		const FAssetData AD = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(ExpectedObjPath));
		if (AD.IsValid())
		{
			if (IsBlueprintAssetMissingGeneratedClass(AD))
			{
				return UnrealAiToolJson::Error(FString::Printf(
					TEXT("asset_create: existing blueprint asset is invalid (GeneratedClass missing): %s"),
					*ExpectedObjPath));
			}
			UObject* ExistingObj = LoadObject<UObject>(nullptr, *ExpectedObjPath);
			if (ExistingObj && ExistingObj->IsA(AssetClass))
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetBoolField(TEXT("ok"), true);
				O->SetStringField(TEXT("object_path"), ExistingObj->GetPathName());
				O->SetBoolField(TEXT("already_existed"), true);
				return UnrealAiToolJson::Ok(O);
			}

			return UnrealAiToolJson::Error(TEXT("asset_create: asset already exists with a different class"));
		}
	}

	// Resolve / infer a non-interactive factory when omitted.
	const FUnrealAiAssetFactoryResolver::FResolveResult FactoryRes =
		FUnrealAiAssetFactoryResolver::Resolve(AssetClass, FactoryClassPath);
	if (!FactoryRes.Factory)
	{
		// Return a deterministic suggested_correct_call that includes a best-guess factory_class when available.
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("package_path"), PackagePath);
		SuggestedArgs->SetStringField(TEXT("asset_name"), AssetName);
		SuggestedArgs->SetStringField(TEXT("asset_class"), ClassPath);
		if (!FactoryRes.FactoryClassPath.IsEmpty())
		{
			SuggestedArgs->SetStringField(TEXT("factory_class"), FactoryRes.FactoryClassPath);
		}

		FString Msg = TEXT("Could not resolve a non-interactive factory for asset_create.");
		if (FactoryRes.Notes.Num() > 0)
		{
			Msg += TEXT(" notes=");
			Msg += FString::Join(FactoryRes.Notes, TEXT(" | "));
		}
		if (FactoryRes.Candidates.Num() > 0)
		{
			Msg += TEXT(" candidates=");
			Msg += FString::Join(FactoryRes.Candidates, TEXT(", "));
		}
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			Msg,
			TEXT("asset_create"),
			SuggestedArgs);
	}
	UFactory* Factory = FactoryRes.Factory;

	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnAssetCreate", "Unreal AI: create asset"));
	UObject* NewObj = IAssetTools::Get().CreateAsset(AssetName, PackagePath, AssetClass, Factory);
	if (!NewObj)
	{
		// Best-effort: if the asset exists now, treat it as success.
		{
			const FString ExpectedObjPath = BuildExpectedObjectPath(PackagePath, AssetName);
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			const FAssetData AD = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(ExpectedObjPath));
			if (AD.IsValid())
			{
				if (IsBlueprintAssetMissingGeneratedClass(AD))
				{
					return UnrealAiToolJson::Error(FString::Printf(
						TEXT("asset_create: existing blueprint asset is invalid (GeneratedClass missing): %s"),
						*ExpectedObjPath));
				}
				UObject* ExistingObj = LoadObject<UObject>(nullptr, *ExpectedObjPath);
				if (ExistingObj && ExistingObj->IsA(AssetClass))
				{
					TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetBoolField(TEXT("ok"), true);
					O->SetStringField(TEXT("object_path"), ExistingObj->GetPathName());
					O->SetBoolField(TEXT("already_existed"), true);
					return UnrealAiToolJson::Ok(O);
				}
			}
		}

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
	{
		const TArray<const TCHAR*> Aliases = { TEXT("path") };
		if (!UnrealAiToolDispatchArgRepair::TryGetStringFieldCanonical(
				Args,
				TEXT("object_path"),
				Aliases,
				Path)
			|| Path.IsEmpty())
		{
			TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
			SuggestedArgs->SetStringField(TEXT("object_path"), TEXT("/Game/Blueprints/MyBP.MyBP"));
			return UnrealAiToolJson::ErrorWithSuggestedCall(
				TEXT("object_path is required (alias: path)."),
				TEXT("asset_export_properties"),
				SuggestedArgs);
		}
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
	const TArray<const TCHAR*> PathAliases = { TEXT("path"), TEXT("asset_path"), TEXT("object") };
	if (!UnrealAiToolDispatchArgRepair::TryGetStringFieldCanonical(Args, TEXT("object_path"), PathAliases, Path) || Path.IsEmpty())
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

	if (Cast<UMaterialInterface>(Obj))
	{
		TArray<FString> BadKeys;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Delta)->Values)
		{
			if (IsCommonTextOrWidgetPropertyMistakeOnMaterial(Pair.Key))
			{
				BadKeys.Add(Pair.Key);
			}
		}
		if (BadKeys.Num() > 0)
		{
			return BuildMaterialTextPropertyConfusionError(Obj, BadKeys);
		}
	}

	TArray<FString> ApplyErrors;
	TArray<FString> IgnoredProperties;
	FScopedTransaction Txn(
		NSLOCTEXT("UnrealAiEditor", "TxnApplyProps", "Unreal AI: apply properties"),
		!bDryRun);

	if (!bDryRun)
	{
		Obj->Modify();
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Delta)->Values)
	{
		// Naming is usually not an editable/reflection-safe property on Unreal assets.
		// Agents often try to set it after creation; treat these keys as no-ops.
		{
			const FString K = Pair.Key;
			if (K.Equals(TEXT("AssetName"), ESearchCase::IgnoreCase) //
				|| K.Equals(TEXT("ObjectName"), ESearchCase::IgnoreCase))
			{
				IgnoredProperties.Add(K);
				continue;
			}
		}

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
		// If everything failed due to unknown/non-editable properties, suggest exporting properties first.
		bool bAllUnknown = true;
		for (const FString& E : ApplyErrors)
		{
			if (!E.Contains(TEXT("not editable or unknown property")))
			{
				bAllUnknown = false;
				break;
			}
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), false);
		O->SetStringField(TEXT("status"), TEXT("apply_errors"));
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& E : ApplyErrors)
		{
			Arr.Add(MakeShareable(new FJsonValueString(E)));
		}
		O->SetArrayField(TEXT("errors"), Arr);
		if (bAllUnknown)
		{
			TSharedPtr<FJsonObject> SuggestedCall = MakeShared<FJsonObject>();
			SuggestedCall->SetStringField(TEXT("tool"), TEXT("asset_export_properties"));
			TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
			SuggestedArgs->SetStringField(TEXT("object_path"), Obj->GetPathName());
			SuggestedCall->SetObjectField(TEXT("args"), SuggestedArgs);
			O->SetObjectField(TEXT("suggested_correct_call"), SuggestedCall);
		}
		FUnrealAiToolInvocationResult R;
		R.bOk = false;
		// Harness consumes ErrorMessage when bOk=false, so include structured details.
		R.ErrorMessage = UnrealAiToolJson::SerializeObject(O);
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
	if (IgnoredProperties.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> IgnArr;
		for (const FString& P : IgnoredProperties)
		{
			IgnArr.Add(MakeShareable(new FJsonValueString(P)));
		}
		O->SetArrayField(TEXT("ignored_properties"), IgnArr);
	}
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetFindReferencers(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	Args->TryGetStringField(TEXT("object_path"), Path);
	if (Path.IsEmpty())
	{
		// Common model mistake: uses `path` (as with content_browser_sync_asset) instead of `object_path`.
		Args->TryGetStringField(TEXT("path"), Path);
	}
	if (Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("object_path is required (asset object path, e.g. /Game/Folder/Asset.Asset)"));
	}
	UnrealAiToolDispatchArgRepair::NormalizeAssetLikeObjectPath(Path);
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AR = ARM.Get();
	const FSoftObjectPath SOP(Path);
	const FAssetData AD = AR.GetAssetByObjectPath(SOP);
	if (!AD.IsValid())
	{
		// Best-effort fuzzy recovery: ask for concrete object paths under /Game.
		FString Query;
		{
			int32 LastSlash = INDEX_NONE;
			if (Path.FindLastChar(TEXT('/'), LastSlash) && LastSlash > 0 && LastSlash < Path.Len() - 1)
			{
				FString Leaf = Path.Mid(LastSlash + 1);
				int32 LastDot = INDEX_NONE;
				if (Leaf.FindLastChar(TEXT('.'), LastDot) && LastDot > 0 && LastDot < Leaf.Len() - 1)
				{
					Leaf = Leaf.Left(LastDot);
				}
				Leaf.TrimStartAndEndInline();
				Query = Leaf;
			}
		}
		if (Query.IsEmpty())
		{
			Query = TEXT("Asset");
		}
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("query"), Query);
		SuggestedArgs->SetStringField(TEXT("path_prefix"), TEXT("/Game"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("Asset not found for object_path; resolve a valid /Game object path first."),
			TEXT("asset_index_fuzzy_search"),
			SuggestedArgs);
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
	Args->TryGetStringField(TEXT("object_path"), Path);
	if (Path.IsEmpty())
	{
		Args->TryGetStringField(TEXT("path"), Path);
	}
	if (Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("object_path is required (asset object path, e.g. /Game/Folder/Asset.Asset)"));
	}
	UnrealAiToolDispatchArgRepair::NormalizeAssetLikeObjectPath(Path);
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AR = ARM.Get();
	const FSoftObjectPath SOP(Path);
	const FAssetData AD = AR.GetAssetByObjectPath(SOP);
	if (!AD.IsValid())
	{
		FString Query;
		{
			int32 LastSlash = INDEX_NONE;
			if (Path.FindLastChar(TEXT('/'), LastSlash) && LastSlash > 0 && LastSlash < Path.Len() - 1)
			{
				FString Leaf = Path.Mid(LastSlash + 1);
				int32 LastDot = INDEX_NONE;
				if (Leaf.FindLastChar(TEXT('.'), LastDot) && LastDot > 0 && LastDot < Leaf.Len() - 1)
				{
					Leaf = Leaf.Left(LastDot);
				}
				Leaf.TrimStartAndEndInline();
				Query = Leaf;
			}
		}
		if (Query.IsEmpty())
		{
			Query = TEXT("Asset");
		}
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("query"), Query);
		SuggestedArgs->SetStringField(TEXT("path_prefix"), TEXT("/Game"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("Asset not found for object_path; resolve a valid /Game object path first."),
			TEXT("asset_index_fuzzy_search"),
			SuggestedArgs);
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
		return UnrealAiToolJson::Error(TEXT("package_path must be /Game or a path under /Game/ (e.g. /Game/Blueprints)"));
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
