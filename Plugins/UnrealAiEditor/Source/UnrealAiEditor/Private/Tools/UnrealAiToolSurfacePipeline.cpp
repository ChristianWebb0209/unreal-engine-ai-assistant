#include "Tools/UnrealAiToolSurfacePipeline.h"

#include "Context/AgentContextTypes.h"
#include "Context/IAgentContextService.h"
#include "UnrealAiEditorModule.h"
#include "Misc/UnrealAiRuntimeDefaults.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Misc/SecureHash.h"
#include "Tools/UnrealAiToolBm25Index.h"
#include "Tools/UnrealAiToolCatalog.h"
#include "Tools/UnrealAiToolContextBias.h"
#include "Tools/UnrealAiToolKPolicy.h"
#include "Tools/UnrealAiToolQueryShaper.h"
#include "Tools/UnrealAiToolUsagePrior.h"

namespace UnrealAiToolSurfacePipelinePriv
{
	struct FScored
	{
		FString ToolId;
		float Score = 0.f;
		float Bm25Norm01 = 0.f;
		float ContextMult = 1.f;
		float UsagePrior01 = 0.f;
		bool bUsagePriorBlended = false;
		bool bGuardrail = false;
	};

	static FString HashUtf8(const FString& S)
	{
		FTCHARToUTF8 Utf(*S);
		FMD5 Md5;
		Md5.Update(reinterpret_cast<const uint8*>(Utf.Get()), Utf.Length());
		uint8 Digest[16];
		Md5.Final(Digest);
		return BytesToHex(Digest, 16);
	}
} // namespace UnrealAiToolSurfacePipelinePriv

