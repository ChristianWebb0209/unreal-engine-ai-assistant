#include "GraphBuilder/UnrealAiGraphEditDomain.h"

#include "Animation/AnimBlueprint.h"

namespace UnrealAiGraphEditDomainEntry
{
	namespace Private_AnimBlueprint
	{
		class FDomain final : public IUnrealAiGraphEditDomain
		{
		public:
			virtual EUnrealAiBlueprintBuilderTargetKind GetKind() const override
			{
				return EUnrealAiBlueprintBuilderTargetKind::AnimBlueprint;
			}

			virtual FGraphEditDomainCapabilities GetCapabilities() const override
			{
				FGraphEditDomainCapabilities C;
				C.bSupportsKismetIR = true;
				// State machine / blend graphs still lack a full contract; K2 event graph tooling is partial.
				C.bImplementationComplete = false;
				return C;
			}

			virtual FGraphEditDomainValidationResult ValidateAssetPathForDomain(const FString& AssetPath) const override
			{
				FGraphEditDomainValidationResult R;
				UAnimBlueprint* AB = LoadObject<UAnimBlueprint>(nullptr, *AssetPath);
				if (!AB)
				{
					R.bOk = false;
					R.bAssetMissing = true;
					R.ErrorMessage = FString::Printf(
						TEXT("[graph_domain] Could not load animation logic Blueprint at %s."),
						*AssetPath);
					return R;
				}
				return R;
			}
		};

		static FDomain GDomain;
	}

	IUnrealAiGraphEditDomain* AnimBlueprint()
	{
		return &Private_AnimBlueprint::GDomain;
	}
}
