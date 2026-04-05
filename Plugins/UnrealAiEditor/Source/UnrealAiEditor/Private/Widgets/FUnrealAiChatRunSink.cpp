#include "Widgets/FUnrealAiChatRunSink.h"

#include "Observability/UnrealAiGameThreadPerf.h"
#include "Tools/UnrealAiToolCatalog.h"
#include "Widgets/UnrealAiToolDisplayName.h"
#include "Tools/UnrealAiBuildBlueprintTag.h"
#include "Widgets/UnrealAiChatTranscript.h"
#include "Widgets/UnrealAiToolUi.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Widgets/UnrealAiPlanDraftPersist.h"

FUnrealAiChatRunSink::FUnrealAiChatRunSink(
	TSharedPtr<FUnrealAiChatTranscript> InTranscript,
	TSharedPtr<FUnrealAiChatUiSession> InSession,
	IUnrealAiPersistence* InPersistence,
	const FString& InProjectId,
	const FString& InThreadId,
	const EUnrealAiAgentMode InAgentMode,
	FUnrealAiToolCatalog* InToolCatalog)
	: Transcript(MoveTemp(InTranscript))
	, Session(MoveTemp(InSession))
	, Persistence(InPersistence)
	, ProjectId(InProjectId)
	, ThreadId(InThreadId)
	, AgentMode(InAgentMode)
	, ToolCatalog(InToolCatalog)
{
}

void FUnrealAiChatRunSink::AppendStreamChunkFilteringTranscriptEchoLines(
	FString& LineCarry,
	const FString& Chunk,
	TFunctionRef<void(const FString&)> EmitDelta)
{
	if (Chunk.IsEmpty())
	{
		return;
	}
	FString Work = LineCarry + Chunk;
	LineCarry.Reset();
	int32 ReadAt = 0;
	while (ReadAt < Work.Len())
	{
		const int32 Nl = Work.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ReadAt);
		if (Nl == INDEX_NONE)
		{
			LineCarry = Work.Mid(ReadAt);
			break;
		}
		const FString Line = Work.Mid(ReadAt, Nl - ReadAt);
		ReadAt = Nl + 1;
		FString T = Line;
		T.TrimStartAndEndInline();
		if (!UnrealAiIsTranscriptStyleDelimiterTrimmedLine(T))
		{
			EmitDelta(Line + TEXT('\n'));
		}
	}
}

void FUnrealAiChatRunSink::FlushStreamLineCarries()
{
	auto FlushOne = [this](FString& Carry, TFunctionRef<void(const FString&)> Emit)
	{
		if (Carry.IsEmpty() || !Transcript.IsValid())
		{
			Carry.Reset();
			return;
		}
		FString T = Carry;
		Carry.Reset();
		T.TrimStartAndEndInline();
		if (T.IsEmpty())
		{
			return;
		}
		if (UnrealAiIsTranscriptStyleDelimiterTrimmedLine(T))
		{
			return;
		}
		Emit(T);
	};

	FlushOne(AssistantStreamLineCarry, [this](const FString& S) { Transcript->AppendAssistantDelta(S); });
	FlushOne(ThinkingStreamLineCarry, [this](const FString& S) { Transcript->AppendThinkingDelta(S); });
}

void FUnrealAiChatRunSink::OnRunStarted(const FUnrealAiRunIds& Ids)
{
	AssistantStreamLineCarry.Reset();
	ThinkingStreamLineCarry.Reset();
	if (Transcript.IsValid())
	{
		if (!Ids.ParentRunId.IsValid())
		{
			Transcript->BeginRun(Ids.RunId);
			Transcript->SetRunProgress(TEXT("Run started"));
		}
		else
		{
			Transcript->AppendRunEvent(FString::Printf(
				TEXT("Subagent started: run=%s parent=%s worker=%d"),
				*Ids.RunId.ToString(EGuidFormats::DigitsWithHyphens),
				*Ids.ParentRunId.ToString(EGuidFormats::DigitsWithHyphens),
				Ids.WorkerIndex));
		}
		// Do not open an empty "thinking" row here — it animated forever (dots timer) when the model
		// goes straight to tools with no reasoning. A thinking block is created on the first reasoning delta.
	}
}

