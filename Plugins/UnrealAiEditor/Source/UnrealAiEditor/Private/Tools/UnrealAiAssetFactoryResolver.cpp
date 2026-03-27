#include "Tools/UnrealAiAssetFactoryResolver.h"

#include "Factories/Factory.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"

// Editor-only factories live in UnrealEd; include specific ones for the tiny mapping.
#include "Engine/Blueprint.h"
#include "Engine/Texture2D.h"
#include "Factories/BlueprintFactory.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/TextureFactory.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"

namespace UnrealAiAssetFactoryResolverPriv
{
	static bool IsFactoryClassEligible(UClass* FC)
	{
		if (!FC || !FC->IsChildOf(UFactory::StaticClass()))
		{
			return false;
		}
		if (FC->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			return false;
		}
		return true;
	}

	static bool IsInteractiveOrImportFactory(UClass* FC)
	{
		if (!FC)
		{
			return true;
		}
		const FString N = FC->GetName();
		return N.Contains(TEXT("Import")) || N.Contains(TEXT("Reimport"));
	}

	static int32 ScoreFactory(UClass* AssetClass, UFactory* F)
	{
		if (!AssetClass || !F || !F->CanCreateNew())
		{
			return MIN_int32 / 2;
		}
		int32 Score = 0;
		if (F->SupportedClass == AssetClass)
		{
			Score += 100;
		}
		else if (F->SupportedClass && AssetClass->IsChildOf(F->SupportedClass))
		{
			Score += 50;
		}
		else
		{
			Score -= 10;
		}
		if (IsInteractiveOrImportFactory(F->GetClass()))
		{
			Score -= 1000;
		}
		return Score;
	}
}

FUnrealAiAssetFactoryResolver::FResolveResult FUnrealAiAssetFactoryResolver::Resolve(
	UClass* AssetClass,
	const FString& OptionalFactoryClassPath)
{
	if (!AssetClass)
	{
		FResolveResult R;
		R.Notes.Add(TEXT("AssetClass was null"));
		return R;
	}

	if (!OptionalFactoryClassPath.IsEmpty())
	{
		return ResolveExplicit(AssetClass, OptionalFactoryClassPath);
	}

	// First: tiny explicit mapping (deterministic).
	{
		FResolveResult M = ResolveMapped(AssetClass);
		if (M.Factory)
		{
			return M;
		}
	}

	// Fallback: generic scoring over eligible factories.
	return ResolveGenericScored(AssetClass);
}

FUnrealAiAssetFactoryResolver::FResolveResult FUnrealAiAssetFactoryResolver::ResolveExplicit(
	UClass* AssetClass,
	const FString& FactoryClassPath)
{
	FResolveResult R;
	R.FactoryClassPath = FactoryClassPath;

	UClass* FC = LoadObject<UClass>(nullptr, *FactoryClassPath);
	if (!FC || !FC->IsChildOf(UFactory::StaticClass()))
	{
		R.Notes.Add(TEXT("factory_class must be a UFactory subclass"));
		return R;
	}

	UFactory* F = NewObject<UFactory>(GetTransientPackage(), FC);
	if (!F)
	{
		R.Notes.Add(TEXT("failed to instantiate factory_class"));
		return R;
	}
	if (!F->CanCreateNew())
	{
		R.Notes.Add(TEXT("factory_class.CanCreateNew() returned false"));
		return R;
	}
	if (F->SupportedClass && !(AssetClass->IsChildOf(F->SupportedClass)))
	{
		R.Notes.Add(TEXT("factory_class SupportedClass does not match asset_class"));
		return R;
	}

	R.Factory = F;
	return R;
}

