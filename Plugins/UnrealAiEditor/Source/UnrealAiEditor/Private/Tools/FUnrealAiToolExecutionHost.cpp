#include "Tools/FUnrealAiToolExecutionHost.h"

#include "Backend/UnrealAiBackendRegistry.h"
#include "Tools/UnrealAiToolDispatch.h"
#include "Tools/UnrealAiToolFocus.h"
#include "Tools/UnrealAiToolInvocationArgs.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void FUnrealAiToolExecutionHost::ReloadCatalog()
{
	Catalog.LoadFromPlugin();
}

FUnrealAiToolExecutionHost::FUnrealAiToolExecutionHost(FUnrealAiBackendRegistry* InRegistry)
	: Registry(InRegistry)
{
	if (!Catalog.LoadFromPlugin())
	{
		UE_LOG(LogTemp, Warning, TEXT("UnrealAiToolCatalog: failed to load Resources/UnrealAiToolCatalog.json"));
	}
}

void FUnrealAiToolExecutionHost::SetToolSession(const FString& ProjectId, const FString& ThreadId)
{
	SessionProjectId = ProjectId;
	SessionThreadId = ThreadId;
}

FUnrealAiToolInvocationResult FUnrealAiToolExecutionHost::InvokeTool(
	const FString& ToolName,
	const FString& ArgumentsJson,
	const FString& ToolCallId)
{
	(void)ToolCallId;
	TSharedPtr<FJsonObject> Args;
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgumentsJson);
		if (!FJsonSerializer::Deserialize(Reader, Args) || !Args.IsValid())
		{
			Args = MakeShared<FJsonObject>();
		}
	}

	const bool bFocused = UnrealAiConsumeFocusedFlag(Args);
	const TSharedPtr<FJsonObject> Entry = Catalog.FindToolDefinition(ToolName);
	FUnrealAiToolInvocationResult Result =
		UnrealAiDispatchTool(ToolName, Args, Entry, Registry, SessionProjectId, SessionThreadId);
	if (Result.bOk && bFocused)
	{
		UnrealAiApplyPostToolEditorFocus(ToolName, Args, Result);
	}
	return Result;
}
