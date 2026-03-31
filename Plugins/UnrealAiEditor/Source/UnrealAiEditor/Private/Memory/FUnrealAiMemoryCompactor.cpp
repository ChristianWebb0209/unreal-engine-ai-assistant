#include "Memory/FUnrealAiMemoryCompactor.h"

#include "Harness/UnrealAiConversationJson.h"
#include "Memory/IUnrealAiMemoryService.h"
#include "Misc/Guid.h"

FUnrealAiMemoryCompactor::FUnrealAiMemoryCompactor(IUnrealAiMemoryService* InMemoryService)
	: MemoryService(InMemoryService)
{
}

namespace
{
	static FString BuildMeaningfulTitle(const FUnrealAiMemoryCompactionInput& Input)
	{
		TArray<FUnrealAiConversationMessage> Messages;
		TArray<FString> Warnings;
		if (UnrealAiConversationJson::JsonToMessages(Input.ConversationJson, Messages, Warnings))
		{
			for (int32 i = Messages.Num() - 1; i >= 0; --i)
			{
				const FUnrealAiConversationMessage& Message = Messages[i];
				if (Message.Role != TEXT("user"))
				{
					continue;
				}
				FString Candidate = Message.Content;
				Candidate.ReplaceInline(TEXT("\r"), TEXT(" "));
				Candidate.ReplaceInline(TEXT("\n"), TEXT(" "));
				Candidate.TrimStartAndEndInline();
				if (Candidate.IsEmpty())
				{
					continue;
				}
				const int32 Period = Candidate.Find(TEXT("."));
				if (Period > 8)
				{
					Candidate = Candidate.Left(Period);
				}
				if (Candidate.Len() > 64)
				{
					Candidate = Candidate.Left(64).TrimEnd();
					Candidate += TEXT("...");
				}
				return FString::Printf(TEXT("Lesson: %s"), *Candidate);
			}
		}
		return TEXT("Thread memory");
	}

	static void AddHashtagTags(const FString& Text, TArray<FString>& InOutTags)
	{
		if (Text.IsEmpty())
		{
			return;
		}
		TSet<FString> Existing;
		for (const FString& T : InOutTags)
		{
			Existing.Add(T.ToLower());
		}
		FString Copy = Text;
		Copy.ReplaceInline(TEXT("\r"), TEXT(" "));
		Copy.ReplaceInline(TEXT("\n"), TEXT(" "));
		TArray<FString> Parts;
		Copy.ParseIntoArray(Parts, TEXT(" "), true);
		for (FString P : Parts)
		{
			P.TrimStartAndEndInline();
			if (!P.StartsWith(TEXT("#")))
			{
				continue;
			}
			P = P.RightChop(1).ToLower();
			while (!P.IsEmpty() && !FChar::IsAlnum(P[P.Len() - 1]) && P[P.Len() - 1] != TEXT('_') && P[P.Len() - 1] != TEXT('-'))
			{
				P.LeftChopInline(1);
			}
			if (P.Len() < 2)
			{
				continue;
			}
			if (!Existing.Contains(P))
			{
				Existing.Add(P);
				InOutTags.Add(P);
			}
			if (InOutTags.Num() >= 12)
			{
				break;
			}
		}
	}
}

FUnrealAiMemoryCompactionResult FUnrealAiMemoryCompactor::Run(
	const FUnrealAiMemoryCompactionInput& Input,
	const int32 MaxToCreate,
	const float MinConfidence)
{
	FUnrealAiMemoryCompactionResult Out;
	if (!MemoryService || MaxToCreate <= 0 || Input.ConversationJson.IsEmpty())
	{
		return Out;
	}
	FUnrealAiMemoryGenerationStatus Status;
	Status.State = EUnrealAiMemoryGenerationState::Running;
	Status.ErrorCode = EUnrealAiMemoryGenerationErrorCode::None;
	Status.ErrorMessage.Reset();
	Status.LastAttemptAtUtc = FDateTime::UtcNow();
	Status.LastSuccessAtUtc = MemoryService->GetGenerationStatus().LastSuccessAtUtc;
	MemoryService->SetGenerationStatus(Status);

	if (Input.bExpectProviderGeneration && !Input.bApiKeyConfigured)
	{
		Status.State = EUnrealAiMemoryGenerationState::Failed;
		Status.ErrorCode = EUnrealAiMemoryGenerationErrorCode::MissingApiKey;
		Status.ErrorMessage = TEXT("Memories failed to generate: API key is missing.");
		MemoryService->SetGenerationStatus(Status);
		return Out;
	}

	// Conservative extractor v1: create at most one compacted memory per invocation,
	// and allow zero output when confidence gating fails.
	const FString CandidateTitle = BuildMeaningfulTitle(Input);
	const FString CandidateDescription = TEXT("Auto-compacted summary candidate from recent thread activity.");
	const FString CandidateBody = Input.ConversationJson;
	const float Confidence = Input.ConversationJson.Len() >= 200 ? 0.55f : 0.25f;

	Out.Candidates = 1;
	if (Confidence < MinConfidence)
	{
		Status.State = EUnrealAiMemoryGenerationState::Success;
		Status.ErrorCode = EUnrealAiMemoryGenerationErrorCode::None;
		Status.ErrorMessage = TEXT("No useful memory candidates generated for this turn.");
		MemoryService->SetGenerationStatus(Status);
		return Out;
	}

	FUnrealAiMemoryRecord Record;
	Record.Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Record.Title = CandidateTitle;
	Record.Description = CandidateDescription;
	Record.Body = CandidateBody;
	Record.Scope = EUnrealAiMemoryScope::Thread;
	Record.Status = EUnrealAiMemoryStatus::Active;
	Record.Confidence = Confidence;
	Record.Tags = { TEXT("auto"), TEXT("lesson"), TEXT("thread") };
	AddHashtagTags(Input.ConversationJson, Record.Tags);
	Record.CreatedAtUtc = FDateTime::UtcNow();
	Record.UpdatedAtUtc = Record.CreatedAtUtc;
	Record.LastUsedAtUtc = Record.CreatedAtUtc;
	if (!Input.ThreadId.IsEmpty())
	{
		FUnrealAiMemorySourceRef Ref;
		Ref.Kind = TEXT("thread");
		Ref.Value = Input.ThreadId;
		Record.SourceRefs.Add(Ref);
		Record.Tags.AddUnique(FString::Printf(TEXT("thread_%s"), *Input.ThreadId.Left(8).ToLower()));
	}

	const FString AcceptedId = Record.Id;
	if (MemoryService->UpsertMemory(MoveTemp(Record)))
	{
		Out.Accepted = 1;
		Out.AcceptedMemoryIds.Add(AcceptedId);
		Status.State = EUnrealAiMemoryGenerationState::Success;
		Status.ErrorCode = EUnrealAiMemoryGenerationErrorCode::None;
		Status.ErrorMessage.Reset();
		Status.LastSuccessAtUtc = FDateTime::UtcNow();
		MemoryService->SetGenerationStatus(Status);
	}
	else
	{
		Status.State = EUnrealAiMemoryGenerationState::Failed;
		Status.ErrorCode = EUnrealAiMemoryGenerationErrorCode::Unknown;
		Status.ErrorMessage = TEXT("Memories failed to generate: write failed.");
		MemoryService->SetGenerationStatus(Status);
	}
	return Out;
}
