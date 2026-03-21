#pragma once

#include "CoreMinimal.h"

struct FUnrealAiDagNode
{
	FString Id;
	FString Title;
	FString Hint;
	TArray<FString> DependsOn;
};

struct FUnrealAiOrchestrateDag
{
	FString Title;
	TArray<FUnrealAiDagNode> Nodes;
};

namespace UnrealAiOrchestrateDag
{
	/** Parse JSON payload (expects schema unreal_ai.orchestrate_dag and nodes[]). */
	bool ParseDagJson(const FString& DagJson, FUnrealAiOrchestrateDag& OutDag, FString& OutError);
	/** Validate graph integrity + acyclicity and bounded size. */
	bool ValidateDag(const FUnrealAiOrchestrateDag& Dag, int32 MaxNodes, FString& OutError);
	/** Ready = not terminal, and all dependencies are terminal-success/skip. */
	void GetReadyNodeIds(
		const FUnrealAiOrchestrateDag& Dag,
		const TMap<FString, FString>& NodeStatusById,
		TArray<FString>& OutNodeIds);
	/** Status helpers used by executor/context state. */
	bool IsTerminalStatus(const FString& Status);
	bool IsSuccessfulStatus(const FString& Status);
}

