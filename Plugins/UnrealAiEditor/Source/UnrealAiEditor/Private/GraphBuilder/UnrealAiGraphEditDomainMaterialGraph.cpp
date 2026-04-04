#include "GraphBuilder/UnrealAiGraphEditDomain.h"

#include "Materials/Material.h"
namespace UnrealAiGraphEditDomainEntry
{
	namespace
	{
		class FDomain final : public IUnrealAiGraphEditDomain
		{
		public:
			virtual EUnrealAiBlueprintBuilderTargetKind GetKind() const override
			{
				return EUnrealAiBlueprintBuilderTargetKind::MaterialGraph;
			}

			virtual FGraphEditDomainCapabilities GetCapabilities() const override
			{
				FGraphEditDomainCapabilities C;
				C.bImplementationComplete = true;
				return C;
			}

			virtual FGraphEditDomainValidationResult ValidateAssetPathForDomain(const FString& AssetPath) const override
			{
				FGraphEditDomainValidationResult R;
				UMaterial* M = LoadObject<UMaterial>(nullptr, *AssetPath);
				if (!M)
				{
					R.bOk = false;
					R.bAssetMissing = true;
					R.ErrorMessage = FString::Printf(TEXT("[graph_domain] Could not load base Material at %s."), *AssetPath);
					return R;
				}
				return R;
			}
		};

		static FDomain GDomain;
	}

	IUnrealAiGraphEditDomain* MaterialGraph()
	{
		return &GDomain;
	}
}
