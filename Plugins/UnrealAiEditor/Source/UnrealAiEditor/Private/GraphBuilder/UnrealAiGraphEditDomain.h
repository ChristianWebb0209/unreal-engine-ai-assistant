#pragma once

#include "CoreMinimal.h"
#include "UnrealAiBlueprintBuilderTargetKind.h"

/** What this graph-edit domain can rely on for deterministic editing. */
struct FGraphEditDomainCapabilities
{
	bool bSupportsKismetIR = false;
	bool bSupportsParameterOnlyMutation = false;
	/** True when the domain’s primary editing surface is implemented end-to-end for harness use. */
	bool bImplementationComplete = false;
	bool bSupportsDesignerLayout = false;
};

/** Result of validating an asset path against the active builder domain. */
struct FGraphEditDomainValidationResult
{
	bool bOk = true;
	/** True when nothing exists at the path — preflight should not block; the tool handles the error. */
	bool bAssetMissing = false;
	FString ErrorMessage;
};

/**
 * Per–target-kind policy for Blueprint Builder sub-turns: asset class checks and capability flags.
 * Implementations live one cpp per kind under GraphBuilder/.
 */
class IUnrealAiGraphEditDomain
{
public:
	virtual ~IUnrealAiGraphEditDomain() = default;

	virtual EUnrealAiBlueprintBuilderTargetKind GetKind() const = 0;
	virtual FGraphEditDomainCapabilities GetCapabilities() const = 0;

	/** Load the asset on the game thread and verify it matches this domain. */
	virtual FGraphEditDomainValidationResult ValidateAssetPathForDomain(const FString& AssetPath) const = 0;
};

struct FUnrealAiAgentTurnRequest;
struct FUnrealAiToolInvocationResult;

/** Maps EUnrealAiBlueprintBuilderTargetKind to a shared domain instance (not owned). */
struct FUnrealAiGraphEditDomainRegistry
{
	static IUnrealAiGraphEditDomain* Get(EUnrealAiBlueprintBuilderTargetKind Kind);
};

/**
 * When the active turn is a Blueprint Builder sub-turn, optionally block tool invocation early if the
 * primary asset path in the JSON does not match Request.BlueprintBuilderTargetKind.
 * @return true if the caller should treat OutBlock as the tool result and skip InvokeTool.
 */
bool UnrealAiGraphEditDomainPreflight_ShouldBlockInvocation(
	const FUnrealAiAgentTurnRequest& Request,
	const FString& ToolName,
	const FString& ArgumentsJson,
	FUnrealAiToolInvocationResult& OutBlock);
