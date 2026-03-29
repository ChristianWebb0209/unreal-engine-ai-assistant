#include "Harness/FAgentRunFileSink.h"

#include "Context/AgentContextTypes.h"
#include "Context/AgentContextFormat.h"
#include "Context/IAgentContextService.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Logging/LogMacros.h"
#include "Harness/UnrealAiConversationJson.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/UnrealAiHarnessProgressTelemetry.h"
#include "Misc/UnrealAiRuntimeDefaults.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Prompt/UnrealAiPromptAssemblyStrategy.h"
#include "UnrealAiEditorSettings.h"

namespace UnrealAiAgentRunFileSinkPriv
{
	static FString AgentModeString(const EUnrealAiAgentMode Mode)
	{
		switch (Mode)
		{
		case EUnrealAiAgentMode::Ask:
			return TEXT("ask");
		case EUnrealAiAgentMode::Agent:
			return TEXT("agent");
		case EUnrealAiAgentMode::Plan:
			return TEXT("plan");
		default:
			return TEXT("unknown");
		}
	}

	static bool ShouldLogLlmRequestsToHarnessRunFile()
	{
		const FString Env = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_LOG_LLM_REQUESTS")).TrimStartAndEnd();
		if (!Env.IsEmpty())
		{
			if (Env == TEXT("1") || Env.Equals(TEXT("true"), ESearchCase::IgnoreCase))
			{
				return true;
			}
			if (Env == TEXT("0") || Env.Equals(TEXT("false"), ESearchCase::IgnoreCase))
			{
				return false;
			}
		}
		if (const UUnrealAiEditorSettings* S = GetDefault<UUnrealAiEditorSettings>())
		{
			return S->bLogLlmRequestsToHarnessRunFile;
		}
		return false;
	}
}

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

FAgentRunFileSink::~FAgentRunFileSink()
{
	// If Succeed()/Fail() never reached OnRunFinished, headed batch scripts stall on missing `"type":"run_finished"`.
	// If OnRunFinished ran but disk append failed, bFinished was released so this can retry.
	// After a successful terminal write, compare_exchange in OnRunFinished blocks duplicate lines.
	OnRunFinished(
		false,
		TEXT("Harness run sink destroyed before normal completion; run.jsonl may be truncated."));
}

void FAgentRunFileSink::AppendHarnessDiagnosticJson(const TSharedPtr<FJsonObject>& Obj)
{
	AppendJsonObject(Obj);
}

bool FAgentRunFileSink::AppendJsonObject(const TSharedPtr<FJsonObject>& Obj, const bool bLogOnFailure)
{
	if (!Obj.IsValid())
	{
		return false;
	}
	FString Line;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	Line += TEXT("\n");
	const bool bOk = FFileHelper::SaveStringToFile(
		Line,
		*JsonlPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(),
		FILEWRITE_Append);
	if (!bOk && bLogOnFailure)
	{
		UE_LOG(LogTemp, Warning, TEXT("UnrealAi harness: AppendJsonObject failed for %s"), *JsonlPath);
	}
	return bOk;
}

bool FAgentRunFileSink::AppendRunFinishedLineWithRetry(const TSharedPtr<FJsonObject>& Obj)
{
	for (int32 Attempt = 0; Attempt < 4; ++Attempt)
	{
		if (Attempt > 0)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		if (AppendJsonObject(Obj, false))
		{
			return true;
		}
	}
	UE_LOG(LogTemp, Error, TEXT("UnrealAi harness: failed to append run_finished to %s after retries"), *JsonlPath);
	return false;
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
		bool bVerboseContext = UnrealAiRuntimeDefaults::HarnessContextVerboseDefault;
		{
			const FString V = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_HARNESS_VERBOSE_CONTEXT")).TrimStartAndEnd();
			if (V == TEXT("0") || V.Equals(TEXT("false"), ESearchCase::IgnoreCase))
			{
				bVerboseContext = false;
			}
			else if (V == TEXT("1") || V.Equals(TEXT("true"), ESearchCase::IgnoreCase))
			{
				bVerboseContext = true;
			}
		}
		Opt.bVerboseContextBuild = bVerboseContext;
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
	{
		const EUnrealAiPromptAssemblyKind PromptKind = UnrealAiPromptAssembly::GetEffectiveAssemblyKind();
		O->SetStringField(TEXT("prompt_assembly_kind"), UnrealAiPromptAssembly::KindToTelemetryString(PromptKind));
	}
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
	++PlanSubTurnCompleteCount;
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("type"), TEXT("plan_harness_sub_turn_complete"));
		O->SetNumberField(TEXT("sub_turn_index"), static_cast<double>(PlanSubTurnCompleteCount));
		O->SetStringField(TEXT("utc_iso8601"), FDateTime::UtcNow().ToIso8601());
		O->SetStringField(TEXT("note"), TEXT("Planner or plan-node RunTurn finished; sync wait resets for next segment."));
		AppendJsonObject(O);
	}
	// Do not call MaybeDumpContextWindow here: it runs on the game thread and duplicates OnRunContinuation
	// (context_window_continuation_*) while blocking the next LLM segment; can stall or trip watchdogs.
	NotifyPlanHarnessSyncSegmentBoundary();
}

