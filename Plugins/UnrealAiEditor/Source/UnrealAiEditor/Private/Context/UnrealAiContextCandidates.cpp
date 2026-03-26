#include "Context/UnrealAiContextCandidates.h"

#include "Context/UnrealAiActiveTodoSummary.h"
#include "Context/UnrealAiContextRankingPolicy.h"
#include "Context/UnrealAiEditorContextQueries.h"
#include "Memory/IUnrealAiMemoryService.h"
#include "Misc/EngineVersion.h"

namespace UnrealAiContextCandidates
{
	namespace
	{
		static int32 EstimateTokens(const FString& Text)
		{
			return FMath::Max(1, Text.Len() / 4);
		}

		static TSet<FString> BuildUserTokens(const FString& UserText)
		{
			TSet<FString> Tokens;
			FString Copy = UserText.ToLower();
			for (int32 i = 0; i < Copy.Len(); ++i)
			{
				const TCHAR C = Copy[i];
				if (!FChar::IsAlnum(C) && C != TEXT('_') && C != TEXT('/') && C != TEXT('.'))
				{
					Copy[i] = TEXT(' ');
				}
			}
			TArray<FString> Parts;
			Copy.ParseIntoArray(Parts, TEXT(" "), true);
			for (const FString& P : Parts)
			{
				if (P.Len() >= 3)
				{
					Tokens.Add(P);
				}
			}
			return Tokens;
		}

		static float ComputeMentionHit(const FString& UserLower, const FString& PayloadLower)
		{
			if (UserLower.IsEmpty() || PayloadLower.IsEmpty())
			{
				return 0.f;
			}
			return UserLower.Contains(PayloadLower.Left(FMath::Min(64, PayloadLower.Len()))) ? 1.f : 0.f;
		}

		static float ComputeHeuristicSemantic(const TSet<FString>& UserTokens, const FString& Payload)
		{
			if (UserTokens.Num() == 0 || Payload.IsEmpty())
			{
				return 0.f;
			}
			TSet<FString> PayloadTokens = BuildUserTokens(Payload);
			if (PayloadTokens.Num() == 0)
			{
				return 0.f;
			}
			int32 Hits = 0;
			for (const FString& T : UserTokens)
			{
				if (PayloadTokens.Contains(T))
				{
					++Hits;
				}
			}
			return static_cast<float>(Hits) / static_cast<float>(FMath::Max(UserTokens.Num(), 1));
		}
	}

