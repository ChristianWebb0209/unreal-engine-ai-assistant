#include "Widgets/UnrealAiChatUiHelpers.h"

#include "Widgets/UnrealAiChatUiSession.h"
#include "Widgets/UnrealAiChatTranscript.h"
#include "Widgets/SChatMessageList.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/IAgentContextService.h"
#include "Context/UnrealAiProjectId.h"
#include "Harness/IUnrealAiAgentHarness.h"
#include "Harness/UnrealAiConversationJson.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Widgets/UnrealAiPlanDraftPersist.h"

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
	Session->OnChatNameChanged.Broadcast();
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
		Persist->ForgetThread(ProjectId, OldThreadIdStr);
	}

	Session->ThreadId = FGuid::NewGuid();
	Session->ChatName.Reset();
	Session->OnChatNameChanged.Broadcast();
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

void UnrealAiChatUi_LoadPersistedThreadIntoUi(
	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry,
	TSharedPtr<FUnrealAiChatUiSession> Session,
	TSharedPtr<SChatMessageList> MessageList)
{
	if (!BackendRegistry.IsValid() || !Session.IsValid() || !MessageList.IsValid())
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

	IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence();
	if (!Persist)
	{
		return;
	}

	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	const FString ThreadIdStr = Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);

	if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
	{
		Ctx->LoadOrCreate(ProjectId, ThreadIdStr);
	}

	Session->ChatName.Reset();
	FString Named;
	if (Persist->GetThreadChatName(ProjectId, ThreadIdStr, Named) && !Named.IsEmpty())
	{
		Session->ChatName = MoveTemp(Named);
	}
	Session->OnChatNameChanged.Broadcast();

	FString ConvJson;
	TArray<FUnrealAiConversationMessage> Messages;
	if (Persist->LoadThreadConversationJson(ProjectId, ThreadIdStr, ConvJson))
	{
		TArray<FString> Warnings;
		if (!UnrealAiConversationJson::JsonToMessages(ConvJson, Messages, Warnings))
		{
			Messages.Reset();
		}
	}

	MessageList->HydrateTranscriptFromPersistedConversation(Messages);

	FString DraftFile;
	if (Persist->LoadThreadPlanDraftJson(ProjectId, ThreadIdStr, DraftFile))
	{
		FString Dag;
		if (UnrealAiPlanDraftPersist::TryUnwrapDraftFile(DraftFile, Dag))
		{
			if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
			{
				Ctx->SetActivePlanDag(Dag);
			}
			MessageList->GetTranscript()->AddPlanDraftPending(Dag);
		}
	}
}
