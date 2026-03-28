#include "Tools/UnrealAiToolSurfacePipeline.h"

#include "Context/AgentContextTypes.h"
#include "Context/IAgentContextService.h"
#include "HAL/PlatformMisc.h"
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
	/** Default on (empty env). Opt out: 0/off/false/no/legacy. */
	static bool EnvEligibilityDisabledByUser()
	{
		const FString E = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_TOOL_ELIGIBILITY")).TrimStartAndEnd().ToLower();
		return E == TEXT("0") || E == TEXT("off") || E == TEXT("false") || E == TEXT("no") || E == TEXT("legacy")
			|| E == TEXT("disable") || E == TEXT("disabled");
	}

	static bool EnvEligibilityEnabled()
	{
		return !EnvEligibilityDisabledByUser();
	}

	/** Default on (empty env). Opt out: 0/off/false/no. */
	static bool EnvUsagePriorDisabledByUser()
	{
		const FString E = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_TOOL_USAGE_PRIOR")).TrimStartAndEnd().ToLower();
		return E == TEXT("0") || E == TEXT("off") || E == TEXT("false") || E == TEXT("no");
	}

	static bool EnvUsagePriorEnabled()
	{
		return !EnvUsagePriorDisabledByUser();
	}

	static int32 ReadEnvInt(const TCHAR* Name, int32 Def, int32 MinV, int32 MaxV)
	{
		const FString V = FPlatformMisc::GetEnvironmentVariable(Name).TrimStartAndEnd();
		if (V.IsEmpty())
		{
			return Def;
		}
		return FMath::Clamp(FCString::Atoi(*V), MinV, MaxV);
	}

	struct FScored
	{
		FString ToolId;
		float Score = 0.f;
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
	if (!UnrealAiToolSurfacePipelinePriv::EnvEligibilityEnabled())
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
	FString Shaped;
	EUnrealAiToolQueryShape ShapeUsed = EUnrealAiToolQueryShape::Raw;
	UnrealAiToolQueryShaper::ShapeForRetrieval(Request.UserText, Shaped, ShapeUsed);
	FString Hybrid;
	UnrealAiToolQueryShaper::BuildHybridQuery(Request.UserText, Shaped, Hybrid);
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
			UnrealAiToolSurfacePipelinePriv::FScored Row;
			Row.ToolId = Tid;
			const float Bm = RawScores.FindRef(Tid) / MaxS;
			TArray<FString> ToolTags;
			UnrealAiToolContextBias::CollectToolDomainTags(Def, ToolTags);
			const float Mult = UnrealAiToolContextBias::ScoreMultiplierForTool(ToolTags, ActiveTags);
			float Combined = Bm * Mult;
			const bool bPrior = UnrealAiToolSurfacePipelinePriv::EnvUsagePriorEnabled();
			if (bPrior)
			{
				const float Prior = UnrealAiToolUsagePrior::GetOperationalPrior01(Tid);
				const float WUse = 0.3f;
				const float WSim = 0.7f;
				Combined = WSim * Combined + WUse * Prior;
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

	const int32 KMin = UnrealAiToolSurfacePipelinePriv::ReadEnvInt(TEXT("UNREAL_AI_TOOL_K_MIN"), 8, 1, 512);
	const int32 KMax = UnrealAiToolSurfacePipelinePriv::ReadEnvInt(TEXT("UNREAL_AI_TOOL_K_MAX"), 36, KMin, 512);
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

	const int32 ExpandedCount = UnrealAiToolSurfacePipelinePriv::ReadEnvInt(TEXT("UNREAL_AI_TOOL_EXPANDED_COUNT"), 6, 0, 64);
	const int32 Budget = UnrealAiToolSurfacePipelinePriv::ReadEnvInt(TEXT("UNREAL_AI_TOOL_SURFACE_BUDGET_CHARS"), 120000, 8000, 2000000);

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
