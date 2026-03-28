#include "Harness/FUnrealAiAgentHarness.h"

#include "Async/Async.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Templates/SharedPointer.h"
#include "Tools/UnrealAiToolCatalog.h"
#include "Harness/FUnrealAiConversationStore.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Harness/IAgentRunSink.h"
#include "Harness/ILlmTransport.h"
#include "Harness/UnrealAiTurnLlmRequestBuilder.h"
#include "Harness/IToolExecutionHost.h"
#include "Context/IAgentContextService.h"
#include "Context/AgentContextTypes.h"
#include "Backend/FUnrealAiUsageTracker.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Memory/FUnrealAiMemoryCompactor.h"
#include "Memory/IUnrealAiMemoryService.h"
#include "HAL/PlatformMisc.h"
#include "Harness/UnrealAiLlmInvocationService.h"
#include "Misc/UnrealAiEditorModalMonitor.h"
#include "Widgets/UnrealAiToolUi.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include <atomic>

namespace UnrealAiAgentHarnessPriv
{
	static const TCHAR* GUnrealAiDispatchToolName = TEXT("unreal_ai_dispatch");

	/** Stop after this many consecutive identical tool failures (same invoke + args). */
	static constexpr int32 GHarnessRepeatedFailureStopCount = 4;
	/** Default token budget per agent turn when profile does not set maxAgentTurnTokens and env is unset. */
	static constexpr int32 GHarnessDefaultMaxTurnTokens = 500000;
	/** Hard backstop on LLM↔tool iterations if token/repeat limits do not apply first. */
	static constexpr int32 GHarnessMaxLlmRoundBackstop = 512;

	static int32 ReadEnvInt(const TCHAR* Name, const int32 DefaultValue, const int32 MinValue = 0, const int32 MaxValue = INT32_MAX)
	{
		const FString Raw = FPlatformMisc::GetEnvironmentVariable(Name);
		if (Raw.IsEmpty())
		{
			return DefaultValue;
		}
		const int32 Parsed = FCString::Atoi(*Raw);
		return FMath::Clamp(Parsed, MinValue, MaxValue);
	}

	static bool IsHarnessSyntheticUserMessage(const FString& Content)
	{
		return Content.StartsWith(TEXT("[Harness]"));
	}

	static FString GetLastRealUserMessage(const TArray<FUnrealAiConversationMessage>& Messages)
	{
		for (int32 i = Messages.Num() - 1; i >= 0; --i)
		{
			if (Messages[i].Role != TEXT("user"))
			{
				continue;
			}
			if (IsHarnessSyntheticUserMessage(Messages[i].Content))
			{
				continue;
			}
			return Messages[i].Content;
		}
		return FString();
	}

	static bool UserLikelyRequestsActionTool(const FString& UserText)
	{
		const FString T = UserText.ToLower();
		if (T.IsEmpty())
		{
			return false;
		}
		static const TCHAR* Tokens[] = {
			TEXT("run"), TEXT("start"), TEXT("stop"), TEXT("compile"), TEXT("save"),
			TEXT("open"), TEXT("re-open"), TEXT("reopen"), TEXT("fix"), TEXT("apply"),
			TEXT("change"), TEXT("adjust"), TEXT("tune"), TEXT("create"), TEXT("delete"),
			TEXT("playtest"), TEXT("test"), TEXT("resolve")
		};
		for (const TCHAR* K : Tokens)
		{
			if (T.Contains(K))
			{
				return true;
			}
		}
		return false;
	}

	static bool UserLikelyRequestsMutation(const FString& UserText)
	{
		const FString T = UserText.ToLower();
		if (T.IsEmpty())
		{
			return false;
		}
		static const TCHAR* Tokens[] = {
			TEXT("fix"), TEXT("apply"), TEXT("change"), TEXT("adjust"), TEXT("tune"),
			TEXT("reduce"), TEXT("increase"), TEXT("set "), TEXT("compile"), TEXT("save"),
			TEXT("resolve"), TEXT("make ")
		};
		for (const TCHAR* K : Tokens)
		{
			if (T.Contains(K))
			{
				return true;
			}
		}
		return false;
	}

	static bool AssistantContainsExplicitBlocker(const FString& AssistantText)
	{
		const FString T = AssistantText.ToLower();
		if (T.IsEmpty())
		{
			return false;
		}
		static const TCHAR* Tokens[] = {
			TEXT("blocked"),
			TEXT("blocker"),
			TEXT("cannot"),
			TEXT("can't"),
			TEXT("unable"),
			TEXT("failed"),
			TEXT("error"),
			TEXT("missing"),
			TEXT("not available"),
			TEXT("no access"),
			TEXT("permission"),
			TEXT("manual step"),
			TEXT("need you to"),
			TEXT("requires")
		};
		int32 Hits = 0;
		for (const TCHAR* K : Tokens)
		{
			if (T.Contains(K))
			{
				++Hits;
			}
		}
		return Hits > 0 && T.Len() >= 24;
	}

	static bool IsLikelyReadOnlyToolName(const FString& ToolName)
	{
		const FString T = ToolName.ToLower();
		return T.Contains(TEXT("_search"))
			|| T.Contains(TEXT("_query"))
			|| T.Contains(TEXT("_read"))
			|| T.Contains(TEXT("_get_"))
			|| T.Contains(TEXT("_list_"))
			|| T.Contains(TEXT("snapshot"))
			|| T.Contains(TEXT("_status"))
			|| T == TEXT("blueprint_export_ir");
	}

	static int32 CountConversationUserTurnsForMemory(const TArray<FUnrealAiConversationMessage>& Messages)
	{
		int32 Count = 0;
		for (const FUnrealAiConversationMessage& M : Messages)
		{
			if (M.Role != TEXT("user"))
			{
				continue;
			}
			// Skip harness synthetic nudges; count only real chat turns.
			if (M.Content.StartsWith(TEXT("[Harness]")))
			{
				continue;
			}
			++Count;
		}
		return Count;
	}

	/** Tools whose JSON payload is mostly an echo of assembled context — skip persisting to avoid nested duplication. */
	static bool ShouldPersistToolResultToContextState(const FString& InvokeName)
	{
		static const TCHAR* SkipToolIds[] = {
			// Returns BuildContextWindow output as `context_block`; snapshot + ranked candidates already cover this.
			TEXT("editor_state_snapshot_read"),
		};
		for (const TCHAR* Id : SkipToolIds)
		{
			if (InvokeName == Id)
			{
				return false;
			}
		}
		return true;
	}

