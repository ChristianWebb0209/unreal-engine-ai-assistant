#include "Widgets/UnrealAiChatTranscript.h"

void FUnrealAiChatTranscript::Clear()
{
	Blocks.Reset();
	ActiveRunId = FGuid();
	bHasActiveRun = false;
	bAssistantSegmentOpen = false;
	bThinkingOpen = false;
	OnStructuralChange.Broadcast();
}

void FUnrealAiChatTranscript::AddUserMessage(const FString& Text)
{
	FUnrealAiChatBlock B;
	B.Id = FGuid::NewGuid();
	B.Kind = EUnrealAiChatBlockKind::User;
	B.UserText = Text;
	Blocks.Add(MoveTemp(B));
	OnStructuralChange.Broadcast();
}

void FUnrealAiChatTranscript::BeginRun(const FGuid& RunId)
{
	ActiveRunId = RunId;
	bHasActiveRun = true;
	bAssistantSegmentOpen = false;
	bThinkingOpen = false;
}

void FUnrealAiChatTranscript::AppendThinkingDelta(const FString& Chunk)
{
	if (Chunk.IsEmpty())
	{
		return;
	}
	const bool bFirst = !bThinkingOpen;
	if (!bThinkingOpen)
	{
		FUnrealAiChatBlock B;
		B.Id = FGuid::NewGuid();
		B.RunId = ActiveRunId;
		B.Kind = EUnrealAiChatBlockKind::Thinking;
		B.ThinkingText = Chunk;
		Blocks.Add(MoveTemp(B));
		bThinkingOpen = true;
		OnStructuralChange.Broadcast();
	}
	else
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
	if (!bAssistantSegmentOpen || Blocks.Num() == 0 || Blocks.Last().Kind != EUnrealAiChatBlockKind::Assistant)
	{
		FUnrealAiChatBlock B;
		B.Id = FGuid::NewGuid();
		B.RunId = ActiveRunId;
		B.Kind = EUnrealAiChatBlockKind::Assistant;
		B.AssistantText = Chunk;
		Blocks.Add(MoveTemp(B));
		bAssistantSegmentOpen = true;
		OnStructuralChange.Broadcast();
		// Rebuild paints this chunk via SetFullText; avoid double-appending via OnAssistantStreamDelta.
		return;
	}
	Blocks.Last().AssistantText += Chunk;
	OnAssistantStreamDelta.Broadcast(Chunk);
}

void FUnrealAiChatTranscript::BeginToolCall(const FString& ToolName, const FString& CallId, const FString& ArgsPreview)
{
	CloseAssistantSegment();
	bThinkingOpen = false;
	FUnrealAiChatBlock B;
	B.Id = FGuid::NewGuid();
	B.RunId = ActiveRunId;
	B.Kind = EUnrealAiChatBlockKind::ToolCall;
	B.ToolName = ToolName;
	B.ToolCallId = CallId;
	B.ToolArgsPreview = ArgsPreview;
	B.bToolRunning = true;
	B.bToolOk = false;
	Blocks.Add(MoveTemp(B));
	OnStructuralChange.Broadcast();
}

void FUnrealAiChatTranscript::EndToolCall(const FString& CallId, bool bSuccess, const FString& ResultPreview)
{
	const int32 Idx = FindToolIndexByCallId(CallId);
	if (Idx != INDEX_NONE)
	{
		Blocks[Idx].bToolRunning = false;
		Blocks[Idx].bToolOk = bSuccess;
		Blocks[Idx].ToolResultPreview = ResultPreview;
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

void FUnrealAiChatTranscript::SetRunProgress(const FString& Label)
{
	FUnrealAiChatBlock B;
	B.Id = FGuid::NewGuid();
	B.RunId = ActiveRunId;
	B.Kind = EUnrealAiChatBlockKind::RunProgress;
	B.ProgressLabel = Label;
	Blocks.Add(MoveTemp(B));
	OnStructuralChange.Broadcast();
}

void FUnrealAiChatTranscript::EndRun(bool bSuccess, const FString& ErrorMessage)
{
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
	const FString Label = TotalPhasesHint > 0
		? FString::Printf(TEXT("Continuing — round %d / %d"), PhaseIndex + 1, TotalPhasesHint)
		: FString::Printf(TEXT("Continuing — round %d"), PhaseIndex + 1);
	SetRunProgress(Label);
}

void UnrealAiCollectToolsAfterAssistant(
	const TArray<FUnrealAiChatBlock>& Blocks,
	int32 AssistantIndex,
	TArray<FString>& OutOrderedToolNames)
{
	OutOrderedToolNames.Reset();
	if (!Blocks.IsValidIndex(AssistantIndex)
		|| Blocks[AssistantIndex].Kind != EUnrealAiChatBlockKind::Assistant)
	{
		return;
	}
	for (int32 i = AssistantIndex + 1; i < Blocks.Num(); ++i)
	{
		const EUnrealAiChatBlockKind K = Blocks[i].Kind;
		if (K == EUnrealAiChatBlockKind::ToolCall)
		{
			OutOrderedToolNames.Add(Blocks[i].ToolName);
		}
		else if (K == EUnrealAiChatBlockKind::Assistant || K == EUnrealAiChatBlockKind::User)
		{
			break;
		}
	}
}
