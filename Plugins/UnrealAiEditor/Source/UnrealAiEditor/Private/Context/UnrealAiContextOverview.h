#pragma once

#include "Containers/UnrealString.h"

class IAgentContextService;
struct FAgentContextBuildOptions;

/**
 * Human-readable summary of what would be sent as agent context (attachments, tool memory, snapshot, budget).
 * Refreshes editor snapshot and runs the same BuildContextWindow path as a real turn (no LLM call).
 */
FString UnrealAiFormatContextOverviewForUi(
	IAgentContextService* Ctx,
	const FString& ProjectId,
	const FString& ThreadId,
	const FAgentContextBuildOptions& Options);