	static bool UnwrapDispatchToolCall(const FUnrealAiToolCallSpec& Tc, FString& OutInvokeName, FString& OutInvokeArgsJson, FString& OutError)
	{
		if (Tc.Name != GUnrealAiDispatchToolName)
		{
			OutInvokeName = Tc.Name;
			OutInvokeArgsJson = Tc.ArgumentsJson;
			OutError.Reset();
			return true;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Tc.ArgumentsJson);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("unreal_ai_dispatch: invalid JSON arguments");
			return false;
		}
		if (!Root->TryGetStringField(TEXT("tool_id"), OutInvokeName) || OutInvokeName.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("unreal_ai_dispatch: missing or empty tool_id");
			return false;
		}
		OutInvokeName.TrimStartAndEndInline();
		const TSharedPtr<FJsonObject>* ArgsObjPtr = nullptr;
		if (Root->TryGetObjectField(TEXT("arguments"), ArgsObjPtr) && ArgsObjPtr && (*ArgsObjPtr).IsValid())
		{
			FString Out;
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
			if (FJsonSerializer::Serialize((*ArgsObjPtr).ToSharedRef(), W))
			{
				OutInvokeArgsJson = Out;
			}
			else
			{
				OutInvokeArgsJson = TEXT("{}");
			}
		}
		else
		{
			FString ArgsStr;
			if (Root->TryGetStringField(TEXT("arguments"), ArgsStr))
			{
				OutInvokeArgsJson = ArgsStr;
			}
			else
			{
				OutInvokeArgsJson = TEXT("{}");
			}
		}
		OutError.Reset();
		return true;
	}

	static bool ShouldRetryTransientTransportError(const FString& Msg)
	{
		if (Msg.IsEmpty())
		{
			return false;
		}
		const bool bTransportFail = Msg.Contains(TEXT("HTTP request failed"), ESearchCase::IgnoreCase)
			|| Msg.Contains(TEXT("Failed (Cancelled)"), ESearchCase::IgnoreCase);
		const bool bTransient = Msg.Contains(TEXT("Cancelled"), ESearchCase::IgnoreCase)
			|| Msg.Contains(TEXT("ConnectionError"), ESearchCase::IgnoreCase)
			|| Msg.Contains(TEXT("TimedOut"), ESearchCase::IgnoreCase)
			|| Msg.Contains(TEXT("Timeout"), ESearchCase::IgnoreCase);
		return bTransportFail && bTransient;
	}

	static void MergeToolCallDeltas(TArray<FUnrealAiToolCallSpec>& Acc, const TArray<FUnrealAiToolCallSpec>& Delta)
	{
		for (const FUnrealAiToolCallSpec& D : Delta)
		{
			if (D.StreamMergeIndex >= 0)
			{
				while (Acc.Num() <= D.StreamMergeIndex)
				{
					Acc.AddDefaulted();
				}
				FUnrealAiToolCallSpec& Slot = Acc[D.StreamMergeIndex];
				if (!D.Id.IsEmpty())
				{
					Slot.Id = D.Id;
				}
				if (!D.Name.IsEmpty())
				{
					Slot.Name = D.Name;
				}
				Slot.ArgumentsJson += D.ArgumentsJson;
			}
			else
			{
				Acc.Add(D);
			}
		}
	}

	struct FAgentTurnRunner : public TSharedFromThis<FAgentTurnRunner>
	{
		FUnrealAiAgentTurnRequest Request;
		TSharedPtr<IAgentRunSink> Sink;
		IUnrealAiPersistence* Persistence = nullptr;
		IAgentContextService* ContextService = nullptr;
		FUnrealAiModelProfileRegistry* Profiles = nullptr;
		FUnrealAiToolCatalog* Catalog = nullptr;
		TSharedPtr<ILlmTransport> Transport;
		IToolExecutionHost* Tools = nullptr;
		FUnrealAiUsageTracker* UsageTracker = nullptr;
		IUnrealAiMemoryService* MemoryService = nullptr;
		TUniquePtr<FUnrealAiConversationStore> Conv;

		FUnrealAiTokenUsage UsageThisRound;
		FUnrealAiTokenUsage AccumulatedUsage;

		FGuid RunId;
		int32 LlmRound = 0;
		double LastLlmSubmitSeconds = 0.0;
		/** From model profile + env; set each DispatchLlm (backstop iterations). */
		int32 EffectiveMaxLlmRounds = GHarnessMaxLlmRoundBackstop;
		/** Last resolved token budget for this turn (for near-budget hints in CompleteToolPath). */
		int32 EffectiveMaxTurnTokensHint = 0;
		/** Retries transient HTTP-level cancellations without consuming a round. */
		int32 TransientTransportRetryCountThisRound = 0;
		// Keep retries minimal so headed live runs fail fast instead of stalling for many minutes.
		static constexpr int32 MaxTransientTransportRetriesPerRound = 0;

		FString AssistantBuffer;
		TArray<FUnrealAiToolCallSpec> PendingToolCalls;
		TArray<int32> CompletedToolCallQueue;
		TSet<int32> EnqueuedToolCallIndices;
		TSet<FString> ExecutedToolCallIds;
		TMap<int32, int32> ToolCallFirstSeenEventCount;
		TMap<int32, double> ToolCallFirstSeenSeconds;
		FString LastToolFailureSignature;
		int32 RepeatedToolFailureCount = 0;
		TMap<FString, int32> ToolInvokeCountByName;
		TMap<FString, int32> ToolInvokeCountBySignature;
		// Counts repeated non-progress discovery/search results (e.g. empty low-confidence searches).
		// Used to trigger replan-or-stop earlier than max rounds.
		TMap<FString, int32> NonProgressEmptySearchCountByToolName;
		int32 ReplanCount = 0;
		int32 QueueStepsPending = 0;
		bool bFinishSeen = false;
		int32 ActionNoToolNudgeCount = 0;
		int32 MutationFollowthroughNudgeCount = 0;
		int32 ActionIntentTurnCount = 0;
		int32 ActionTurnsWithToolCallsCount = 0;
		int32 ActionTurnsWithExplicitBlockerCount = 0;
		int32 MutationIntentTurnCount = 0;
		bool bActionIntentCounted = false;
		bool bActionToolOutcomeCounted = false;
		bool bActionBlockerOutcomeCounted = false;
		bool bMutationIntentCounted = false;
		bool bToolExecutionInProgress = false;
		bool bAssistantToolCallMessageRecorded = false;
		bool bFinishReceived = false;
		FString FinishReason;
		int32 StreamToolEventCount = 0;
		int32 CurrentRoundToolSuccessCount = 0;
		int32 CurrentRoundToolFailCount = 0;
		bool bCurrentRoundRepeatedToolLoop = false;
		bool bCurrentRoundRepeatedEmptySearch = false;
		TArray<FString> CurrentRoundExecutedToolNames;
		std::atomic<bool> bCancelled{false};
		std::atomic<bool> bTerminal{false};

		void HandleEvent(const FUnrealAiLlmStreamEvent& Ev);
		void DispatchLlm(bool bRetrySameRound = false);
		void CompleteToolPath();
		void StartOrContinueStreamedToolExecution();
		void ExecuteSingleToolCall(int32 ToolIndex);
		bool TryParseArgumentsJsonComplete(const FString& ArgsJson) const;
		bool IsToolCallReadyForExecution(const FUnrealAiToolCallSpec& Tc) const;
		void EnqueueNewlyCompleteCalls();
		bool CheckIncompleteToolCallTimeout(bool bForceOnFinish);
		void CompleteAssistantOnly();
		void Fail(const FString& Msg);
		void Succeed();
		void AccumulateRoundUsage();
		void EmitPlanningDecision(const FString& ModeUsed, const TArray<FString>& TriggerReasons);
		void EmitEnforcementEvent(const FString& EventType, const FString& Detail);
		void EmitEnforcementSummary();

		static constexpr int32 CharPerTokenApprox = 4;
	};

	void FAgentTurnRunner::EmitPlanningDecision(const FString& ModeUsed, const TArray<FString>& TriggerReasons)
	{
		if (Sink.IsValid())
		{
			Sink->OnPlanningDecision(ModeUsed, TriggerReasons, ReplanCount, QueueStepsPending);
		}
	}

	void FAgentTurnRunner::EmitEnforcementEvent(const FString& EventType, const FString& Detail)
	{
		if (Sink.IsValid())
		{
			Sink->OnEnforcementEvent(EventType, Detail);
		}
	}

	void FAgentTurnRunner::EmitEnforcementSummary()
	{
		if (Sink.IsValid())
		{
			Sink->OnEnforcementSummary(
				ActionIntentTurnCount,
				ActionTurnsWithToolCallsCount,
				ActionTurnsWithExplicitBlockerCount,
				ActionNoToolNudgeCount,
				MutationIntentTurnCount,
				MutationFollowthroughNudgeCount);
		}
	}

	void FAgentTurnRunner::Fail(const FString& Msg)
	{
		bool Expected = false;
		if (!bTerminal.compare_exchange_strong(Expected, true, std::memory_order_relaxed))
		{
			return;
		}
		UE_LOG(
			LogTemp,
			Display,
			TEXT("UnrealAi harness: runner_terminal_set result=failed msg=%s"),
			Msg.IsEmpty() ? TEXT("<none>") : *Msg);
		FUnrealAiEditorModalMonitor::NotifyAgentTurnEndedForSink(Sink);
		if (Sink.IsValid())
		{
			EmitEnforcementSummary();
			Sink->OnRunFinished(false, Msg);
		}
	}

	void FAgentTurnRunner::Succeed()
	{
		bool Expected = false;
		if (!bTerminal.compare_exchange_strong(Expected, true, std::memory_order_relaxed))
		{
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("UnrealAi harness: runner_terminal_set result=success"));
		if (Conv.IsValid())
		{
			Conv->SaveNow();
		}
		if (UsageTracker && Profiles && !Request.ModelProfileId.IsEmpty()
			&& (AccumulatedUsage.PromptTokens > 0 || AccumulatedUsage.CompletionTokens > 0
				|| AccumulatedUsage.TotalTokens > 0))
		{
			UsageTracker->RecordUsage(Request.ModelProfileId, AccumulatedUsage, *Profiles);
		}
		if (Request.bRecordAssistantAsStubToolResult && ContextService && Conv.IsValid())
		{
			FString LastAssistant;
			for (int32 i = Conv->GetMessages().Num() - 1; i >= 0; --i)
			{
				if (Conv->GetMessages()[i].Role == TEXT("assistant") && Conv->GetMessages()[i].ToolCalls.Num() == 0)
				{
					LastAssistant = Conv->GetMessages()[i].Content;
					break;
				}
			}
			if (!LastAssistant.IsEmpty())
			{
				ContextService->LoadOrCreate(Request.ProjectId, Request.ThreadId);
				FContextRecordPolicy Policy;
				ContextService->RecordToolResult(FName(TEXT("assistant_harness")), LastAssistant, Policy);
				ContextService->SaveNow(Request.ProjectId, Request.ThreadId);
			}
		}
		if (Sink.IsValid())
		{
			EmitEnforcementSummary();
			Sink->OnRunFinished(true, FString());
		}
		if (MemoryService && Conv.IsValid())
		{
			const TArray<FUnrealAiConversationMessage>& Ms = Conv->GetMessages();
			const int32 UserTurns = CountConversationUserTurnsForMemory(Ms);
			const int32 TurnInterval = ReadEnvInt(TEXT("UNREAL_AI_MEMORY_COMPACT_TURN_INTERVAL"), 4, 1, 1000);
			const int32 TokenThreshold = ReadEnvInt(TEXT("UNREAL_AI_MEMORY_COMPACT_TOKEN_THRESHOLD"), 3000, 0, 5000000);
			const int32 PromptThreshold = ReadEnvInt(TEXT("UNREAL_AI_MEMORY_COMPACT_PROMPT_THRESHOLD"), 1800, 0, 5000000);
			const bool bByTurns = (UserTurns > 0) && ((UserTurns % TurnInterval) == 0);
			const bool bByTokens = AccumulatedUsage.TotalTokens >= TokenThreshold;
			const bool bByPromptChars = Request.UserText.Len() >= PromptThreshold;
			if (bByTurns || bByTokens || bByPromptChars)
			{
				const int32 HistoryMessages = ReadEnvInt(TEXT("UNREAL_AI_MEMORY_COMPACT_HISTORY_MESSAGES"), 12, 4, 128);
				const int32 MaxBodyChars = ReadEnvInt(TEXT("UNREAL_AI_MEMORY_COMPACT_MAX_BODY_CHARS"), 2400, 400, 20000);
				const int32 MaxToCreate = ReadEnvInt(TEXT("UNREAL_AI_MEMORY_COMPACT_MAX_CREATE"), 1, 1, 16);
				const float MinConfidence = static_cast<float>(ReadEnvInt(TEXT("UNREAL_AI_MEMORY_COMPACT_MIN_CONFIDENCE_PERCENT"), 55, 0, 100)) / 100.0f;
				const int32 PruneMaxItems = ReadEnvInt(TEXT("UNREAL_AI_MEMORY_PRUNE_MAX_ITEMS"), 120, 1, 20000);
				const int32 PruneRetentionDays = ReadEnvInt(TEXT("UNREAL_AI_MEMORY_PRUNE_RETENTION_DAYS"), 90, 0, 3650);
				const float PruneMinConfidence = static_cast<float>(ReadEnvInt(TEXT("UNREAL_AI_MEMORY_PRUNE_MIN_CONFIDENCE_PERCENT"), 30, 0, 100)) / 100.0f;

				FString ConversationForCompactor;
				const int32 Start = FMath::Max(0, Ms.Num() - HistoryMessages);
				for (int32 i = Start; i < Ms.Num(); ++i)
				{
					ConversationForCompactor += Ms[i].Role + TEXT(": ") + Ms[i].Content.Left(400) + TEXT("\n");
				}
				if (ConversationForCompactor.Len() > MaxBodyChars)
				{
					ConversationForCompactor = ConversationForCompactor.Right(MaxBodyChars);
				}

				FUnrealAiMemoryCompactionInput CompactionInput;
				CompactionInput.ProjectId = Request.ProjectId;
				CompactionInput.ThreadId = Request.ThreadId;
				CompactionInput.ConversationJson = ConversationForCompactor;
				CompactionInput.bApiKeyConfigured = Profiles && Profiles->HasAnyConfiguredApiKey();
				CompactionInput.bExpectProviderGeneration = true;
				FUnrealAiMemoryCompactor Compactor(MemoryService);
				const FUnrealAiMemoryCompactionResult Cmp = Compactor.Run(CompactionInput, MaxToCreate, MinConfidence);
				const int32 Pruned = MemoryService->Prune(PruneMaxItems, PruneRetentionDays, PruneMinConfidence);
				UE_LOG(
					LogTemp,
					Display,
					TEXT("UnrealAi memory dispatch: turns=%d byTurns=%d byTokens=%d byPromptChars=%d totalTokens=%d accepted=%d pruned=%d"),
					UserTurns,
					bByTurns ? 1 : 0,
					bByTokens ? 1 : 0,
					bByPromptChars ? 1 : 0,
					AccumulatedUsage.TotalTokens,
					Cmp.Accepted,
					Pruned);
			}
			else
			{
				UE_LOG(
					LogTemp,
					VeryVerbose,
					TEXT("UnrealAi memory dispatch skipped: turns=%d totalTokens=%d thresholds(turnInterval=%d token=%d promptChars=%d)"),
					UserTurns,
					AccumulatedUsage.TotalTokens,
					TurnInterval,
					TokenThreshold,
					PromptThreshold);
			}
		}
		FUnrealAiEditorModalMonitor::NotifyAgentTurnEndedForSink(Sink);
	}

	void FAgentTurnRunner::AccumulateRoundUsage()
	{
		int32 P = UsageThisRound.PromptTokens;
		int32 C = UsageThisRound.CompletionTokens;
		if (P == 0 && C == 0 && UsageThisRound.TotalTokens > 0)
		{
			P = UsageThisRound.TotalTokens / 2;
			C = UsageThisRound.TotalTokens - P;
		}
		AccumulatedUsage.PromptTokens += P;
		AccumulatedUsage.CompletionTokens += C;
		AccumulatedUsage.TotalTokens += UsageThisRound.TotalTokens;
		UsageThisRound = FUnrealAiTokenUsage();
	}

	void FAgentTurnRunner::DispatchLlm(bool bRetrySameRound)
	{
		if (!Transport.IsValid() || !Profiles || !Catalog || !ContextService || !Conv.IsValid() || !Tools)
		{
			Fail(TEXT("Harness not configured"));
			return;
		}
		if (bCancelled.load(std::memory_order_relaxed))
		{
			Fail(TEXT("Cancelled"));
			return;
		}
		FUnrealAiModelCapabilities CapLimits;
		Profiles->GetEffectiveCapabilities(Request.ModelProfileId, CapLimits);
		// Per-turn token budget. Env overrides profile when set. 0 = no token cap. Default 500k when both unset.
		int32 EffectiveMaxTurnTokens = CapLimits.MaxAgentTurnTokens;
		{
			const FString TokEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_HARNESS_MAX_TOKENS_PER_TURN"));
			if (TokEnv.Equals(TEXT("-1"), ESearchCase::IgnoreCase) || TokEnv.Equals(TEXT("unlimited"), ESearchCase::IgnoreCase))
			{
				EffectiveMaxTurnTokens = 0;
			}
			else if (!TokEnv.IsEmpty())
			{
				EffectiveMaxTurnTokens = FCString::Atoi(*TokEnv);
			}
			else if (EffectiveMaxTurnTokens <= 0)
			{
				EffectiveMaxTurnTokens = GHarnessDefaultMaxTurnTokens;
			}
		}
		EffectiveMaxTurnTokensHint = EffectiveMaxTurnTokens;
		int32 ParsedMax = CapLimits.MaxAgentLlmRounds > 0 ? CapLimits.MaxAgentLlmRounds : GHarnessMaxLlmRoundBackstop;
		ParsedMax = FMath::Clamp(ParsedMax, 1, GHarnessMaxLlmRoundBackstop);
		{
			const FString MaxRoundsEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_HARNESS_MAX_LLM_ROUNDS"));
			if (!MaxRoundsEnv.IsEmpty())
			{
				const int32 EnvMax = FCString::Atoi(*MaxRoundsEnv);
				if (EnvMax > 0)
				{
					ParsedMax = FMath::Min(ParsedMax, EnvMax);
				}
			}
		}
		if (Request.LlmRoundBudgetFloor > 0)
		{
			ParsedMax = FMath::Max(ParsedMax, Request.LlmRoundBudgetFloor);
		}
		ParsedMax = FMath::Clamp(ParsedMax, 1, GHarnessMaxLlmRoundBackstop);
		EffectiveMaxLlmRounds = ParsedMax;

		if (!bRetrySameRound)
		{
			if (RepeatedToolFailureCount >= GHarnessRepeatedFailureStopCount)
			{
				Fail(FString::Printf(
					TEXT("Stopped after %d consecutive identical tool failures (%s). Fix arguments, use suggested_correct_call, or state a concise blocker."),
					GHarnessRepeatedFailureStopCount,
					LastToolFailureSignature.IsEmpty() ? TEXT("unknown signature") : *LastToolFailureSignature));
				return;
			}
			if (EffectiveMaxTurnTokens > 0 && AccumulatedUsage.TotalTokens >= EffectiveMaxTurnTokens)
			{
				Fail(FString::Printf(
					TEXT("Agent turn token budget exceeded (%d tokens, limit %d). Raise maxAgentTurnTokens in the model profile or UNREAL_AI_HARNESS_MAX_TOKENS_PER_TURN, or set the env to -1 for unlimited."),
					AccumulatedUsage.TotalTokens,
					EffectiveMaxTurnTokens));
				return;
			}
			if (LlmRound >= EffectiveMaxLlmRounds)
			{
				const bool bHasRepeatValidationFailures = RepeatedToolFailureCount >= 2;
				Fail(FString::Printf(
					TEXT("LLM round backstop reached (%d rounds). Primary limits are repeated identical tool failures (%d max) and token budget; increase maxAgentLlmRounds or UNREAL_AI_HARNESS_MAX_LLM_ROUNDS only if needed. %s"),
					EffectiveMaxLlmRounds,
					GHarnessRepeatedFailureStopCount,
					bHasRepeatValidationFailures
						? TEXT("Repeated tool validation failures were detected; provide a concise blocked summary with the last failing tool+args. ")
						: TEXT("")));
				return;
			}
			++LlmRound;
			TransientTransportRetryCountThisRound = 0;
			if (LlmRound > 1 && Sink.IsValid())
			{
				Sink->OnRunContinuation(LlmRound - 1, EffectiveMaxLlmRounds);
			}
		}
		UsageThisRound = FUnrealAiTokenUsage();
		AssistantBuffer.Reset();
		PendingToolCalls.Reset();
		CompletedToolCallQueue.Reset();
		EnqueuedToolCallIndices.Reset();
		ExecutedToolCallIds.Reset();
		ToolCallFirstSeenEventCount.Reset();
		ToolCallFirstSeenSeconds.Reset();
		bFinishSeen = false;
		bFinishReceived = false;
		FinishReason.Reset();
		StreamToolEventCount = 0;
		bToolExecutionInProgress = false;
		bAssistantToolCallMessageRecorded = false;
		CurrentRoundToolSuccessCount = 0;
		CurrentRoundToolFailCount = 0;
		bCurrentRoundRepeatedToolLoop = false;
		bCurrentRoundRepeatedEmptySearch = false;
		CurrentRoundExecutedToolNames.Reset();

		FUnrealAiLlmRequest LlmReq;
		FString BuildErr;
		TArray<FString> ContextUserMsgs;
		const FString RetrievalTurnKey = FString::Printf(
			TEXT("%s|%s|round_%d"),
			*Request.ThreadId,
			*RunId.ToString(EGuidFormats::DigitsWithHyphens),
			LlmRound);
		ContextService->StartRetrievalPrefetch(RetrievalTurnKey, Request.UserText);
		if (!UnrealAiTurnLlmRequestBuilder::Build(
				Request,
				LlmRound,
				EffectiveMaxLlmRounds,
				RetrievalTurnKey,
				ContextService,
				Profiles,
				Catalog,
				Conv.Get(),
				CharPerTokenApprox,
				LlmReq,
				ContextUserMsgs,
				BuildErr))
		{
			Fail(BuildErr);
			return;
		}
		if (Sink.IsValid() && ContextUserMsgs.Num() > 0)
		{
			Sink->OnContextUserMessages(ContextUserMsgs);
		}

		// Optional pacing to reduce request burstiness and provider 429 rate limits.
		int32 MinDelayMs = 0;
		{
			const FString DelayEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_HARNESS_ROUND_MIN_DELAY_MS"));
			if (!DelayEnv.IsEmpty())
			{
				const int32 Parsed = FCString::Atoi(*DelayEnv);
				MinDelayMs = FMath::Clamp(Parsed, 0, 5000);
			}
		}
		if (MinDelayMs > 0 && LastLlmSubmitSeconds > 0.0)
		{
			const double NowSec = FPlatformTime::Seconds();
			const double ElapsedMs = (NowSec - LastLlmSubmitSeconds) * 1000.0;
			if (ElapsedMs < static_cast<double>(MinDelayMs))
			{
				const float SleepSec = static_cast<float>((static_cast<double>(MinDelayMs) - ElapsedMs) / 1000.0);
				if (SleepSec > 0.0f)
				{
					UE_LOG(LogTemp, Display, TEXT("UnrealAi harness: pacing LLM dispatch by %.2fs (min_delay_ms=%d)."), SleepSec, MinDelayMs);
					FPlatformProcess::SleepNoStats(SleepSec);
				}
			}
		}

		const TSharedPtr<FAgentTurnRunner> Self = AsShared();
		UE_LOG(LogTemp, Display,
			TEXT("UnrealAi harness: LLM round %d/%d — submitting HTTP request (stream=%s). "
				 "No viewport progress until tools execute."),
			LlmRound,
			EffectiveMaxLlmRounds,
			LlmReq.bStream ? TEXT("yes") : TEXT("no"));
		const FUnrealAiLlmInvocationService Invoker(Transport);
		Invoker.SubmitStreamChatCompletion(
			LlmReq,
			FUnrealAiLlmStreamCallback::CreateLambda(
				[Self](const FUnrealAiLlmStreamEvent& Ev)
				{
					AsyncTask(ENamedThreads::GameThread,
							  [Self, Ev]()
							  {
								  Self->HandleEvent(Ev);
							  });
				}));
		LastLlmSubmitSeconds = FPlatformTime::Seconds();
	}

	bool FAgentTurnRunner::TryParseArgumentsJsonComplete(const FString& ArgsJson) const
	{
		if (ArgsJson.TrimStartAndEnd().IsEmpty())
		{
			return false;
		}
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> ReaderObj = TJsonReaderFactory<>::Create(ArgsJson);
		if (FJsonSerializer::Deserialize(ReaderObj, Obj) && Obj.IsValid())
		{
			return true;
		}
		TArray<TSharedPtr<FJsonValue>> Arr;
		TSharedRef<TJsonReader<>> ReaderArr = TJsonReaderFactory<>::Create(ArgsJson);
		return FJsonSerializer::Deserialize(ReaderArr, Arr);
	}

	bool FAgentTurnRunner::IsToolCallReadyForExecution(const FUnrealAiToolCallSpec& Tc) const
	{
		return !Tc.Name.TrimStartAndEnd().IsEmpty() && TryParseArgumentsJsonComplete(Tc.ArgumentsJson);
	}

	void FAgentTurnRunner::EnqueueNewlyCompleteCalls()
	{
		for (int32 I = 0; I < PendingToolCalls.Num(); ++I)
		{
			const FUnrealAiToolCallSpec& Tc = PendingToolCalls[I];
			if (!IsToolCallReadyForExecution(Tc))
			{
				continue;
			}
			if (EnqueuedToolCallIndices.Contains(I))
			{
				continue;
			}
			if (!Tc.Id.IsEmpty() && ExecutedToolCallIds.Contains(Tc.Id))
			{
				continue;
			}
			CompletedToolCallQueue.Add(I);
			EnqueuedToolCallIndices.Add(I);
			EmitEnforcementEvent(TEXT("stream_tool_ready"), FString::Printf(TEXT("index=%d id=%s name=%s"), I, *Tc.Id, *Tc.Name));
		}
	}

	bool FAgentTurnRunner::CheckIncompleteToolCallTimeout(const bool bForceOnFinish)
	{
		const int32 MaxEvents = ReadEnvInt(TEXT("UNREAL_AI_STREAM_TOOL_INCOMPLETE_MAX_EVENTS"), 12, 1, 1000);
		const int32 MaxMs = ReadEnvInt(TEXT("UNREAL_AI_STREAM_TOOL_INCOMPLETE_MAX_MS"), 2500, 100, 600000);
		const double NowSec = FPlatformTime::Seconds();
		for (int32 I = 0; I < PendingToolCalls.Num(); ++I)
		{
			const FUnrealAiToolCallSpec& Tc = PendingToolCalls[I];
			if (IsToolCallReadyForExecution(Tc))
			{
				continue;
			}
			if (!Tc.Id.IsEmpty() && ExecutedToolCallIds.Contains(Tc.Id))
			{
				continue;
			}
			const int32 FirstSeenEvent = ToolCallFirstSeenEventCount.FindRef(I);
			const double FirstSeenSec = ToolCallFirstSeenSeconds.FindRef(I);
			const int32 AgeEvents = FMath::Max(0, StreamToolEventCount - FirstSeenEvent);
			const int32 AgeMs = FirstSeenSec > 0.0 ? static_cast<int32>((NowSec - FirstSeenSec) * 1000.0) : 0;
			if (!bForceOnFinish && AgeEvents < MaxEvents && AgeMs < MaxMs)
			{
				continue;
			}
			EmitEnforcementEvent(
				TEXT("stream_tool_call_incomplete_timeout"),
				FString::Printf(TEXT("index=%d id=%s name=%s age_events=%d age_ms=%d"), I, *Tc.Id, *Tc.Name, AgeEvents, AgeMs));
			Fail(FString::Printf(
				TEXT("Streamed tool call did not reach complete JSON arguments in time (index=%d, id=%s, name=%s)."),
				I,
				*Tc.Id,
				*Tc.Name));
			return true;
		}
		return false;
	}

	void FAgentTurnRunner::HandleEvent(const FUnrealAiLlmStreamEvent& Ev)
	{
		if (bCancelled.load(std::memory_order_relaxed) || bTerminal.load(std::memory_order_relaxed))
		{
			return;
		}
		if (!Sink.IsValid())
		{
			return;
		}
		switch (Ev.Type)
		{
		case EUnrealAiLlmStreamEventType::AssistantDelta:
			AssistantBuffer += Ev.DeltaText;
			Sink->OnAssistantDelta(Ev.DeltaText);
			break;
		case EUnrealAiLlmStreamEventType::ThinkingDelta:
			Sink->OnThinkingDelta(Ev.DeltaText);
			break;
		case EUnrealAiLlmStreamEventType::ToolCalls:
			++StreamToolEventCount;
			MergeToolCallDeltas(PendingToolCalls, Ev.ToolCalls);
			for (const FUnrealAiToolCallSpec& Tc : Ev.ToolCalls)
			{
				if (Tc.StreamMergeIndex >= 0)
				{
					ToolCallFirstSeenEventCount.FindOrAdd(Tc.StreamMergeIndex, StreamToolEventCount);
					ToolCallFirstSeenSeconds.FindOrAdd(Tc.StreamMergeIndex, FPlatformTime::Seconds());
				}
			}
			EnqueueNewlyCompleteCalls();
			StartOrContinueStreamedToolExecution();
			if (CheckIncompleteToolCallTimeout(false))
			{
				return;
			}
			break;
		case EUnrealAiLlmStreamEventType::Finish:
			UsageThisRound.PromptTokens = FMath::Max(UsageThisRound.PromptTokens, Ev.Usage.PromptTokens);
			UsageThisRound.CompletionTokens = FMath::Max(UsageThisRound.CompletionTokens, Ev.Usage.CompletionTokens);
			UsageThisRound.TotalTokens = FMath::Max(UsageThisRound.TotalTokens, Ev.Usage.TotalTokens);
			bFinishSeen = true;
			bFinishReceived = true;
			FinishReason = Ev.FinishReason;
			if (Ev.FinishReason == TEXT("tool_calls") && PendingToolCalls.Num() == 0)
			{
				Fail(TEXT("Model requested tools but sent no tool_calls"));
				break;
			}
			EnqueueNewlyCompleteCalls();
			StartOrContinueStreamedToolExecution();
			if (CheckIncompleteToolCallTimeout(true))
			{
				return;
			}
			if (Ev.FinishReason == TEXT("tool_calls") || PendingToolCalls.Num() > 0 || ExecutedToolCallIds.Num() > 0)
			{
				CompleteToolPath();
			}
			else
			{
				CompleteAssistantOnly();
			}
			break;
		case EUnrealAiLlmStreamEventType::Error:
		{
			const FString ErrorMsg = Ev.ErrorMessage.IsEmpty() ? TEXT("LLM error") : Ev.ErrorMessage;
			if (!bCancelled.load(std::memory_order_relaxed)
				&& ShouldRetryTransientTransportError(ErrorMsg)
				&& TransientTransportRetryCountThisRound < MaxTransientTransportRetriesPerRound)
			{
				++TransientTransportRetryCountThisRound;
				DispatchLlm(true);
				break;
			}
			Fail(ErrorMsg);
			break;
		}
		default:
			break;
		}
	}

	void FAgentTurnRunner::StartOrContinueStreamedToolExecution()
	{
		if (bToolExecutionInProgress)
		{
			return;
		}
		bToolExecutionInProgress = true;
		while (CompletedToolCallQueue.Num() > 0)
		{
			const int32 ToolIndex = CompletedToolCallQueue[0];
			CompletedToolCallQueue.RemoveAt(0);
			ExecuteSingleToolCall(ToolIndex);
			if (bTerminal.load(std::memory_order_relaxed))
			{
				break;
			}
		}
		bToolExecutionInProgress = false;
	}

	void FAgentTurnRunner::ExecuteSingleToolCall(const int32 ToolIndex)
	{
		if (!PendingToolCalls.IsValidIndex(ToolIndex))
		{
			return;
		}
		const FUnrealAiToolCallSpec& Tc = PendingToolCalls[ToolIndex];
		if (!Tc.Id.IsEmpty() && ExecutedToolCallIds.Contains(Tc.Id))
		{
			return;
		}
		if (!bAssistantToolCallMessageRecorded && Conv.IsValid())
		{
			FUnrealAiConversationMessage Am;
			Am.Role = TEXT("assistant");
			Am.Content = AssistantBuffer;
			Am.ToolCalls = PendingToolCalls;
			Conv->GetMessagesMutable().Add(Am);
			bAssistantToolCallMessageRecorded = true;
		}
		FString InvokeName;
		FString InvokeArgs;
		FString UnwrapErr;
		if (!UnwrapDispatchToolCall(Tc, InvokeName, InvokeArgs, UnwrapErr))
		{
			if (Sink.IsValid())
			{
				Sink->OnToolCallStarted(Tc.Name, Tc.Id, Tc.ArgumentsJson);
				Sink->OnToolCallFinished(Tc.Name, Tc.Id, false, UnrealAiTruncateForUi(UnwrapErr), nullptr);
			}
			if (Conv.IsValid())
			{
				FUnrealAiConversationMessage Tm;
				Tm.Role = TEXT("tool");
				Tm.ToolCallId = Tc.Id;
				Tm.Content = UnwrapErr;
				Conv->GetMessagesMutable().Add(Tm);
			}
			++CurrentRoundToolFailCount;
			if (!Tc.Id.IsEmpty())
			{
				ExecutedToolCallIds.Add(Tc.Id);
			}
			EmitEnforcementEvent(TEXT("stream_tool_exec_done"), FString::Printf(TEXT("id=%s ok=0"), *Tc.Id));
			return;
		}
		if (Sink.IsValid())
		{
			Sink->OnToolCallStarted(InvokeName, Tc.Id, InvokeArgs);
		}
		const FString InvokeSignature = (InvokeName + TEXT("|") + InvokeArgs.Left(220)).ToLower();
		const int32 NameCount = ToolInvokeCountByName.FindOrAdd(InvokeName) + 1;
		ToolInvokeCountByName.FindOrAdd(InvokeName) = NameCount;
		const int32 SigCount = ToolInvokeCountBySignature.FindOrAdd(InvokeSignature) + 1;
		ToolInvokeCountBySignature.FindOrAdd(InvokeSignature) = SigCount;
		const bool bRepeatSignature = Request.Mode == EUnrealAiAgentMode::Agent && InvokeName != TEXT("agent_emit_todo_plan")
			&& (SigCount >= GHarnessRepeatedFailureStopCount || NameCount >= 6);
		EmitEnforcementEvent(TEXT("stream_tool_exec_start"), FString::Printf(TEXT("id=%s name=%s"), *Tc.Id, *InvokeName));
		const FUnrealAiToolInvocationResult Inv = Tools->InvokeTool(InvokeName, InvokeArgs, Tc.Id);
		CurrentRoundExecutedToolNames.Add(InvokeName);
		const FString DialogFootnote = FUnrealAiEditorModalMonitor::ConsumePendingToolDialogFootnote();
		FString ModelToolContent = Inv.bOk ? Inv.ContentForModel : Inv.ErrorMessage;
		if (bRepeatSignature && !Inv.bOk)
		{
			bCurrentRoundRepeatedToolLoop = true;
		}
		if (!DialogFootnote.IsEmpty())
		{
			if (!ModelToolContent.IsEmpty())
			{
				ModelToolContent += TEXT("\n");
			}
			ModelToolContent += FString::Printf(TEXT("[Editor blocking dialog during tool]: %s"), *DialogFootnote);
		}
		if (Inv.bOk)
		{
			auto IsNonProgressEmptySearchResult = [&]()
			{
				if (InvokeName == TEXT("asset_index_fuzzy_search"))
				{
					const bool bLowConf = ModelToolContent.Contains(TEXT("\"low_confidence\":true")) || ModelToolContent.Contains(TEXT("\"low_confidence\": true"));
					const bool bEmptyMatches = ModelToolContent.Contains(TEXT("\"matches\":[]")) || ModelToolContent.Contains(TEXT("\"matches\": []"));
					const bool bCountZero = ModelToolContent.Contains(TEXT("\"count\":0")) || ModelToolContent.Contains(TEXT("\"count\": 0"));
					return bLowConf && (bEmptyMatches || bCountZero);
				}
				if (InvokeName == TEXT("scene_fuzzy_search"))
				{
					const bool bCountZero = ModelToolContent.Contains(TEXT("\"count\":0")) || ModelToolContent.Contains(TEXT("\"count\": 0"));
					return bCountZero;
				}
				if (InvokeName == TEXT("source_search_symbol"))
				{
					const bool bZeroFiles = ModelToolContent.Contains(TEXT("\"files_considered\":0")) || ModelToolContent.Contains(TEXT("\"files_considered\": 0"));
					const bool bZeroCandidates = ModelToolContent.Contains(TEXT("\"path_candidates\":0")) || ModelToolContent.Contains(TEXT("\"path_candidates\": 0"));
					const bool bEmptyPathMatches = ModelToolContent.Contains(TEXT("\"path_matches\":[]")) || ModelToolContent.Contains(TEXT("\"path_matches\": []"));
					return bZeroFiles || bZeroCandidates || bEmptyPathMatches;
				}
				return false;
			};
			if (IsNonProgressEmptySearchResult())
			{
				int32& C = NonProgressEmptySearchCountByToolName.FindOrAdd(InvokeName);
				++C;
				if (C >= 3)
				{
					bCurrentRoundRepeatedEmptySearch = true;
					bCurrentRoundRepeatedToolLoop = true;
				}
			}
			else
			{
				NonProgressEmptySearchCountByToolName.Remove(InvokeName);
			}
		}
		if (Inv.bOk)
		{
			++CurrentRoundToolSuccessCount;
			LastToolFailureSignature.Reset();
			RepeatedToolFailureCount = 0;
		}
		else
		{
			++CurrentRoundToolFailCount;
			const FString FailureSig = InvokeSignature;
			if (!FailureSig.IsEmpty() && FailureSig == LastToolFailureSignature)
			{
				++RepeatedToolFailureCount;
			}
			else
			{
				LastToolFailureSignature = FailureSig;
				RepeatedToolFailureCount = 1;
			}
		}
		if (Sink.IsValid())
		{
			Sink->OnToolCallFinished(InvokeName, Tc.Id, Inv.bOk, UnrealAiTruncateForUi(ModelToolContent), Inv.EditorPresentation);
		}
		if (ContextService && Inv.bOk && InvokeName != TEXT("agent_emit_todo_plan")
			&& ShouldPersistToolResultToContextState(InvokeName))
		{
			ContextService->LoadOrCreate(Request.ProjectId, Request.ThreadId);
			FContextRecordPolicy Policy;
			ContextService->RecordToolResult(FName(*InvokeName), ModelToolContent, Policy);
		}
		if (InvokeName == TEXT("agent_emit_todo_plan") && Sink.IsValid())
		{
			++ReplanCount;
			FString PlanTitle = TEXT("Plan");
			TSharedPtr<FJsonObject> ArgsObj;
			TSharedRef<TJsonReader<>> AR = TJsonReaderFactory<>::Create(InvokeArgs);
			if (FJsonSerializer::Deserialize(AR, ArgsObj) && ArgsObj.IsValid())
			{
				ArgsObj->TryGetStringField(TEXT("title"), PlanTitle);
			}
			const FString PlanBody = (Inv.bOk && Inv.ContentForModel.TrimStart().StartsWith(TEXT("{"))) ? Inv.ContentForModel : InvokeArgs;
			Sink->OnTodoPlanEmitted(PlanTitle, PlanBody);
		}
		if (Conv.IsValid())
		{
			FUnrealAiConversationMessage Tm;
			Tm.Role = TEXT("tool");
			Tm.ToolCallId = Tc.Id;
			Tm.Content = ModelToolContent;
			Conv->GetMessagesMutable().Add(Tm);
		}
		if (!Tc.Id.IsEmpty())
		{
			ExecutedToolCallIds.Add(Tc.Id);
		}
		EmitEnforcementEvent(TEXT("stream_tool_exec_done"), FString::Printf(TEXT("id=%s ok=%d"), *Tc.Id, Inv.bOk ? 1 : 0));
	}

	void FAgentTurnRunner::CompleteToolPath()
	{
		if (!Tools || !Conv.IsValid())
		{
			Fail(TEXT("Tool host missing"));
			return;
		}
		if (PendingToolCalls.Num() == 0 && ExecutedToolCallIds.Num() == 0)
		{
			Fail(TEXT("Model completed tool_calls but no valid tool name (empty function.name)"));
			return;
		}
		// In stream-first mode tools may already be executed before Finish arrives.
		// Ensure any newly complete calls execute now before finalizing this round.
		EnqueueNewlyCompleteCalls();
		StartOrContinueStreamedToolExecution();
		if (CheckIncompleteToolCallTimeout(true))
		{
			return;
		}

		FString TodoPlanEffectiveName;
		if (PendingToolCalls.Num() == 1)
		{
			FString TmpArgs, UnwrapE;
			if (!UnwrapDispatchToolCall(PendingToolCalls[0], TodoPlanEffectiveName, TmpArgs, UnwrapE))
			{
				TodoPlanEffectiveName.Reset();
			}
		}
		const bool bTodoPlanOnly = PendingToolCalls.Num() == 1
			&& TodoPlanEffectiveName == TEXT("agent_emit_todo_plan");
		const bool bAgentMode = Request.Mode == EUnrealAiAgentMode::Agent;
		const int32 ToolBatchSize = PendingToolCalls.Num();
		const bool bLargeBatch = false; // stream-first: execute sequentially as calls complete.
		const bool bNearRoundBudget = (EffectiveMaxLlmRounds - LlmRound) <= 2;
		const bool bNearTokenBudget = EffectiveMaxTurnTokensHint > 0
			&& (static_cast<int64>(AccumulatedUsage.TotalTokens) * 100 >= static_cast<int64>(EffectiveMaxTurnTokensHint) * 85);
		const bool bNearBudget = bAgentMode && !bTodoPlanOnly && (bNearRoundBudget || bNearTokenBudget);
		const bool bDeferredQueue = false;
		const bool bRepeatedToolLoop = bCurrentRoundRepeatedToolLoop;
		const bool bRepeatedEmptySearch = bCurrentRoundRepeatedEmptySearch;
		const int32 ToolSuccessCount = CurrentRoundToolSuccessCount;
		const int32 ToolFailCount = CurrentRoundToolFailCount;
		// Number of tool calls that were actually completed in this tool path.
		// Used only for deferred-queue accounting; bDeferredQueue is currently false but this must compile.
		const int32 ToolsToExecute = ToolSuccessCount + ToolFailCount;
		const TArray<FString>& ExecutedToolNames = CurrentRoundExecutedToolNames;
		// Dynamic queue progress: when a stored plan exists and this round made progress,
		// mark one pending todo step as done (best-effort coarse progress signal).
		if (ContextService && bAgentMode && ToolSuccessCount > 0)
		{
			ContextService->LoadOrCreate(Request.ProjectId, Request.ThreadId);
			if (const FAgentContextState* St = ContextService->GetState(Request.ProjectId, Request.ThreadId))
			{
				if (!St->ActiveTodoPlanJson.IsEmpty())
				{
					for (int32 i = 0; i < St->TodoStepsDone.Num(); ++i)
					{
						if (!St->TodoStepsDone[i])
						{
							ContextService->SetTodoStepDone(i, true);
							break;
						}
					}
					ContextService->SaveNow(Request.ProjectId, Request.ThreadId);
				}
			}
		}
		if (bDeferredQueue)
		{
			QueueStepsPending = FMath::Max(QueueStepsPending, ToolBatchSize - ToolsToExecute);
		}
		else
		{
			QueueStepsPending = FMath::Max(0, QueueStepsPending - ToolSuccessCount);
		}
		TArray<FString> TriggerReasons;
		if (bLargeBatch)
		{
			TriggerReasons.Add(TEXT("many_tool_calls"));
		}
		if (bNearBudget)
		{
			TriggerReasons.Add(TEXT("budget_pressure"));
		}
		if (RepeatedToolFailureCount >= 2 || ToolFailCount >= 2)
		{
			TriggerReasons.Add(TEXT("repeated_tool_failures"));
		}
		if (bRepeatedToolLoop)
		{
			TriggerReasons.Add(TEXT("repeated_tool_loop"));
		}
		if (bRepeatedEmptySearch)
		{
			TriggerReasons.Add(TEXT("repeated_empty_search"));
		}
		if (bTodoPlanOnly)
		{
			TriggerReasons.Add(TEXT("explicit_todo_plan"));
		}
		if (TriggerReasons.Num() == 0)
		{
			TriggerReasons.Add(TEXT("act_now"));
		}
		EmitPlanningDecision(bTodoPlanOnly ? TEXT("explicit") : TEXT("implicit"), TriggerReasons);
		const bool bRepeatedValidationLoop = RepeatedToolFailureCount >= 3;
		if (bRepeatedValidationLoop && Request.Mode != EUnrealAiAgentMode::Ask)
		{
			FUnrealAiConversationMessage RepairNudge;
			RepairNudge.Role = TEXT("user");
			RepairNudge.Content = TEXT(
				"[Harness][reason=repeated_validation_failure] The same tool validation failure repeated multiple times. Repair-or-stop now: either call one corrected tool invocation with fixed arguments, or provide a concise blocked summary with the exact failing tool and required fields. Last failing pattern: ");
			RepairNudge.Content += LastToolFailureSignature;
			Conv->GetMessagesMutable().Add(RepairNudge);
		}
		if (bRepeatedToolLoop && Request.Mode == EUnrealAiAgentMode::Agent && !bTodoPlanOnly)
		{
			FUnrealAiConversationMessage LoopNudge;
			LoopNudge.Role = TEXT("user");
			LoopNudge.Content = TEXT(
				"[Harness][reason=repeated_tool_loop] Repeated tool loop detected. Replan-or-stop now: either emit one concise `agent_emit_todo_plan` for remaining work and continue from step 1, or provide a concise blocked summary with the exact blocker.");
			Conv->GetMessagesMutable().Add(LoopNudge);
		}
		// The next round still runs (DispatchLlm below), but models often reply with text-only and end
		// the run. A synthetic user line nudges execution when the only tool was the todo plan.
		if (bTodoPlanOnly && Request.Mode != EUnrealAiAgentMode::Ask)
		{
			FUnrealAiConversationMessage Nudge;
			Nudge.Role = TEXT("user");
			Nudge.Content = TEXT(
				"[Harness][reason=todo_plan_only] Plan recorded. Continue immediately: execute the first pending plan step using "
				"Unreal Editor tools. Do not finish with narration only—invoke tools this turn unless truly blocked.");
			Conv->GetMessagesMutable().Add(Nudge);
		}
		else if (Request.Mode == EUnrealAiAgentMode::Agent)
		{
			// Agent mode should keep iterating after tool execution. This avoids frequent one-tool stalls where
			// the next model turn summarizes and ends instead of taking the next concrete action.
			FUnrealAiConversationMessage Nudge;
			Nudge.Role = TEXT("user");
			if (bDeferredQueue)
			{
				Nudge.Content = FString::Printf(
					TEXT("[Harness][reason=tool_round_complete_deferred] Tool round complete (ok=%d, failed=%d). Additional requested tools were deferred (%d) due to dynamic planning policy. ")
					TEXT("Now emit/update a concise todo plan that queues remaining work, then execute the first pending step."),
					ToolSuccessCount,
					ToolFailCount,
					ToolBatchSize - ToolsToExecute);
			}
			else
			{
				Nudge.Content = FString::Printf(
					TEXT("[Harness][reason=tool_round_complete] Tool round complete (ok=%d, failed=%d). Continue executing the user's request. ")
					TEXT("If the task is not complete, call the next tool now. Only finish when the requested scene/work is actually done or you are truly blocked."),
					ToolSuccessCount,
					ToolFailCount);
			}
			Conv->GetMessagesMutable().Add(Nudge);
		}
		{
			const FString LastRealUser = GetLastRealUserMessage(Conv->GetMessages());
			// Plan-mode planner passes are DAG-only (no tools); do not treat as Agent action-intent turns.
			const bool bModeWantsTools =
				(Request.Mode == EUnrealAiAgentMode::Agent);
			if (bModeWantsTools && UserLikelyRequestsActionTool(LastRealUser))
			{
				if (!bActionIntentCounted)
				{
					++ActionIntentTurnCount;
					bActionIntentCounted = true;
				}
				if (!bActionToolOutcomeCounted)
				{
					++ActionTurnsWithToolCallsCount;
					bActionToolOutcomeCounted = true;
					EmitEnforcementEvent(TEXT("action_with_tool_calls"), TEXT("action-intent turn executed one or more tools"));
				}
			}
		}
		{
			const FString LastRealUser = GetLastRealUserMessage(Conv->GetMessages());
			const bool bNeedsMutationFollowthrough = Request.Mode == EUnrealAiAgentMode::Agent
				&& UserLikelyRequestsMutation(LastRealUser)
				&& ExecutedToolNames.Num() > 0;
			if (bNeedsMutationFollowthrough)
			{
				if (!bMutationIntentCounted)
				{
					++MutationIntentTurnCount;
					bMutationIntentCounted = true;
				}
				bool bAllReadOnly = true;
				for (const FString& Name : ExecutedToolNames)
				{
					if (!IsLikelyReadOnlyToolName(Name))
					{
						bAllReadOnly = false;
						break;
					}
				}
				if (bAllReadOnly && MutationFollowthroughNudgeCount < 2)
				{
					++MutationFollowthroughNudgeCount;
					EmitEnforcementEvent(TEXT("mutation_read_only_nudge"), TEXT("mutation-intent turn used read-only tools only"));
					FUnrealAiConversationMessage FollowthroughNudge;
					FollowthroughNudge.Role = TEXT("user");
					FollowthroughNudge.Content = TEXT(
						"[Harness][reason=mutation_readonly_loop] The user requested an actual change, but only read/discovery tools were executed. "
						"Continue now with at least one concrete write/exec tool call that advances the requested change, "
						"or state the exact blocker.");
					Conv->GetMessagesMutable().Add(FollowthroughNudge);
				}
			}
		}
		PendingToolCalls.Reset();
		AssistantBuffer.Reset();
		AccumulateRoundUsage();
		DispatchLlm();
	}

	void FAgentTurnRunner::CompleteAssistantOnly()
	{
		if (!Conv.IsValid())
		{
			Fail(TEXT("Conversation missing"));
			return;
		}
		AccumulateRoundUsage();
		FUnrealAiConversationMessage Am;
		Am.Role = TEXT("assistant");
		Am.Content = AssistantBuffer;
		Conv->GetMessagesMutable().Add(Am);

		// Second+ LLM rounds often end with finish_reason=stop and no tool_calls. Models sometimes return an
		// empty assistant message (streaming quirk or "done" signal) even though work is unfinished—treat
		// that as a stall, not a successful completion, and schedule another round while under the cap.
		const bool bAgentModeWantsToolExecution = (Request.Mode == EUnrealAiAgentMode::Agent);
		const bool bPlanPlannerPass = (Request.Mode == EUnrealAiAgentMode::Plan);
		const FString LastRealUser = GetLastRealUserMessage(Conv->GetMessages());
		const bool bActionIntent = UserLikelyRequestsActionTool(LastRealUser);
		const bool bHasExplicitBlocker = AssistantContainsExplicitBlocker(AssistantBuffer);
		if (bAgentModeWantsToolExecution && bActionIntent)
		{
			if (!bActionIntentCounted)
			{
				++ActionIntentTurnCount;
				bActionIntentCounted = true;
			}
			if (bHasExplicitBlocker)
			{
				if (!bActionBlockerOutcomeCounted)
				{
					++ActionTurnsWithExplicitBlockerCount;
					bActionBlockerOutcomeCounted = true;
					EmitEnforcementEvent(TEXT("action_explicit_blocker"), TEXT("action-intent turn completed with explicit blocker"));
				}
			}
		}
		if (bAgentModeWantsToolExecution && AssistantBuffer.TrimStartAndEnd().IsEmpty() && LlmRound < EffectiveMaxLlmRounds)
		{
			FUnrealAiConversationMessage Nudge;
			Nudge.Role = TEXT("user");
			Nudge.Content = TEXT(
				"[Harness][reason=empty_assistant] The model returned an empty assistant message. Continue the user's task: call the ")
				TEXT("next tool(s) now, or briefly explain what blocks you. Do not reply with an empty message.");
			Conv->GetMessagesMutable().Add(Nudge);
			AssistantBuffer.Reset();
			DispatchLlm();
			return;
		}
		if (bPlanPlannerPass && AssistantBuffer.TrimStartAndEnd().IsEmpty() && LlmRound < EffectiveMaxLlmRounds)
		{
			FUnrealAiConversationMessage Nudge;
			Nudge.Role = TEXT("user");
			Nudge.Content = TEXT(
				"[Harness][reason=empty_planner] The model returned an empty assistant message. Output a single JSON object for the plan ")
				TEXT("(schema unreal_ai.plan_dag, nodes array with id/title/hint/dependsOn). No tools—JSON only.");
			Conv->GetMessagesMutable().Add(Nudge);
			AssistantBuffer.Reset();
			DispatchLlm();
			return;
		}
		if (bAgentModeWantsToolExecution && bActionIntent && !bHasExplicitBlocker && LlmRound < EffectiveMaxLlmRounds && ActionNoToolNudgeCount < 2)
		{
			++ActionNoToolNudgeCount;
			EmitEnforcementEvent(TEXT("action_no_tool_nudge"), TEXT("action-intent turn ended without tool calls; nudge emitted"));
			FUnrealAiConversationMessage Nudge;
			Nudge.Role = TEXT("user");
			Nudge.Content = TEXT(
				"[Harness][reason=action_no_tool] The user asked for concrete editor actions. Do not end with narration-only output. "
				"Call at least one relevant Unreal tool now (or report the exact blocker if no tool can proceed).");
			Conv->GetMessagesMutable().Add(Nudge);
			AssistantBuffer.Reset();
			DispatchLlm();
			return;
		}
		if (bAgentModeWantsToolExecution && bActionIntent && !bHasExplicitBlocker && ActionNoToolNudgeCount >= 2)
		{
			EmitEnforcementEvent(TEXT("action_no_tool_fail"), TEXT("action-intent turn failed after bounded nudges"));
			Fail(TEXT("Action-intent turn ended without tool calls or explicit blocker explanation."));
			return;
		}

		Succeed();
	}
}

