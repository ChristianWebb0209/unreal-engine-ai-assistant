#pragma once

#include "CoreMinimal.h"
#include "Context/AgentContextTypes.h"
#include "UnrealAiEnvironmentBuilderTargetKind.h"

class FUnrealAiToolCatalog;
struct FUnrealAiModelCapabilities;
struct FUnrealAiToolPackOptions;

/** Environment / PCG Builder tool-surface policy (retrieval shaping, core-tool merge, appendix verbosity). */
namespace UnrealAiEnvironmentBuilderToolSurface
{
	void AugmentHybridRetrievalQuery(FString& InOutHybrid);

	FString SurfaceProfileTelemetryId();

	void MergeEnvironmentCoreToolsAfterGuardrails(
		const FUnrealAiToolCatalog& Catalog,
		EUnrealAiAgentMode Mode,
		const FUnrealAiModelCapabilities& Caps,
		const FUnrealAiToolPackOptions* PackOptions,
		TFunctionRef<bool(const FString& ToolId)> ToolFilter,
		const TSet<FString>& GuardrailIds,
		TArray<FString>& InOutOrdered);

	void WidenKeffectiveToFullEligibleRoster(int32 NonGuardrailPoolCount, int32& InOutKeffective);

	bool TryComputeAppendixBudgetFromModelContext(
		int32 MaxContextTokens,
		int32 CharPerTokenApprox,
		int32& OutAppendixBudgetChars,
		FString* OutFailureReason = nullptr);

	void GetVerboseAppendixSettings(
		int32 OrderedToolCount,
		int32 MaxAppendixBudgetChars,
		int32& OutExpandedCount,
		int32& OutBudgetChars,
		int32& OutParametersExcerptMaxChars);

	FString BuildAutomatedSubturnHarnessPreamble(EUnrealAiEnvironmentBuilderTargetKind TargetKind);
}
