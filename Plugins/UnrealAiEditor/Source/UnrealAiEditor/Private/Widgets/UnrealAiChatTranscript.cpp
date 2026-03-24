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

FString FUnrealAiChatTranscript::FormatPlainText() const
{
	FString Out;
	for (const FUnrealAiChatBlock& B : Blocks)
	{
		switch (B.Kind)
		{
		case EUnrealAiChatBlockKind::User:
			Out += FString::Printf(TEXT("--- User ---\n%s\n\n"), *B.UserText);
			break;
		case EUnrealAiChatBlockKind::Thinking:
			Out += FString::Printf(TEXT("--- Thinking ---\n%s\n\n"), *B.ThinkingText);
			break;
		case EUnrealAiChatBlockKind::Assistant:
			Out += FString::Printf(TEXT("--- Assistant ---\n%s\n\n"), *B.AssistantText);
			break;
		case EUnrealAiChatBlockKind::ToolCall:
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
		case EUnrealAiChatBlockKind::RunProgress:
			Out += FString::Printf(TEXT("--- Progress ---\n%s\n\n"), *B.ProgressLabel);
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

FGuid FUnrealAiChatTranscript::AddUserMessage(const FString& Text, FGuid DesiredId)
{
	FUnrealAiChatBlock B;
	const FGuid NewId = DesiredId.IsValid() ? DesiredId : FGuid::NewGuid();
	B.Id = NewId;
	B.Kind = EUnrealAiChatBlockKind::User;
	B.UserText = Text;
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
}

void FUnrealAiChatTranscript::AppendThinkingDelta(const FString& Chunk)
{
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
	B.ToolEditorPresentation = nullptr;
	B.bToolRunning = true;
	B.bToolOk = false;
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

void UnrealAiCollectToolDetailsAfterAssistant(
	const TArray<FUnrealAiChatBlock>& Blocks,
	int32 AssistantIndex,
	TArray<FUnrealAiAssistantSegmentToolInfo>& OutDetails)
{
	OutDetails.Reset();
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
			FUnrealAiAssistantSegmentToolInfo Row;
			Row.ToolName = Blocks[i].ToolName;
			Row.ToolCallId = Blocks[i].ToolCallId;
			Row.ArgsPreview = Blocks[i].ToolArgsPreview;
			Row.ResultPreview = Blocks[i].ToolResultPreview;
			Row.bRunning = Blocks[i].bToolRunning;
			Row.bOk = Blocks[i].bToolOk;
			OutDetails.Add(MoveTemp(Row));
		}
		else if (K == EUnrealAiChatBlockKind::Assistant || K == EUnrealAiChatBlockKind::User)
		{
			break;
		}
	}
}
