#include "Widgets/FUnrealAiChatRunSink.h"

#include "Widgets/UnrealAiChatTranscript.h"
#include "Widgets/UnrealAiToolUi.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "Backend/IUnrealAiPersistence.h"

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
	const FString& InThreadId)
	: Transcript(MoveTemp(InTranscript))
	, Session(MoveTemp(InSession))
	, Persistence(InPersistence)
	, ProjectId(InProjectId)
	, ThreadId(InThreadId)
{
}

void FUnrealAiChatRunSink::OnRunStarted(const FUnrealAiRunIds& Ids)
{
	if (Transcript.IsValid())
	{
		Transcript->BeginRun(Ids.RunId);
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

		if (!Session.IsValid() || Persistence == nullptr || ThreadId.IsEmpty())
		{
			return;
		}

		// Always strip the token from visible assistant output if present,
		// but only apply/rename once per chat.
		bool bModifiedAnyAssistantText = false;
		FString ExtractedName;

		// Walk backwards so we usually pick up the first (most recent) assistant token.
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
				break; // only expect one token
			}
		}

		if (bModifiedAnyAssistantText)
		{
			Transcript->OnStructuralChange.Broadcast();
		}

		if (Session->ChatName.IsEmpty() && bModifiedAnyAssistantText)
		{
			FString FinalName = ExtractedName;
			if (FinalName.IsEmpty())
			{
				// Fallback to something derived from the user's first/most-recent message.
				for (int32 i = Transcript->Blocks.Num() - 1; i >= 0; --i)
				{
					const FUnrealAiChatBlock& B = Transcript->Blocks[i];
					if (B.Kind == EUnrealAiChatBlockKind::User && !B.UserText.IsEmpty())
					{
						FinalName = B.UserText;
						FinalName.ReplaceInline(TEXT("\r"), TEXT(""));
						FinalName.ReplaceInline(TEXT("\n"), TEXT(" "));
						FinalName.TrimStartAndEndInline();
						// Limit to a short human-readable label.
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

			if (!FinalName.IsEmpty() && Persistence->SetThreadChatName(ProjectId, ThreadId, FinalName))
			{
				Session->ChatName = FinalName;
			}
		}
	}
}
