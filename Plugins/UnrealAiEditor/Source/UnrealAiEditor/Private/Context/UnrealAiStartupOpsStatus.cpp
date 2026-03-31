#include "Context/UnrealAiStartupOpsStatus.h"

#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/AgentContextTypes.h"
#include "Context/UnrealAiProjectTreeSampler.h"
#include "Retrieval/IUnrealAiRetrievalService.h"

namespace UnrealAiStartupOpsStatus
{
	namespace
	{
		static FString DeriveDiscoveryState(const FProjectTreeSummary& Summary)
		{
			if (Summary.UpdatedUtc == FDateTime::MinValue())
			{
				return TEXT("discovery_pending");
			}
			if (Summary.LastQueryStatus.StartsWith(TEXT("no_registry"))
				|| Summary.LastQueryStatus.StartsWith(TEXT("error")))
			{
				return TEXT("error");
			}
			return TEXT("discovery_ready");
		}

		static FString DeriveRetrievalState(IUnrealAiRetrievalService* RetrievalService, const FString& ProjectId)
		{
			if (!RetrievalService)
			{
				return TEXT("unavailable");
			}
			const FUnrealAiRetrievalProjectStatus Status = RetrievalService->GetProjectStatus(ProjectId);
			if (!Status.bEnabled)
			{
				return TEXT("disabled");
			}
			if (Status.bBusy || Status.StateText.Equals(TEXT("indexing"), ESearchCase::IgnoreCase))
			{
				return TEXT("indexing");
			}
			if (Status.StateText.Equals(TEXT("ready"), ESearchCase::IgnoreCase))
			{
				return TEXT("ready");
			}
			if (Status.StateText.Equals(TEXT("error"), ESearchCase::IgnoreCase))
			{
				return TEXT("error");
			}
			return TEXT("degraded");
		}
	}

	FStatus BuildStatus(IUnrealAiRetrievalService* RetrievalService, const FProjectTreeSummary& Summary, const FString& ProjectId)
	{
		FStatus Out;
		Out.DiscoveryState = DeriveDiscoveryState(Summary);
		Out.RetrievalState = DeriveRetrievalState(RetrievalService, ProjectId);
		if (Out.DiscoveryState == TEXT("error") || Out.RetrievalState == TEXT("error"))
		{
			Out.AggregateState = TEXT("error");
		}
		else if (Out.DiscoveryState == TEXT("discovery_pending") || Out.RetrievalState == TEXT("indexing"))
		{
			Out.AggregateState = TEXT("indexing");
		}
		else if (Out.DiscoveryState == TEXT("discovery_ready")
			&& (Out.RetrievalState == TEXT("ready") || Out.RetrievalState == TEXT("disabled") || Out.RetrievalState == TEXT("unavailable")))
		{
			Out.AggregateState = TEXT("ready");
		}
		else
		{
			Out.AggregateState = TEXT("degraded");
		}
		return Out;
	}

	FString BuildCompactLine(const FStatus& Status)
	{
		return FString::Printf(
			TEXT("Startup ops: %s | discovery=%s | retrieval=%s"),
			*Status.AggregateState,
			*Status.DiscoveryState,
			*Status.RetrievalState);
	}

	FString BuildCompactLine(const TSharedPtr<FUnrealAiBackendRegistry>& BackendRegistry, const FString& ProjectId)
	{
		IUnrealAiRetrievalService* Retrieval = BackendRegistry.IsValid() ? BackendRegistry->GetRetrievalService() : nullptr;
		const FProjectTreeSummary& Summary = UnrealAiProjectTreeSampler::GetOrRefreshProjectSummary(ProjectId, false);
		return BuildCompactLine(BuildStatus(Retrieval, Summary, ProjectId));
	}
}
