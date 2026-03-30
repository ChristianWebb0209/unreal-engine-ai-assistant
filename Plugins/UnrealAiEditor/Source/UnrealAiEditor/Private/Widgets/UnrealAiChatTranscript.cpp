#include "Widgets/UnrealAiChatTranscript.h"

#include "HAL/PlatformTime.h"

FString UnrealAiFormatStepDurationForUi(const double Sec)
{
	if (Sec < 0.0)
	{
		return FString();
	}
	const int64 TotalSec = FMath::Max<int64>(0, FMath::RoundToInt64(Sec));
	const int64 H = TotalSec / 3600;
	const int64 M = (TotalSec % 3600) / 60;
	const int64 S = TotalSec % 60;
	if (H > 0)
	{
		return FString::Printf(
			TEXT("%dh %dm %ds"),
			static_cast<int32>(H),
			static_cast<int32>(M),
			static_cast<int32>(S));
	}
	if (M > 0)
	{
		return FString::Printf(TEXT("%dm %ds"), static_cast<int32>(M), static_cast<int32>(S));
	}
	return FString::Printf(TEXT("%ds"), static_cast<int32>(S));
}

namespace UnrealAiTranscriptTiming
{
	static void FinalizeNamedStep(FUnrealAiChatBlock& B, const TCHAR* StepLabelEn)
	{
		if (B.StepMonotonicStart <= 0.0)
		{
			return;
		}
		const double Dur = FPlatformTime::Seconds() - B.StepMonotonicStart;
		B.StepMonotonicStart = 0.0;
		if (Dur < 0.001)
		{
			return;
		}
		B.StepTimingCaption =
			FString::Printf(TEXT("%s · %s"), StepLabelEn, *UnrealAiFormatStepDurationForUi(Dur));
	}

	static void FinalizeToolStep(FUnrealAiChatBlock& B)
	{
		if (B.StepMonotonicStart <= 0.0)
		{
			return;
		}
		const double Dur = FPlatformTime::Seconds() - B.StepMonotonicStart;
		B.StepMonotonicStart = 0.0;
		const FString Label =
			B.ToolName.IsEmpty() ? FString(TEXT("Tool")) : B.ToolName.Left(56);
		B.StepTimingCaption =
			FString::Printf(TEXT("%s · %s"), *Label, *UnrealAiFormatStepDurationForUi(Dur));
	}
}

void FUnrealAiChatTranscript::Clear()
{
	Blocks.Reset();
	ActiveRunId = FGuid();
	bHasActiveRun = false;
	bAssistantSegmentOpen = false;
	bThinkingOpen = false;
	LastAssistantStreamChunkMonotonicTime = 0.0;
	OnStructuralChange.Broadcast();
}