bool UnrealAiToolSurfacePipeline::TryBuildTieredToolSurface(
	const FUnrealAiAgentTurnRequest& Request,
	const int32 LlmRound,
	IAgentContextService* ContextService,
	FUnrealAiToolCatalog* Catalog,
	const FUnrealAiModelCapabilities& Caps,
	const FUnrealAiToolPackOptions* PackOptions,
	const bool bWantDispatchSurface,
	FString& OutToolIndexMarkdown,
	FUnrealAiToolSurfaceTelemetry& OutTelemetry)
{
	OutToolIndexMarkdown.Reset();
	OutTelemetry = FUnrealAiToolSurfaceTelemetry();
	if (!UnrealAiRuntimeDefaults::ToolEligibilityTelemetryEnabled)
	{
		OutTelemetry.ToolSurfaceMode = TEXT("off");
		return false;
	}
	if (!Catalog || !Catalog->IsLoaded() || !bWantDispatchSurface || Request.Mode != EUnrealAiAgentMode::Agent)
	{
		OutTelemetry.ToolSurfaceMode = TEXT("off");
		return false;
	}
	if (LlmRound != 1)
	{
		// Cache / topic: only reshape tool surface on first LLM round of a user send (repair rounds keep prior surface in conv).
		OutTelemetry.ToolSurfaceMode = TEXT("off");
		return false;
	}

	const double T0 = FPlatformTime::Seconds();
	const FString& QueryForTools = Request.ContextComplexityUserText.IsEmpty() ? Request.UserText : Request.ContextComplexityUserText;
	FString Shaped;
	EUnrealAiToolQueryShape ShapeUsed = EUnrealAiToolQueryShape::Raw;
	UnrealAiToolQueryShaper::ShapeForRetrieval(QueryForTools, Shaped, ShapeUsed);
	FString Hybrid;
	UnrealAiToolQueryShaper::BuildHybridQuery(QueryForTools, Shaped, Hybrid);
	OutTelemetry.QueryShape = (ShapeUsed == EUnrealAiToolQueryShape::Heuristic) ? TEXT("hybrid") : TEXT("raw");
	OutTelemetry.QueryHash = UnrealAiToolSurfacePipelinePriv::HashUtf8(Hybrid);

	FUnrealAiToolBm25Index Index;
	Index.RebuildForEnabledTools(*Catalog, Request.Mode, Caps, PackOptions);
	TMap<FString, float> RawScores;
	Index.ScoreQuery(Hybrid, RawScores);

	float MaxS = 0.f;
	for (const auto& P : RawScores)
	{
		MaxS = FMath::Max(MaxS, P.Value);
	}
	if (MaxS < 1.e-6f)
	{
		MaxS = 1.f;
	}

	TArray<UnrealAiToolSurfacePipelinePriv::FScored> Pool;
	const FAgentContextState* State = ContextService
		? ContextService->GetState(Request.ProjectId, Request.ThreadId)
		: nullptr;
	TArray<FString> ActiveTags;
	UnrealAiToolContextBias::GatherActiveDomainTags(State, ActiveTags);

	Catalog->ForEachEnabledToolForMode(
		Request.Mode,
		Caps,
		PackOptions,
		[&](const FString& Tid, const TSharedPtr<FJsonObject>& Def)
		{
			const FString CodePref = FUnrealAiEditorModule::GetAgentCodeTypePreference();
			if (CodePref == TEXT("cpp_only"))
			{
				if (Tid == TEXT("blueprint_apply_ir"))
				{
					return;
				}
			}
			else if (CodePref == TEXT("blueprint_only"))
			{
				if (Tid == TEXT("cpp_project_compile"))
				{
					return;
				}
			}
			UnrealAiToolSurfacePipelinePriv::FScored Row;
			Row.ToolId = Tid;
			const float Bm = RawScores.FindRef(Tid) / MaxS;
			Row.Bm25Norm01 = Bm;
			TArray<FString> ToolTags;
			UnrealAiToolContextBias::CollectToolDomainTags(Def, ToolTags);
			const float Mult = UnrealAiToolContextBias::ScoreMultiplierForTool(ToolTags, ActiveTags);
			Row.ContextMult = Mult;
			float Combined = Bm * Mult;
			const bool bPrior = UnrealAiRuntimeDefaults::ToolUsagePriorEnabled;
			if (bPrior)
			{
				const float Prior = UnrealAiToolUsagePrior::GetOperationalPrior01(Tid);
				Row.UsagePrior01 = Prior;
				Row.bUsagePriorBlended = true;
				const float WUse = 0.3f;
				const float WSim = 0.7f;
				Combined = WSim * Combined + WUse * Prior;
			}
			else
			{
				Row.UsagePrior01 = UnrealAiToolUsagePrior::GetOperationalPrior01(Tid);
			}
			Row.Score = Combined;
			bool bGr = false;
			const TSharedPtr<FJsonObject>* Ctx = nullptr;
			if (Def->TryGetObjectField(TEXT("context_selector"), Ctx) && Ctx && (*Ctx).IsValid())
			{
				(*Ctx)->TryGetBoolField(TEXT("always_include_in_core_pack"), bGr);
			}
			Row.bGuardrail = bGr;
			Pool.Add(Row);
		});

	Pool.Sort([](const UnrealAiToolSurfacePipelinePriv::FScored& A, const UnrealAiToolSurfacePipelinePriv::FScored& B)
	{
		if (A.Score != B.Score)
		{
			return A.Score > B.Score;
		}
		return A.ToolId < B.ToolId;
	});

	TArray<float> NonGuardScores;
	for (const UnrealAiToolSurfacePipelinePriv::FScored& R : Pool)
	{
		if (!R.bGuardrail)
		{
			NonGuardScores.Add(R.Score);
		}
	}

	// Defaults keep the tiered appendix smaller so user + history retain more of the context window.
	const int32 KMin = UnrealAiRuntimeDefaults::ToolKMin;
	const int32 KMax = FMath::Clamp(UnrealAiRuntimeDefaults::ToolKMax, KMin, 512);
	const int32 Keffective = UnrealAiToolKPolicy::ComputeEffectiveK(NonGuardScores, KMin, KMax);

	TSet<FString> GuardrailIds;
	TArray<FString> Ordered;
	for (const UnrealAiToolSurfacePipelinePriv::FScored& R : Pool)
	{
		if (R.bGuardrail)
		{
			GuardrailIds.Add(R.ToolId);
			Ordered.Add(R.ToolId);
		}
	}
	int32 Taken = 0;
	for (const UnrealAiToolSurfacePipelinePriv::FScored& R : Pool)
	{
		if (R.bGuardrail)
		{
			continue;
		}
		if (Taken >= Keffective)
		{
			break;
		}
		Ordered.Add(R.ToolId);
		++Taken;
	}

	TMap<FString, UnrealAiToolSurfacePipelinePriv::FScored> ScoreById;
	ScoreById.Reserve(Pool.Num());
	for (const UnrealAiToolSurfacePipelinePriv::FScored& R : Pool)
	{
		ScoreById.Add(R.ToolId, R);
	}
	int32 RankIx = 0;
	for (const FString& Tid : Ordered)
	{
		FUnrealAiToolSurfaceRankedEntry E;
		E.Rank = RankIx++;
		E.ToolId = Tid;
		if (const UnrealAiToolSurfacePipelinePriv::FScored* Found = ScoreById.Find(Tid))
		{
			E.CombinedScore = Found->Score;
			E.Bm25Norm01 = Found->Bm25Norm01;
			E.ContextMultiplier = Found->ContextMult;
			E.UsagePrior01 = Found->UsagePrior01;
			E.bUsagePriorBlended = Found->bUsagePriorBlended;
			E.bGuardrail = Found->bGuardrail;
			E.SelectionReason = Found->bGuardrail ? TEXT("guardrail_always_in_core_pack")
												: (Found->bUsagePriorBlended ? TEXT("ranked_bm25_x_context_blended_with_usage_prior")
																			: TEXT("ranked_bm25_x_context"));
		}
		OutTelemetry.RankedTools.Add(E);
	}

	const int32 ExpandedCount = UnrealAiRuntimeDefaults::ToolExpandedCount;
	const int32 Budget = UnrealAiRuntimeDefaults::ToolSurfaceBudgetChars;

	Catalog->BuildCompactToolIndexAppendixTiered(
		Request.Mode,
		Caps,
		PackOptions,
		Ordered,
		GuardrailIds,
		ExpandedCount,
		Budget,
		OutToolIndexMarkdown);

	const double T1 = FPlatformTime::Seconds();
	OutTelemetry.ToolSurfaceMode = TEXT("dispatch_eligibility");
	OutTelemetry.EligibleCount = Ordered.Num();
	OutTelemetry.RosterChars = OutToolIndexMarkdown.Len();
	OutTelemetry.BudgetRemaining = FMath::Max(0, Budget - OutToolIndexMarkdown.Len());
	OutTelemetry.RetrievalLatencyMs = static_cast<int32>((T1 - T0) * 1000.0);
	OutTelemetry.KEffective = Keffective;
	for (int32 I = 0; I < FMath::Min(ExpandedCount, Ordered.Num()); ++I)
	{
		OutTelemetry.ExpandedToolIds.Add(Ordered[I]);
	}
	return !OutToolIndexMarkdown.IsEmpty();
}
