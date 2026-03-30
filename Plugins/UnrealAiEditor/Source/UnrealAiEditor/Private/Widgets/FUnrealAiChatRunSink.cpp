#include "Widgets/FUnrealAiChatRunSink.h"

#include "Widgets/UnrealAiChatTranscript.h"
#include "Widgets/UnrealAiToolUi.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Widgets/UnrealAiPlanDraftPersist.h"

static bool TryStripChatNameTokenFromText(FString& InOutText, FString& OutChatName)
{
	OutChatName.Reset();
	bool bFoundAny = false;
	int32 SearchFrom = 0;

	while (true)
	{
		const int32 TagStart = InOutText.Find(TEXT("<chat-name"), ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchFrom);
		if (TagStart < 0)
		{
			break;
		}
		const int32 TagEnd = InOutText.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, TagStart);
		if (TagEnd < 0 || TagEnd <= TagStart)
		{
			break;
		}

		const int32 TagLen = (TagEnd - TagStart) + 1;
		const FString Tag = InOutText.Mid(TagStart, TagLen);

		FString LocalName;
		const int32 Colon = Tag.Find(TEXT(":"));
		if (Colon >= 0)
		{
			FString Inner = Tag.Mid(Colon + 1);
			Inner.TrimStartAndEndInline();
			// Drop trailing '>' if it remains inside the captured inner string.
			if (Inner.EndsWith(TEXT(">")))
			{
				Inner.LeftInline(Inner.Len() - 1);
				Inner.TrimStartAndEndInline();
			}
			// Strip optional quotes.
			if (Inner.Len() >= 2 &&
				((Inner.StartsWith(TEXT("\"")) && Inner.EndsWith(TEXT("\""))) ||
				 (Inner.StartsWith(TEXT("'")) && Inner.EndsWith(TEXT("'")))))
			{
				Inner = Inner.Mid(1, Inner.Len() - 2);
				Inner.TrimStartAndEndInline();
			}
			LocalName = Inner;
		}

		InOutText.RemoveAt(TagStart, TagLen);
		bFoundAny = true;
		if (!LocalName.IsEmpty())
		{
			OutChatName = LocalName;
		}
		SearchFrom = TagStart; // continue from removal point
	}

	// Return whether we stripped any token. OutChatName may still be empty if the model
	// produced an invalid/empty name.
	return bFoundAny;
}

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

void FUnrealAiChatRunSink::OnRunStarted(const FUnrealAiRunIds& Ids)
{
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
			Transcript->AddInformationalNotice(Line);
		}
	}
}

void FUnrealAiChatRunSink::OnAssistantDelta(const FString& Chunk)
{
	if (Transcript.IsValid())
	{
		Transcript->AppendAssistantDelta(Chunk);
	}
}

void FUnrealAiChatRunSink::OnThinkingDelta(const FString& Chunk)
{
	if (Transcript.IsValid())
	{
		Transcript->AppendThinkingDelta(Chunk);
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
		Transcript->AppendRunEvent(FString::Printf(TEXT("Enforcement: %s — %s"), *EventType, *Detail));
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
	Transcript->AppendRunEvent(Line);
}

void FUnrealAiChatRunSink::OnRunFinished(bool bSuccess, const FString& ErrorMessage)
{
	if (!Transcript.IsValid())
	{
		return;
	}

	Transcript->SetRunProgress(bSuccess ? TEXT("Run completed") : TEXT("Run failed"));
	Transcript->AppendRunEvent(bSuccess ? TEXT("Run finished successfully.") : FString::Printf(TEXT("Run finished with error: %s"), *ErrorMessage));
	Transcript->EndRun(bSuccess, ErrorMessage);

	// Always strip the token from visible assistant output if present;
	// rename UI once when ChatName is still empty (independent of persistence success).
	bool bModifiedAnyAssistantText = false;
	FString ExtractedName;

	for (int32 i = Transcript->Blocks.Num() - 1; i >= 0; --i)
	{
		FUnrealAiChatBlock& B = Transcript->Blocks[i];
		if (B.Kind != EUnrealAiChatBlockKind::Assistant || B.AssistantText.IsEmpty())
		{
			continue;
		}

		FString LocalName;
		FString LocalText = B.AssistantText;
		if (TryStripChatNameTokenFromText(LocalText, LocalName))
		{
			B.AssistantText = LocalText;
			B.AssistantText.TrimStartAndEndInline();
			bModifiedAnyAssistantText = true;
			ExtractedName = LocalName;
			break;
		}
	}

	if (bModifiedAnyAssistantText)
	{
		Transcript->OnStructuralChange.Broadcast();
	}

	if (!Session.IsValid() || !Session->ChatName.IsEmpty() || !bModifiedAnyAssistantText)
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
