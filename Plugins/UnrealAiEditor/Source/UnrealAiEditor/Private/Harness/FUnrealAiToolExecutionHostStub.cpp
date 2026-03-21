#include "Harness/FUnrealAiToolExecutionHostStub.h"

FUnrealAiToolInvocationResult FUnrealAiToolExecutionHostStub::InvokeTool(
	const FString& ToolName,
	const FString& ArgumentsJson,
	const FString& ToolCallId)
{
	FUnrealAiToolInvocationResult R;
	R.bOk = true;
	R.ContentForModel = FString::Printf(
		TEXT("{\"ok\":true,\"stub\":true,\"tool\":\"%s\"}"),
		*ToolName.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\"")));
	return R;
}
