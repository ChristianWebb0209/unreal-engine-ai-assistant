#pragma once

#include "CoreMinimal.h"
#include "Context/AgentContextTypes.h"

/** Deterministic complexity signal (see docs/context/context-management.md Â§8.1). */
struct FUnrealAiComplexityAssessment
{
	float ScoreNormalized = 0.f;
	FString Label;
	TArray<FString> Signals;
	bool bRecommendPlanGate = false;

	/** [Complexity] block for system prompt / logs. */
	FString FormatBlock() const;
};

struct FUnrealAiComplexityAssessorInputs
{
	FString UserMessage;
	int32 AttachmentCount = 0;
	int32 ToolResultCount = 0;
	int32 ContextBlockCharCount = 0;
	EUnrealAiAgentMode Mode = EUnrealAiAgentMode::Agent;
	/** Content Browser selection count from editor snapshot (0 if none). */
	int32 ContentBrowserSelectionCount = 0;
};

namespace UnrealAiComplexityAssessor
{
	FUnrealAiComplexityAssessment Assess(const FUnrealAiComplexityAssessorInputs& In);
}
