#pragma once

#include "CoreMinimal.h"

struct FUnrealAiDagNode
{
	FString Id;
	FString Title;
	FString Hint;
	TArray<FString> DependsOn;
};

struct FUnrealAiPlanDag
{
	FString Title;
	TArray<FUnrealAiDagNode> Nodes;
};

namespace UnrealAiPlanDag
{
	/** Parse JSON payload: canonical `nodes[]` (+ optional schema unreal_ai.plan_dag). Legacy `steps[]` is still accepted but deprecated—prefer `nodes[]` in prompts. */
	bool ParseDagJson(const FString& DagJson, FUnrealAiPlanDag& OutDag, FString& OutError);
	/** Validate graph integrity + acyclicity and bounded size. */
	bool ValidateDag(const FUnrealAiPlanDag& Dag, int32 MaxNodes, FString& OutError);
	/** Ready = not terminal, and all dependencies are terminal-success/skip. */
	void GetReadyNodeIds(
		const FUnrealAiPlanDag& Dag,
		const TMap<FString, FString>& NodeStatusById,
		TArray<FString>& OutNodeIds);
	/** Status helpers used by executor/context state. */
	bool IsTerminalStatus(const FString& Status);
	bool IsSuccessfulStatus(const FString& Status);

	/**
	 * All node ids that transitively depend on FailedNodeId (forward along edges dep->dependent).
	 * Does not include FailedNodeId. Order is BFS order (not semantically significant).
	 */
	void CollectTransitiveDependents(const FUnrealAiPlanDag& Dag, const FString& FailedNodeId, TArray<FString>& OutDependents);

	/** Serialize DAG for planner context or persistence (canonical `nodes[]`, schema unreal_ai.plan_dag). */
	bool SerializeDagJson(const FUnrealAiPlanDag& Dag, FString& OutJson, FString& OutError);

	/**
	 * After a node failure, merge the model's **new** outstanding-work DAG onto successfully completed nodes.
	 * Success nodes are copied in declaration order from OldDag; NewDagFromPlanner nodes are appended (new ids only).
	 * OutFreshNodeIds lists every id from NewDagFromPlanner (caller resets context progress for those ids).
	 */
	bool MergeReplanNewNodesOntoSuccesses(
		const FUnrealAiPlanDag& OldDag,
		const TMap<FString, FString>& NodeStatusById,
		const FUnrealAiPlanDag& NewDagFromPlanner,
		FUnrealAiPlanDag& OutMerged,
		TSet<FString>& OutFreshNodeIds,
		FString& OutError);

	/**
	 * Static execution waves: wave 0 has no dependencies; each next wave contains nodes whose
	 * dependencies all appear in earlier waves. Preserves declaration order within each wave.
	 * Preconditions: Dag must be acyclic with valid ids (call ValidateDag first); if violated, OutWaves may be partial.
	 */
	void ComputeParallelWaves(const FUnrealAiPlanDag& Dag, TArray<TArray<FString>>& OutWaves);
}
