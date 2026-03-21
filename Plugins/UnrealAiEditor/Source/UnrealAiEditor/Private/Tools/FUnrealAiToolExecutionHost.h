#pragma once

#include "CoreMinimal.h"
#include "Harness/IToolExecutionHost.h"
#include "Tools/UnrealAiToolCatalog.h"

class FUnrealAiBackendRegistry;

/** Game-thread tool execution; loads Resources/UnrealAiToolCatalog.json and dispatches to UE5 handlers. */
class FUnrealAiToolExecutionHost final : public IToolExecutionHost
{
public:
	explicit FUnrealAiToolExecutionHost(FUnrealAiBackendRegistry* InRegistry);

	/** Reload Resources/UnrealAiToolCatalog.json (e.g. after catalog edits). */
	void ReloadCatalog();

	virtual void SetToolSession(const FString& ProjectId, const FString& ThreadId) override;

	virtual FUnrealAiToolInvocationResult InvokeTool(
		const FString& ToolName,
		const FString& ArgumentsJson,
		const FString& ToolCallId) override;

private:
	FUnrealAiBackendRegistry* Registry = nullptr;
	FUnrealAiToolCatalog Catalog;
	FString SessionProjectId;
	FString SessionThreadId;
};
