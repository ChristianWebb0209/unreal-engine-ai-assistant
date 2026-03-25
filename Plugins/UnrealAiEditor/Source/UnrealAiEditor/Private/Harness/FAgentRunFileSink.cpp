#include "Harness/FAgentRunFileSink.h"

#include "Context/AgentContextTypes.h"
#include "Context/AgentContextFormat.h"
#include "Context/IAgentContextService.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FAgentRunFileSink::FAgentRunFileSink(
	FString InJsonlPath,
	IAgentContextService* InContextService,
	const FString& InProjectId,
	const FString& InThreadId,
	const bool bInDumpAfterTool,
	const bool bInDumpOnFinished,
	FEvent* InDoneEvent,
	bool* InOutSuccess,
	FString* InOutFinishError)
	: JsonlPath(MoveTemp(InJsonlPath))
	, ContextService(InContextService)
	, ProjectId(InProjectId)
	, ThreadId(InThreadId)
	, bDumpContextAfterEachTool(bInDumpAfterTool)
	, bDumpContextOnRunFinished(bInDumpOnFinished)
	, DoneEvent(InDoneEvent)
	, CompletionSuccessPtr(InOutSuccess)
	, CompletionErrorPtr(InOutFinishError)
{
}

void FAgentRunFileSink::AppendJsonObject(const TSharedPtr<FJsonObject>& Obj)
{
	if (!Obj.IsValid())
	{
		return;
	}
	FString Line;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	Line += TEXT("\n");
	FFileHelper::SaveStringToFile(Line, *JsonlPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}

void FAgentRunFileSink::MaybeDumpContextWindow(const TCHAR* Reason)
{
	if (!ContextService)
	{
		return;
	}
	FAgentContextBuildOptions Opt;
	Opt.Mode = EUnrealAiAgentMode::Agent;
	const FAgentContextBuildResult Built = ContextService->BuildContextWindow(Opt);
	const FString Base = FPaths::GetPath(JsonlPath) / FString::Printf(TEXT("context_window_%s.txt"), Reason);
	FFileHelper::SaveStringToFile(Built.ContextBlock, *Base, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void FAgentRunFileSink::OnRunStarted(const FUnrealAiRunIds& Ids)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("run_started"));
	O->SetStringField(TEXT("run_id"), Ids.RunId.ToString(EGuidFormats::DigitsWithHyphens));
	AppendJsonObject(O);
}

void FAgentRunFileSink::OnContextUserMessages(const TArray<FString>& Messages)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("context_user_messages"));
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FString& M : Messages)
	{
		Arr.Add(MakeShared<FJsonValueString>(M));
	}
	O->SetArrayField(TEXT("messages"), Arr);
	AppendJsonObject(O);
}

void FAgentRunFileSink::OnAssistantDelta(const FString& Chunk)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("assistant_delta"));
	O->SetStringField(TEXT("chunk"), Chunk);
	AppendJsonObject(O);
}

void FAgentRunFileSink::OnThinkingDelta(const FString& Chunk)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("thinking_delta"));
	O->SetStringField(TEXT("chunk"), Chunk);
	AppendJsonObject(O);
}

void FAgentRunFileSink::OnToolCallStarted(const FString& ToolName, const FString& CallId, const FString& ArgumentsJson)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("tool_start"));
	O->SetStringField(TEXT("tool"), ToolName);
	O->SetStringField(TEXT("call_id"), CallId);
	O->SetStringField(TEXT("arguments_json"), ArgumentsJson);
	AppendJsonObject(O);
}

void FAgentRunFileSink::OnToolCallFinished(
	const FString& ToolName,
	const FString& CallId,
	const bool bSuccess,
	const FString& ResultPreview,
	const TSharedPtr<FUnrealAiToolEditorPresentation>& EditorPresentation)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("tool_finish"));
	O->SetStringField(TEXT("tool"), ToolName);
	O->SetStringField(TEXT("call_id"), CallId);
	O->SetBoolField(TEXT("success"), bSuccess);
	O->SetStringField(TEXT("result_preview"), ResultPreview);
	AppendJsonObject(O);
	if (bDumpContextAfterEachTool)
	{
		const FString Reason = FString::Printf(TEXT("after_tool_%s"), *ToolName);
		MaybeDumpContextWindow(*Reason);
	}
}

void FAgentRunFileSink::OnEditorBlockingDialogDuringTools(const FString& Summary)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("editor_blocking_dialog"));
	O->SetStringField(TEXT("summary"), Summary);
	AppendJsonObject(O);
}

void FAgentRunFileSink::OnRunContinuation(const int32 PhaseIndex, const int32 TotalPhasesHint)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("continuation"));
	O->SetNumberField(TEXT("phase_index"), static_cast<double>(PhaseIndex));
	O->SetNumberField(TEXT("total_phases_hint"), static_cast<double>(TotalPhasesHint));
	AppendJsonObject(O);
}

void FAgentRunFileSink::OnTodoPlanEmitted(const FString& Title, const FString& PlanJson)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("todo_plan"));
	O->SetStringField(TEXT("title"), Title);
	O->SetStringField(TEXT("plan_json"), PlanJson);
	AppendJsonObject(O);
}

void FAgentRunFileSink::OnRunFinished(const bool bSuccess, const FString& ErrorMessage)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("run_finished"));
	O->SetBoolField(TEXT("success"), bSuccess);
	O->SetStringField(TEXT("error_message"), ErrorMessage);
	AppendJsonObject(O);
	if (bDumpContextOnRunFinished)
	{
		MaybeDumpContextWindow(TEXT("run_finished"));
	}
	if (CompletionSuccessPtr)
	{
		*CompletionSuccessPtr = bSuccess;
	}
	if (CompletionErrorPtr)
	{
		*CompletionErrorPtr = ErrorMessage;
	}
	if (DoneEvent)
	{
		DoneEvent->Trigger();
	}
}