FUnrealAiAssetFactoryResolver::FResolveResult FUnrealAiAssetFactoryResolver::ResolveMapped(UClass* AssetClass)
{
	FResolveResult R;

	// Minimal mapping to eliminate common ambiguity.
	if (AssetClass == UBlueprint::StaticClass())
	{
		UBlueprintFactory* F = NewObject<UBlueprintFactory>(GetTransientPackage(), UBlueprintFactory::StaticClass());
		if (F)
		{
			// Non-interactive default so CreateAsset succeeds without prompting.
			F->ParentClass = AActor::StaticClass();
			R.Factory = F;
			R.FactoryClassPath = UBlueprintFactory::StaticClass()->GetPathName();
			R.Notes.Add(TEXT("mapped: UBlueprint -> UBlueprintFactory"));
		}
		return R;
	}
	if (AssetClass == UMaterial::StaticClass())
	{
		UMaterialFactoryNew* F = NewObject<UMaterialFactoryNew>(GetTransientPackage(), UMaterialFactoryNew::StaticClass());
		if (F)
		{
			R.Factory = F;
			R.FactoryClassPath = UMaterialFactoryNew::StaticClass()->GetPathName();
			R.Notes.Add(TEXT("mapped: UMaterial -> UMaterialFactoryNew"));
		}
		return R;
	}
	if (AssetClass == UTexture2D::StaticClass())
	{
		UTextureFactory* F = NewObject<UTextureFactory>(GetTransientPackage(), UTextureFactory::StaticClass());
		if (F && F->CanCreateNew())
		{
			R.Factory = F;
			R.FactoryClassPath = UTextureFactory::StaticClass()->GetPathName();
			R.Notes.Add(TEXT("mapped: UTexture2D -> UTextureFactory"));
		}
		return R;
	}
	// MaterialInstanceConstant: often absent in projects; enable deterministic creation for task flows.
	{
		if (AssetClass->GetPathName() == TEXT("/Script/Engine.MaterialInstanceConstant"))
		{
			UClass* FC = LoadObject<UClass>(nullptr, TEXT("/Script/UnrealEd.MaterialInstanceConstantFactoryNew"));
			if (FC && FC->IsChildOf(UFactory::StaticClass()))
			{
				UFactory* F = NewObject<UFactory>(GetTransientPackage(), FC);
				if (F && F->CanCreateNew())
				{
					R.Factory = F;
					R.FactoryClassPath = FC->GetPathName();
					R.Notes.Add(TEXT("mapped: /Script/Engine.MaterialInstanceConstant -> /Script/UnrealEd.MaterialInstanceConstantFactoryNew"));
				}
			}
			return R;
		}
	}
	// StaticMesh frequently needs an explicit factory; load by class path to avoid brittle headers.
	{
		// Compare by name to avoid requiring Engine/StaticMesh.h here.
		if (AssetClass->GetPathName() == TEXT("/Script/Engine.StaticMesh"))
		{
			UClass* FC = LoadObject<UClass>(nullptr, TEXT("/Script/UnrealEd.StaticMeshFactoryNew"));
			if (FC && FC->IsChildOf(UFactory::StaticClass()))
			{
				UFactory* F = NewObject<UFactory>(GetTransientPackage(), FC);
				if (F && F->CanCreateNew())
				{
					R.Factory = F;
					R.FactoryClassPath = FC->GetPathName();
					R.Notes.Add(TEXT("mapped: /Script/Engine.StaticMesh -> /Script/UnrealEd.StaticMeshFactoryNew"));
				}
			}
			return R;
		}
	}
	return R;
}

FUnrealAiAssetFactoryResolver::FResolveResult FUnrealAiAssetFactoryResolver::ResolveGenericScored(UClass* AssetClass)
{
	using namespace UnrealAiAssetFactoryResolverPriv;

	FResolveResult R;
	struct FCand
	{
		int32 Score = MIN_int32 / 2;
		UFactory* Factory = nullptr;
		UClass* FactoryClass = nullptr;
		FString ClassPath;
	};

	FCand Best;
	TArray<FCand> Top;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* FC = *It;
		if (!IsFactoryClassEligible(FC))
		{
			continue;
		}

		UFactory* F = NewObject<UFactory>(GetTransientPackage(), FC);
		if (!F)
		{
			continue;
		}
		if (!F->CanCreateNew())
		{
			continue;
		}
		if (F->SupportedClass && !AssetClass->IsChildOf(F->SupportedClass))
		{
			continue;
		}

		const int32 S = ScoreFactory(AssetClass, F);
		FCand C;
		C.Score = S;
		C.Factory = F;
		C.FactoryClass = FC;
		C.ClassPath = FC->GetPathName();

		Top.Add(C);
		Top.Sort([](const FCand& A, const FCand& B)
		{
			if (A.Score != B.Score)
			{
				return A.Score > B.Score;
			}
			return A.ClassPath < B.ClassPath;
		});
		if (Top.Num() > 10)
		{
			Top.SetNum(10);
		}
		if (!Best.Factory || S > Best.Score || (S == Best.Score && C.ClassPath < Best.ClassPath))
		{
			Best = C;
		}
	}

	for (const FCand& C : Top)
	{
		R.Candidates.Add(C.ClassPath);
	}

	if (Best.Factory && Best.Score > (MIN_int32 / 4))
	{
		R.Factory = Best.Factory;
		R.FactoryClassPath = Best.FactoryClass ? Best.FactoryClass->GetPathName() : FString();
		R.Notes.Add(FString::Printf(TEXT("generic-scored pick: %s score=%d"), *R.FactoryClassPath, Best.Score));
	}
	else
	{
		R.Notes.Add(TEXT("no eligible factory candidates found"));
	}

	return R;
}