FUnrealAiAgentHarness::FUnrealAiAgentHarness(
	IUnrealAiPersistence* InPersistence,
	IAgentContextService* InContext,
	FUnrealAiModelProfileRegistry* InProfiles,
	FUnrealAiToolCatalog* InCatalog,
	TSharedPtr<ILlmTransport> InTransport,
	IToolExecutionHost* InToolHost,
	FUnrealAiUsageTracker* InUsageTracker,
	IUnrealAiMemoryService* InMemoryService)
	: Persistence(InPersistence)
	, Context(InContext)
	, Profiles(InProfiles)
	, Catalog(InCatalog)
	, Transport(MoveTemp(InTransport))
	, ToolHost(InToolHost)
	, UsageTracker(InUsageTracker)
	, MemoryService(InMemoryService)
{
}

FUnrealAiAgentHarness::~FUnrealAiAgentHarness() = default;

void FUnrealAiAgentHarness::CancelTurn()
{
	if (Context && ActiveRunner.IsValid())
	{
		Context->CancelRetrievalPrefetchForThread(ActiveRunner->Request.ProjectId, ActiveRunner->Request.ThreadId);
	}
	if (ActiveRunner.IsValid())
	{
		ActiveRunner->bCancelled.store(true, std::memory_order_relaxed);
	}
	if (Transport.IsValid())
	{
		Transport->CancelActiveRequest();
	}
}

