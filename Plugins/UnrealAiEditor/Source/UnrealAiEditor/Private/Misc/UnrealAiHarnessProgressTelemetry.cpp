#include "Misc/UnrealAiHarnessProgressTelemetry.h"

#include "Dom/JsonObject.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformTime.h"

namespace UnrealAiHarnessProgressTelemetry
{
	namespace
	{
		FCriticalSection Mutex;
		double GRunStartSec = 0.0;
		double GLastAssistantDeltaSec = 0.0;
		double GLastThinkingDeltaSec = 0.0;
		double GLastToolFinishSec = 0.0;
		FString GLastToolName;
		double GLastLlmSubmitSec = 0.0;
		double GLastHttpResponseCompleteSec = 0.0;
		int32 GPlanSubTurnCompleteCount = 0;

		static double AgeSec(const double LastSec, const double NowSec)
		{
			if (LastSec <= 0.0)
			{
				return -1.0;
			}
			return NowSec - LastSec;
		}

		static void SetNumberOrNegOne(TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, double SecOrNegOne)
		{
			if (SecOrNegOne < 0.0)
			{
				Obj->SetNumberField(Field, -1.0);
			}
			else
			{
				Obj->SetNumberField(Field, SecOrNegOne);
			}
		}
	} // namespace

	void ResetForRun()
	{
		FScopeLock Lock(&Mutex);
		const double Now = FPlatformTime::Seconds();
		GRunStartSec = Now;
		GLastAssistantDeltaSec = 0.0;
		GLastThinkingDeltaSec = 0.0;
		GLastToolFinishSec = 0.0;
		GLastToolName.Reset();
		GLastLlmSubmitSec = 0.0;
		GLastHttpResponseCompleteSec = 0.0;
		GPlanSubTurnCompleteCount = 0;
	}

	void NotifyAssistantDelta()
	{
		FScopeLock Lock(&Mutex);
		GLastAssistantDeltaSec = FPlatformTime::Seconds();
	}

	void NotifyThinkingDelta()
	{
		FScopeLock Lock(&Mutex);
		GLastThinkingDeltaSec = FPlatformTime::Seconds();
	}

	void NotifyToolFinish(const FString& ToolName)
	{
		FScopeLock Lock(&Mutex);
		GLastToolFinishSec = FPlatformTime::Seconds();
		GLastToolName = ToolName;
	}

	void NotifyLlmSubmit()
	{
		FScopeLock Lock(&Mutex);
		GLastLlmSubmitSec = FPlatformTime::Seconds();
	}

	void NotifyHttpResponseComplete()
	{
		FScopeLock Lock(&Mutex);
		GLastHttpResponseCompleteSec = FPlatformTime::Seconds();
	}

	void NotifyPlanSubTurnComplete()
	{
		FScopeLock Lock(&Mutex);
		++GPlanSubTurnCompleteCount;
	}

	TSharedPtr<FJsonObject> BuildHarnessSyncTimeoutDiagnosticJson(
		const FString& AgentModeLabel,
		const uint32 SyncWaitMs,
		const int32 PlanSubTurnCompletionsBeforeTimeout,
		const bool bPlanMode)
	{
		double NowSec = 0.0;
		double RunStartSec = 0.0;
		double LastAssistant = 0.0;
		double LastThinking = 0.0;
		double LastTool = 0.0;
		FString LastToolNameCopy;
		double LastLlm = 0.0;
		double LastHttp = 0.0;
		int32 PlanSubTurnCount = 0;
		{
			FScopeLock Lock(&Mutex);
			NowSec = FPlatformTime::Seconds();
			RunStartSec = GRunStartSec;
			LastAssistant = GLastAssistantDeltaSec;
			LastThinking = GLastThinkingDeltaSec;
			LastTool = GLastToolFinishSec;
			LastToolNameCopy = GLastToolName;
			LastLlm = GLastLlmSubmitSec;
			LastHttp = GLastHttpResponseCompleteSec;
			PlanSubTurnCount = GPlanSubTurnCompleteCount;
		}

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("type"), TEXT("harness_sync_timeout_diagnostic"));
		O->SetStringField(TEXT("agent_mode"), AgentModeLabel);
		O->SetBoolField(TEXT("plan_mode"), bPlanMode);
		O->SetNumberField(TEXT("sync_wait_ms"), static_cast<double>(SyncWaitMs));
		O->SetNumberField(TEXT("plan_sub_turn_completions_before_timeout"), static_cast<double>(PlanSubTurnCompletionsBeforeTimeout));
		O->SetNumberField(TEXT("plan_sub_turn_signals_total"), static_cast<double>(PlanSubTurnCount));
		O->SetNumberField(TEXT("wall_now_sec"), NowSec);
		O->SetNumberField(TEXT("run_start_sec"), RunStartSec);
		SetNumberOrNegOne(O, TEXT("seconds_since_run_start"), RunStartSec > 0.0 ? (NowSec - RunStartSec) : -1.0);
		SetNumberOrNegOne(O, TEXT("seconds_since_last_assistant_delta"), AgeSec(LastAssistant, NowSec));
		SetNumberOrNegOne(O, TEXT("seconds_since_last_thinking_delta"), AgeSec(LastThinking, NowSec));
		SetNumberOrNegOne(O, TEXT("seconds_since_last_tool_finish"), AgeSec(LastTool, NowSec));
		O->SetStringField(TEXT("last_tool_name"), LastToolNameCopy);
		SetNumberOrNegOne(O, TEXT("seconds_since_last_llm_submit"), AgeSec(LastLlm, NowSec));
		SetNumberOrNegOne(O, TEXT("seconds_since_last_http_response_complete"), AgeSec(LastHttp, NowSec));