FString FUnrealAiChatTranscript::FormatPlainText() const
{
	FString Out;
	for (const FUnrealAiChatBlock& B : Blocks)
	{
		switch (B.Kind)
		{
		case EUnrealAiChatBlockKind::User:
			Out += FString::Printf(
				TEXT("%s\n%s\n\n"),
				B.bHarnessSystemUser ? TEXT("--- Harness ---") : TEXT("--- User ---"),
				*B.UserText);
			break;
		case EUnrealAiChatBlockKind::Thinking:
			if (!B.StepTimingCaption.IsEmpty())
			{
				Out += B.StepTimingCaption + TEXT("\n");
			}
			Out += FString::Printf(TEXT("--- Thinking ---\n%s\n\n"), *B.ThinkingText);
			break;
		case EUnrealAiChatBlockKind::Assistant:
			if (!B.StepTimingCaption.IsEmpty())
			{
				Out += B.StepTimingCaption + TEXT("\n");
			}
			Out += FString::Printf(TEXT("--- Assistant ---\n%s\n\n"), *B.AssistantText);
			break;
		case EUnrealAiChatBlockKind::ToolCall:
			if (!B.StepTimingCaption.IsEmpty())
			{
				Out += B.StepTimingCaption + TEXT("\n");
			}
			Out += FString::Printf(
				TEXT("--- Tool: %s ---\nArgs: %s\nResult: %s\nStatus: %s\n\n"),
				*B.ToolName,
				*B.ToolArgsPreview,
				*B.ToolResultPreview,
				B.bToolRunning ? TEXT("running") : (B.bToolOk ? TEXT("ok") : TEXT("failed")));
			break;
		case EUnrealAiChatBlockKind::TodoPlan:
			Out += FString::Printf(TEXT("--- Todo plan: %s ---\n%s\n\n"), *B.TodoTitle, *B.TodoJson);
			break;
		case EUnrealAiChatBlockKind::PlanDraftPending:
			Out += FString::Printf(TEXT("--- Plan draft (awaiting Build) ---\n%s\n\n"), *B.TodoJson);
			break;
		case EUnrealAiChatBlockKind::RunProgress:
			Out += FString::Printf(TEXT("--- Progress ---\n%s\n"), *B.ProgressLabel);
			if (!B.ProgressDetails.IsEmpty())
			{
				Out += B.ProgressDetails + TEXT("\n");
			}
			Out += TEXT("\n");
			break;
		case EUnrealAiChatBlockKind::Notice:
			Out += FString::Printf(TEXT("--- Notice ---\n%s\n\n"), *B.NoticeText);
			break;
		default:
			break;
		}
	}
	return Out;
}

namespace
{
	static bool IsHarnessInjectedUserText(const FString& Text)
	{
		FString T = Text;
		T.TrimStartInline();
		return T.StartsWith(TEXT("[Harness]"), ESearchCase::IgnoreCase);
	}
}

FGuid FUnrealAiChatTranscript::AddUserMessage(const FString& Text, FGuid DesiredId)
{
	FUnrealAiChatBlock B;
	const FGuid NewId = DesiredId.IsValid() ? DesiredId : FGuid::NewGuid();
	B.Id = NewId;
	B.Kind = EUnrealAiChatBlockKind::User;
	B.UserText = Text;
	B.bHarnessSystemUser = IsHarnessInjectedUserText(Text);
	Blocks.Add(MoveTemp(B));
	OnStructuralChange.Broadcast();
	return NewId;
}

void FUnrealAiChatTranscript::BeginRun(const FGuid& RunId)
{
	ActiveRunId = RunId;
	bHasActiveRun = true;
	bAssistantSegmentOpen = false;
	bThinkingOpen = false;
	LastAssistantStreamChunkMonotonicTime = 0.0;
}

void FUnrealAiChatTranscript::TouchAssistantStreamChunkTime()
{
	LastAssistantStreamChunkMonotonicTime = FPlatformTime::Seconds();
}

bool FUnrealAiChatTranscript::IsAssistantStreamRecentlyActive(const float QuietSeconds) const
{
	if (!bAssistantSegmentOpen || Blocks.Num() == 0)
	{
		return false;
	}
	if (Blocks.Last().Kind != EUnrealAiChatBlockKind::Assistant)
	{
		return false;
	}
	if (LastAssistantStreamChunkMonotonicTime <= 0.0)
	{
		return false;
	}
	return (FPlatformTime::Seconds() - LastAssistantStreamChunkMonotonicTime) < static_cast<double>(QuietSeconds);
}