void FUnrealAiChatRunSink::OnContextUserMessages(const TArray<FString>& Messages)
{
	if (!Transcript.IsValid())
	{
		return;
	}
	for (const FString& Line : Messages)
	{
		if (!Line.IsEmpty())
		{
			// These "ops" lines come from context background housekeeping (project tree refresh / startup status),
			// and they should never be shown as end-user-visible chat content.
			const FString LowerLine = Line.ToLower();
			if (LowerLine.Contains(TEXT("background query ran"))
				|| LowerLine.Contains(TEXT("startup ops"))
				|| LowerLine.Contains(TEXT("start ops")))
			{
				continue;
			}
			Transcript->AddInformationalNotice(Line);
		}
	}
}

void FUnrealAiChatRunSink::OnAssistantDelta(const FString& Chunk)
{
	UNREALAI_GT_PERF_SCOPE("UI.RunSink.OnAssistantDelta");
	if (Transcript.IsValid())
	{
		AppendStreamChunkFilteringTranscriptEchoLines(
			AssistantStreamLineCarry,
			Chunk,
			[this](const FString& Emit) { Transcript->AppendAssistantDelta(Emit); });
	}
}

void FUnrealAiChatRunSink::OnThinkingDelta(const FString& Chunk)
{
	UNREALAI_GT_PERF_SCOPE("UI.RunSink.OnThinkingDelta");
	if (Transcript.IsValid())
	{
		AppendStreamChunkFilteringTranscriptEchoLines(
			ThinkingStreamLineCarry,
			Chunk,
			[this](const FString& Emit) { Transcript->AppendThinkingDelta(Emit); });
	}
}

void FUnrealAiChatRunSink::OnToolCallStarted(const FString& ToolName, const FString& CallId, const FString& ArgumentsJson)
{
	if (Transcript.IsValid())
	{
		const FString DisplayTitle = UnrealAiResolveToolUserFacingName(ToolName, ToolCatalog);
		Transcript->BeginToolCall(ToolName, CallId, UnrealAiTruncateForUi(ArgumentsJson), DisplayTitle);
	}
}

void FUnrealAiChatRunSink::OnToolCallFinished(
	const FString& ToolName,
	const FString& CallId,
	bool bSuccess,
	const FString& ResultPreview,
	const TSharedPtr<FUnrealAiToolEditorPresentation>& EditorPresentation)
{
	(void)ToolName;
	if (Transcript.IsValid())
	{
		Transcript->EndToolCall(CallId, bSuccess, ResultPreview, EditorPresentation);
	}
}

void FUnrealAiChatRunSink::OnEditorBlockingDialogDuringTools(const FString& Summary)
{
	if (Transcript.IsValid())
	{
		Transcript->AddEditorBlockingDialogNotice(Summary);
	}
}

void FUnrealAiChatRunSink::OnRunContinuation(int32 PhaseIndex, int32 TotalPhasesHint)
{
	if (Transcript.IsValid())
	{
		const int32 SafeTotal = FMath::Max(1, TotalPhasesHint);
		const int32 SafePhase = FMath::Clamp(PhaseIndex, 0, SafeTotal);
		Transcript->SetRunProgress(FString::Printf(
			TEXT("Running phase %d/%d"),
			SafePhase,
			SafeTotal));
		Transcript->OnContinuation(PhaseIndex, TotalPhasesHint);
	}
}

void FUnrealAiChatRunSink::OnTodoPlanEmitted(const FString& Title, const FString& PlanJson)
{
	if (!Transcript.IsValid())
	{
		return;
	}
	if (AgentMode == EUnrealAiAgentMode::Agent)
	{
		return;
	}
	Transcript->AddTodoPlan(Title, PlanJson);
}

void FUnrealAiChatRunSink::OnPlanDraftReady(const FString& DagJsonText)
{
	if (!Transcript.IsValid())
	{
		return;
	}
	Transcript->RemovePlanDraftPendingBlocks();
	if (Persistence && !ProjectId.IsEmpty() && !ThreadId.IsEmpty())
	{
		Persistence->SaveThreadPlanDraftJson(ProjectId, ThreadId, UnrealAiPlanDraftPersist::WrapDraftFile(DagJsonText));
	}
	Transcript->AddPlanDraftPending(DagJsonText);
	Transcript->AppendRunEvent(TEXT("Plan draft ready. Review/edit and click Build to continue."));
}

