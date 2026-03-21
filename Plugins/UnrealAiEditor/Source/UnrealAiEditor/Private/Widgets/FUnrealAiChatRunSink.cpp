#include "Widgets/FUnrealAiChatRunSink.h"

#include "Widgets/UnrealAiChatTranscript.h"
#include "Widgets/UnrealAiToolUi.h"

FUnrealAiChatRunSink::FUnrealAiChatRunSink(TSharedPtr<FUnrealAiChatTranscript> InTranscript)
	: Transcript(MoveTemp(InTranscript))
{
}

void FUnrealAiChatRunSink::OnRunStarted(const FUnrealAiRunIds& Ids)
{
	if (Transcript.IsValid())
	{
		Transcript->BeginRun(Ids.RunId);
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
	const FString& ResultPreview)
{
	(void)ToolName;
	if (Transcript.IsValid())
	{
		Transcript->EndToolCall(CallId, bSuccess, ResultPreview);
	}
}

void FUnrealAiChatRunSink::OnRunContinuation(int32 PhaseIndex, int32 TotalPhasesHint)
{
	if (Transcript.IsValid())
	{
		Transcript->OnContinuation(PhaseIndex, TotalPhasesHint);
	}
}

void FUnrealAiChatRunSink::OnTodoPlanEmitted(const FString& Title, const FString& PlanJson)
{
	if (Transcript.IsValid())
	{
		Transcript->AddTodoPlan(Title, PlanJson);
	}
}

void FUnrealAiChatRunSink::OnRunFinished(bool bSuccess, const FString& ErrorMessage)
{
	if (Transcript.IsValid())
	{
		Transcript->EndRun(bSuccess, ErrorMessage);
	}
}
