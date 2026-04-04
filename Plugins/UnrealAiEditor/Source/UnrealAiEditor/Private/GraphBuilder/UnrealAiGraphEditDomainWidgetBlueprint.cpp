#include "GraphBuilder/UnrealAiGraphEditDomain.h"

#include "Engine/Blueprint.h"

namespace UnrealAiGraphEditDomainEntry
{
	namespace
	{
		class FDomain final : public IUnrealAiGraphEditDomain
		{
		public:
			virtual EUnrealAiBlueprintBuilderTargetKind GetKind() const override
			{
				return EUnrealAiBlueprintBuilderTargetKind::WidgetBlueprint;
			}

			virtual FGraphEditDomainCapabilities GetCapabilities() const override
			{
				FGraphEditDomainCapabilities C;
				C.bSupportsKismetIR = true;
				C.bSupportsDesignerLayout = false;
				C.bImplementationComplete = false;
				return C;
			}

			virtual FGraphEditDomainValidationResult ValidateAssetPathForDomain(const FString& AssetPath) const override
			{
				FGraphEditDomainValidationResult R;
				UBlueprint* B = LoadObject<UBlueprint>(nullptr, *AssetPath);
				if (!B)
				{
					R.bOk = false;
					R.bAssetMissing = true;
					R.ErrorMessage = FString::Printf(TEXT("[graph_domain] Could not load Blueprint at %s."), *AssetPath);
					return R;
				}
				if (B->GetClass()->GetFName() != FName(TEXT("WidgetBlueprint")))
				{
					R.bOk = false;
					R.ErrorMessage = TEXT(
						"[graph_domain] Asset is not a UI Blueprint (WidgetBlueprint). Adjust target_kind or asset path.");
					return R;
				}
				return R;
			}
		};

		static FDomain GDomain;
	}

	IUnrealAiGraphEditDomain* WidgetBlueprint()
	{
		return &GDomain;
	}
}
