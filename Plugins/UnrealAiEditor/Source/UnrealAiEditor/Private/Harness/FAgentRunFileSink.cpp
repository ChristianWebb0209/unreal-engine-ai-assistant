#include "Harness/FAgentRunFileSink.h"

#include "Context/AgentContextTypes.h"
#include "Context/AgentContextFormat.h"
#include "Context/IAgentContextService.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
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
	Opt.ContextBuildInvocationReason = FString::Printf(TEXT("harness_dump_%s"), Reason);
	{
		const FString EnvVerbose = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_CONTEXT_VERBOSE"));
		const bool bVerbose = EnvVerbose.Equals(TEXT("1"), ESearchCase::IgnoreCase)
			|| EnvVerbose.Equals(TEXT("true"), ESearchCase::IgnoreCase)
			|| EnvVerbose.Equals(TEXT("yes"), ESearchCase::IgnoreCase);
		Opt.bVerboseContextBuild = bVerbose;
	}
	const FAgentContextBuildResult Built = ContextService->BuildContextWindow(Opt);
	const FString BaseDir = FPaths::GetPath(JsonlPath);
	const FString Base = BaseDir / FString::Printf(TEXT("context_window_%s.txt"), Reason);
	FFileHelper::SaveStringToFile(Built.ContextBlock, *Base, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	{
		TSharedPtr<FJsonObject> TraceJson = MakeShared<FJsonObject>();
		TraceJson->SetStringField(TEXT("reason"), Reason);
		TraceJson->SetStringField(TEXT("project_id"), ProjectId);
		TraceJson->SetStringField(TEXT("thread_id"), ThreadId);
		TraceJson->SetNumberField(TEXT("context_chars"), static_cast<double>(Built.ContextBlock.Len()));
		TArray<TSharedPtr<FJsonValue>> WarningArr;
		for (const FString& W : Built.Warnings)
		{
			WarningArr.Add(MakeShared<FJsonValueString>(W));
		}
		TraceJson->SetArrayField(TEXT("warnings"), WarningArr);
		TArray<TSharedPtr<FJsonValue>> VerboseArr;
		for (const FString& L : Built.VerboseTraceLines)
		{
			VerboseArr.Add(MakeShared<FJsonValueString>(L));
		}
		TraceJson->SetArrayField(TEXT("verbose_trace_lines"), VerboseArr);
		FString TraceJsonStr;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> JsonWriter =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&TraceJsonStr);
		FJsonSerializer::Serialize(TraceJson.ToSharedRef(), JsonWriter);
		const FString TraceJsonPath = BaseDir / FString::Printf(TEXT("context_build_trace_%s.json"), Reason);
		FFileHelper::SaveStringToFile(TraceJsonStr + TEXT("\n"), *TraceJsonPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	if (Opt.bVerboseContextBuild && Built.VerboseTraceLines.Num() > 0)
	{
		const FString TracePath = BaseDir / FString::Printf(TEXT("context_build_trace_%s.txt"), Reason);
		FString Trace;
		Trace += FString::Printf(TEXT("Context build trace (reason=%s)\n"), Reason);
		if (Built.Warnings.Num() > 0)
		{
			Trace += FString::Printf(TEXT("Warnings (%d)\n"), Built.Warnings.Num());
			for (const FString& W : Built.Warnings)
			{
				Trace += FString::Printf(TEXT("- %s\n"), *W);
			}
			Trace += TEXT("\n");
		}
		for (const FString& L : Built.VerboseTraceLines)
		{
			Trace += L + TEXT("\n");
		}
		FFileHelper::SaveStringToFile(Trace, *TracePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

void FAgentRunFileSink::OnRunStarted(const FUnrealAiRunIds& Ids)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("run_started"));
	O->SetStringField(TEXT("run_id"), Ids.RunId.ToString(EGuidFormats::DigitsWithHyphens));
	AppendJsonObject(O);
	MaybeDumpContextWindow(TEXT("run_started"));
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
	const FString Reason = FString::Printf(TEXT("continuation_%d"), PhaseIndex);
	MaybeDumpContextWindow(*Reason);
}

void FAgentRunFileSink::OnTodoPlanEmitted(const FString& Title, const FString& PlanJson)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("todo_plan"));
	O->SetStringField(TEXT("title"), Title);
	O->SetStringField(TEXT("plan_json"), PlanJson);
	AppendJsonObject(O);
}

void FAgentRunFileSink::OnPlanningDecision(
	const FString& ModeUsed,
	const TArray<FString>& TriggerReasons,
	const int32 ReplanCount,
	const int32 QueueStepsPending)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("planning_decision"));
	O->SetStringField(TEXT("mode_used"), ModeUsed);
	TArray<TSharedPtr<FJsonValue>> Reasons;
	for (const FString& R : TriggerReasons)
	{
		Reasons.Add(MakeShared<FJsonValueString>(R));
	}
	O->SetArrayField(TEXT("trigger_reasons"), Reasons);
	O->SetNumberField(TEXT("replan_count"), static_cast<double>(ReplanCount));
	O->SetNumberField(TEXT("queue_steps_pending"), static_cast<double>(QueueStepsPending));
	AppendJsonObject(O);
}

void FAgentRunFileSink::OnRunFinished(const bool bSuccess, const FString& ErrorMessage)
{
	bool bExpected = false;
	if (!bFinished.compare_exchange_strong(bExpected, true, std::memory_order_relaxed))
	{
		return;
	}
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
