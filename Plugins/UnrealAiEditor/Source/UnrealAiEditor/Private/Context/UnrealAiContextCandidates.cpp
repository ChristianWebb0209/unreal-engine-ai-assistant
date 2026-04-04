#include "Context/UnrealAiContextCandidates.h"

#include "Context/UnrealAiContextIngestion.h"
#include "Context/UnrealAiContextRankingPolicy.h"
#include "Memory/IUnrealAiMemoryService.h"
#include "Misc/Paths.h"
#include "Retrieval/UnrealAiRetrievalTypes.h"

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

		static TArray<FString> ExtractExplicitTagHints(const FString& UserText)
		{
			TArray<FString> Out;
			if (UserText.IsEmpty())
			{
				return Out;
			}
			TSet<FString> Seen;
			FString Copy = UserText;
			Copy.ReplaceInline(TEXT("\r"), TEXT(" "));
			Copy.ReplaceInline(TEXT("\n"), TEXT(" "));
			TArray<FString> Parts;
			Copy.ParseIntoArray(Parts, TEXT(" "), true);
			for (FString P : Parts)
			{
				P.TrimStartAndEndInline();
				if (!P.StartsWith(TEXT("#")))
				{
					continue;
				}
				P = P.RightChop(1).ToLower();
				while (!P.IsEmpty() && !FChar::IsAlnum(P[P.Len() - 1]) && P[P.Len() - 1] != TEXT('_') && P[P.Len() - 1] != TEXT('-'))
				{
					P.LeftChopInline(1);
				}
				if (P.Len() < 2)
				{
					continue;
				}
				if (!Seen.Contains(P))
				{
					Seen.Add(P);
					Out.Add(P);
				}
			}
			return Out;
		}

		static bool HasLikelyActionablePath(const FString& Text)
		{
			return Text.Contains(TEXT("/Game/"))
				|| Text.Contains(TEXT("PersistentLevel."))
				|| Text.Contains(TEXT("/Script/"))
				|| Text.Contains(TEXT("object_path"))
				|| Text.Contains(TEXT("actor_path"))
				|| Text.Contains(TEXT("blueprint_path"))
				|| Text.Contains(TEXT("material_path"));
		}

		static bool IsLikelyMutatingToolName(const FString& ToolName)
		{
			const FString T = ToolName.ToLower();
			return T.Contains(TEXT("_set_"))
				|| T.Contains(TEXT("_apply"))
				|| T.Contains(TEXT("_compile"))
				|| T.Contains(TEXT("_save"))
				|| T.Contains(TEXT("_create"))
				|| T.Contains(TEXT("_rename"))
				|| T.Contains(TEXT("_open_editor"))
				|| T.Contains(TEXT("pie_start"))
				|| T.Contains(TEXT("pie_stop"));
		}

		static FString ExtractToolNameFromSourceId(const FString& SourceId)
		{
			const int32 LastColon = SourceId.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (LastColon == INDEX_NONE || LastColon + 1 >= SourceId.Len())
			{
				return FString();
			}
			return SourceId.Mid(LastColon + 1);
		}

		static FString ExtractSummaryFolderKey(const FString& SourceId)
		{
			if (SourceId.StartsWith(TEXT("virtual://summary/directory")))
			{
				return SourceId.RightChop(FCString::Strlen(TEXT("virtual://summary/directory")));
			}
			if (SourceId.StartsWith(TEXT("virtual://summary/asset_family")))
			{
				FString Tail = SourceId.RightChop(FCString::Strlen(TEXT("virtual://summary/asset_family")));
				int32 Slash = INDEX_NONE;
				if (Tail.FindChar(TEXT('/'), Slash) && Slash > 0)
				{
					return Tail.Left(Slash);
				}
				return Tail;
			}
			return FString();
		}
	}

	void CollectCandidates(
		const FAgentContextState& State,
		const FAgentContextBuildOptions& Options,
		IUnrealAiMemoryService* MemoryService,
		const TArray<FUnrealAiRetrievalSnippet>* RetrievalSnippets,
		const FProjectTreeSummary* ProjectTreeForIngestion,
		TArray<FContextCandidateEnvelope>& OutCandidates)
	{
		UnrealAiContextIngestion::AppendAllCandidates(
			State,
			Options,
			MemoryService,
			RetrievalSnippets,
			ProjectTreeForIngestion,
			OutCandidates);
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
					|| C.Type == UnrealAiContextRankingPolicy::ECandidateType::RecentTab
					|| C.Type == UnrealAiContextRankingPolicy::ECandidateType::ProjectTreeSummary
					|| C.Type == UnrealAiContextRankingPolicy::ECandidateType::WorkingSetAsset
					|| C.Type == UnrealAiContextRankingPolicy::ECandidateType::ThreadAssetL1Blurb)
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
			C.Score.VectorSimilarity = FMath::Clamp(C.Features.VectorSimilarity, 0.f, 1.f) * W.VectorSimilarity;
			C.Score.ThreadScope = C.Features.ThreadScope * W.ThreadScope;
			C.Score.Total =
				C.Score.Base + C.Score.MentionHit + C.Score.HeuristicSemantic + C.Score.Recency +
				C.Score.FreshnessReliability + C.Score.SafetyPenalty + C.Score.ActiveBonus +
				C.Score.ThreadOverlayBonus + C.Score.Frequency + C.Score.VectorSimilarity + C.Score.ThreadScope;
		}
	}

	void PackCandidatesUnderBudget(
		TArray<FContextCandidateEnvelope>& Candidates,
		const int32 BudgetChars,
		const FAgentContextBuildOptions& Options,
		FUnifiedContextBuildResult& OutResult)
	{
		auto ApplyRelationNeighborhoodCompression = [&Candidates, &Options](FContextCandidateEnvelope& InOutCandidate)
		{
			if (InOutCandidate.Type != UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet)
			{
				return;
			}
			if (InOutCandidate.RetrievalRepresentationLevel == ERetrievalRepresentationLevel::L0)
			{
				return;
			}
			if (InOutCandidate.Features.VectorSimilarity < 0.45f)
			{
				return;
			}

			TArray<FString> Neighbors;
			if (!InOutCandidate.EntityId.IsEmpty())
			{
				if (const TArray<FString>* ExplicitNeighbors = Options.RetrievalNeighborsByEntity.Find(InOutCandidate.EntityId))
				{
					Neighbors = *ExplicitNeighbors;
				}
			}

			if (Neighbors.Num() == 0)
			{
				const FString FolderKey = ExtractSummaryFolderKey(InOutCandidate.SourceId);
				if (FolderKey.IsEmpty())
				{
					return;
				}
				const int32 MaxNeighbors = 8;
				for (const FContextCandidateEnvelope& C : Candidates)
				{
					if (&C == &InOutCandidate || C.Type != UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet)
					{
						continue;
					}
					if (C.SourceId.Contains(FolderKey))
					{
						Neighbors.Add(C.SourceId);
						if (Neighbors.Num() >= MaxNeighbors)
						{
							break;
						}
					}
				}
			}
			if (Neighbors.Num() == 0)
			{
				return;
			}
			InOutCandidate.RenderedText += TEXT("\nRelated neighborhood (R=1, capped): ");
			InOutCandidate.RenderedText += FString::Join(Neighbors, TEXT(", "));
			InOutCandidate.TokenCostEstimate = EstimateTokens(InOutCandidate.RenderedText);
		};

		const int32 UserCharCount = Options.UserMessageForComplexity.Len();
		const int32 UserTokenCount = BuildUserTokens(Options.UserMessageForComplexity).Num();
		const bool bShortPrompt =
			(UserCharCount > 0)
			&& (UserTokenCount <= UnrealAiContextRankingPolicy::ShortPromptUserTokenThreshold
				|| UserCharCount <= UnrealAiContextRankingPolicy::ShortPromptCharThreshold);
		const UnrealAiContextRankingPolicy::FPerTypeBudgetCaps PerTypeCaps = UnrealAiContextRankingPolicy::GetPerTypeBudgetCaps();
		const float Agg = FMath::Clamp(Options.ContextAggression, 0.f, 1.f);
		const float SoftFillFraction = FMath::Lerp(0.4f, 0.8f, Agg);
		const int32 AggressiveMaxPacked = FMath::RoundToInt(FMath::Lerp(16.0f, 48.0f, Agg));
		const float MinMemoryScoreGate = FMath::Lerp(10.0f, 6.0f, Agg);
		const float MinRetrievalScoreGate = FMath::Lerp(8.0f, 4.0f, Agg);
		const int32 RetrievalCapAggressive = FMath::RoundToInt(FMath::Lerp(4.0f, 8.0f, Agg));
		OutResult.PromptCharCount = UserCharCount;
		OutResult.PromptTokenCount = UserTokenCount;
		OutResult.bShortPrompt = bShortPrompt;
		OutResult.MemorySnippetCapApplied = bShortPrompt ? PerTypeCaps.MaxMemorySnippetShortPrompt : PerTypeCaps.MaxMemorySnippet;
		OutResult.SoftBudgetCharsApplied = FMath::Min(
			BudgetChars,
			FMath::Max(
				UnrealAiContextRankingPolicy::MinSoftBudgetChars,
				static_cast<int32>(static_cast<float>(BudgetChars) * SoftFillFraction)));
		OutResult.MaxPackedCandidatesApplied = AggressiveMaxPacked;

		TArray<int32> Idx;
		Idx.Reserve(Candidates.Num());
		for (int32 i = 0; i < Candidates.Num(); ++i)
		{
			if (!Candidates[i].bDropped)
			{
				Idx.Add(i);
			}
		}

		// Anchor minimums: include a tiny set of "always important" live editor context first (if present),
		// even when we are under heavy candidate pressure.
		//
		// We intentionally do NOT guarantee minimums for every type; only a few live signals.
		TArray<bool> bPacked;
		bPacked.Init(false, Candidates.Num());
		TMap<UnrealAiContextRankingPolicy::ECandidateType, int32> Counts;
		int32 UsedChars = 0;
		int32 LongTailFloorKept = 0;

		auto TryPackIdx = [&](const int32 Index, const TCHAR* DropReasonIfAny) -> bool
		{
			if (!Candidates.IsValidIndex(Index))
			{
				return false;
			}
			FContextCandidateEnvelope& C = Candidates[Index];
			if (C.bDropped || bPacked[Index])
			{
				return false;
			}
			int32 Cap = UnrealAiContextRankingPolicy::GetPerTypeCap(C.Type);
			if (C.Type == UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet)
			{
				Cap = RetrievalCapAggressive;
			}
			if (bShortPrompt && C.Type == UnrealAiContextRankingPolicy::ECandidateType::MemorySnippet)
			{
				Cap = PerTypeCaps.MaxMemorySnippetShortPrompt;
			}
			const int32 Cur = Counts.FindRef(C.Type);
			const bool bAllowLongTailCapBypass =
				(C.Type == UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet)
				&& C.bRetrievalLongTailFloorCandidate
				&& LongTailFloorKept < Options.RetrievalLongTailFloorCount;
			if (Cap > 0 && Cur >= Cap && !bAllowLongTailCapBypass)
			{
				C.bDropped = true;
				C.DropReason = TEXT("pack:per_type_cap");
				return false;
			}
			const int32 CandidateChars = C.RenderedText.Len() + 2;
			if ((UsedChars + CandidateChars + UnrealAiContextRankingPolicy::MinBudgetReserveChars) > BudgetChars)
			{
				C.bDropped = true;
				C.DropReason = DropReasonIfAny ? DropReasonIfAny : TEXT("pack:budget");
				return false;
			}
			UsedChars += CandidateChars;
			Counts.Add(C.Type, Cur + 1);
			OutResult.Packed.Add(C);
			bPacked[Index] = true;
			if (C.Type == UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet && C.bRetrievalLongTailFloorCandidate)
			{
				++LongTailFloorKept;
			}
			if (C.Type == UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet)
			{
				if (C.RetrievalRepresentationLevel == ERetrievalRepresentationLevel::L0)
				{
					++OutResult.PackedRetrievalL0Count;
				}
				else if (C.RetrievalRepresentationLevel == ERetrievalRepresentationLevel::L1)
				{
					++OutResult.PackedRetrievalL1Count;
				}
				else
				{
					++OutResult.PackedRetrievalL2Count;
				}
			}
			return true;
		};

		// - One active recent tab (best-effort): ensures the model has the "what I'm looking at" anchor.
		{
			int32 BestActiveRecent = INDEX_NONE;
			float BestScore = -FLT_MAX;
			for (const int32 I : Idx)
			{
				const FContextCandidateEnvelope& C = Candidates[I];
				if (C.Type != UnrealAiContextRankingPolicy::ECandidateType::RecentTab)
				{
					continue;
				}
				if (C.Features.ActiveBonus <= 0.f)
				{
					continue;
				}
				if (C.Score.Total > BestScore)
				{
					BestScore = C.Score.Total;
					BestActiveRecent = I;
				}
			}
			if (BestActiveRecent != INDEX_NONE)
			{
				TryPackIdx(BestActiveRecent, TEXT("pack:budget"));
			}
		}

		// - Two key snapshot fields: selected actors + content browser path (best-effort).
		{
			int32 SelectedActors = INDEX_NONE;
			int32 ContentBrowserPath = INDEX_NONE;
			for (const int32 I : Idx)
			{
				const FContextCandidateEnvelope& C = Candidates[I];
				if (C.Type != UnrealAiContextRankingPolicy::ECandidateType::EditorSnapshotField)
				{
					continue;
				}
				if (C.SourceId == TEXT("snap:selected_actors"))
				{
					SelectedActors = I;
				}
				else if (C.SourceId == TEXT("snap:cb_path"))
				{
					ContentBrowserPath = I;
				}
			}
			if (SelectedActors != INDEX_NONE)
			{
				TryPackIdx(SelectedActors, TEXT("pack:budget"));
			}
			if (ContentBrowserPath != INDEX_NONE)
			{
				TryPackIdx(ContentBrowserPath, TEXT("pack:budget"));
			}
		}
		// - Up to two actionable tool result anchors (best-effort): preserve recently resolved concrete targets.
		//   Prefer a mutating-tool target anchor first, then a discovery/read anchor, to reduce rediscovery loops.
		{
			int32 BestMutatingActionableTool = INDEX_NONE;
			float BestMutatingScore = -FLT_MAX;
			int32 BestDiscoveryActionableTool = INDEX_NONE;
			float BestDiscoveryScore = -FLT_MAX;
			for (const int32 I : Idx)
			{
				const FContextCandidateEnvelope& C = Candidates[I];
				if (C.Type != UnrealAiContextRankingPolicy::ECandidateType::ToolResult)
				{
					continue;
				}
				if (!HasLikelyActionablePath(C.Payload))
				{
					continue;
				}
				const FString ToolName = ExtractToolNameFromSourceId(C.SourceId);
				if (IsLikelyMutatingToolName(ToolName))
				{
					if (C.Score.Total > BestMutatingScore)
					{
						BestMutatingScore = C.Score.Total;
						BestMutatingActionableTool = I;
					}
				}
				else
				{
					if (C.Score.Total > BestDiscoveryScore)
					{
						BestDiscoveryScore = C.Score.Total;
						BestDiscoveryActionableTool = I;
					}
				}
			}
			if (BestMutatingActionableTool != INDEX_NONE)
			{
				TryPackIdx(BestMutatingActionableTool, TEXT("pack:budget_anchor_actionable_target"));
			}
			if (BestDiscoveryActionableTool != INDEX_NONE
				&& BestDiscoveryActionableTool != BestMutatingActionableTool)
			{
				TryPackIdx(BestDiscoveryActionableTool, TEXT("pack:budget_anchor_actionable_target"));
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

		for (const int32 I : Idx)
		{
			FContextCandidateEnvelope& C = Candidates[I];
			if (bPacked.IsValidIndex(I) && bPacked[I])
			{
				continue;
			}
			if (OutResult.MaxPackedCandidatesApplied > 0 && OutResult.Packed.Num() >= OutResult.MaxPackedCandidatesApplied)
			{
				C.bDropped = true;
				C.DropReason = TEXT("pack:max_candidates");
				continue;
			}
			int32 Cap = UnrealAiContextRankingPolicy::GetPerTypeCap(C.Type);
			if (C.Type == UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet)
			{
				Cap = RetrievalCapAggressive;
			}
			if (bShortPrompt && C.Type == UnrealAiContextRankingPolicy::ECandidateType::MemorySnippet)
			{
				Cap = PerTypeCaps.MaxMemorySnippetShortPrompt;
			}
			const int32 Cur = Counts.FindRef(C.Type);
			const bool bAllowLongTailCapBypass =
				(C.Type == UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet)
				&& C.bRetrievalLongTailFloorCandidate
				&& LongTailFloorKept < Options.RetrievalLongTailFloorCount;
			if (Cap > 0 && Cur >= Cap && !bAllowLongTailCapBypass)
			{
				C.bDropped = true;
				C.DropReason = TEXT("pack:per_type_cap");
				continue;
			}
			const int32 CandidateChars = C.RenderedText.Len() + 2;
			if ((UsedChars + CandidateChars + UnrealAiContextRankingPolicy::MinBudgetReserveChars) > OutResult.SoftBudgetCharsApplied)
			{
				C.bDropped = true;
				C.DropReason = TEXT("pack:soft_budget");
				continue;
			}
			if ((UsedChars + CandidateChars + UnrealAiContextRankingPolicy::MinBudgetReserveChars) > BudgetChars)
			{
				C.bDropped = true;
				C.DropReason = TEXT("pack:budget");
				continue;
			}
			if (C.Type == UnrealAiContextRankingPolicy::ECandidateType::MemorySnippet
				&& C.Score.Total < MinMemoryScoreGate)
			{
				C.bDropped = true;
				C.DropReason = TEXT("pack:memory_min_score");
				continue;
			}
			if (C.Type == UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet
				&& C.Score.Total < MinRetrievalScoreGate
				&& !(C.bRetrievalLongTailFloorCandidate && LongTailFloorKept < Options.RetrievalLongTailFloorCount))
			{
				C.bDropped = true;
				C.DropReason = TEXT("pack:retrieval_min_score");
				continue;
			}
			UsedChars += CandidateChars;
			Counts.Add(C.Type, Cur + 1);
			ApplyRelationNeighborhoodCompression(C);
			OutResult.Packed.Add(C);
			if (C.Type == UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet && C.bRetrievalLongTailFloorCandidate)
			{
				++LongTailFloorKept;
			}
			if (C.Type == UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet)
			{
				if (C.RetrievalRepresentationLevel == ERetrievalRepresentationLevel::L0)
				{
					++OutResult.PackedRetrievalL0Count;
				}
				else if (C.RetrievalRepresentationLevel == ERetrievalRepresentationLevel::L1)
				{
					++OutResult.PackedRetrievalL1Count;
				}
				else
				{
					++OutResult.PackedRetrievalL2Count;
				}
			}
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
		const TArray<FUnrealAiRetrievalSnippet>* RetrievalSnippets,
		const int32 BudgetChars,
		const FProjectTreeSummary* ProjectTreeForIngestion)
	{
		FUnifiedContextBuildResult R;
		TArray<FContextCandidateEnvelope> Candidates;
		CollectCandidates(State, Options, MemoryService, RetrievalSnippets, ProjectTreeForIngestion, Candidates);
		FilterHardPolicy(Candidates, Options);
		ScoreCandidates(Candidates, Options);
		PackCandidatesUnderBudget(Candidates, BudgetChars, Options, R);

		R.TraceLines.Add(FString::Printf(TEXT("[Ranker] candidates_total=%d packed=%d dropped=%d budget_chars=%d emitted_chars=%d"),
			Candidates.Num(),
			R.Packed.Num(),
			R.Dropped.Num(),
			BudgetChars,
			R.ContextBlock.Len()));
		{
			int32 PackedRetrievalSummary = 0;
			int32 PackedRetrievalMember = 0;
			int32 PackedRetrievalLongTailFloor = 0;
			float UtilityMultiplierSum = 0.0f;
			int32 UtilityMultiplierCount = 0;
			for (const FContextCandidateEnvelope& P : R.Packed)
			{
				if (P.Type != UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet)
				{
					continue;
				}
				if (P.SourceId.StartsWith(TEXT("virtual://summary/")))
				{
					++PackedRetrievalSummary;
				}
				else
				{
					++PackedRetrievalMember;
				}
				if (P.bRetrievalLongTailFloorCandidate)
				{
					++PackedRetrievalLongTailFloor;
				}
				UtilityMultiplierSum += P.Features.UtilityMultiplier;
				++UtilityMultiplierCount;
			}
			const float UtilityMultiplierAvg = UtilityMultiplierCount > 0
				? (UtilityMultiplierSum / static_cast<float>(UtilityMultiplierCount))
				: 0.0f;
			R.TraceLines.Add(FString::Printf(
				TEXT("[Ranker] retrieval_packed summary=%d member=%d long_tail_floor=%d utility_mult_avg=%.2f"),
				PackedRetrievalSummary,
				PackedRetrievalMember,
				PackedRetrievalLongTailFloor,
				UtilityMultiplierAvg));
		}
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
