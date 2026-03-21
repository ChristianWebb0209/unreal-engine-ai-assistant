#include "Harness/FUnrealAiConversationStore.h"

#include "Backend/IUnrealAiPersistence.h"
#include "Harness/UnrealAiConversationJson.h"

FUnrealAiConversationStore::FUnrealAiConversationStore(IUnrealAiPersistence* InPersistence)
	: Persistence(InPersistence)
{
}

void FUnrealAiConversationStore::LoadOrCreate(const FString& ProjectId, const FString& ThreadId)
{
	ActiveProjectId = ProjectId;
	ActiveThreadId = ThreadId;
	Messages.Reset();
	if (!Persistence)
	{
		return;
	}
	FString Json;
	if (Persistence->LoadThreadConversationJson(ProjectId, ThreadId, Json))
	{
		TArray<FString> Warnings;
		UnrealAiConversationJson::JsonToMessages(Json, Messages, Warnings);
	}
}

void FUnrealAiConversationStore::SaveNow()
{
	if (!Persistence || ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FString Json;
	if (UnrealAiConversationJson::MessagesToJson(Messages, Json))
	{
		Persistence->SaveThreadConversationJson(ActiveProjectId, ActiveThreadId, Json);
	}
}