void FUnrealAiChatTranscript::AppendThinkingDelta(const FString& Chunk)
{
	const bool bFirst = !bThinkingOpen;
	if (!bThinkingOpen)
	{
		if (Blocks.Num() > 0 && Blocks.Last().Kind == EUnrealAiChatBlockKind::Assistant && bAssistantSegmentOpen)
		{
			UnrealAiTranscriptTiming::FinalizeNamedStep(Blocks.Last(), TEXT("reply"));
			CloseAssistantSegment();
		}
		FUnrealAiChatBlock B;
		B.Id = FGuid::NewGuid();
		B.RunId = ActiveRunId;
		B.Kind = EUnrealAiChatBlockKind::Thinking;
		B.ThinkingText = Chunk;
		B.StepMonotonicStart = FPlatformTime::Seconds();
		Blocks.Add(MoveTemp(B));
		bThinkingOpen = true;
		OnStructuralChange.Broadcast();
	}
	else if (!Chunk.IsEmpty())
	{
		for (int32 i = Blocks.Num() - 1; i >= 0; --i)
		{
			if (Blocks[i].Kind == EUnrealAiChatBlockKind::Thinking)
			{
				Blocks[i].ThinkingText += Chunk;
				break;
			}
		}
	}
	OnThinkingStreamDelta.Broadcast(Chunk, bFirst);
}

void FUnrealAiChatTranscript::CloseAssistantSegment()
{
	bAssistantSegmentOpen = false;
}