void FUnrealAiChatRunSink::OnPlanHarnessSubTurnComplete()
{
	if (Transcript.IsValid())
	{
		Transcript->AppendRunEvent(TEXT("Plan sub-turn complete."));
	}
}

void FUnrealAiChatRunSink::OnPlanningDecision(
	const FString& ModeUsed,
	const TArray<FString>& TriggerReasons,
	int32 ReplanCount,
	int32 QueueStepsPending)
{
	if (!Transcript.IsValid())
	{
		return;
	}
	const FString Reasons = TriggerReasons.Num() > 0 ? FString::Join(TriggerReasons, TEXT(", ")) : TEXT("none");
	Transcript->AppendRunEvent(FString::Printf(
		TEXT("Planning decision: mode=%s reasons=[%s] replans=%d queue=%d"),
		*ModeUsed,
		*Reasons,
		ReplanCount,
		QueueStepsPending));
}

void FUnrealAiChatRunSink::OnSubagentBuilderHandoff(const FString& BuilderDisplayName)
{
	if (!Transcript.IsValid() || BuilderDisplayName.IsEmpty())
	{
		return;
	}
	// Prefix marks harness-injected user rows (muted bubble + "--- Harness ---" in plain-text export).
	Transcript->AddUserMessage(
		FString::Printf(TEXT("[Harness] Delegated to %s."), *BuilderDisplayName));
}

void FUnrealAiChatRunSink::OnEnforcementEvent(const FString& EventType, const FString& Detail)
{
	if (Transcript.IsValid())
	{
		const bool bHideInternalBackgroundOps =
			EventType.Equals(TEXT("background_op"), ESearchCase::IgnoreCase)
			|| EventType.Equals(TEXT("tool_selector_ranks"), ESearchCase::IgnoreCase)
			|| EventType.StartsWith(TEXT("tool_surface_"), ESearchCase::IgnoreCase)
			|| EventType.Equals(TEXT("blueprint_builder_chain"), ESearchCase::IgnoreCase)
			|| EventType.Equals(TEXT("environment_builder_chain"), ESearchCase::IgnoreCase);

		if (!bHideInternalBackgroundOps)
		{
			Transcript->AppendRunEvent(FString::Printf(TEXT("Enforcement: %s — %s"), *EventType, *Detail));
		}
	}
}

void FUnrealAiChatRunSink::OnEnforcementSummary(
	int32 ActionIntentTurns,
	int32 ActionTurnsWithToolCalls,
	int32 ActionTurnsWithExplicitBlocker,
	int32 ActionNoToolNudges,
	int32 MutationIntentTurns,
	int32 MutationReadOnlyNudges)
{
	if (!Transcript.IsValid())
	{
		return;
	}
	Transcript->AppendRunEvent(FString::Printf(
		TEXT("Enforcement summary: action_intent=%d tool_calls=%d blockers=%d nudges=%d mutation_intent=%d ro_nudges=%d"),
		ActionIntentTurns,
		ActionTurnsWithToolCalls,
		ActionTurnsWithExplicitBlocker,
		ActionNoToolNudges,
		MutationIntentTurns,
		MutationReadOnlyNudges));
}

void FUnrealAiChatRunSink::OnHarnessProgressLog(const FString& Line)
{
	if (!Transcript.IsValid() || Line.IsEmpty())
	{
		return;
	}
	FString TrimLine = Line;
	TrimLine.TrimStartAndEndInline();
	if (UnrealAiIsTranscriptNoiseOrHarnessDisplayLine(TrimLine))
	{
		return;
	}
	const FString LowerLine = Line.ToLower();
	if (LowerLine.Contains(TEXT("background query ran")))
	{
		// Keep this only in file logs/harness traces, not the visible chat transcript.
		return;
	}
	Transcript->AppendRunEvent(Line);
}

