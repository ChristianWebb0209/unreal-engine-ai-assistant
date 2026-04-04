#include "GraphBuilder/UnrealAiGraphEditDomain.h"

#include "Animation/AnimBlueprint.h"
#include "Engine/Blueprint.h"

namespace UnrealAiGraphEditDomainEntry
{
	namespace
	{
		bool IsWidgetBlueprintAsset(UBlueprint* B)
		{
			return B && B->GetClass()->GetFName() == FName(TEXT("WidgetBlueprint"));
		}

		class FDomain final : public IUnrealAiGraphEditDomain
		{
		public:
			virtual EUnrealAiBlueprintBuilderTargetKind GetKind() const override
			{
				return EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint;
			}

			virtual FGraphEditDomainCapabilities GetCapabilities() const override
			{
				FGraphEditDomainCapabilities C;
				C.bSupportsKismetIR = true;
				C.bImplementationComplete = true;
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
					R.ErrorMessage = FString::Printf(
						TEXT("[graph_domain] Could not load Blueprint at %s (expected a normal gameplay Blueprint for this builder kind)."),
						*AssetPath);
					return R;
				}
				if (Cast<UAnimBlueprint>(B))
				{
					R.bOk = false;
					R.ErrorMessage = TEXT(
						"[graph_domain] Asset is an animation logic Blueprint; this builder sub-turn expects a standard gameplay Blueprint. Adjust target_kind in the handoff or pick the correct asset.");
					return R;
				}
				if (IsWidgetBlueprintAsset(B))
				{
					R.bOk = false;
					R.ErrorMessage = TEXT(
						"[graph_domain] Asset is a UI Blueprint; this builder sub-turn expects a standard gameplay Blueprint. Adjust target_kind in the handoff or pick the correct asset.");
					return R;
				}
				return R;
			}
		};

		static FDomain GDomain;
	}

	IUnrealAiGraphEditDomain* ScriptBlueprint()
	{
		return &GDomain;
	}
}