	void CollectCandidates(
		const FAgentContextState& State,
		const FAgentContextBuildOptions& Options,
		IUnrealAiMemoryService* MemoryService,
		TArray<FContextCandidateEnvelope>& OutCandidates)
	{
		OutCandidates.Reset();
		OutCandidates.Reserve(UnrealAiContextRankingPolicy::DefaultHardMaxCandidates);

		{
			FContextCandidateEnvelope E;
			E.Type = UnrealAiContextRankingPolicy::ECandidateType::EngineHeader;
			E.SourceId = TEXT("engine");
			const FEngineVersion& V = FEngineVersion::Current();
			E.Payload = FString::Printf(TEXT("Unreal Engine %d.%d"), V.GetMajor(), V.GetMinor());
			E.RenderedText = FString::Printf(TEXT("### ((%s))"), *E.Payload);
			E.TokenCostEstimate = EstimateTokens(E.RenderedText);
			OutCandidates.Add(MoveTemp(E));
		}

		for (int32 i = 0; i < State.Attachments.Num(); ++i)
		{
			const FContextAttachment& A = State.Attachments[i];
			FContextCandidateEnvelope E;
			E.Type = UnrealAiContextRankingPolicy::ECandidateType::Attachment;
			E.SourceId = FString::Printf(TEXT("attachment:%d"), i);
			E.Payload = UnrealAiEditorContextQueries::BuildRichAttachmentPayload(A);
			E.bImageLikeAttachment = UnrealAiEditorContextQueries::IsImageLikeAttachment(A);
			const FString TypeStr = [&A]()
			{
				switch (A.Type)
				{
				case EContextAttachmentType::AssetPath: return TEXT("asset");
				case EContextAttachmentType::FilePath: return TEXT("file");
				case EContextAttachmentType::FreeText: return TEXT("text");
				case EContextAttachmentType::BlueprintNodeRef: return TEXT("bp_node");
				case EContextAttachmentType::ActorReference: return TEXT("actor");
				case EContextAttachmentType::ContentFolder: return TEXT("folder");
				default: return TEXT("unknown");
				}
			}();
			E.RenderedText = FString::Printf(TEXT("### Attachment (%s)\n%s"), *TypeStr, *E.Payload);
			E.TokenCostEstimate = EstimateTokens(E.RenderedText);
			OutCandidates.Add(MoveTemp(E));
		}

		for (int32 i = 0; i < State.ToolResults.Num(); ++i)
		{
			const FToolContextEntry& T = State.ToolResults[i];
			FContextCandidateEnvelope E;
			E.Type = UnrealAiContextRankingPolicy::ECandidateType::ToolResult;
			E.SourceId = FString::Printf(TEXT("tool:%d:%s"), i, *T.ToolName);
			E.Payload = T.TruncatedResult;
			E.Features.Recency = static_cast<float>(FMath::Max(0.0, 1.0 - (FDateTime::UtcNow() - T.Timestamp).GetTotalMinutes() / 60.0));
			E.Features.FreshnessReliability = E.Features.Recency;
			E.RenderedText = FString::Printf(TEXT("### Tool: %s\n%s"), *T.ToolName, *T.TruncatedResult);
			E.TokenCostEstimate = EstimateTokens(E.RenderedText);
			OutCandidates.Add(MoveTemp(E));
		}

		if (State.EditorSnapshot.IsSet() && State.EditorSnapshot.GetValue().bValid)
		{
			const FEditorContextSnapshot& S = State.EditorSnapshot.GetValue();
			auto AddSnapshotLine = [&OutCandidates](const FString& Id, const FString& Text)
			{
				if (Text.IsEmpty())
				{
					return;
				}
				FContextCandidateEnvelope E;
				E.Type = UnrealAiContextRankingPolicy::ECandidateType::EditorSnapshotField;
				E.SourceId = Id;
				E.Payload = Text;
				E.RenderedText = Text;
				E.TokenCostEstimate = EstimateTokens(E.RenderedText);
				OutCandidates.Add(MoveTemp(E));
			};

			AddSnapshotLine(TEXT("snap:selected_actors"), FString::Printf(TEXT("Level selection: %s"), *S.SelectedActorsSummary));
			AddSnapshotLine(TEXT("snap:cb_path"), FString::Printf(TEXT("Content Browser folder: %s"), *S.ContentBrowserPath));
			if (S.ContentBrowserSelectedAssets.Num() > 0)
			{
				for (const FString& Asset : S.ContentBrowserSelectedAssets)
				{
					AddSnapshotLine(TEXT("snap:cb_asset"), FString::Printf(TEXT("Content Browser selected asset: %s"), *Asset));
				}
			}
			for (const FString& OpenAsset : S.OpenEditorAssets)
			{
				AddSnapshotLine(TEXT("snap:open_asset"), FString::Printf(TEXT("Open asset editor: %s"), *OpenAsset));
			}
			for (const FRecentUiEntry& Ui : S.RecentUiEntries)
			{
				FContextCandidateEnvelope E;
				E.Type = UnrealAiContextRankingPolicy::ECandidateType::RecentTab;
				E.SourceId = Ui.StableId;
				E.Payload = Ui.DisplayName;
				E.Features.ActiveBonus = Ui.bCurrentlyActive ? 1.f : 0.f;
				E.Features.ThreadOverlayBonus = Ui.bThreadLocalPreferred ? 1.f : 0.f;
				E.Features.Frequency = static_cast<float>(Ui.SeenCount);
				const double AgeSec = FMath::Max(0.0, (FDateTime::UtcNow() - Ui.LastSeenUtc).GetTotalSeconds());
				E.Features.Recency = static_cast<float>(FMath::Max(0.0, 1.0 - AgeSec / 120.0));
				E.RenderedText = FString::Printf(TEXT("Recent UI focus: [%s] %s%s"),
					*Ui.StableId,
					*Ui.DisplayName,
					Ui.bCurrentlyActive ? TEXT(" [active]") : TEXT(""));
				E.TokenCostEstimate = EstimateTokens(E.RenderedText);
				OutCandidates.Add(MoveTemp(E));
			}
		}

		if (!State.ActiveTodoPlanJson.IsEmpty())
		{
			FContextCandidateEnvelope E;
			E.Type = UnrealAiContextRankingPolicy::ECandidateType::TodoState;
			E.SourceId = TEXT("todo:summary");
			E.Payload = UnrealAiFormatActiveTodoSummary(State.ActiveTodoPlanJson, State.TodoStepsDone);
			if (E.Payload.IsEmpty())
			{
				E.Payload = TEXT("Active todo plan present.");
			}
			E.RenderedText = FString::Printf(TEXT("Todo state: %s"), *E.Payload);
			E.TokenCostEstimate = EstimateTokens(E.RenderedText);
			OutCandidates.Add(MoveTemp(E));
		}

		if (!State.ActiveOrchestrateDagJson.IsEmpty())
		{
			FContextCandidateEnvelope E;
			E.Type = UnrealAiContextRankingPolicy::ECandidateType::OrchestrateState;
			E.SourceId = TEXT("orch:dag");
			E.Payload = TEXT("Active orchestrate DAG present.");
			E.RenderedText = TEXT("Orchestrate state: Active DAG present.");
			E.TokenCostEstimate = EstimateTokens(E.RenderedText);
			OutCandidates.Add(MoveTemp(E));
		}

		if (MemoryService && !Options.UserMessageForComplexity.IsEmpty())
		{
			FUnrealAiMemoryQuery Query;
			Query.QueryText = Options.UserMessageForComplexity;
			Query.bIncludeBodies = false;
			Query.bTitleDescriptionOnly = true;
			Query.bPreferThreadScope = true;
			Query.MaxResults = UnrealAiContextRankingPolicy::MaxMemoryCandidatesScanned;
			Query.MinConfidence = 0.30f;
			TArray<FUnrealAiMemoryQueryResult> Hits;
			MemoryService->QueryRelevantMemories(Query, Hits);
			for (const FUnrealAiMemoryQueryResult& Hit : Hits)
			{
				FContextCandidateEnvelope E;
				E.Type = UnrealAiContextRankingPolicy::ECandidateType::MemorySnippet;
				E.SourceId = FString::Printf(TEXT("memory:%s"), *Hit.IndexRow.Id);
				E.Payload = Hit.IndexRow.Title + TEXT("\n") + Hit.IndexRow.Description;
				E.Features.HeuristicSemantic = Hit.Score;
				E.Features.FreshnessReliability = FMath::Clamp(Hit.IndexRow.Confidence, 0.f, 1.f);
				E.Features.Frequency = static_cast<float>(Hit.IndexRow.UseCount);
				const double AgeSec = FMath::Max(0.0, (FDateTime::UtcNow() - Hit.IndexRow.UpdatedAtUtc).GetTotalSeconds());
				E.Features.Recency = static_cast<float>(FMath::Max(0.0, 1.0 - AgeSec / (3600.0 * 24.0 * 30.0)));
				E.RenderedText = FString::Printf(TEXT("Memory: %s\n%s"), *Hit.IndexRow.Title, *Hit.IndexRow.Description);
				E.TokenCostEstimate = EstimateTokens(E.RenderedText);
				OutCandidates.Add(MoveTemp(E));
			}
		}
	}

