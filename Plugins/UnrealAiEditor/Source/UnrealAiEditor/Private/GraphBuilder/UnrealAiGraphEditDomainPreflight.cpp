#include "GraphBuilder/UnrealAiGraphEditDomain.h"

#include "Harness/IToolExecutionHost.h"
#include "Harness/UnrealAiAgentTypes.h"

#include "Animation/AnimBlueprint.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <initializer_list>

namespace
{
	bool TryExtractPrimaryAssetPath(
		EUnrealAiBlueprintBuilderTargetKind Kind,
		const TSharedPtr<FJsonObject>& Args,
		FString& OutPath)
	{
		if (!Args.IsValid())
		{
			return false;
		}
		auto TryFields = [&Args, &OutPath](std::initializer_list<const TCHAR*> Names) -> bool
		{
			for (const TCHAR* N : Names)
			{
				FString P;
				if (Args->TryGetStringField(N, P))
				{
					P.TrimStartAndEndInline();
					if (!P.IsEmpty())
					{
						OutPath = P;
						return true;
					}
				}
			}
			return false;
		};

		switch (Kind)
		{
		case EUnrealAiBlueprintBuilderTargetKind::MaterialInstance:
			return TryFields({TEXT("material_path"), TEXT("object_path"), TEXT("blueprint_path"), TEXT("asset_path")});
		case EUnrealAiBlueprintBuilderTargetKind::MaterialGraph:
			return TryFields({TEXT("material_path"), TEXT("object_path"), TEXT("blueprint_path"), TEXT("asset_path")});
		case EUnrealAiBlueprintBuilderTargetKind::Niagara:
			return TryFields({TEXT("object_path"), TEXT("blueprint_path"), TEXT("asset_path"), TEXT("material_path")});
		default:
			return TryFields({TEXT("blueprint_path"), TEXT("object_path"), TEXT("material_path"), TEXT("asset_path")});
		}
	}
}

bool UnrealAiGraphEditDomainPreflight_ShouldBlockInvocation(
	const FUnrealAiAgentTurnRequest& Request,
	const FString& ToolName,
	const FString& ArgumentsJson,
	FUnrealAiToolInvocationResult& OutBlock)
{
	(void)ToolName;
	OutBlock = FUnrealAiToolInvocationResult();
	if (!Request.bBlueprintBuilderTurn)
	{
		return false;
	}

	TSharedPtr<FJsonObject> Args;
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgumentsJson);
		if (!FJsonSerializer::Deserialize(Reader, Args) || !Args.IsValid())
		{
			Args = MakeShared<FJsonObject>();
		}
	}

	IUnrealAiGraphEditDomain* Domain = FUnrealAiGraphEditDomainRegistry::Get(Request.BlueprintBuilderTargetKind);
	if (!Domain)
	{
		return false;
	}

	FString Path;
	if (!TryExtractPrimaryAssetPath(Request.BlueprintBuilderTargetKind, Args, Path))
	{
		return false;
	}

	// Deterministic recovery hint: script_blueprint handoff pointed at an AnimBlueprint asset.
	if (Request.BlueprintBuilderTargetKind == EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint)
	{
		if (UAnimBlueprint* AB = LoadObject<UAnimBlueprint>(nullptr, *Path))
		{
			(void)AB;
			const FString Correction = FString::Printf(
				TEXT("[Harness][reason=blueprint_builder_target_kind_mismatch] ")
				TEXT("Path '%s' resolves to /Script/Engine.AnimBlueprint, but this builder sub-turn is using target_kind: script_blueprint. ")
				TEXT("Re-emit a single `<unreal_ai_build_blueprint>` handoff with the same path and `target_kind: anim_blueprint`; do not retry script_blueprint mutators first."),
				*Path);
			OutBlock.bOk = false;
			OutBlock.ErrorMessage = Correction;
			OutBlock.ContentForModel = Correction;
			return true;
		}
	}

	const FGraphEditDomainValidationResult VR = Domain->ValidateAssetPathForDomain(Path);
	if (VR.bOk)
	{
		return false;
	}
	if (VR.bAssetMissing)
	{
		return false;
	}

	OutBlock.bOk = false;
	OutBlock.ErrorMessage = VR.ErrorMessage;
	OutBlock.ContentForModel = VR.ErrorMessage;
	return true;
}
