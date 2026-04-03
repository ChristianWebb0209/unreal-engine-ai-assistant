#include "Widgets/UnrealAiChatUiHelpers.h"

#include "Misc/EngineVersion.h"
#include "Dom/JsonObject.h"
#include "Internationalization/Text.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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
		UnrealAiChatUi_MaybeInjectLlmSetupNotice(BackendRegistry, MessageList);
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
		UnrealAiChatUi_MaybeInjectLlmSetupNotice(BackendRegistry, MessageList);
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

bool UnrealAiChatUi_IsPersistedApiKeyConfigured(TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry)
{
	if (!BackendRegistry.IsValid())
	{
		return false;
	}
	IUnrealAiPersistence* const P = BackendRegistry->GetPersistence();
	if (!P)
	{
		return false;
	}
	FString Json;
	if (!P->LoadSettingsJson(Json) || Json.IsEmpty())
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* ApiObj = nullptr;
	if (!Root->TryGetObjectField(TEXT("api"), ApiObj) || !ApiObj->IsValid())
	{
		return false;
	}
	FString Key;
	if (!(*ApiObj)->TryGetStringField(TEXT("apiKey"), Key))
	{
		return false;
	}
	return !Key.TrimStartAndEnd().IsEmpty();
}

void UnrealAiChatUi_MaybeInjectLlmSetupNotice(
	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry,
	TSharedPtr<SChatMessageList> MessageList)
{
	if (!BackendRegistry.IsValid() || !MessageList.IsValid())
	{
		return;
	}
	if (UnrealAiChatUi_IsPersistedApiKeyConfigured(BackendRegistry))
	{
		return;
	}
	const TSharedPtr<FUnrealAiChatTranscript> Tr = MessageList->GetTranscript();
	if (!Tr.IsValid() || Tr->Blocks.Num() > 0)
	{
		return;
	}
	const FText NoticeText = NSLOCTEXT(
		"UnrealAiEditor",
		"ChatLlmSetupNotice",
		"Welcome! Open AI Settings (Window → Unreal AI → AI Settings, or Tools → Unreal AI → AI Settings), enter your API key, "
		"then click Save and apply. Chat and vector indexing require a working provider connection.");
	Tr->AddInformationalNotice(NoticeText.ToString());
}

FText UnrealAiChatUi_GetComposerFooterVersionText()
{
	FString PluginVer = TEXT("dev");
	if (TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(TEXT("UnrealAiEditor")); P.IsValid())
	{
		PluginVer = P->GetDescriptor().VersionName;
	}
	const FString EngineLabel = FEngineVersion::Current().ToString();
	return FText::FromString(FString::Printf(TEXT("Unreal AI Editor %s · %s"), *PluginVer, *EngineLabel));
}
