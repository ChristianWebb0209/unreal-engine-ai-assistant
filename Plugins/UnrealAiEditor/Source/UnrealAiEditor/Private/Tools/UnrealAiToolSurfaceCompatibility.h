#pragma once

#include "CoreMinimal.h"
#include "Containers/Set.h"

class FJsonObject;

/** Which agent pipeline may surface or invoke a tool (orthogonal to modes: ask/agent/plan). */
enum class EUnrealAiToolSurfaceKind : uint8
{
	MainAgent,
	BlueprintBuilder,
};

namespace UnrealAiToolSurfaceCompatibility
{
	/** JSON token: agent_surfaces: ["all"] */
	extern const FString GAgentSurfaceToken_All;
	/** JSON token: main agent turn (not a Blueprint Builder sub-turn). */
	extern const FString GAgentSurfaceToken_MainAgent;
	/** JSON token: Blueprint Builder sub-turn after handoff. */
	extern const FString GAgentSurfaceToken_BlueprintBuilder;

	/**
	 * Reads tools[].agent_surfaces (array of strings).
	 * Missing or empty array => bOutAll=true (same as ["all"]).
	 * A single entry "all" (any case) => bOutAll=true.
	 * Otherwise OutTokens holds lowercased trimmed tokens (e.g. main_agent, blueprint_builder).
	 */
	void ParseAgentSurfaces(const FJsonObject& ToolDef, TSet<FString>& OutTokens, bool& bOutAll);

	/** Returns whether the tool may appear on the given surface when bOutAll from Parse is false. */
	bool ToolAllowedOnSurface(const TSet<FString>& Tokens, bool bAll, EUnrealAiToolSurfaceKind Kind);
}
