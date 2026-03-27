#include "Context/UnrealAiContextCandidates.h"

#include "Context/UnrealAiActiveTodoSummary.h"
#include "Context/UnrealAiContextRankingPolicy.h"
#include "Context/UnrealAiEditorContextQueries.h"
#include "Memory/IUnrealAiMemoryService.h"
#include "Misc/EngineVersion.h"
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

		static bool IsDiscoveryToolName(const FString& ToolName)
		{
			const FString T = ToolName.ToLower();
			return T.Contains(TEXT("fuzzy_search"))
				|| T.Contains(TEXT("_query"))
				|| T.Contains(TEXT("_read"))
				|| T.Contains(TEXT("snapshot"))
				|| T == TEXT("editor_get_selection")
				|| T == TEXT("asset_registry_query")
				|| T == TEXT("blueprint_export_ir");
		}

		static bool HasLikelyActionablePath(const FString& Text)
		{
			return Text.Contains(TEXT("/Game/"))
				|| Text.Contains(TEXT("PersistentLevel."))
				|| Text.Contains(TEXT("/Script/"));
		}
	}

	void CollectCandidates(
		const FAgentContextState& State,
		const FAgentContextBuildOptions& Options,
		IUnrealAiMemoryService* MemoryService,
		const TArray<FUnrealAiRetrievalSnippet>* RetrievalSnippets,
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
			const bool bDiscoveryTool = IsDiscoveryToolName(T.ToolName);
			const bool bHasActionablePath = HasLikelyActionablePath(T.TruncatedResult);
			if (bDiscoveryTool && bHasActionablePath)
			{
				// Keep recently resolved concrete targets around longer so the model can skip redundant re-discovery.
				E.Features.ActiveBonus = 1.f;
				E.Features.ThreadOverlayBonus = 0.75f;
			}
			else if (bDiscoveryTool && !bHasActionablePath)
			{
				// Discovery outputs with no actionable target should decay faster under pressure.
				E.Features.FreshnessReliability *= 0.5f;
			}
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
			Query.PreferredThreadId = Options.ThreadIdForMemory;
			Query.bIncludeBodies = false;
			Query.bTitleDescriptionOnly = true;
			Query.bPreferThreadScope = true;
			Query.MaxResults = UnrealAiContextRankingPolicy::MaxMemoryCandidatesScanned;
			Query.MinConfidence = 0.30f;
			Query.RequiredTags = ExtractExplicitTagHints(Options.UserMessageForComplexity);
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

		if (RetrievalSnippets)
		{
			for (const FUnrealAiRetrievalSnippet& Snippet : *RetrievalSnippets)
			{
				FContextCandidateEnvelope E;
				E.Type = UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet;
				E.SourceId = Snippet.SourceId.IsEmpty() ? Snippet.SnippetId : Snippet.SourceId;
				E.Payload = Snippet.Text;
				E.Features.VectorSimilarity = FMath::Max(0.f, Snippet.Score);
				if (!Snippet.ThreadId.IsEmpty() && !Options.ThreadIdForMemory.IsEmpty())
				{
					E.Features.ThreadScope = Snippet.ThreadId == Options.ThreadIdForMemory
						? UnrealAiContextRankingPolicy::RetrievalInThreadScopeBoost
						: UnrealAiContextRankingPolicy::RetrievalCrossThreadPenalty;
				}
				E.RenderedText = FString::Printf(TEXT("Retrieved context: %s\n%s"), *E.SourceId, *Snippet.Text);
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
		const int32 UserCharCount = Options.UserMessageForComplexity.Len();
		const int32 UserTokenCount = BuildUserTokens(Options.UserMessageForComplexity).Num();
		const bool bShortPrompt =
			(UserCharCount > 0)
			&& (UserTokenCount <= UnrealAiContextRankingPolicy::ShortPromptUserTokenThreshold
				|| UserCharCount <= UnrealAiContextRankingPolicy::ShortPromptCharThreshold);
		const UnrealAiContextRankingPolicy::FPerTypeBudgetCaps PerTypeCaps = UnrealAiContextRankingPolicy::GetPerTypeBudgetCaps();
		OutResult.PromptCharCount = UserCharCount;
		OutResult.PromptTokenCount = UserTokenCount;
		OutResult.bShortPrompt = bShortPrompt;
		OutResult.MemorySnippetCapApplied = bShortPrompt ? PerTypeCaps.MaxMemorySnippetShortPrompt : PerTypeCaps.MaxMemorySnippet;
		OutResult.SoftBudgetCharsApplied = FMath::Min(
			BudgetChars,
			FMath::Max(
				UnrealAiContextRankingPolicy::MinSoftBudgetChars,
				static_cast<int32>(static_cast<float>(BudgetChars) * UnrealAiContextRankingPolicy::SoftContextFillFraction)));
		OutResult.MaxPackedCandidatesApplied = UnrealAiContextRankingPolicy::MaxPackedCandidatesSoft;

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
			if (bShortPrompt && C.Type == UnrealAiContextRankingPolicy::ECandidateType::MemorySnippet)
			{
				Cap = PerTypeCaps.MaxMemorySnippetShortPrompt;
			}
			const int32 Cur = Counts.FindRef(C.Type);
			if (Cap > 0 && Cur >= Cap)
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
		// - One actionable tool result anchor (best-effort): preserve a recently resolved concrete target path.
		{
			int32 BestActionableTool = INDEX_NONE;
			float BestScore = -FLT_MAX;
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
				if (C.Score.Total > BestScore)
				{
					BestScore = C.Score.Total;
					BestActionableTool = I;
				}
			}
			if (BestActionableTool != INDEX_NONE)
			{
				TryPackIdx(BestActionableTool, TEXT("pack:budget"));
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
			if (bShortPrompt && C.Type == UnrealAiContextRankingPolicy::ECandidateType::MemorySnippet)
			{
				Cap = PerTypeCaps.MaxMemorySnippetShortPrompt;
			}
			const int32 Cur = Counts.FindRef(C.Type);
			if (Cap > 0 && Cur >= Cap)
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
				&& C.Score.Total < UnrealAiContextRankingPolicy::MinMemoryCandidateScoreToPack)
			{
				C.bDropped = true;
				C.DropReason = TEXT("pack:memory_min_score");
				continue;
			}
			if (C.Type == UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet
				&& C.Score.Total < UnrealAiContextRankingPolicy::MinRetrievalCandidateScoreToPack)
			{
				C.bDropped = true;
				C.DropReason = TEXT("pack:retrieval_min_score");
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
		const TArray<FUnrealAiRetrievalSnippet>* RetrievalSnippets,
		const int32 BudgetChars)
	{
		FUnifiedContextBuildResult R;
		TArray<FContextCandidateEnvelope> Candidates;
		CollectCandidates(State, Options, MemoryService, RetrievalSnippets, Candidates);
		FilterHardPolicy(Candidates, Options);
		ScoreCandidates(Candidates, Options);
		PackCandidatesUnderBudget(Candidates, BudgetChars, Options, R);

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