void FAgentRunFileSink::OnLlmRequestPreparedForHttp(
	const FUnrealAiAgentTurnRequest& TurnRequest,
	const FGuid& RunId,
	const int32 LlmRound,
	const int32 EffectiveMaxLlmRounds,
	const FUnrealAiLlmRequest& LlmRequest)
{
	using namespace UnrealAiAgentRunFileSinkPriv;
	// FAgentRunFileSink is headed-harness / automation only: log full prompts unless explicitly disabled.
	if (!ShouldLogLlmRequestsToHarnessRunFile())
	{
		return;
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("type"), TEXT("llm_request"));
	O->SetStringField(TEXT("utc_iso8601"), FDateTime::UtcNow().ToIso8601());
	O->SetStringField(TEXT("run_id"), RunId.ToString(EGuidFormats::DigitsWithHyphens));
	O->SetStringField(TEXT("project_id"), ProjectId);
	O->SetStringField(TEXT("thread_id"), TurnRequest.ThreadId);
	O->SetStringField(TEXT("agent_mode"), AgentModeString(TurnRequest.Mode));
	O->SetStringField(TEXT("model_profile_id"), TurnRequest.ModelProfileId);
	{
		const EUnrealAiPromptAssemblyKind PromptKind = UnrealAiPromptAssembly::GetEffectiveAssemblyKind();
		O->SetStringField(TEXT("prompt_assembly_kind"), UnrealAiPromptAssembly::KindToTelemetryString(PromptKind));
	}
	O->SetStringField(TEXT("user_text"), TurnRequest.UserText);
	O->SetNumberField(TEXT("llm_round"), static_cast<double>(LlmRound));
	O->SetNumberField(TEXT("effective_max_llm_rounds"), static_cast<double>(EffectiveMaxLlmRounds));
	O->SetStringField(TEXT("api_model"), LlmRequest.ApiModelName);
	O->SetBoolField(TEXT("stream"), LlmRequest.bStream);
	O->SetNumberField(TEXT("max_output_tokens"), static_cast<double>(LlmRequest.MaxOutputTokens));
	O->SetNumberField(TEXT("http_timeout_override_sec"), static_cast<double>(LlmRequest.HttpTimeoutOverrideSec));
	O->SetStringField(TEXT("api_base_url"), LlmRequest.ApiBaseUrl);
	O->SetBoolField(TEXT("api_key_redacted"), !LlmRequest.ApiKey.IsEmpty());

	FString MsgArrStr;
	if (UnrealAiConversationJson::MessagesToChatCompletionsJsonArray(LlmRequest.Messages, MsgArrStr))
	{
		TArray<TSharedPtr<FJsonValue>> MsgArr;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MsgArrStr);
		if (FJsonSerializer::Deserialize(Reader, MsgArr))
		{
			O->SetArrayField(TEXT("messages"), MsgArr);
		}
		else
		{
			O->SetStringField(TEXT("messages_serialize_note"), TEXT("messages_json_deserialize_failed_after_build"));
		}
	}
	else
	{
		O->SetStringField(TEXT("messages_serialize_note"), TEXT("messages_to_chat_completions_array_failed"));
	}

	const FString ToolsTrim = LlmRequest.ToolsJsonArray.TrimStartAndEnd();
	if (!ToolsTrim.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> ToolsArr;
		TSharedRef<TJsonReader<>> ToolsReader = TJsonReaderFactory<>::Create(ToolsTrim);
		if (FJsonSerializer::Deserialize(ToolsReader, ToolsArr))
		{
			O->SetArrayField(TEXT("tools"), ToolsArr);
		}
		else
		{
			O->SetStringField(TEXT("tools_json_raw"), LlmRequest.ToolsJsonArray);
		}
	}

	FString Line;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
	FJsonSerializer::Serialize(O.ToSharedRef(), Writer);
	Line += TEXT("\n");
	const FString Path = FPaths::Combine(FPaths::GetPath(JsonlPath), TEXT("llm_requests.jsonl"));
	FFileHelper::SaveStringToFile(Line, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
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
	const bool bAppendOk = AppendRunFinishedLineWithRetry(O);
	// If append failed after claiming bFinished, release so ~FAgentRunFileSink can retry OnRunFinished (otherwise
	// compare_exchange blocks a second write and run.jsonl may lack run_finished entirely).
	if (!bAppendOk)
	{
		bFinished.store(false, std::memory_order_relaxed);
		UE_LOG(LogTemp, Warning, TEXT("UnrealAi harness: run_finished append failed; released finish slot for destructor retry. %s"), *JsonlPath);
	}
	// BuildContextWindow for "run_finished" blocks DoneEvent; keep off unless explicitly requested.
	bool bDumpRunFinishedContext = UnrealAiRuntimeDefaults::HarnessRunFinishedContextDump;
	{
		const FString E = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_HARNESS_DUMP_CONTEXT_ON_FINISH")).TrimStartAndEnd();
		if (E == TEXT("1") || E.Equals(TEXT("true"), ESearchCase::IgnoreCase))
		{
			bDumpRunFinishedContext = true;
		}
		else if (E == TEXT("0") || E.Equals(TEXT("false"), ESearchCase::IgnoreCase))
		{
			bDumpRunFinishedContext = false;
		}
	}
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
