#include "Prompt/UnrealAiPromptBuilder.h"

#include "Prompt/UnrealAiPromptAssemblyStrategy.h"

FString UnrealAiPromptBuilder::BuildSystemDeveloperContent(const FUnrealAiPromptAssembleParams& Params)
{
	const EUnrealAiPromptAssemblyKind Kind = UnrealAiPromptAssembly::GetEffectiveAssemblyKind();
	return UnrealAiPromptAssembly::GetStrategyForKind(Kind).BuildSystemDeveloperContent(Params);
}
