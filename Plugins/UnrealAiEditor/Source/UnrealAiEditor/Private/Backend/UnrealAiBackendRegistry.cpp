#include "Backend/UnrealAiBackendRegistry.h"

#include "Backend/FUnrealAiChatServiceStub.h"
#include "Backend/FUnrealAiModelConnectorHttp.h"
#include "Backend/FUnrealAiModelConnectorStub.h"
#include "Backend/FUnrealAiPersistenceStub.h"
#include "Backend/FUnrealAiUsageTracker.h"
#include "Context/FUnrealAiContextService.h"
#include "Harness/FUnrealAiAgentHarness.h"
#include "Harness/FUnrealAiLlmTransportFixture.h"
#include "Harness/FUnrealAiLlmTransportStub.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Harness/IUnrealAiAgentHarness.h"
#include "Harness/ILlmTransport.h"
#include "Memory/FUnrealAiMemoryService.h"
#include "Retrieval/FUnrealAiRetrievalService.h"
#include "Tools/FUnrealAiToolExecutionHost.h"
#include "Tools/UnrealAiToolCatalog.h"
#include "Transport/FOpenAiCompatibleHttpTransport.h"
#include "Pricing/FUnrealAiModelPricingCatalog.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"

FUnrealAiBackendRegistry::FUnrealAiBackendRegistry()
{
	Persistence = MakeUnique<FUnrealAiPersistenceStub>();
	PricingCatalog = MakeUnique<FUnrealAiModelPricingCatalog>();
	UsageTracker = MakeUnique<FUnrealAiUsageTracker>(Persistence.Get(), PricingCatalog.Get());
	MemoryService = MakeUnique<FUnrealAiMemoryService>(Persistence.Get());
	MemoryService->Load();
	ModelProfiles = MakeUnique<FUnrealAiModelProfileRegistry>(Persistence.Get());
	RetrievalService = MakeUnique<FUnrealAiRetrievalService>(Persistence.Get(), ModelProfiles.Get(), MemoryService.Get());
	ContextService = MakeUnique<FUnrealAiContextService>(Persistence.Get(), MemoryService.Get(), RetrievalService.Get());
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
	const FString FixtureEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_LLM_FIXTURE"));
	if (!FixtureEnv.IsEmpty())
	{
		FString FullPath = FixtureEnv;
		if (FPaths::IsRelative(FullPath))
		{
			FullPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), FullPath);
		}
		if (!FixtureTransport.IsValid() || FixtureTransport->GetFixturePath() != FullPath)
		{
			FixtureTransport = MakeShared<FUnrealAiLlmTransportFixture>(FullPath);
		}
		ActiveLlmTransport = FixtureTransport;
		return;
	}
	FixtureTransport.Reset();

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
		ToolExecutionHost.Get(),
		UsageTracker.Get(),
		MemoryService.Get());
}

void FUnrealAiBackendRegistry::ReloadLlmConfiguration()
{
	if (AgentHarness.IsValid())
	{
		AgentHarness->CancelTurn();
	}
	ModelProfiles->Reload();
	if (UsageTracker)
	{
		UsageTracker->ReloadFromDisk();
	}
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
