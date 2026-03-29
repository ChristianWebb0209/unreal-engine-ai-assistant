#include "Context/UnrealAiContextDecisionLogger.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/UnrealAiRuntimeDefaults.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealAiContextDecisionLogger
{
	namespace
	{
		static const TCHAR* ModeName(const EUnrealAiAgentMode Mode)
		{
			switch (Mode)
			{
			case EUnrealAiAgentMode::Ask: return TEXT("ask");
			case EUnrealAiAgentMode::Agent: return TEXT("agent");
			case EUnrealAiAgentMode::Plan: return TEXT("plan");
			default: return TEXT("unknown");
			}
		}

		static FString SanitizePathPart(const FString& In)
		{
			FString S = In;
			if (S.IsEmpty())
			{
				return TEXT("unknown");
			}
			static const TCHAR* Bad = TEXT("\\/:*?\"<>|");
			for (const TCHAR* P = Bad; *P; ++P)
			{
				TCHAR Search[2] = {*P, TEXT('\0')};
				S.ReplaceInline(Search, TEXT("_"));
			}
			return S;
		}

		static TSharedPtr<FJsonObject> CandidateToJson(
			const UnrealAiContextCandidates::FContextCandidateEnvelope& C,
			const TCHAR* Decision,
			const FString& Reason,
			const FString& InvocationReason)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("decision"), Decision);
			O->SetStringField(TEXT("reason"), Reason);
			O->SetStringField(TEXT("invocationReason"), InvocationReason);
			O->SetStringField(TEXT("type"), UnrealAiContextRankingPolicy::CandidateTypeName(C.Type));
			O->SetStringField(TEXT("sourceId"), C.SourceId);
			O->SetNumberField(TEXT("tokenCostEstimate"), C.TokenCostEstimate);
			O->SetNumberField(TEXT("scoreTotal"), C.Score.Total);

			TSharedPtr<FJsonObject> Features = MakeShared<FJsonObject>();
			Features->SetNumberField(TEXT("mentionHit"), C.Features.MentionHit);
			Features->SetNumberField(TEXT("heuristicSemantic"), C.Features.HeuristicSemantic);
			Features->SetNumberField(TEXT("recency"), C.Features.Recency);
			Features->SetNumberField(TEXT("freshnessReliability"), C.Features.FreshnessReliability);
			Features->SetNumberField(TEXT("safetyRisk"), C.Features.SafetyRisk);
			Features->SetNumberField(TEXT("activeBonus"), C.Features.ActiveBonus);
			Features->SetNumberField(TEXT("threadOverlayBonus"), C.Features.ThreadOverlayBonus);
			Features->SetNumberField(TEXT("frequency"), C.Features.Frequency);
			Features->SetNumberField(TEXT("vectorSimilarity"), C.Features.VectorSimilarity);
			Features->SetNumberField(TEXT("threadScope"), C.Features.ThreadScope);
			O->SetObjectField(TEXT("features"), Features);

			TSharedPtr<FJsonObject> Score = MakeShared<FJsonObject>();
			Score->SetNumberField(TEXT("base"), C.Score.Base);
			Score->SetNumberField(TEXT("mentionHit"), C.Score.MentionHit);
			Score->SetNumberField(TEXT("heuristicSemantic"), C.Score.HeuristicSemantic);
			Score->SetNumberField(TEXT("recency"), C.Score.Recency);
			Score->SetNumberField(TEXT("freshnessReliability"), C.Score.FreshnessReliability);
			Score->SetNumberField(TEXT("safetyPenalty"), C.Score.SafetyPenalty);
			Score->SetNumberField(TEXT("activeBonus"), C.Score.ActiveBonus);
			Score->SetNumberField(TEXT("threadOverlayBonus"), C.Score.ThreadOverlayBonus);
			Score->SetNumberField(TEXT("frequency"), C.Score.Frequency);
			Score->SetNumberField(TEXT("vectorSimilarity"), C.Score.VectorSimilarity);
			Score->SetNumberField(TEXT("threadScope"), C.Score.ThreadScope);
			Score->SetNumberField(TEXT("total"), C.Score.Total);
			O->SetObjectField(TEXT("scoreBreakdown"), Score);

			return O;
		}

		static TSharedPtr<FJsonObject> TypeCountsToJson(
			const TArray<UnrealAiContextCandidates::FContextCandidateEnvelope>& Items)
		{
			TMap<FString, int32> Counts;
			for (const UnrealAiContextCandidates::FContextCandidateEnvelope& C : Items)
			{
				const FString K = UnrealAiContextRankingPolicy::CandidateTypeName(C.Type);
				Counts.FindOrAdd(K) += 1;
			}
			TArray<FString> Keys;
			Counts.GetKeys(Keys);
			Keys.Sort();

			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			for (const FString& K : Keys)
			{
				O->SetNumberField(K, Counts[K]);
			}
			return O;
		}
	}

	bool ShouldLogDecisions(const bool bVerboseContextBuild)
	{
		return bVerboseContextBuild || UnrealAiRuntimeDefaults::ContextDecisionLogEnabled;
	}

	void WriteDecisionLog(
		const FString& ProjectId,
		const FString& ThreadId,
		const FString& InvocationReason,
		const FAgentContextBuildOptions& Options,
		const int32 BudgetChars,
		const UnrealAiContextCandidates::FUnifiedContextBuildResult& Unified,
		const FString& EmittedContextBlock)
	{
		const FString SafeThread = SanitizePathPart(ThreadId);
		const FString Root = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("UnrealAiEditor"),
			TEXT("ContextDecisionLogs"),
			SafeThread);
		IFileManager::Get().MakeDirectory(*Root, true);

		const FString Ts = FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
		const FString JsonlPath = FPaths::Combine(Root, Ts + TEXT(".jsonl"));
		const FString SummaryPath = FPaths::Combine(Root, Ts + TEXT("-summary.md"));

		auto WriteJsonlLine = [&JsonlPath](const TSharedPtr<FJsonObject>& Obj)
		{
			FString Line;
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
			if (FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer))
			{
				Line += TEXT("\n");
				FFileHelper::SaveStringToFile(
					Line,
					*JsonlPath,
					FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
					&IFileManager::Get(),
					FILEWRITE_Append);
			}
		};

		{
			TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
			Meta->SetStringField(TEXT("event"), TEXT("meta"));
			Meta->SetStringField(TEXT("timestampUtc"), FDateTime::UtcNow().ToIso8601());
			Meta->SetStringField(TEXT("projectId"), ProjectId);
			Meta->SetStringField(TEXT("threadId"), ThreadId);
			Meta->SetStringField(TEXT("invocationReason"), InvocationReason);
			Meta->SetStringField(TEXT("mode"), ModeName(Options.Mode));
			Meta->SetNumberField(TEXT("promptCharCount"), Unified.PromptCharCount);
			Meta->SetNumberField(TEXT("promptTokenCount"), Unified.PromptTokenCount);
			Meta->SetBoolField(TEXT("shortPrompt"), Unified.bShortPrompt);
			Meta->SetNumberField(TEXT("memorySnippetCapApplied"), Unified.MemorySnippetCapApplied);
			Meta->SetNumberField(TEXT("softBudgetCharsApplied"), Unified.SoftBudgetCharsApplied);
			Meta->SetNumberField(TEXT("maxPackedCandidatesApplied"), Unified.MaxPackedCandidatesApplied);
			Meta->SetNumberField(TEXT("budgetChars"), BudgetChars);
			Meta->SetNumberField(TEXT("emittedChars"), EmittedContextBlock.Len());
			Meta->SetNumberField(TEXT("keptCount"), Unified.Packed.Num());
			Meta->SetNumberField(TEXT("droppedCount"), Unified.Dropped.Num());
			Meta->SetObjectField(TEXT("keptByType"), TypeCountsToJson(Unified.Packed));
			Meta->SetObjectField(TEXT("droppedByType"), TypeCountsToJson(Unified.Dropped));
			WriteJsonlLine(Meta);
		}

		for (const UnrealAiContextCandidates::FContextCandidateEnvelope& C : Unified.Packed)
		{
			TSharedPtr<FJsonObject> Obj = CandidateToJson(C, TEXT("kept"), TEXT("pack:kept"), InvocationReason);
			Obj->SetStringField(TEXT("event"), TEXT("candidate"));
			WriteJsonlLine(Obj);
		}
		for (const UnrealAiContextCandidates::FContextCandidateEnvelope& C : Unified.Dropped)
		{
			TSharedPtr<FJsonObject> Obj = CandidateToJson(C, TEXT("dropped"), C.DropReason, InvocationReason);
			Obj->SetStringField(TEXT("event"), TEXT("candidate"));
			WriteJsonlLine(Obj);
		}

		FString Summary;
		Summary += TEXT("# Context decision summary\n\n");
		Summary += FString::Printf(TEXT("- invocation reason: `%s`\n"), *InvocationReason);
		Summary += FString::Printf(TEXT("- mode: `%s`\n"), ModeName(Options.Mode));
		Summary += FString::Printf(TEXT("- budget chars: `%d`\n"), BudgetChars);
		Summary += FString::Printf(TEXT("- emitted chars: `%d`\n"), EmittedContextBlock.Len());
		Summary += FString::Printf(TEXT("- kept: `%d`\n"), Unified.Packed.Num());
		Summary += FString::Printf(TEXT("- dropped: `%d`\n"), Unified.Dropped.Num());
		Summary += TEXT("\n## Top kept\n");
		for (int32 i = 0; i < Unified.Packed.Num() && i < 12; ++i)
		{
			const auto& C = Unified.Packed[i];
			Summary += FString::Printf(
				TEXT("- `%s` score=%.2f tokens=%d source=%s\n"),
				UnrealAiContextRankingPolicy::CandidateTypeName(C.Type),
				C.Score.Total,
				C.TokenCostEstimate,
				*C.SourceId);
		}
		Summary += TEXT("\n## Top dropped\n");
		for (int32 i = 0; i < Unified.Dropped.Num() && i < 20; ++i)
		{
			const auto& C = Unified.Dropped[i];
			Summary += FString::Printf(
				TEXT("- `%s` reason=`%s` score=%.2f source=%s\n"),
				UnrealAiContextRankingPolicy::CandidateTypeName(C.Type),
				*C.DropReason,
				C.Score.Total,
				*C.SourceId);
		}
		FFileHelper::SaveStringToFile(Summary, *SummaryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}
