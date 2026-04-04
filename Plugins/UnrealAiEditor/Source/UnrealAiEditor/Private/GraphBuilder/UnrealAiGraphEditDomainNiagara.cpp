#include "GraphBuilder/UnrealAiGraphEditDomain.h"

#include "UObject/UObjectGlobals.h"

namespace UnrealAiGraphEditDomainEntry
{
	namespace
	{
		static bool IsLikelyNiagaraAsset(UObject* Obj)
		{
			if (!Obj)
			{
				return false;
			}
			const FString Name = Obj->GetClass()->GetName();
			return Name.Contains(TEXT("Niagara"));
		}

		class FDomain final : public IUnrealAiGraphEditDomain
		{
		public:
			virtual EUnrealAiBlueprintBuilderTargetKind GetKind() const override
			{
				return EUnrealAiBlueprintBuilderTargetKind::Niagara;
			}

			virtual FGraphEditDomainCapabilities GetCapabilities() const override
			{
				FGraphEditDomainCapabilities C;
				C.bImplementationComplete = false;
				return C;
			}

			virtual FGraphEditDomainValidationResult ValidateAssetPathForDomain(const FString& AssetPath) const override
			{
				FGraphEditDomainValidationResult R;
				UObject* Obj = LoadObject<UObject>(nullptr, *AssetPath);
				if (!Obj)
				{
					R.bOk = false;
					R.bAssetMissing = true;
					R.ErrorMessage = FString::Printf(TEXT("[graph_domain] Could not load object at %s."), *AssetPath);
					return R;
				}
				if (!IsLikelyNiagaraAsset(Obj))
				{
					R.bOk = false;
					R.ErrorMessage = TEXT(
						"[graph_domain] Asset does not look like a Niagara system/emitter class (expected Niagara* type).");
					return R;
				}
				return R;
			}
		};

		static FDomain GDomain;
	}

	IUnrealAiGraphEditDomain* Niagara()
	{
		return &GDomain;
	}
}
