#include "Widgets/UnrealAiChatUiHelpers.h"

#include "Widgets/UnrealAiChatUiSession.h"
#include "Widgets/SChatMessageList.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/IAgentContextService.h"
#include "Context/UnrealAiProjectId.h"
#include "Harness/IUnrealAiAgentHarness.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

void UnrealAiChatUi_StartNewChat(
	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry,
	TSharedPtr<FUnrealAiChatUiSession> Session,
	TSharedPtr<SChatMessageList> MessageList)
{
	if (!BackendRegistry.IsValid() || !Session.IsValid())
	{
		return;
	}

	if (IUnrealAiAgentHarness* H = BackendRegistry->GetAgentHarness())
	{
		if (H->IsTurnInProgress())
		{
			H->CancelTurn();
		}
	}

	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	const FString OldThreadIdStr = Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
	if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
	{
		Ctx->SaveNow(ProjectId, OldThreadIdStr);
	}
	Session->ThreadId = FGuid::NewGuid();
	Session->ChatName.Reset();
	const FString NewThreadIdStr = Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
	if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
	{
		Ctx->LoadOrCreate(ProjectId, NewThreadIdStr);
	}
	if (MessageList.IsValid())
	{
		MessageList->ClearTranscript();
	}
}

void UnrealAiChatUi_DeleteChatPermanently(
	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry,
	TSharedPtr<FUnrealAiChatUiSession> Session,
	TSharedPtr<SChatMessageList> MessageList)
{
	if (!BackendRegistry.IsValid() || !Session.IsValid())
	{
		return;
	}

	if (IUnrealAiAgentHarness* H = BackendRegistry->GetAgentHarness())
	{
		if (H->IsTurnInProgress())
		{
			H->CancelTurn();
		}
	}

	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	const FString OldThreadIdStr = Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);

	if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
	{
		Ctx->ClearSession(ProjectId, OldThreadIdStr);
	}

	if (IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence())
	{
		const FString ThreadDir = FPaths::Combine(
			Persist->GetDataRootDirectory(),
			TEXT("chats"),
			ProjectId,
			TEXT("threads"),
			OldThreadIdStr);
		IFileManager::Get().DeleteDirectory(*ThreadDir, false, true);
	}

	Session->ThreadId = FGuid::NewGuid();
	Session->ChatName.Reset();
	const FString NewThreadIdStr = Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
	if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
	{
		Ctx->LoadOrCreate(ProjectId, NewThreadIdStr);
	}
	if (MessageList.IsValid())
	{
		MessageList->ClearTranscript();
	}
}
