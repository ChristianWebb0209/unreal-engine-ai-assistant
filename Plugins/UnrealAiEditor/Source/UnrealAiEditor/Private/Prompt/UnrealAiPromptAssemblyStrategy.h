#pragma once

#include "CoreMinimal.h"
#include "Prompt/UnrealAiPromptAssembleParams.h"

class IUnrealAiPromptAssemblyStrategy
{
public:
	virtual ~IUnrealAiPromptAssemblyStrategy() = default;
	virtual FString BuildSystemDeveloperContent(const FUnrealAiPromptAssembleParams& Params) const = 0;
};

/** Linear assembly from `prompts/chunks/*.md` (see `UnrealAiPromptAssemblyStrategy.cpp`). */
class FUnrealAiLinearPromptAssemblyStrategy final : public IUnrealAiPromptAssemblyStrategy
{
public:
	explicit FUnrealAiLinearPromptAssemblyStrategy(const TCHAR* InChunkSubdir)
		: ChunkSubdir(InChunkSubdir)
	{
	}

	virtual FString BuildSystemDeveloperContent(const FUnrealAiPromptAssembleParams& Params) const override;

private:
	const TCHAR* ChunkSubdir = nullptr;
};

/** Only assembly mode: legacy `prompts/chunks`. */
enum class EUnrealAiPromptAssemblyKind : uint8
{
	LegacyChunks,
};

namespace UnrealAiPromptAssembly
{
	EUnrealAiPromptAssemblyKind GetEffectiveAssemblyKind();

	const IUnrealAiPromptAssemblyStrategy& GetStrategyForKind(EUnrealAiPromptAssemblyKind Kind);

	FString KindToTelemetryString(EUnrealAiPromptAssemblyKind Kind);
}