	void FilterHardPolicy(TArray<FContextCandidateEnvelope>& Candidates, const FAgentContextBuildOptions& Options)
	{
		for (FContextCandidateEnvelope& C : Candidates)
		{
			if (C.bDropped)
			{
				continue;
			}
			if (C.Type == UnrealAiContextRankingPolicy::ECandidateType::Attachment && !Options.bIncludeAttachments)
			{
				C.bDropped = true;
				C.DropReason = TEXT("policy:attachments_disabled");
				continue;
			}
			if (C.Type == UnrealAiContextRankingPolicy::ECandidateType::ToolResult
				&& (!Options.bIncludeToolResults || Options.Mode == EUnrealAiAgentMode::Ask))
			{
				C.bDropped = true;
				C.DropReason = TEXT("policy:tool_results_disabled_for_mode");
				continue;
			}
			if ((C.Type == UnrealAiContextRankingPolicy::ECandidateType::EditorSnapshotField
					|| C.Type == UnrealAiContextRankingPolicy::ECandidateType::RecentTab)
				&& !Options.bIncludeEditorSnapshot)
			{
				C.bDropped = true;
				C.DropReason = TEXT("policy:editor_snapshot_disabled");
				continue;
			}
			if (C.Type == UnrealAiContextRankingPolicy::ECandidateType::Attachment && C.bImageLikeAttachment && !Options.bModelSupportsImages)
			{
				C.bDropped = true;
				C.DropReason = TEXT("policy:model_no_image_support");
				C.Features.SafetyRisk = 1.f;
			}
		}
	}

