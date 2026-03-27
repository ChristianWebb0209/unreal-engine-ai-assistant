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
		/** From model profile `maxAgentLlmRounds` (default 32), set each DispatchLlm. */
		int32 EffectiveMaxLlmRounds = 32;
		/** Retries transient HTTP-level cancellations without consuming a round. */
		int32 TransientTransportRetryCountThisRound = 0;
		// Keep retries minimal so headed live runs fail fast instead of stalling for many minutes.
		static constexpr int32 MaxTransientTransportRetriesPerRound = 0;

		FString AssistantBuffer;
		TArray<FUnrealAiToolCallSpec> PendingToolCalls;
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
		std::atomic<bool> bCancelled{false};
		std::atomic<bool> bTerminal{false};

		void HandleEvent(const FUnrealAiLlmStreamEvent& Ev);
		void DispatchLlm(bool bRetrySameRound = false);
		void CompleteToolPath();
		void CompleteAssistantOnly();
		void Fail(const FString& Msg);
		void Succeed();
		void AccumulateRoundUsage();
		void EmitPlanningDecision(const FString& ModeUsed, const TArray<FString>& TriggerReasons);

		static constexpr int32 CharPerTokenApprox = 4;
	};

	void FAgentTurnRunner::EmitPlanningDecision(const FString& ModeUsed, const TArray<FString>& TriggerReasons)
	{
		if (Sink.IsValid())
		{
			Sink->OnPlanningDecision(ModeUsed, TriggerReasons, ReplanCount, QueueStepsPending);
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
		int32 ParsedMax = CapLimits.MaxAgentLlmRounds > 0 ? CapLimits.MaxAgentLlmRounds : 32;
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
		EffectiveMaxLlmRounds = FMath::Clamp(ParsedMax, 1, 512);

		if (!bRetrySameRound)
		{
			if (LlmRound >= EffectiveMaxLlmRounds)
			{
				const bool bHasRepeatValidationFailures = RepeatedToolFailureCount >= 2;
				Fail(FString::Printf(
					TEXT("Max tool/LLM rounds exceeded (%d). %sIncrease \"Max agent LLM rounds\" in AI Settings."),
					EffectiveMaxLlmRounds,
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
		bFinishSeen = false;

		FUnrealAiLlmRequest LlmReq;
		FString BuildErr;
		TArray<FString> ContextUserMsgs;
		if (!UnrealAiTurnLlmRequestBuilder::Build(
				Request,
				LlmRound,
				EffectiveMaxLlmRounds,
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
			MergeToolCallDeltas(PendingToolCalls, Ev.ToolCalls);
			break;
		case EUnrealAiLlmStreamEventType::Finish:
			UsageThisRound.PromptTokens = FMath::Max(UsageThisRound.PromptTokens, Ev.Usage.PromptTokens);
			UsageThisRound.CompletionTokens = FMath::Max(UsageThisRound.CompletionTokens, Ev.Usage.CompletionTokens);
			UsageThisRound.TotalTokens = FMath::Max(UsageThisRound.TotalTokens, Ev.Usage.TotalTokens);
			bFinishSeen = true;
			if (Ev.FinishReason == TEXT("tool_calls") && PendingToolCalls.Num() == 0)
			{
				Fail(TEXT("Model requested tools but sent no tool_calls"));
				break;
			}
			if (Ev.FinishReason == TEXT("tool_calls") || PendingToolCalls.Num() > 0)
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

	void FAgentTurnRunner::CompleteToolPath()
	{
		if (!Tools || !Conv.IsValid())
		{
			Fail(TEXT("Tool host missing"));
			return;
		}
		PendingToolCalls.RemoveAll([](const FUnrealAiToolCallSpec& T)
		{
			return T.Name.TrimStartAndEnd().IsEmpty();
		});
		if (PendingToolCalls.Num() == 0)
		{
			Fail(TEXT("Model completed tool_calls but no valid tool name (empty function.name)"));
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
		const bool bLargeBatch = bAgentMode && !bTodoPlanOnly && ToolBatchSize >= 4;
		const bool bNearBudget = bAgentMode && !bTodoPlanOnly && (EffectiveMaxLlmRounds - LlmRound) <= 2;
		const int32 MaxDirectToolsBeforeQueue = 3;
		const int32 ToolsToExecute = (bLargeBatch && ToolBatchSize > MaxDirectToolsBeforeQueue)
			? MaxDirectToolsBeforeQueue
			: ToolBatchSize;
		bool bDeferredQueue = false;
		bool bRepeatedToolLoop = false;
		bool bRepeatedEmptySearch = false;
		int32 ToolSuccessCount = 0;
		int32 ToolFailCount = 0;
		TArray<FString> ExecutedToolNames;
		FUnrealAiConversationMessage Am;
		Am.Role = TEXT("assistant");
		Am.Content = AssistantBuffer;
		Am.ToolCalls = PendingToolCalls;
		Conv->GetMessagesMutable().Add(Am);

		for (int32 TcIdx = 0; TcIdx < PendingToolCalls.Num(); ++TcIdx)
		{
			const FUnrealAiToolCallSpec& Tc = PendingToolCalls[TcIdx];
			if (TcIdx >= ToolsToExecute)
			{
				bDeferredQueue = true;
				FUnrealAiConversationMessage Deferred;
				Deferred.Role = TEXT("tool");
				Deferred.ToolCallId = Tc.Id;
				Deferred.Content = TEXT("{\"ok\":false,\"error\":\"deferred_by_harness_for_planning: tool batch exceeded fast-path budget; emit/update todo plan and continue by queued steps\"}");
				Conv->GetMessagesMutable().Add(Deferred);
				continue;
			}
			FString InvokeName;
			FString InvokeArgs;
			FString UnwrapErr;
			if (!UnwrapDispatchToolCall(Tc, InvokeName, InvokeArgs, UnwrapErr))
			{
				if (Sink.IsValid())
				{
					Sink->OnToolCallStarted(Tc.Name, Tc.Id, Tc.ArgumentsJson);
				}
				const FString ModelToolContent = UnwrapErr;
				if (Sink.IsValid())
				{
					Sink->OnToolCallFinished(Tc.Name, Tc.Id, false, UnrealAiTruncateForUi(ModelToolContent), nullptr);
				}
				FUnrealAiConversationMessage Tm;
				Tm.Role = TEXT("tool");
				Tm.ToolCallId = Tc.Id;
				Tm.Content = ModelToolContent;
				Conv->GetMessagesMutable().Add(Tm);
				++ToolFailCount;
				continue;
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
			const bool bRepeatSignature = bAgentMode && InvokeName != TEXT("agent_emit_todo_plan")
				&& (SigCount >= 3 || NameCount >= 5);
			const FUnrealAiToolInvocationResult Inv = Tools->InvokeTool(InvokeName, InvokeArgs, Tc.Id);
			ExecutedToolNames.Add(InvokeName);
			const FString DialogFootnote = FUnrealAiEditorModalMonitor::ConsumePendingToolDialogFootnote();
			FString ModelToolContent = Inv.bOk ? Inv.ContentForModel : Inv.ErrorMessage;
			if (bRepeatSignature && !Inv.bOk)
			{
				bRepeatedToolLoop = true;
			}
			if (!DialogFootnote.IsEmpty())
			{
				if (!ModelToolContent.IsEmpty())
				{
					ModelToolContent += TEXT("\n");
				}
				ModelToolContent +=
					FString::Printf(TEXT("[Editor blocking dialog during tool]: %s"), *DialogFootnote);
			}

			// Detect repeated non-progress discovery/search tool results.
			// Tools can "succeed" while returning no actionable matches, causing round-cap loops.
			if (Inv.bOk)
			{
				auto IsNonProgressEmptySearchResult = [&]()
				{
					if (InvokeName == TEXT("asset_index_fuzzy_search"))
					{
						const bool bLowConf =
							ModelToolContent.Contains(TEXT("\"low_confidence\":true")) ||
							ModelToolContent.Contains(TEXT("\"low_confidence\": true"));
						const bool bEmptyMatches =
							ModelToolContent.Contains(TEXT("\"matches\":[]")) ||
							ModelToolContent.Contains(TEXT("\"matches\": []"));
						const bool bCountZero =
							ModelToolContent.Contains(TEXT("\"count\":0")) ||
							ModelToolContent.Contains(TEXT("\"count\": 0"));
						return bLowConf && (bEmptyMatches || bCountZero);
					}

					if (InvokeName == TEXT("scene_fuzzy_search"))
					{
						const bool bCountZero =
							ModelToolContent.Contains(TEXT("\"count\":0")) ||
							ModelToolContent.Contains(TEXT("\"count\": 0"));
						return bCountZero;
					}

					if (InvokeName == TEXT("source_search_symbol"))
					{
						const bool bZeroFiles =
							ModelToolContent.Contains(TEXT("\"files_considered\":0")) ||
							ModelToolContent.Contains(TEXT("\"files_considered\": 0"));
						const bool bZeroCandidates =
							ModelToolContent.Contains(TEXT("\"path_candidates\":0")) ||
							ModelToolContent.Contains(TEXT("\"path_candidates\": 0"));
						const bool bEmptyPathMatches =
							ModelToolContent.Contains(TEXT("\"path_matches\":[]")) ||
							ModelToolContent.Contains(TEXT("\"path_matches\": []"));
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
						bRepeatedEmptySearch = true;
						bRepeatedToolLoop = true;
					}
				}
				else
				{
					NonProgressEmptySearchCountByToolName.Remove(InvokeName);
				}
			}
			if (Inv.bOk)
			{
				++ToolSuccessCount;
				LastToolFailureSignature.Reset();
				RepeatedToolFailureCount = 0;
			}
			else
			{
				++ToolFailCount;
				// Repeated invalid call patterns should be detected by tool+args signature, not by the
				// (potentially variable) error string content.
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
				Sink->OnToolCallFinished(
					InvokeName,
					Tc.Id,
					Inv.bOk,
					UnrealAiTruncateForUi(ModelToolContent),
					Inv.EditorPresentation);
			}
			// Persist successful tool results into thread context so future context builds can rank/trim them.
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
				const FString PlanBody = (Inv.bOk && Inv.ContentForModel.TrimStart().StartsWith(TEXT("{")))
					? Inv.ContentForModel
					: InvokeArgs;
				Sink->OnTodoPlanEmitted(PlanTitle, PlanBody);
			}
			FUnrealAiConversationMessage Tm;
			Tm.Role = TEXT("tool");
			Tm.ToolCallId = Tc.Id;
			Tm.Content = ModelToolContent;
			Conv->GetMessagesMutable().Add(Tm);
		}
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
			const bool bNeedsMutationFollowthrough = Request.Mode == EUnrealAiAgentMode::Agent
				&& UserLikelyRequestsMutation(LastRealUser)
				&& ExecutedToolNames.Num() > 0;
			if (bNeedsMutationFollowthrough)
			{
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
		const bool bModeWantsTools =
			(Request.Mode == EUnrealAiAgentMode::Agent || Request.Mode == EUnrealAiAgentMode::Orchestrate);
		const FString LastRealUser = GetLastRealUserMessage(Conv->GetMessages());
		const bool bActionIntent = UserLikelyRequestsActionTool(LastRealUser);
		const bool bHasExplicitBlocker = AssistantContainsExplicitBlocker(AssistantBuffer);
		if (bModeWantsTools && AssistantBuffer.TrimStartAndEnd().IsEmpty() && LlmRound < EffectiveMaxLlmRounds)
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
		if (bModeWantsTools && bActionIntent && !bHasExplicitBlocker && LlmRound < EffectiveMaxLlmRounds && ActionNoToolNudgeCount < 2)
		{
			++ActionNoToolNudgeCount;
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
		if (bModeWantsTools && bActionIntent && !bHasExplicitBlocker && ActionNoToolNudgeCount >= 2)
		{
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
