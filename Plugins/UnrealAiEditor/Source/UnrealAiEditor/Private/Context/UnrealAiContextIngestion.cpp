#include "Context/UnrealAiContextIngestion.h"

#include "Context/UnrealAiActiveTodoSummary.h"
#include "Planning/UnrealAiStructuredPlanSummary.h"
#include "Context/UnrealAiContextRankingPolicy.h"
#include "Context/UnrealAiEditorContextQueries.h"
#include "Context/UnrealAiProjectTreeSampler.h"
#include "Memory/IUnrealAiMemoryService.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Retrieval/UnrealAiRetrievalTypes.h"
#include "UnrealAiEditorModule.h"

namespace UnrealAiContextIngestion
{
	namespace
	{
		using UnrealAiContextCandidates::ERetrievalRepresentationLevel;
		using UnrealAiContextCandidates::FContextCandidateEnvelope;

		static int32 EstimateTokens(const FString& Text)
		{
			return FMath::Max(1, Text.Len() / 4);
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
				|| T == TEXT("blueprint_graph_introspect");
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

		static FString BuildEquivalenceClassIdFromName(const FString& RawName)
		{
			FString Name = RawName.ToLower();
			Name.ReplaceInline(TEXT("."), TEXT("_"));
			Name.ReplaceInline(TEXT("-"), TEXT("_"));

			const TArray<FString> Prefixes = { TEXT("bp_"), TEXT("mi_"), TEXT("m_"), TEXT("sm_"), TEXT("sk_"), TEXT("t_") };
			for (const FString& Prefix : Prefixes)
			{
				if (Name.StartsWith(Prefix))
				{
					Name = Name.RightChop(Prefix.Len());
					break;
				}
			}

			const TArray<FString> Suffixes = { TEXT("_c"), TEXT("_inst"), TEXT("_instance"), TEXT("_mat"), TEXT("_mesh"), TEXT("_lod0"), TEXT("_lod1") };
			for (const FString& Suffix : Suffixes)
			{
				if (Name.EndsWith(Suffix))
				{
					Name.LeftChopInline(Suffix.Len());
					break;
				}
			}

			if (Name.IsEmpty())
			{
				return TEXT("name:unknown");
			}
			return FString::Printf(TEXT("name:%s"), *Name);
		}

		static FString DeriveRetrievalEntityIdFromSourceId(const FString& SourceId)
		{
			if (SourceId.IsEmpty())
			{
				return TEXT("entity:unknown");
			}

			if (SourceId.StartsWith(TEXT("virtual://asset_registry/")))
			{
				return TEXT("asset_registry:shard");
			}
			if (SourceId.StartsWith(TEXT("memory:")))
			{
				return TEXT("memory:record");
			}

			FString Normalized = SourceId;
			Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));

			if (Normalized.StartsWith(TEXT("/Game/")) || Normalized.StartsWith(TEXT("/Engine/")))
			{
				FString PackagePart = Normalized;
				int32 DotIndex = INDEX_NONE;
				if (PackagePart.FindChar(TEXT('.'), DotIndex))
				{
					PackagePart = PackagePart.Left(DotIndex);
				}
				int32 LastSlash = INDEX_NONE;
				if (PackagePart.FindLastChar(TEXT('/'), LastSlash) && LastSlash > 0)
				{
					const FString Folder = PackagePart.Left(LastSlash);
					const FString Basename = PackagePart.Mid(LastSlash + 1);
					return FString::Printf(TEXT("asset_family:%s:%s"), *Folder, *BuildEquivalenceClassIdFromName(Basename));
				}
				return FString::Printf(TEXT("asset:%s"), *PackagePart);
			}

			if (Normalized.StartsWith(TEXT("/")) || Normalized.Contains(TEXT("/")))
			{
				const FString Directory = FPaths::GetPath(Normalized);
				return Directory.IsEmpty()
					? FString::Printf(TEXT("file:%s"), *Normalized)
					: FString::Printf(TEXT("folder:%s"), *Directory);
			}

			return FString::Printf(TEXT("source:%s"), *Normalized);
		}

