#include "GraphBuilder/UnrealAiGraphEditDomain.h"

#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"

namespace UnrealAiGraphEditDomainEntry
{
	namespace
	{
		class FDomain final : public IUnrealAiGraphEditDomain
		{
		public:
			virtual EUnrealAiBlueprintBuilderTargetKind GetKind() const override
			{
				return EUnrealAiBlueprintBuilderTargetKind::MaterialInstance;
			}

			virtual FGraphEditDomainCapabilities GetCapabilities() const override
			{
				FGraphEditDomainCapabilities C;
				C.bSupportsParameterOnlyMutation = true;
				C.bImplementationComplete = true;
				return C;
			}

			virtual FGraphEditDomainValidationResult ValidateAssetPathForDomain(const FString& AssetPath) const override
			{
				FGraphEditDomainValidationResult R;
				UMaterialInterface* MI = LoadObject<UMaterialInterface>(nullptr, *AssetPath);
				if (!MI)
				{
					R.bOk = false;
					R.bAssetMissing = true;
					R.ErrorMessage = FString::Printf(TEXT("[graph_domain] Could not load MaterialInterface at %s."), *AssetPath);
					return R;
				}
				if (!MI->IsA(UMaterialInstanceConstant::StaticClass()))
				{
					R.bOk = false;
					R.ErrorMessage = TEXT(
						"[graph_domain] Path does not resolve to a Material Instance (UMaterialInstanceConstant). Duplicate to an instance or adjust target_kind.");
					return R;
				}
				return R;
			}
		};

		static FDomain GDomain;
	}

	IUnrealAiGraphEditDomain* MaterialInstance()
	{
		return &GDomain;
	}
}