void FUnrealAiChatRunSink::OnRunFinished(bool bSuccess, const FString& ErrorMessage)
{
	if (!Transcript.IsValid())
	{
		return;
	}

	FlushStreamLineCarries();

	if (!bSuccess)
	{
		Transcript->SetRunProgress(TEXT("Run failed"));
		Transcript->AppendRunEvent(FString::Printf(TEXT("Run finished with error: %s"), *ErrorMessage));
	}
	else
	{
		// Successful completion should not add a visible "run completed" chat line.
		Transcript->ClearRunProgress();
	}
	Transcript->EndRun(bSuccess, ErrorMessage);

	// Strip chat-name tokens from any user-visible block (assistant, merged thinking subline, user).
	// Reasoning streams often carry `<chat-name: ...>` even when assistant text does not.
	bool bModifiedAnyVisibleText = false;

	for (int32 i = Transcript->Blocks.Num() - 1; i >= 0; --i)
	{
		FUnrealAiChatBlock& B = Transcript->Blocks[i];
		FString UnusedChatNameFromTag;

		if (B.Kind == EUnrealAiChatBlockKind::Assistant && !B.AssistantText.IsEmpty())
		{
			FString LocalText = B.AssistantText;
			const int32 LenBeforeStrip = LocalText.Len();
			UnrealAiStripTranscriptStyleDelimiterLines(LocalText);
			if (LocalText.Len() != LenBeforeStrip)
			{
				B.AssistantText = LocalText;
				B.AssistantText.TrimStartAndEndInline();
				bModifiedAnyVisibleText = true;
			}
			{
				const int32 LenBeforeProto = B.AssistantText.Len();
				UnrealAiBuildBlueprintTag::StripProtocolMarkersForUi(B.AssistantText);
				if (B.AssistantText.Len() != LenBeforeProto)
				{
					bModifiedAnyVisibleText = true;
				}
			}
			if (UnrealAiStripChatNameTagsFromText(B.AssistantText, UnusedChatNameFromTag))
			{
				B.AssistantText.TrimStartAndEndInline();
				bModifiedAnyVisibleText = true;
			}
		}
		else if (B.Kind == EUnrealAiChatBlockKind::Thinking && !B.ThinkingText.IsEmpty())
		{
			FString LocalText = B.ThinkingText;
			const int32 LenBeforeStrip = LocalText.Len();
			UnrealAiStripTranscriptStyleDelimiterLines(LocalText);
			if (LocalText.Len() != LenBeforeStrip)
			{
				B.ThinkingText = LocalText;
				B.ThinkingText.TrimStartAndEndInline();
				bModifiedAnyVisibleText = true;
			}
			if (UnrealAiStripChatNameTagsFromText(B.ThinkingText, UnusedChatNameFromTag))
			{
				B.ThinkingText.TrimStartAndEndInline();
				bModifiedAnyVisibleText = true;
			}
		}
		else if (B.Kind == EUnrealAiChatBlockKind::User && !B.UserText.IsEmpty())
		{
			FString LocalText = B.UserText;
			if (UnrealAiStripChatNameTagsFromText(LocalText, UnusedChatNameFromTag))
			{
				B.UserText = LocalText;
				B.UserText.TrimStartAndEndInline();
				bModifiedAnyVisibleText = true;
			}
		}
	}

	if (bModifiedAnyVisibleText)
	{
		Transcript->OnStructuralChange.Broadcast();
	}

	if (!Session.IsValid() || !Session->ChatName.IsEmpty())
	{
		return;
	}

	FString FinalName;
	for (const FUnrealAiChatBlock& B : Transcript->Blocks)
	{
		if (B.Kind != EUnrealAiChatBlockKind::User || B.UserText.IsEmpty())
		{
			continue;
		}
		FinalName = UnrealAiMakeChatTabTitleFromUserMessage(B.UserText);
		if (!FinalName.IsEmpty())
		{
			break;
		}
	}

	if (FinalName.IsEmpty())
	{
		return;
	}

	Session->ChatName = FinalName;
	if (Persistence && !ProjectId.IsEmpty() && !ThreadId.IsEmpty())
	{
		Persistence->SetThreadChatName(ProjectId, ThreadId, FinalName);
	}
	Session->OnChatNameChanged.Broadcast();
}