	void ScoreCandidates(TArray<FContextCandidateEnvelope>& Candidates, const FAgentContextBuildOptions& Options)
	{
		const UnrealAiContextRankingPolicy::FScoreWeights W = UnrealAiContextRankingPolicy::GetScoreWeights();
		const FString UserLower = Options.UserMessageForComplexity.ToLower();
		const TSet<FString> UserTokens = BuildUserTokens(Options.UserMessageForComplexity);

		for (FContextCandidateEnvelope& C : Candidates)
		{
			if (C.bDropped)
			{
				continue;
			}
			const float Base = UnrealAiContextRankingPolicy::GetBaseTypeImportance(C.Type);
			const FString PayloadLower = C.Payload.ToLower();

			const float Mention = ComputeMentionHit(UserLower, PayloadLower);
			const float Sem = ComputeHeuristicSemantic(UserTokens, C.Payload);
			// TODO(embeddings): replace/augment heuristic semantic with embedding/vector retrieval score.
			const float Rec = FMath::Clamp(C.Features.Recency, 0.f, 1.f);
			const float Fresh = FMath::Clamp(C.Features.FreshnessReliability, 0.f, 1.f);
			const float Safety = FMath::Clamp(C.Features.SafetyRisk, 0.f, 1.f);

			C.Score.Base = Base;
			C.Score.MentionHit = Mention * W.MentionHit;
			C.Score.HeuristicSemantic = Sem * W.HeuristicSemantic;
			if (C.Type == UnrealAiContextRankingPolicy::ECandidateType::MemorySnippet)
			{
				C.Score.HeuristicSemantic = FMath::Max(C.Score.HeuristicSemantic, C.Features.HeuristicSemantic * W.HeuristicSemantic);
			}
			C.Score.Recency = Rec * W.Recency;
			C.Score.FreshnessReliability = Fresh * W.FreshnessReliability;
			C.Score.SafetyPenalty = Safety * W.SafetyPenalty;
			C.Score.ActiveBonus = FMath::Clamp(C.Features.ActiveBonus, 0.f, 1.f) * W.ActiveBonus;
			C.Score.ThreadOverlayBonus = FMath::Clamp(C.Features.ThreadOverlayBonus, 0.f, 1.f) * W.ThreadOverlayBonus;
			C.Score.Frequency = FMath::Clamp(C.Features.Frequency, 0.f, 30.f) * W.Frequency;
			C.Score.Total =
				C.Score.Base + C.Score.MentionHit + C.Score.HeuristicSemantic + C.Score.Recency +
				C.Score.FreshnessReliability + C.Score.SafetyPenalty + C.Score.ActiveBonus +
				C.Score.ThreadOverlayBonus + C.Score.Frequency;
		}
	}

