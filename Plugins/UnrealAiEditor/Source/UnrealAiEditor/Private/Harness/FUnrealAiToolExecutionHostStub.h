#pragma once

#include "CoreMinimal.h"
#include "Harness/IToolExecutionHost.h"

/** Placeholder tool host until real editor tools are wired. */
class FUnrealAiToolExecutionHostStub final : public IToolExecutionHost
{
public:
	virtual FUnrealAiToolInvocationResult InvokeTool(
		const FString& ToolName,
		const FString& ArgumentsJson,
		const FString& ToolCallId) override;
};
