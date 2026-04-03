#include "Tools/FUnrealAiToolExecutionHost.h"

#include "UnrealAiEditorModule.h"
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

	// Optional per-call opt-out: "focused": false suppresses follow navigation when global Editor focus is on.
	bool bSuppressEditorFollow = false;
	if (Args.IsValid() && Args->HasField(TEXT("focused")))
	{
		bool bFocusedField = true;
		if (Args->TryGetBoolField(TEXT("focused"), bFocusedField) && !bFocusedField)
		{
			bSuppressEditorFollow = true;
		}
	}
	UnrealAiConsumeFocusedFlag(Args);
	const TSharedPtr<FJsonObject> Entry = Catalog.FindToolDefinition(ToolName);
	FUnrealAiToolInvocationResult Result =
		UnrealAiDispatchTool(ToolName, Args, Entry, Registry, SessionProjectId, SessionThreadId);
	// When Editor focus is enabled, automatically open/foreground relevant editors after successful
	// blueprint/asset/content tools (models rarely pass focused:true). Explicit focused:false opts out.
	if (Result.bOk && FUnrealAiEditorModule::IsEditorFocusEnabled() && !bSuppressEditorFollow)
	{
		UnrealAiApplyPostToolEditorFocus(ToolName, Args, Result);
	}
	return Result;
}