		static FString TouchSourceLabel(const EThreadAssetTouchSource S)
		{
			switch (S)
			{
			case EThreadAssetTouchSource::OpenEditor: return TEXT("open_editor");
			case EThreadAssetTouchSource::ContentBrowserSelection: return TEXT("content_browser");
			case EThreadAssetTouchSource::ToolResult: return TEXT("tool");
			case EThreadAssetTouchSource::Attachment: return TEXT("attachment");
			default: return TEXT("unknown");
			}
		}
	}

	void AppendAllCandidates(
		const FAgentContextState& State,
		const FAgentContextBuildOptions& Options,
		IUnrealAiMemoryService* MemoryService,
		const TArray<FUnrealAiRetrievalSnippet>* RetrievalSnippets,
		const FProjectTreeSummary* ProjectTreeForIngestion,
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
			if (!FUnrealAiEditorModule::IsPieToolsEnabled())
			{
				const FString ToolNameLower = T.ToolName.ToLower();
				if (ToolNameLower.Contains(TEXT("pie_")))
				{
					continue;
				}
			}
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
				E.Features.ActiveBonus = 1.f;
				E.Features.ThreadOverlayBonus = 0.75f;
			}
			else if (bDiscoveryTool && !bHasActionablePath)
			{
				E.Features.FreshnessReliability *= 0.5f;
			}
			if (T.ToolName == TEXT("blueprint_compile") || T.ToolName == TEXT("cpp_project_compile"))
			{
				E.Features.ActiveBonus = FMath::Max(E.Features.ActiveBonus, 0.85f);
				if (T.TruncatedResult.Contains(TEXT("\"ok\": false")) || T.TruncatedResult.Contains(TEXT("\"ok\":false")))
				{
					E.Features.ActiveBonus = 1.f;
				}
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

		for (int32 Wi = 0; Wi < State.ThreadAssetWorkingSet.Num(); ++Wi)
		{
			const FThreadAssetWorkingEntry& W = State.ThreadAssetWorkingSet[Wi];
			if (W.ObjectPath.IsEmpty())
			{
				continue;
			}
			FContextCandidateEnvelope E;
			E.Type = UnrealAiContextRankingPolicy::ECandidateType::WorkingSetAsset;
			E.SourceId = FString::Printf(TEXT("workingset:%d"), Wi);
			E.EntityId = DeriveRetrievalEntityIdFromSourceId(W.ObjectPath);
			E.Payload = W.ObjectPath;
			const double AgeSec = FMath::Max(0.0, (FDateTime::UtcNow() - W.LastTouchedUtc).GetTotalSeconds());
			E.Features.Recency = static_cast<float>(FMath::Max(0.0, 1.0 - AgeSec / 600.0));
			E.Features.FreshnessReliability = E.Features.Recency;
			if (W.TouchSource == EThreadAssetTouchSource::ToolResult)
			{
				E.Features.ThreadOverlayBonus = 0.5f;
			}
			const FString ClassBit = W.AssetClassPath.IsEmpty() ? FString() : FString::Printf(TEXT(" class=%s"), *W.AssetClassPath);
			const FString ToolBit = W.LastToolName.IsEmpty() ? FString() : FString::Printf(TEXT(" via_tool=%s"), *W.LastToolName);
			E.RenderedText = FString::Printf(
				TEXT("Working set (MRU): %s [source=%s]%s%s"),
				*W.ObjectPath,
				*TouchSourceLabel(W.TouchSource),
				*ClassBit,
				*ToolBit);
			E.TokenCostEstimate = EstimateTokens(E.RenderedText);
			OutCandidates.Add(MoveTemp(E));
		}

		{
			const int32 L1Cap = UnrealAiContextRankingPolicy::GetPerTypeBudgetCaps().MaxThreadAssetL1Blurb;
			int32 L1Emitted = 0;
			for (const FThreadAssetWorkingEntry& W : State.ThreadAssetWorkingSet)
			{
				if (L1Emitted >= L1Cap || W.ObjectPath.IsEmpty())
				{
					continue;
				}
				FContextCandidateEnvelope E;
				E.Type = UnrealAiContextRankingPolicy::ECandidateType::ThreadAssetL1Blurb;
				E.SourceId = FString::Printf(TEXT("l1hint:%d"), L1Emitted);
				E.EntityId = DeriveRetrievalEntityIdFromSourceId(W.ObjectPath);
				E.Payload = W.ObjectPath;
				const FString ClassBit = W.AssetClassPath.IsEmpty() ? FString() : FString::Printf(TEXT(" class=%s"), *W.AssetClassPath);
				E.RenderedText = FString::Printf(TEXT("Asset hint (L1): %s%s"), *W.ObjectPath, *ClassBit);
				E.TokenCostEstimate = EstimateTokens(E.RenderedText);
				const double AgeSec = FMath::Max(0.0, (FDateTime::UtcNow() - W.LastTouchedUtc).GetTotalSeconds());
				E.Features.Recency = static_cast<float>(FMath::Max(0.0, 1.0 - AgeSec / 600.0));
				E.Features.FreshnessReliability = E.Features.Recency;
				OutCandidates.Add(MoveTemp(E));
				++L1Emitted;
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

		if (!State.ActivePlanDagJson.IsEmpty())
		{
			FContextCandidateEnvelope E;
			E.Type = UnrealAiContextRankingPolicy::ECandidateType::PlanState;
			E.SourceId = TEXT("plan:dag");
			E.Payload = UnrealAiFormatActivePlanDagSummary(State.ActivePlanDagJson, State.PlanNodeStatusById);
			if (E.Payload.IsEmpty())
			{
				E.Payload = TEXT("Active plan DAG present.");
			}
			E.RenderedText = FString::Printf(TEXT("Plan DAG state: %s"), *E.Payload);
			E.TokenCostEstimate = EstimateTokens(E.RenderedText);
			OutCandidates.Add(MoveTemp(E));
		}

		if (ProjectTreeForIngestion && ProjectTreeForIngestion->UpdatedUtc != FDateTime::MinValue())
		{
			const FString Blurb = UnrealAiProjectTreeSampler::BuildContextBlurb(*ProjectTreeForIngestion);
			if (!Blurb.IsEmpty())
			{
				FContextCandidateEnvelope E;
				E.Type = UnrealAiContextRankingPolicy::ECandidateType::ProjectTreeSummary;
				E.SourceId = TEXT("project_tree:blurb");
				E.Payload = Blurb;
				E.RenderedText = Blurb;
				E.Features.FreshnessReliability = 1.f;
				const double AgeMin = FMath::Max(0.0, (FDateTime::UtcNow() - ProjectTreeForIngestion->UpdatedUtc).GetTotalMinutes());
				E.Features.Recency = static_cast<float>(FMath::Max(0.0, 1.0 - AgeMin / (60.0 * 24.0)));
				E.TokenCostEstimate = EstimateTokens(E.RenderedText);
				OutCandidates.Add(MoveTemp(E));
			}
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
			const FString IntentLower = Options.UserMessageForComplexity.ToLower();
			const bool bIntentAllowsL2 = IntentLower.Contains(TEXT("inspect"))
				|| IntentLower.Contains(TEXT("verify"))
				|| IntentLower.Contains(TEXT("modify"))
				|| IntentLower.Contains(TEXT("edit"))
				|| IntentLower.Contains(TEXT("code"))
				|| IntentLower.Contains(TEXT("blueprint"));
			for (const FUnrealAiRetrievalSnippet& Snippet : *RetrievalSnippets)
			{
				FContextCandidateEnvelope E;
				E.Type = UnrealAiContextRankingPolicy::ECandidateType::RetrievalSnippet;
				E.SourceId = Snippet.SourceId.IsEmpty() ? Snippet.SnippetId : Snippet.SourceId;
				E.EntityId = DeriveRetrievalEntityIdFromSourceId(E.SourceId);
				E.Payload = Snippet.Text;
				float UtilityMultiplier = 1.0f;
				if (const float* FoundMultiplier = Options.RetrievalUtilityMultiplierByEntity.Find(E.EntityId))
				{
					UtilityMultiplier = FMath::Clamp(*FoundMultiplier, 0.0f, 1.0f);
				}
				E.Features.UtilityMultiplier = UtilityMultiplier;
				E.Features.VectorSimilarity = FMath::Max(0.f, Snippet.Score) * UtilityMultiplier;
				E.bRetrievalLongTailFloorCandidate = Options.RetrievalLongTailEntityFloor.Contains(E.EntityId);
				const bool bInHeadSet = Options.RetrievalHeadEntitySet.Contains(E.EntityId);
				if (E.SourceId.StartsWith(TEXT("virtual://summary/")))
				{
					E.RetrievalRepresentationLevel = bInHeadSet
						? ERetrievalRepresentationLevel::L1
						: ERetrievalRepresentationLevel::L0;
				}
				else
				{
					E.RetrievalRepresentationLevel = (bInHeadSet && bIntentAllowsL2)
						? ERetrievalRepresentationLevel::L2
						: ERetrievalRepresentationLevel::L1;
				}
				if (!Snippet.ThreadId.IsEmpty() && !Options.ThreadIdForMemory.IsEmpty())
				{
					E.Features.ThreadScope = Snippet.ThreadId == Options.ThreadIdForMemory
						? UnrealAiContextRankingPolicy::RetrievalInThreadScopeBoost
						: UnrealAiContextRankingPolicy::RetrievalCrossThreadPenalty;
				}
				FString PayloadForLevel = Snippet.Text;
				if (E.RetrievalRepresentationLevel == ERetrievalRepresentationLevel::L0 && PayloadForLevel.Len() > 260)
				{
					PayloadForLevel = PayloadForLevel.Left(260) + TEXT(" …");
				}
				else if (E.RetrievalRepresentationLevel == ERetrievalRepresentationLevel::L1 && PayloadForLevel.Len() > 1200)
				{
					PayloadForLevel = PayloadForLevel.Left(1200) + TEXT(" …");
				}
				const TCHAR* LevelLabel = E.RetrievalRepresentationLevel == ERetrievalRepresentationLevel::L0
					? TEXT("L0")
					: (E.RetrievalRepresentationLevel == ERetrievalRepresentationLevel::L1 ? TEXT("L1") : TEXT("L2"));
				E.RenderedText = FString::Printf(TEXT("Retrieved context (%s): %s\n%s"), LevelLabel, *E.SourceId, *PayloadForLevel);
				E.TokenCostEstimate = EstimateTokens(E.RenderedText);
				OutCandidates.Add(MoveTemp(E));
			}
		}
	}
}
