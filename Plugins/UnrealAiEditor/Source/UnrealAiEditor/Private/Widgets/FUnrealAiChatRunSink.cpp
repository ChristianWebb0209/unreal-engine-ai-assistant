#include "Widgets/FUnrealAiChatRunSink.h"

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
	const EUnrealAiAgentMode InAgentMode)
	: Transcript(MoveTemp(InTranscript))
	, Session(MoveTemp(InSession))
	, Persistence(InPersistence)
	, ProjectId(InProjectId)
	, ThreadId(InThreadId)
	, AgentMode(InAgentMode)
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
		Transcript->BeginToolCall(ToolName, CallId, UnrealAiTruncateForUi(ArgumentsJson));
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

void FUnrealAiChatRunSink::OnEnforcementEvent(const FString& EventType, const FString& Detail)
{
	if (Transcript.IsValid())
	{
		const bool bHideInternalBackgroundOps =
			EventType.Equals(TEXT("background_op"), ESearchCase::IgnoreCase)
			|| EventType.Equals(TEXT("tool_selector_ranks"), ESearchCase::IgnoreCase)
			|| EventType.StartsWith(TEXT("tool_surface_"), ESearchCase::IgnoreCase);

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
	FString ExtractedName;

	for (int32 i = Transcript->Blocks.Num() - 1; i >= 0; --i)
	{
		FUnrealAiChatBlock& B = Transcript->Blocks[i];
		FString LocalName;

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
			if (UnrealAiStripChatNameTagsFromText(B.AssistantText, LocalName))
			{
				B.AssistantText.TrimStartAndEndInline();
				bModifiedAnyVisibleText = true;
				if (!LocalName.IsEmpty() && ExtractedName.IsEmpty())
				{
					ExtractedName = LocalName;
				}
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
			if (UnrealAiStripChatNameTagsFromText(B.ThinkingText, LocalName))
			{
				B.ThinkingText.TrimStartAndEndInline();
				bModifiedAnyVisibleText = true;
				if (!LocalName.IsEmpty() && ExtractedName.IsEmpty())
				{
					ExtractedName = LocalName;
				}
			}
		}
		else if (B.Kind == EUnrealAiChatBlockKind::User && !B.UserText.IsEmpty())
		{
			FString LocalText = B.UserText;
			if (UnrealAiStripChatNameTagsFromText(LocalText, LocalName))
			{
				B.UserText = LocalText;
				B.UserText.TrimStartAndEndInline();
				bModifiedAnyVisibleText = true;
				if (!LocalName.IsEmpty() && ExtractedName.IsEmpty())
				{
					ExtractedName = LocalName;
				}
			}
		}
	}

	if (bModifiedAnyVisibleText)
	{
		Transcript->OnStructuralChange.Broadcast();
	}

	if (!Session.IsValid() || !Session->ChatName.IsEmpty() || !bModifiedAnyVisibleText)
	{
		return;
	}

	FString FinalName = ExtractedName;
	if (FinalName.IsEmpty())
	{
		for (int32 i = Transcript->Blocks.Num() - 1; i >= 0; --i)
		{
			const FUnrealAiChatBlock& B = Transcript->Blocks[i];
			if (B.Kind == EUnrealAiChatBlockKind::User && !B.UserText.IsEmpty())
			{
				FinalName = B.UserText;
				FinalName.ReplaceInline(TEXT("\r"), TEXT(""));
				FinalName.ReplaceInline(TEXT("\n"), TEXT(" "));
				FinalName.TrimStartAndEndInline();
				const int32 MaxChars = 56;
				if (FinalName.Len() > MaxChars)
				{
					FinalName = FinalName.Left(MaxChars);
					FinalName.TrimEndInline();
				}
				break;
			}
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