		// Heuristic tags for batch review (not authoritative).
		const double AgeHttp = AgeSec(LastHttp, NowSec);
		const double AgeTool = AgeSec(LastTool, NowSec);
		FString StallHint = TEXT("unknown");
		if (LastLlm > 0.0 && LastHttp > 0.0 && LastLlm > LastHttp + 0.01)
		{
			StallHint = TEXT("likely_waiting_on_http_after_llm_submit");
		}
		else if (LastHttp > 0.0 && AgeHttp > 5.0 && LastTool > 0.0 && LastTool > LastHttp)
		{
			StallHint = TEXT("likely_post_http_tool_or_editor_work");
		}
		else if (LastTool > 0.0 && AgeTool > 5.0 && (LastAssistant <= 0.0 || LastAssistant < LastTool))
		{
			StallHint = TEXT("possible_stall_after_tool_before_new_model_tokens");
		}
		O->SetStringField(TEXT("stall_hint"), StallHint);
		O->SetStringField(
			TEXT("interpretation_notes"),
			TEXT("OpenAI transport delivers the full SSE body on one completion callback; seconds_since_last_http_response_complete "
				 "updates when that fires. Long gaps with recent llm_submit suggest blocked HTTP; gaps after tool_finish suggest tools/editor."));
		return O;
	}

	FString FormatHarnessSyncTimeoutLogLine(
		const FString& AgentModeLabel,
		const uint32 SyncWaitMs,
		const int32 PlanSubTurnCompletionsBeforeTimeout,
		const bool bPlanMode)
	{
		TSharedPtr<FJsonObject> J = BuildHarnessSyncTimeoutDiagnosticJson(
			AgentModeLabel,
			SyncWaitMs,
			PlanSubTurnCompletionsBeforeTimeout,
			bPlanMode);
		if (!J.IsValid())
		{
			return TEXT("UnrealAi harness: sync timeout (telemetry unavailable)");
		}
		double SinceHttp = -1.0;
		double SinceTool = -1.0;
		double SinceLlm = -1.0;
		J->TryGetNumberField(TEXT("seconds_since_last_http_response_complete"), SinceHttp);
		J->TryGetNumberField(TEXT("seconds_since_last_tool_finish"), SinceTool);
		J->TryGetNumberField(TEXT("seconds_since_last_llm_submit"), SinceLlm);
		FString Hint;
		J->TryGetStringField(TEXT("stall_hint"), Hint);
		FString LastToolName;
		J->TryGetStringField(TEXT("last_tool_name"), LastToolName);
		return FString::Printf(
			TEXT("UnrealAi harness: sync_wait_timeout diagnostic mode=%s plan=%s wait_ms=%u plan_sub_turns=%d "
				 "age_sec(http=%.2f tool=%.2f llm_submit=%.2f) last_tool=%s hint=%s"),
			*AgentModeLabel,
			bPlanMode ? TEXT("yes") : TEXT("no"),
			SyncWaitMs,
			PlanSubTurnCompletionsBeforeTimeout,
			SinceHttp,
			SinceTool,
			SinceLlm,
			LastToolName.IsEmpty() ? TEXT("<none>") : *LastToolName,
			Hint.IsEmpty() ? TEXT("<none>") : *Hint);
	}
} // namespace UnrealAiHarnessProgressTelemetry
