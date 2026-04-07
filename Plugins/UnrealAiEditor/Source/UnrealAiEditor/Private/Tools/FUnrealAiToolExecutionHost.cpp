#include "Tools/FUnrealAiToolExecutionHost.h"

#include "UnrealAiEditorModule.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Tools/UnrealAiToolDispatch.h"
#include "Tools/UnrealAiToolFocus.h"
#include "Tools/UnrealAiToolInvocationArgs.h"
#include "Tools/UnrealAiToolResolver.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAiToolExecPriv
{
thread_local int32 GInvokeToolNestingDepth = 0;
}

void FUnrealAiToolExecutionHost::ReloadCatalog()
{
	Catalog.LoadFromPlugin();
}

FUnrealAiToolExecutionHost::FUnrealAiToolExecutionHost(FUnrealAiBackendRegistry* InRegistry)
	: Registry(InRegistry)
{
	if (!Catalog.LoadFromPlugin())
	{
		UE_LOG(LogTemp, Warning, TEXT("UnrealAiToolCatalog: failed to load Resources/tools.main.json"));
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
	struct FDepthPop
	{
		~FDepthPop()
		{
			--UnrealAiToolExecPriv::GInvokeToolNestingDepth;
		}
	};
	++UnrealAiToolExecPriv::GInvokeToolNestingDepth;
	const FDepthPop DepthPop;

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
	FUnrealAiToolResolver Resolver(Catalog);
	const FUnrealAiResolvedToolInvocation Resolved = Resolver.Resolve(ToolName, Args);
	if (!Resolved.bResolved)
	{
		return Resolved.FailureResult;
	}

	FUnrealAiToolInvocationResult Result = UnrealAiDispatchTool(
		Resolved.LegacyToolId,
		Resolved.ResolvedArguments,
		Resolved.LegacyToolDefinition,
		Registry,
		SessionProjectId,
		SessionThreadId);
	FUnrealAiToolResolver::AttachAuditToResult(Resolved.Audit, Result);
	// Primary harness lane only (excludes plan workers / builder subagents). Nested InvokeTool: outermost applies.
	if (Result.bOk && FUnrealAiEditorModule::ShouldApplyHarnessEditorNavigation() && !bSuppressEditorFollow
		&& UnrealAiToolExecPriv::GInvokeToolNestingDepth == 1)
	{
		UnrealAiApplyPostToolEditorFocus(Resolved.LegacyToolId, Resolved.ResolvedArguments, Result);
	}
	return Result;
}
