#include "Backend/UnrealAiBackendRegistry.h"

#include "Backend/FUnrealAiChatServiceStub.h"
#include "Backend/FUnrealAiModelConnectorHttp.h"
#include "Backend/FUnrealAiModelConnectorStub.h"
#include "Backend/FUnrealAiPersistenceStub.h"
#include "Context/FUnrealAiContextService.h"
#include "Harness/FUnrealAiAgentHarness.h"
#include "Harness/FUnrealAiLlmTransportStub.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Harness/IUnrealAiAgentHarness.h"
#include "Harness/ILlmTransport.h"
#include "Tools/FUnrealAiToolExecutionHost.h"
#include "Tools/UnrealAiToolCatalog.h"
#include "Transport/FOpenAiCompatibleHttpTransport.h"

FUnrealAiBackendRegistry::FUnrealAiBackendRegistry()
{
	Persistence = MakeUnique<FUnrealAiPersistenceStub>();
	ContextService = MakeUnique<FUnrealAiContextService>(Persistence.Get());
	ModelProfiles = MakeUnique<FUnrealAiModelProfileRegistry>(Persistence.Get());
	ToolCatalog = MakeUnique<FUnrealAiToolCatalog>();
	ToolCatalog->LoadFromPlugin();

	ChatService = MakeUnique<FUnrealAiChatServiceStub>(this);
	RefreshModelConnector();
	ToolExecutionHost = MakeUnique<FUnrealAiToolExecutionHost>(this);

	HttpTransport = MakeShared<FOpenAiCompatibleHttpTransport>();
	StubTransport = MakeShared<FUnrealAiLlmTransportStub>();
	RefreshActiveLlmTransport();
	RebuildAgentHarness();
}

FUnrealAiBackendRegistry::~FUnrealAiBackendRegistry() = default;

void FUnrealAiBackendRegistry::RefreshActiveLlmTransport()
{
	ActiveLlmTransport = ModelProfiles->HasAnyConfiguredApiKey()
		? StaticCastSharedPtr<ILlmTransport>(HttpTransport)
		: StaticCastSharedPtr<ILlmTransport>(StubTransport);
}

void FUnrealAiBackendRegistry::RebuildAgentHarness()
{
	AgentHarness = MakeUnique<FUnrealAiAgentHarness>(
		Persistence.Get(),
		ContextService.Get(),
		ModelProfiles.Get(),
		ToolCatalog.Get(),
		ActiveLlmTransport,
		ToolExecutionHost.Get());
}

void FUnrealAiBackendRegistry::ReloadLlmConfiguration()
{
	if (AgentHarness.IsValid())
	{
		AgentHarness->CancelTurn();
	}
	ModelProfiles->Reload();
	if (ToolCatalog)
	{
		ToolCatalog->LoadFromPlugin();
	}
	if (ToolExecutionHost)
	{
		static_cast<FUnrealAiToolExecutionHost*>(ToolExecutionHost.Get())->ReloadCatalog();
	}
	RefreshModelConnector();
	RefreshActiveLlmTransport();
	RebuildAgentHarness();
}

void FUnrealAiBackendRegistry::RefreshModelConnector()
{
	if (ModelProfiles->HasAnyConfiguredApiKey())
	{
		ModelConnector = MakeUnique<FUnrealAiModelConnectorHttp>(ModelProfiles.Get());
	}
	else
	{
		ModelConnector = MakeUnique<FUnrealAiModelConnectorStub>();
	}
}
