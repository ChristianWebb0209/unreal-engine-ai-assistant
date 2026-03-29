#include "Harness/FAgentRunFileSink.h"

#include "Context/AgentContextTypes.h"
#include "Context/AgentContextFormat.h"
#include "Context/IAgentContextService.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/UnrealAiHarnessProgressTelemetry.h"
#include "Misc/UnrealAiRuntimeDefaults.h"
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
	FString* InOutFinishError,
	FEvent* InPlanSubTurnEvent)
	: JsonlPath(MoveTemp(InJsonlPath))
	, ContextService(InContextService)
	, ProjectId(InProjectId)
	, ThreadId(InThreadId)
	, bDumpContextAfterEachTool(bInDumpAfterTool)
	, bDumpContextOnRunFinished(bInDumpOnFinished)
	, DoneEvent(InDoneEvent)
	, PlanSubTurnEvent(InPlanSubTurnEvent)
	, CompletionSuccessPtr(InOutSuccess)
	, CompletionErrorPtr(InOutFinishError)
{
}

void FAgentRunFileSink::AppendHarnessDiagnosticJson(const TSharedPtr<FJsonObject>& Obj)
{
	AppendJsonObject(Obj);
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
		Opt.bVerboseContextBuild = UnrealAiRuntimeDefaults::ContextVerboseDefault;
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
	UnrealAiHarnessProgressTelemetry::ResetForRun();
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
	UnrealAiHarnessProgressTelemetry::NotifyAssistantDelta();
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("assistant_delta"));
	O->SetStringField(TEXT("chunk"), Chunk);
	AppendJsonObject(O);
}

void FAgentRunFileSink::OnThinkingDelta(const FString& Chunk)
{
	UnrealAiHarnessProgressTelemetry::NotifyThinkingDelta();
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
	UnrealAiHarnessProgressTelemetry::NotifyToolFinish(ToolName);
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

void FAgentRunFileSink::OnEnforcementEvent(const FString& EventType, const FString& Detail)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("enforcement_event"));
	O->SetStringField(TEXT("event_type"), EventType);
	O->SetStringField(TEXT("detail"), Detail);
	AppendJsonObject(O);
}

void FAgentRunFileSink::OnEnforcementSummary(
	const int32 ActionIntentTurns,
	const int32 ActionTurnsWithToolCalls,
	const int32 ActionTurnsWithExplicitBlocker,
	const int32 ActionNoToolNudges,
	const int32 MutationIntentTurns,
	const int32 MutationReadOnlyNudges)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("enforcement_summary"));
	O->SetNumberField(TEXT("action_intent_turns"), static_cast<double>(ActionIntentTurns));
	O->SetNumberField(TEXT("action_turns_with_tool_calls"), static_cast<double>(ActionTurnsWithToolCalls));
	O->SetNumberField(TEXT("action_turns_with_explicit_blocker"), static_cast<double>(ActionTurnsWithExplicitBlocker));
	O->SetNumberField(TEXT("action_no_tool_nudges"), static_cast<double>(ActionNoToolNudges));
	O->SetNumberField(TEXT("mutation_intent_turns"), static_cast<double>(MutationIntentTurns));
	O->SetNumberField(TEXT("mutation_read_only_nudges"), static_cast<double>(MutationReadOnlyNudges));
	AppendJsonObject(O);
}

void FAgentRunFileSink::NotifyPlanHarnessSyncSegmentBoundary()
{
	UnrealAiHarnessProgressTelemetry::NotifyPlanSubTurnComplete();
	if (PlanSubTurnEvent)
	{
		PlanSubTurnEvent->Trigger();
	}
}

void FAgentRunFileSink::OnPlanHarnessSubTurnComplete()
{
	NotifyPlanHarnessSyncSegmentBoundary();
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
	// BuildContextWindow for "run_finished" can block on retrieval/vector index; the harness does not
	// signal DoneEvent until after this returns, so ExecCmds (e.g. Quit) is delayed and automation may
	// try to kill the editor while still inside this path. Opt-in only via env (default: skip).
	const bool bDumpRunFinishedContext = UnrealAiRuntimeDefaults::HarnessRunFinishedContextDump;
	if (bDumpContextOnRunFinished && bDumpRunFinishedContext)
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