int32 FUnrealAiChatTranscript::FindToolIndexByCallId(const FString& CallId) const
{
	for (int32 i = Blocks.Num() - 1; i >= 0; --i)
	{
		if (Blocks[i].Kind == EUnrealAiChatBlockKind::ToolCall && Blocks[i].ToolCallId == CallId)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void FUnrealAiChatTranscript::AppendAssistantDelta(const FString& Chunk)
{
	if (Chunk.IsEmpty())
	{
		return;
	}
	// If we only opened an empty thinking placeholder and the model answers without reasoning, drop it.
	if (bThinkingOpen && Blocks.Num() > 0)
	{
		const int32 LastIdx = Blocks.Num() - 1;
		if (Blocks[LastIdx].Kind == EUnrealAiChatBlockKind::Thinking && Blocks[LastIdx].ThinkingText.IsEmpty())
		{
			Blocks.RemoveAt(LastIdx);
			bThinkingOpen = false;
		}
	}
	if (!bAssistantSegmentOpen || Blocks.Num() == 0 || Blocks.Last().Kind != EUnrealAiChatBlockKind::Assistant)
	{
		if (bThinkingOpen)
		{
			for (int32 i = Blocks.Num() - 1; i >= 0; --i)
			{
				if (Blocks[i].Kind == EUnrealAiChatBlockKind::Thinking)
				{
					UnrealAiTranscriptTiming::FinalizeNamedStep(Blocks[i], TEXT("thinking"));
					break;
				}
			}
			bThinkingOpen = false;
		}
		if (Blocks.Num() > 0 && Blocks.Last().Kind == EUnrealAiChatBlockKind::Assistant)
		{
			UnrealAiTranscriptTiming::FinalizeNamedStep(Blocks.Last(), TEXT("reply"));
		}
		FUnrealAiChatBlock B;
		B.Id = FGuid::NewGuid();
		B.RunId = ActiveRunId;
		B.Kind = EUnrealAiChatBlockKind::Assistant;
		B.AssistantText = Chunk;
		B.StepMonotonicStart = FPlatformTime::Seconds();
		Blocks.Add(MoveTemp(B));
		bAssistantSegmentOpen = true;
		TouchAssistantStreamChunkTime();
		OnStructuralChange.Broadcast();
		// Rebuild paints this chunk via SetFullText; avoid double-appending via OnAssistantStreamDelta.
		return;
	}
	Blocks.Last().AssistantText += Chunk;
	TouchAssistantStreamChunkTime();
	OnAssistantStreamDelta.Broadcast(Chunk);
}

void FUnrealAiChatTranscript::BeginToolCall(const FString& ToolName, const FString& CallId, const FString& ArgsPreview)
{
	if (Blocks.Num() > 0)
	{
		FUnrealAiChatBlock& Last = Blocks.Last();
		if (Last.Kind == EUnrealAiChatBlockKind::Assistant)
		{
			UnrealAiTranscriptTiming::FinalizeNamedStep(Last, TEXT("reply"));
		}
		else if (Last.Kind == EUnrealAiChatBlockKind::Thinking)
		{
			UnrealAiTranscriptTiming::FinalizeNamedStep(Last, TEXT("thinking"));
		}
	}
	CloseAssistantSegment();
	bThinkingOpen = false;
	FUnrealAiChatBlock B;
	B.Id = FGuid::NewGuid();
	B.RunId = ActiveRunId;
	B.Kind = EUnrealAiChatBlockKind::ToolCall;
	B.ToolName = ToolName;
	B.ToolCallId = CallId;
	B.ToolArgsPreview = ArgsPreview;
	B.ToolEditorPresentation = nullptr;
	B.bToolRunning = true;
	B.bToolOk = false;
	B.StepMonotonicStart = FPlatformTime::Seconds();
	Blocks.Add(MoveTemp(B));
	OnStructuralChange.Broadcast();
}

void FUnrealAiChatTranscript::EndToolCall(
	const FString& CallId,
	bool bSuccess,
	const FString& ResultPreview,
	const TSharedPtr<FUnrealAiToolEditorPresentation>& EditorPresentation)
{
	const int32 Idx = FindToolIndexByCallId(CallId);
	if (Idx != INDEX_NONE)
	{
		Blocks[Idx].bToolRunning = false;
		Blocks[Idx].bToolOk = bSuccess;
		Blocks[Idx].ToolResultPreview = ResultPreview;
		Blocks[Idx].ToolEditorPresentation = EditorPresentation;
		UnrealAiTranscriptTiming::FinalizeToolStep(Blocks[Idx]);
		OnStructuralChange.Broadcast();
	}
}

void FUnrealAiChatTranscript::AddTodoPlan(const FString& Title, const FString& PlanJson)
{
	FUnrealAiChatBlock B;
	B.Id = FGuid::NewGuid();
	B.RunId = ActiveRunId;
	B.Kind = EUnrealAiChatBlockKind::TodoPlan;
	B.TodoTitle = Title;
	B.TodoJson = PlanJson;
	Blocks.Add(MoveTemp(B));
	OnStructuralChange.Broadcast();
}

FGuid FUnrealAiChatTranscript::AddPlanDraftPending(const FString& DagJson)
{
	FUnrealAiChatBlock B;
	B.Id = FGuid::NewGuid();
	B.RunId = ActiveRunId;
	B.Kind = EUnrealAiChatBlockKind::PlanDraftPending;
	B.TodoTitle = TEXT("Plan draft");
	B.TodoJson = DagJson;
	Blocks.Add(MoveTemp(B));
	OnStructuralChange.Broadcast();
	return B.Id;
}

void FUnrealAiChatTranscript::RemovePlanDraftPendingBlocks()
{
	const int32 Before = Blocks.Num();
	for (int32 i = Blocks.Num() - 1; i >= 0; --i)
	{
		if (Blocks[i].Kind == EUnrealAiChatBlockKind::PlanDraftPending)
		{
			Blocks.RemoveAt(i);
		}
	}
	if (Blocks.Num() != Before)
	{
		OnStructuralChange.Broadcast();
	}
}

void FUnrealAiChatTranscript::SetPlanDraftJsonForBlock(const FGuid& BlockId, const FString& DagJson)
{
	for (FUnrealAiChatBlock& B : Blocks)
	{
		if (B.Id == BlockId && B.Kind == EUnrealAiChatBlockKind::PlanDraftPending)
		{
			B.TodoJson = DagJson;
			return;
		}
	}
}

void FUnrealAiChatTranscript::SetRunProgress(const FString& Label)
{
	for (int32 i = Blocks.Num() - 1; i >= 0; --i)
	{
		FUnrealAiChatBlock& Existing = Blocks[i];
		if (Existing.Kind == EUnrealAiChatBlockKind::RunProgress && Existing.RunId == ActiveRunId)
		{
			Existing.ProgressLabel = Label;
			OnStructuralChange.Broadcast();
			return;
		}
	}
	FUnrealAiChatBlock NewBlock;
	NewBlock.Id = FGuid::NewGuid();
	NewBlock.RunId = ActiveRunId;
	NewBlock.Kind = EUnrealAiChatBlockKind::RunProgress;
	NewBlock.ProgressLabel = Label;
	Blocks.Add(MoveTemp(NewBlock));
	OnStructuralChange.Broadcast();
}

void FUnrealAiChatTranscript::AppendRunEvent(const FString& EventLine)
{
	if (EventLine.IsEmpty())
	{
		return;
	}
	for (int32 i = Blocks.Num() - 1; i >= 0; --i)
	{
		FUnrealAiChatBlock& Existing = Blocks[i];
		if (Existing.Kind != EUnrealAiChatBlockKind::RunProgress || Existing.RunId != ActiveRunId)
		{
			continue;
		}
		if (!Existing.ProgressDetails.IsEmpty())
		{
			Existing.ProgressDetails += TEXT("\n");
		}
		Existing.ProgressDetails += EventLine;
		TArray<FString> Lines;
		Existing.ProgressDetails.ParseIntoArrayLines(Lines, true);
		static constexpr int32 MaxProgressLines = 40;
		if (Lines.Num() > MaxProgressLines)
		{
			const int32 Start = Lines.Num() - MaxProgressLines;
			TArray<FString> Trimmed;
			Trimmed.Reserve(MaxProgressLines);
			for (int32 Idx = Start; Idx < Lines.Num(); ++Idx)
			{
				Trimmed.Add(Lines[Idx]);
			}
			Lines = MoveTemp(Trimmed);
			Existing.ProgressDetails = FString::Join(Lines, TEXT("\n"));
		}
		OnStructuralChange.Broadcast();
		return;
	}
	SetRunProgress(TEXT("Run in progress"));
	AppendRunEvent(EventLine);
}

void FUnrealAiChatTranscript::ClearRunProgress()
{
	for (int32 i = Blocks.Num() - 1; i >= 0; --i)
	{
		if (Blocks[i].Kind == EUnrealAiChatBlockKind::RunProgress && Blocks[i].RunId == ActiveRunId)
		{
			Blocks.RemoveAt(i);
		}
	}
	OnStructuralChange.Broadcast();
}

void FUnrealAiChatTranscript::EndRun(bool bSuccess, const FString& ErrorMessage)
{
	if (Blocks.Num() > 0)
	{
		FUnrealAiChatBlock& Last = Blocks.Last();
		if (Last.Kind == EUnrealAiChatBlockKind::Assistant && Last.StepMonotonicStart > 0.0)
		{
			UnrealAiTranscriptTiming::FinalizeNamedStep(Last, TEXT("reply"));
		}
		else if (Last.Kind == EUnrealAiChatBlockKind::Thinking && Last.StepMonotonicStart > 0.0)
		{
			UnrealAiTranscriptTiming::FinalizeNamedStep(Last, TEXT("thinking"));
		}
		else if (Last.Kind == EUnrealAiChatBlockKind::ToolCall && Last.StepMonotonicStart > 0.0)
		{
			UnrealAiTranscriptTiming::FinalizeToolStep(Last);
		}
	}
	bHasActiveRun = false;
	bAssistantSegmentOpen = false;
	bThinkingOpen = false;
	if (!bSuccess)
	{
		FUnrealAiChatBlock B;
		B.Id = FGuid::NewGuid();
		B.RunId = ActiveRunId;
		B.Kind = EUnrealAiChatBlockKind::Notice;
		B.bNoticeError = true;
		if (ErrorMessage.Contains(TEXT("Cancelled"), ESearchCase::IgnoreCase))
		{
			B.NoticeText = TEXT("Run stopped.");
			B.bRunCancelled = true;
		}
		else
		{
			B.NoticeText = ErrorMessage.IsEmpty() ? FString(TEXT("Run failed.")) : ErrorMessage;
		}
		Blocks.Add(MoveTemp(B));
		OnStructuralChange.Broadcast();
	}
}

void FUnrealAiChatTranscript::OnContinuation(int32 PhaseIndex, int32 TotalPhasesHint)
{
	(void)PhaseIndex;
	(void)TotalPhasesHint;
	// Internal harness round counter only — do not surface to chat UI.
}

void FUnrealAiChatTranscript::AddInformationalNotice(const FString& Text)
{
	if (Text.IsEmpty())
	{
		return;
	}
	FUnrealAiChatBlock B;
	B.Id = FGuid::NewGuid();
	B.RunId = ActiveRunId;
	B.Kind = EUnrealAiChatBlockKind::Notice;
	B.NoticeText = Text;
	B.bNoticeError = false;
	B.bRunCancelled = false;
	Blocks.Add(MoveTemp(B));
	OnStructuralChange.Broadcast();
}

void FUnrealAiChatTranscript::AddEditorBlockingDialogNotice(const FString& Summary)
{
	if (Summary.IsEmpty())
	{
		return;
	}
	FUnrealAiChatBlock B;
	B.Id = FGuid::NewGuid();
	B.RunId = ActiveRunId;
	B.Kind = EUnrealAiChatBlockKind::Notice;
	B.NoticeText = FString::Printf(
		TEXT("The editor is showing a blocking dialog and is waiting for you:\n%s\n\n")
		TEXT("Respond in the dialog to continue. The agent will receive what you chose as part of the tool result."),
		*Summary);
	B.bNoticeError = true;
	B.bRunCancelled = false;
	Blocks.Add(MoveTemp(B));
	OnStructuralChange.Broadcast();
}

void FUnrealAiChatTranscript::HydrateFromConversationMessages(const TArray<FUnrealAiConversationMessage>& Messages)
{
	Blocks.Reset();
	ActiveRunId = FGuid();
	bHasActiveRun = false;
	bAssistantSegmentOpen = false;
	bThinkingOpen = false;
	LastAssistantStreamChunkMonotonicTime = 0.0;

	for (const FUnrealAiConversationMessage& M : Messages)
	{
		if (M.Role == TEXT("system"))
		{
			continue;
		}
		if (M.Role == TEXT("user"))
		{
			AddUserMessage(M.Content);
			continue;
		}
		if (M.Role == TEXT("assistant"))
		{
			if (!M.Content.IsEmpty())
			{
				FUnrealAiChatBlock B;
				B.Id = FGuid::NewGuid();
				B.Kind = EUnrealAiChatBlockKind::Assistant;
				B.AssistantText = M.Content;
				Blocks.Add(MoveTemp(B));
			}
			for (const FUnrealAiToolCallSpec& Tc : M.ToolCalls)
			{
				if (Tc.Name.TrimStartAndEnd().IsEmpty())
				{
					continue;
				}
				FUnrealAiChatBlock B;
				B.Id = FGuid::NewGuid();
				B.Kind = EUnrealAiChatBlockKind::ToolCall;
				B.ToolName = Tc.Name;
				B.ToolCallId = Tc.Id;
				B.ToolArgsPreview = Tc.ArgumentsJson;
				B.bToolRunning = true;
				B.bToolOk = false;
				Blocks.Add(MoveTemp(B));
			}
			continue;
		}
		if (M.Role == TEXT("tool"))
		{
			for (int32 i = Blocks.Num() - 1; i >= 0; --i)
			{
				if (Blocks[i].Kind == EUnrealAiChatBlockKind::ToolCall && Blocks[i].ToolCallId == M.ToolCallId)
				{
					Blocks[i].bToolRunning = false;
					Blocks[i].bToolOk = true;
					Blocks[i].ToolResultPreview = M.Content;
					break;
				}
			}
			continue;
		}
	}
	OnStructuralChange.Broadcast();
}