bool FUnrealAiAgentHarness::IsTurnInProgress() const
{
	if (!ActiveRunner.IsValid())
	{
		return false;
	}
	return !ActiveRunner->bTerminal.load(std::memory_order_relaxed)
		&& !ActiveRunner->bCancelled.load(std::memory_order_relaxed);
}

void FUnrealAiAgentHarness::RunTurn(const FUnrealAiAgentTurnRequest& Request, TSharedPtr<IAgentRunSink> Sink)
{
	CancelTurn();
	if (!Sink.IsValid() || !Persistence || !Context || !Profiles || !Catalog || !Transport.IsValid() || !ToolHost)
	{
		if (Sink.IsValid())
		{
			Sink->OnRunFinished(false, TEXT("Harness not fully configured"));
		}
		return;
	}

	FUnrealAiEditorModalMonitor::NotifyAgentTurnStarted(Sink);

	const TSharedPtr<UnrealAiAgentHarnessPriv::FAgentTurnRunner> Runner = MakeShared<UnrealAiAgentHarnessPriv::FAgentTurnRunner>();
	ActiveRunner = Runner;
	Runner->Request = Request;
	Runner->Sink = Sink;
	Runner->Persistence = Persistence;
	Runner->ContextService = Context;
	Runner->Profiles = Profiles;
	Runner->Catalog = Catalog;
	Runner->Transport = Transport;
	Runner->Tools = ToolHost;
	Runner->UsageTracker = UsageTracker;
	Runner->MemoryService = MemoryService;
	Runner->AccumulatedUsage = FUnrealAiTokenUsage();
	Runner->Conv = MakeUnique<FUnrealAiConversationStore>(Persistence);
	Runner->Conv->LoadOrCreate(Request.ProjectId, Request.ThreadId);

	Context->LoadOrCreate(Request.ProjectId, Request.ThreadId);
	ToolHost->SetToolSession(Request.ProjectId, Request.ThreadId);

	Runner->RunId = FGuid::NewGuid();
	FUnrealAiRunIds Ids;
	Ids.RunId = Runner->RunId;
	Sink->OnRunStarted(Ids);

	// Chat naming is a first-turn-only instruction. We inject it into the LLM conversation,
	// but it is never shown in the chat UI because the UI transcript is driven by chat send/stream events.
	const bool bFirstUserMessageInThread = (Runner->Conv->GetMessages().Num() == 0);
	if (bFirstUserMessageInThread)
	{
		FUnrealAiConversationMessage NameInstr;
		NameInstr.Role = TEXT("system");
		NameInstr.Content = TEXT(
			"[Hidden] Chat naming:\n"
			"On your FIRST assistant reply in this chat, you MUST append exactly one token:\n"
			"<chat-name: \"<short name>\"> \n"
			"The chat name should be 3-8 words, human-friendly, and derived from the user's goal.\n"
			"Do not explain the token to the user. The application will strip the token from visible output.\n");
		Runner->Conv->GetMessagesMutable().Add(NameInstr);
	}

	FUnrealAiConversationMessage UserMsg;
	UserMsg.Role = TEXT("user");
	UserMsg.Content = Request.UserText;
	Runner->Conv->GetMessagesMutable().Add(UserMsg);

	Runner->DispatchLlm();
}