	void PackCandidatesUnderBudget(TArray<FContextCandidateEnvelope>& Candidates, const int32 BudgetChars, FUnifiedContextBuildResult& OutResult)
	{
		TArray<int32> Idx;
		Idx.Reserve(Candidates.Num());
		for (int32 i = 0; i < Candidates.Num(); ++i)
		{
			if (!Candidates[i].bDropped)
			{
				Idx.Add(i);
			}
		}
		Idx.Sort([&Candidates](const int32 A, const int32 B)
		{
			if (!FMath::IsNearlyEqual(Candidates[A].Score.Total, Candidates[B].Score.Total))
			{
				return Candidates[A].Score.Total > Candidates[B].Score.Total;
			}
			return Candidates[A].TokenCostEstimate < Candidates[B].TokenCostEstimate;
		});

		TMap<UnrealAiContextRankingPolicy::ECandidateType, int32> Counts;
		int32 UsedChars = 0;
		for (const int32 I : Idx)
		{
			FContextCandidateEnvelope& C = Candidates[I];
			const int32 Cap = UnrealAiContextRankingPolicy::GetPerTypeCap(C.Type);
			const int32 Cur = Counts.FindRef(C.Type);
			if (Cap > 0 && Cur >= Cap)
			{
				C.bDropped = true;
				C.DropReason = TEXT("pack:per_type_cap");
				continue;
			}
			const int32 CandidateChars = C.RenderedText.Len() + 2;
			if ((UsedChars + CandidateChars + UnrealAiContextRankingPolicy::MinBudgetReserveChars) > BudgetChars)
			{
				C.bDropped = true;
				C.DropReason = TEXT("pack:budget");
				continue;
			}
			if (C.Type == UnrealAiContextRankingPolicy::ECandidateType::MemorySnippet
				&& C.Score.Total < UnrealAiContextRankingPolicy::MinMemoryCandidateScoreToPack)
			{
				C.bDropped = true;
				C.DropReason = TEXT("pack:memory_min_score");
				continue;
			}
			UsedChars += CandidateChars;
			Counts.Add(C.Type, Cur + 1);
			OutResult.Packed.Add(C);
		}

		for (const FContextCandidateEnvelope& C : Candidates)
		{
			if (C.bDropped)
			{
				OutResult.Dropped.Add(C);
			}
		}

		FString Out;
		for (int32 i = 0; i < OutResult.Packed.Num(); ++i)
		{
			if (i > 0)
			{
				Out += TEXT("\n\n");
			}
			Out += OutResult.Packed[i].RenderedText;
		}
		OutResult.ContextBlock = Out.TrimEnd();
	}

	FUnifiedContextBuildResult BuildUnifiedContext(
		const FAgentContextState& State,
		const FAgentContextBuildOptions& Options,
		IUnrealAiMemoryService* MemoryService,
		const int32 BudgetChars)
	{
		FUnifiedContextBuildResult R;
		TArray<FContextCandidateEnvelope> Candidates;
		CollectCandidates(State, Options, MemoryService, Candidates);
		FilterHardPolicy(Candidates, Options);
		ScoreCandidates(Candidates, Options);
		PackCandidatesUnderBudget(Candidates, BudgetChars, R);

		R.TraceLines.Add(FString::Printf(TEXT("[Ranker] candidates_total=%d packed=%d dropped=%d budget_chars=%d emitted_chars=%d"),
			Candidates.Num(),
			R.Packed.Num(),
			R.Dropped.Num(),
			BudgetChars,
			R.ContextBlock.Len()));
		for (const FContextCandidateEnvelope& P : R.Packed)
		{
			R.TraceLines.Add(FString::Printf(TEXT("[Ranker][keep] type=%s score=%.2f tokens=%d src=%s"),
				UnrealAiContextRankingPolicy::CandidateTypeName(P.Type),
				P.Score.Total,
				P.TokenCostEstimate,
				*P.SourceId));
		}
		for (const FContextCandidateEnvelope& D : R.Dropped)
		{
			R.TraceLines.Add(FString::Printf(TEXT("[Ranker][drop] type=%s reason=%s score=%.2f src=%s"),
				UnrealAiContextRankingPolicy::CandidateTypeName(D.Type),
				*D.DropReason,
				D.Score.Total,
				*D.SourceId));
		}
		return R;
	}
}
