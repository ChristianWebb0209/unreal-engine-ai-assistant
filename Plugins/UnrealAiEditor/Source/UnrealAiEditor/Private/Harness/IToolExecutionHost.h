#pragma once

#include "CoreMinimal.h"

struct FUnrealAiToolEditorPresentation;

/** Result returned to the agent loop for injection as a `tool` message. */
struct FUnrealAiToolInvocationResult
{
	bool bOk = false;
	FString ContentForModel;
	FString ErrorMessage;
	/** Optional UI-only payload for richer tool cards (markdown, images, navigation links). */
	TSharedPtr<FUnrealAiToolEditorPresentation> EditorPresentation;
};

/** Executes tools on behalf of the harness (typically game-thread editor work). */
class IToolExecutionHost
{
public:
	virtual ~IToolExecutionHost() = default;

	virtual FUnrealAiToolInvocationResult InvokeTool(
		const FString& ToolName,
		const FString& ArgumentsJson,
		const FString& ToolCallId) = 0;

	/** Optional: bind project/thread for context-backed tools (default no-op). */
	virtual void SetToolSession(const FString& ProjectId, const FString& ThreadId) {}
};
