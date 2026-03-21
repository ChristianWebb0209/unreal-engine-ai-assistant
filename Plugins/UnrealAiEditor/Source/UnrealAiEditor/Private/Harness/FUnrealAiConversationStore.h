#pragma once

#include "CoreMinimal.h"
#include "Harness/UnrealAiAgentTypes.h"

class IUnrealAiPersistence;

/** Per-thread message list persisted as `conversation.json`. */
class FUnrealAiConversationStore
{
public:
	explicit FUnrealAiConversationStore(IUnrealAiPersistence* InPersistence);

	void LoadOrCreate(const FString& ProjectId, const FString& ThreadId);
	void SaveNow();

	const TArray<FUnrealAiConversationMessage>& GetMessages() const { return Messages; }
	TArray<FUnrealAiConversationMessage>& GetMessagesMutable() { return Messages; }

	const FString& GetActiveProjectId() const { return ActiveProjectId; }
	const FString& GetActiveThreadId() const { return ActiveThreadId; }

private:
	IUnrealAiPersistence* Persistence = nullptr;
	FString ActiveProjectId;
	FString ActiveThreadId;
	TArray<FUnrealAiConversationMessage> Messages;
};
