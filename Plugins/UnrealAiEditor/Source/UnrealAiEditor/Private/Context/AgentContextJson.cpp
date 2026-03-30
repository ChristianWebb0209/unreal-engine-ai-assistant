#include "Context/AgentContextJson.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealAiAgentContextJson
{
	static FString UiKindToStr(const ERecentUiKind K)
	{
		switch (K)
		{
		case ERecentUiKind::SceneViewport: return TEXT("scene_viewport");
		case ERecentUiKind::AssetEditor: return TEXT("asset_editor");
		case ERecentUiKind::DetailsPanel: return TEXT("details_panel");
		case ERecentUiKind::ContentBrowser: return TEXT("content_browser");
		case ERecentUiKind::SceneOutliner: return TEXT("scene_outliner");
		case ERecentUiKind::OutputLog: return TEXT("output_log");
		case ERecentUiKind::BlueprintGraph: return TEXT("blueprint_graph");
		case ERecentUiKind::Inspector: return TEXT("inspector");
		case ERecentUiKind::ToolPanel: return TEXT("tool_panel");
		case ERecentUiKind::NomadTab: return TEXT("nomad_tab");
		default: return TEXT("unknown");
		}
	}

	static ERecentUiKind StrToUiKind(const FString& S)
	{
		if (S == TEXT("scene_viewport")) return ERecentUiKind::SceneViewport;
		if (S == TEXT("asset_editor")) return ERecentUiKind::AssetEditor;
		if (S == TEXT("details_panel")) return ERecentUiKind::DetailsPanel;
		if (S == TEXT("content_browser")) return ERecentUiKind::ContentBrowser;
		if (S == TEXT("scene_outliner")) return ERecentUiKind::SceneOutliner;
		if (S == TEXT("output_log")) return ERecentUiKind::OutputLog;
		if (S == TEXT("blueprint_graph")) return ERecentUiKind::BlueprintGraph;
		if (S == TEXT("inspector")) return ERecentUiKind::Inspector;
		if (S == TEXT("tool_panel")) return ERecentUiKind::ToolPanel;
		if (S == TEXT("nomad_tab")) return ERecentUiKind::NomadTab;
		return ERecentUiKind::Unknown;
	}

	static FString UiSourceToStr(const ERecentUiSource S)
	{
		switch (S)
		{
		case ERecentUiSource::SlateFocus: return TEXT("slate_focus");
		case ERecentUiSource::TabEvent: return TEXT("tab_event");
		case ERecentUiSource::AssetEditorEvent: return TEXT("asset_editor_event");
		case ERecentUiSource::PollFallback: return TEXT("poll_fallback");
		default: return TEXT("unknown");
		}
	}

	static ERecentUiSource StrToUiSource(const FString& S)
	{
		if (S == TEXT("slate_focus")) return ERecentUiSource::SlateFocus;
		if (S == TEXT("tab_event")) return ERecentUiSource::TabEvent;
		if (S == TEXT("asset_editor_event")) return ERecentUiSource::AssetEditorEvent;
		if (S == TEXT("poll_fallback")) return ERecentUiSource::PollFallback;
		return ERecentUiSource::Unknown;
	}

	static TSharedPtr<FJsonObject> RecentUiEntryToJson(const FRecentUiEntry& E)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("stableId"), E.StableId);
		O->SetStringField(TEXT("displayName"), E.DisplayName);
		O->SetStringField(TEXT("uiKind"), UiKindToStr(E.UiKind));
		O->SetStringField(TEXT("source"), UiSourceToStr(E.Source));
		O->SetStringField(TEXT("lastSeenUtc"), E.LastSeenUtc.ToIso8601());
		O->SetNumberField(TEXT("seenCount"), E.SeenCount);
		O->SetBoolField(TEXT("currentlyActive"), E.bCurrentlyActive);
		O->SetBoolField(TEXT("threadLocalPreferred"), E.bThreadLocalPreferred);
		return O;
	}

	static bool JsonToRecentUiEntry(const TSharedPtr<FJsonObject>& O, FRecentUiEntry& Out)
	{
		if (!O.IsValid())
		{
			return false;
		}
		O->TryGetStringField(TEXT("stableId"), Out.StableId);
		O->TryGetStringField(TEXT("displayName"), Out.DisplayName);
		FString K;
		O->TryGetStringField(TEXT("uiKind"), K);
		Out.UiKind = StrToUiKind(K);
		FString Src;
		O->TryGetStringField(TEXT("source"), Src);
		Out.Source = StrToUiSource(Src);
		FString Ts;
		if (O->TryGetStringField(TEXT("lastSeenUtc"), Ts))
		{
			FDateTime::ParseIso8601(*Ts, Out.LastSeenUtc);
		}
		Out.SeenCount = O->HasField(TEXT("seenCount")) ? static_cast<int32>(O->GetNumberField(TEXT("seenCount"))) : 1;
		O->TryGetBoolField(TEXT("currentlyActive"), Out.bCurrentlyActive);
		O->TryGetBoolField(TEXT("threadLocalPreferred"), Out.bThreadLocalPreferred);
		return !Out.StableId.IsEmpty();
	}

	static FString TypeToStr(EContextAttachmentType T)
	{
		switch (T)
		{
		case EContextAttachmentType::AssetPath: return TEXT("asset");
		case EContextAttachmentType::FilePath: return TEXT("file");
		case EContextAttachmentType::FreeText: return TEXT("text");
		case EContextAttachmentType::BlueprintNodeRef: return TEXT("bp_node");
		case EContextAttachmentType::ActorReference: return TEXT("actor");
		case EContextAttachmentType::ContentFolder: return TEXT("folder");
		default: return TEXT("asset");
		}
	}

	static bool StrToType(const FString& S, EContextAttachmentType& Out)
	{
		if (S == TEXT("file"))
		{
			Out = EContextAttachmentType::FilePath;
			return true;
		}
		if (S == TEXT("text"))
		{
			Out = EContextAttachmentType::FreeText;
			return true;
		}
		if (S == TEXT("bp_node"))
		{
			Out = EContextAttachmentType::BlueprintNodeRef;
			return true;
		}
		if (S == TEXT("actor"))
		{
			Out = EContextAttachmentType::ActorReference;
			return true;
		}
		if (S == TEXT("folder"))
		{
			Out = EContextAttachmentType::ContentFolder;
			return true;
		}
		Out = EContextAttachmentType::AssetPath;
		return true;
	}

	static TSharedPtr<FJsonObject> ProjectTreePrefToJson(const FProjectTreePathPreference& P)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("assetFamily"), P.AssetFamily);
		O->SetStringField(TEXT("packagePath"), P.PackagePath);
		O->SetNumberField(TEXT("confidence"), P.Confidence);
		O->SetNumberField(TEXT("observedCount"), P.ObservedCount);
		return O;
	}

	static void JsonToProjectTreePref(const TSharedPtr<FJsonObject>& O, FProjectTreePathPreference& Out)
	{
		if (!O.IsValid())
		{
			return;
		}
		O->TryGetStringField(TEXT("assetFamily"), Out.AssetFamily);
		O->TryGetStringField(TEXT("packagePath"), Out.PackagePath);
		if (O->HasField(TEXT("confidence")))
		{
			Out.Confidence = O->GetNumberField(TEXT("confidence"));
		}
		if (O->HasField(TEXT("observedCount")))
		{
			Out.ObservedCount = static_cast<int32>(O->GetNumberField(TEXT("observedCount")));
		}
	}

	bool StateToJson(const FAgentContextState& State, FString& OutJson)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("schemaVersion"), State.SchemaVersionField);

		TArray<TSharedPtr<FJsonValue>> AttArr;
		for (const FContextAttachment& A : State.Attachments)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("type"), TypeToStr(A.Type));
			O->SetStringField(TEXT("payload"), A.Payload);
			O->SetStringField(TEXT("label"), A.Label);
			if (!A.IconClassPath.IsEmpty())
			{
				O->SetStringField(TEXT("iconClass"), A.IconClassPath);
			}
			AttArr.Add(MakeShared<FJsonValueObject>(O.ToSharedRef()));
		}
		Root->SetArrayField(TEXT("attachments"), AttArr);

		TArray<TSharedPtr<FJsonValue>> ToolArr;
		for (const FToolContextEntry& E : State.ToolResults)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("toolName"), E.ToolName);
			O->SetStringField(TEXT("truncatedResult"), E.TruncatedResult);
			O->SetStringField(TEXT("timestamp"), E.Timestamp.ToIso8601());
			ToolArr.Add(MakeShared<FJsonValueObject>(O.ToSharedRef()));
		}
		Root->SetArrayField(TEXT("toolResults"), ToolArr);

		if (State.EditorSnapshot.IsSet())
		{
			const FEditorContextSnapshot& S = State.EditorSnapshot.GetValue();
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("selectedActorsSummary"), S.SelectedActorsSummary);
			E->SetStringField(TEXT("activeAssetPath"), S.ActiveAssetPath);
			E->SetStringField(TEXT("contentBrowserPath"), S.ContentBrowserPath);
			TArray<TSharedPtr<FJsonValue>> SelArr;
			for (const FString& P : S.ContentBrowserSelectedAssets)
			{
				SelArr.Add(MakeShared<FJsonValueString>(P));
			}
			E->SetArrayField(TEXT("contentBrowserSelectedAssets"), SelArr);
			TArray<TSharedPtr<FJsonValue>> OpenArr;
			for (const FString& P : S.OpenEditorAssets)
			{
				OpenArr.Add(MakeShared<FJsonValueString>(P));
			}
			E->SetArrayField(TEXT("openEditorAssets"), OpenArr);
			E->SetStringField(TEXT("activeUiEntryId"), S.ActiveUiEntryId);
			TArray<TSharedPtr<FJsonValue>> RecentArr;
			for (const FRecentUiEntry& R : S.RecentUiEntries)
			{
				RecentArr.Add(MakeShared<FJsonValueObject>(RecentUiEntryToJson(R).ToSharedRef()));
			}
			E->SetArrayField(TEXT("recentUiEntries"), RecentArr);
			E->SetBoolField(TEXT("valid"), S.bValid);
			Root->SetObjectField(TEXT("editorSnapshot"), E);
		}

		Root->SetNumberField(TEXT("maxContextChars"), State.MaxContextChars);

		if (!State.ActiveTodoPlanJson.IsEmpty())
		{
			Root->SetStringField(TEXT("activeTodoPlan"), State.ActiveTodoPlanJson);
		}
		TArray<TSharedPtr<FJsonValue>> DoneArr;
		for (const bool b : State.TodoStepsDone)
		{
			DoneArr.Add(MakeShared<FJsonValueBoolean>(b));
		}
		Root->SetArrayField(TEXT("todoStepsDone"), DoneArr);
		if (!State.ActivePlanDagJson.IsEmpty())
		{
			Root->SetStringField(TEXT("activePlanDag"), State.ActivePlanDagJson);
		}
		TArray<TSharedPtr<FJsonValue>> OrchStatusArr;
		for (const TPair<FString, FString>& Pair : State.PlanNodeStatusById)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("nodeId"), Pair.Key);
			O->SetStringField(TEXT("status"), Pair.Value);
			if (const FString* Summary = State.PlanNodeSummaryById.Find(Pair.Key))
			{
				O->SetStringField(TEXT("summary"), *Summary);
			}
			OrchStatusArr.Add(MakeShared<FJsonValueObject>(O.ToSharedRef()));
		}
		Root->SetArrayField(TEXT("planNodeStatus"), OrchStatusArr);
		TArray<TSharedPtr<FJsonValue>> ThreadUiArr;
		for (const FRecentUiEntry& R : State.ThreadRecentUiOverlay)
		{
			ThreadUiArr.Add(MakeShared<FJsonValueObject>(RecentUiEntryToJson(R).ToSharedRef()));
		}
		Root->SetArrayField(TEXT("threadRecentUiOverlay"), ThreadUiArr);
		{
			const FProjectTreeSummary& T = State.ProjectTreeSummary;
			TSharedPtr<FJsonObject> PT = MakeShared<FJsonObject>();
			PT->SetStringField(TEXT("samplerVersion"), T.SamplerVersion);
			if (T.UpdatedUtc != FDateTime::MinValue())
			{
				PT->SetStringField(TEXT("updatedUtc"), T.UpdatedUtc.ToIso8601());
			}
			if (T.LastQueryStartUtc != FDateTime::MinValue())
			{
				PT->SetStringField(TEXT("lastQueryStartUtc"), T.LastQueryStartUtc.ToIso8601());
			}
			if (T.LastQueryEndUtc != FDateTime::MinValue())
			{
				PT->SetStringField(TEXT("lastQueryEndUtc"), T.LastQueryEndUtc.ToIso8601());
			}
			PT->SetStringField(TEXT("lastQueryStatus"), T.LastQueryStatus);
			PT->SetNumberField(TEXT("lastQueryDurationMs"), T.LastQueryDurationMs);
			TArray<TSharedPtr<FJsonValue>> Folders;
			for (const FString& Folder : T.TopLevelFolders)
			{
				Folders.Add(MakeShared<FJsonValueString>(Folder));
			}
			PT->SetArrayField(TEXT("topLevelFolders"), Folders);
			TArray<TSharedPtr<FJsonValue>> Prefs;
			for (const FProjectTreePathPreference& P : T.PreferredCreatePaths)
			{
				Prefs.Add(MakeShared<FJsonValueObject>(ProjectTreePrefToJson(P).ToSharedRef()));
			}
			PT->SetArrayField(TEXT("preferredCreatePaths"), Prefs);
			Root->SetObjectField(TEXT("projectTreeSummary"), PT);
		}

		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		if (FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
		{
			OutJson = MoveTemp(Out);
			return true;
		}
		return false;
	}

	bool JsonToState(const FString& Json, FAgentContextState& OutState, TArray<FString>& OutWarnings)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutWarnings.Add(TEXT("Failed to parse context.json"));
			return false;
		}

		OutState = FAgentContextState();
		OutState.SchemaVersionField = Root->HasField(TEXT("schemaVersion"))
			? static_cast<int32>(Root->GetNumberField(TEXT("schemaVersion")))
			: FAgentContextState::SchemaVersion;
		if (OutState.SchemaVersionField != FAgentContextState::SchemaVersion)
		{
			OutWarnings.Add(FString::Printf(TEXT("context.json schema %d (expected %d) — loading best-effort."), OutState.SchemaVersionField, FAgentContextState::SchemaVersion));
		}

		const TArray<TSharedPtr<FJsonValue>>* AttArr = nullptr;
		if (Root->TryGetArrayField(TEXT("attachments"), AttArr) && AttArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *AttArr)
			{
				const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
				if (!V.IsValid() || !V->TryGetObject(ObjPtr) || !ObjPtr || !ObjPtr->IsValid())
				{
					continue;
				}
				const TSharedPtr<FJsonObject>& O = *ObjPtr;
				FContextAttachment A;
				FString TStr;
				O->TryGetStringField(TEXT("type"), TStr);
				StrToType(TStr, A.Type);
				O->TryGetStringField(TEXT("payload"), A.Payload);
				O->TryGetStringField(TEXT("label"), A.Label);
				O->TryGetStringField(TEXT("iconClass"), A.IconClassPath);
				OutState.Attachments.Add(A);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* ToolArr = nullptr;
		if (Root->TryGetArrayField(TEXT("toolResults"), ToolArr) && ToolArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *ToolArr)
			{
				const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
				if (!V.IsValid() || !V->TryGetObject(ObjPtr) || !ObjPtr || !ObjPtr->IsValid())
				{
					continue;
				}
				const TSharedPtr<FJsonObject>& O = *ObjPtr;
				FToolContextEntry E;
				O->TryGetStringField(TEXT("toolName"), E.ToolName);
				O->TryGetStringField(TEXT("truncatedResult"), E.TruncatedResult);
				FString Ts;
				if (O->TryGetStringField(TEXT("timestamp"), Ts))
				{
					FDateTime::ParseIso8601(*Ts, E.Timestamp);
				}
				OutState.ToolResults.Add(E);
			}
		}

		const TSharedPtr<FJsonObject>* EsPtr = nullptr;
		if (Root->TryGetObjectField(TEXT("editorSnapshot"), EsPtr) && EsPtr && EsPtr->IsValid())
		{
			const TSharedPtr<FJsonObject>& Es = *EsPtr;
			FEditorContextSnapshot S;
			Es->TryGetStringField(TEXT("selectedActorsSummary"), S.SelectedActorsSummary);
			Es->TryGetStringField(TEXT("activeAssetPath"), S.ActiveAssetPath);
			Es->TryGetStringField(TEXT("contentBrowserPath"), S.ContentBrowserPath);
			const TArray<TSharedPtr<FJsonValue>>* SelArr = nullptr;
			if (Es->TryGetArrayField(TEXT("contentBrowserSelectedAssets"), SelArr) && SelArr)
			{
				for (const TSharedPtr<FJsonValue>& V : *SelArr)
				{
					FString P;
					if (V.IsValid() && V->Type == EJson::String)
					{
						P = V->AsString();
						S.ContentBrowserSelectedAssets.Add(P);
					}
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* OpenArr = nullptr;
			if (Es->TryGetArrayField(TEXT("openEditorAssets"), OpenArr) && OpenArr)
			{
				for (const TSharedPtr<FJsonValue>& V : *OpenArr)
				{
					FString P;
					if (V.IsValid() && V->Type == EJson::String)
					{
						P = V->AsString();
						S.OpenEditorAssets.Add(P);
					}
				}
			}
			Es->TryGetStringField(TEXT("activeUiEntryId"), S.ActiveUiEntryId);
			const TArray<TSharedPtr<FJsonValue>>* RecentArr = nullptr;
			if (Es->TryGetArrayField(TEXT("recentUiEntries"), RecentArr) && RecentArr)
			{
				for (const TSharedPtr<FJsonValue>& V : *RecentArr)
				{
					const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
					if (!V.IsValid() || !V->TryGetObject(ObjPtr) || !ObjPtr || !ObjPtr->IsValid())
					{
						continue;
					}
					FRecentUiEntry Entry;
					if (JsonToRecentUiEntry(*ObjPtr, Entry))
					{
						S.RecentUiEntries.Add(MoveTemp(Entry));
					}
				}
			}
			Es->TryGetBoolField(TEXT("valid"), S.bValid);
			// Migration v1: only activeAssetPath was set
			if (S.ContentBrowserSelectedAssets.Num() == 0 && !S.ActiveAssetPath.IsEmpty())
			{
				S.ContentBrowserSelectedAssets.Add(S.ActiveAssetPath);
			}
			OutState.EditorSnapshot = S;
		}

		OutState.MaxContextChars = static_cast<int32>(Root->GetNumberField(TEXT("maxContextChars")));

		Root->TryGetStringField(TEXT("activeTodoPlan"), OutState.ActiveTodoPlanJson);
		const TArray<TSharedPtr<FJsonValue>>* DoneArr = nullptr;
		if (Root->TryGetArrayField(TEXT("todoStepsDone"), DoneArr) && DoneArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *DoneArr)
			{
				if (V.IsValid() && V->Type == EJson::Boolean)
				{
					OutState.TodoStepsDone.Add(V->AsBool());
				}
			}
		}
		Root->TryGetStringField(TEXT("activePlanDag"), OutState.ActivePlanDagJson);
		const TArray<TSharedPtr<FJsonValue>>* OrchStatusArr = nullptr;
		if (Root->TryGetArrayField(TEXT("planNodeStatus"), OrchStatusArr) && OrchStatusArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *OrchStatusArr)
			{
				const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
				if (!V.IsValid() || !V->TryGetObject(ObjPtr) || !ObjPtr || !ObjPtr->IsValid())
				{
					continue;
				}
				FString NodeId;
				FString Status;
				if (!(*ObjPtr)->TryGetStringField(TEXT("nodeId"), NodeId) || NodeId.IsEmpty())
				{
					continue;
				}
				(*ObjPtr)->TryGetStringField(TEXT("status"), Status);
				// "running" is non-resumable after reload — treat as unstuck so the DAG can schedule work again.
				if (Status.Equals(TEXT("running"), ESearchCase::IgnoreCase))
				{
					continue;
				}
				OutState.PlanNodeStatusById.Add(NodeId, Status);
				FString Summary;
				if ((*ObjPtr)->TryGetStringField(TEXT("summary"), Summary) && !Summary.IsEmpty())
				{
					OutState.PlanNodeSummaryById.Add(NodeId, Summary);
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* ThreadUiArr = nullptr;
		if (Root->TryGetArrayField(TEXT("threadRecentUiOverlay"), ThreadUiArr) && ThreadUiArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *ThreadUiArr)
			{
				const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
				if (!V.IsValid() || !V->TryGetObject(ObjPtr) || !ObjPtr || !ObjPtr->IsValid())
				{
					continue;
				}
				FRecentUiEntry Entry;
				if (JsonToRecentUiEntry(*ObjPtr, Entry))
				{
					OutState.ThreadRecentUiOverlay.Add(MoveTemp(Entry));
				}
			}
		}
		if (const TSharedPtr<FJsonObject>* ProjectTreeObj = nullptr;
			Root->TryGetObjectField(TEXT("projectTreeSummary"), ProjectTreeObj) && ProjectTreeObj && ProjectTreeObj->IsValid())
		{
			const TSharedPtr<FJsonObject>& PT = *ProjectTreeObj;
			PT->TryGetStringField(TEXT("samplerVersion"), OutState.ProjectTreeSummary.SamplerVersion);
			FString Ts;
			if (PT->TryGetStringField(TEXT("updatedUtc"), Ts))
			{
				FDateTime::ParseIso8601(*Ts, OutState.ProjectTreeSummary.UpdatedUtc);
			}
			if (PT->TryGetStringField(TEXT("lastQueryStartUtc"), Ts))
			{
				FDateTime::ParseIso8601(*Ts, OutState.ProjectTreeSummary.LastQueryStartUtc);
			}
			if (PT->TryGetStringField(TEXT("lastQueryEndUtc"), Ts))
			{
				FDateTime::ParseIso8601(*Ts, OutState.ProjectTreeSummary.LastQueryEndUtc);
			}
			PT->TryGetStringField(TEXT("lastQueryStatus"), OutState.ProjectTreeSummary.LastQueryStatus);
			if (PT->HasField(TEXT("lastQueryDurationMs")))
			{
				OutState.ProjectTreeSummary.LastQueryDurationMs = PT->GetNumberField(TEXT("lastQueryDurationMs"));
			}
			if (const TArray<TSharedPtr<FJsonValue>>* Folders = nullptr;
				PT->TryGetArrayField(TEXT("topLevelFolders"), Folders) && Folders)
			{
				for (const TSharedPtr<FJsonValue>& V : *Folders)
				{
					FString Folder;
					if (V.IsValid() && V->TryGetString(Folder) && !Folder.IsEmpty())
					{
						OutState.ProjectTreeSummary.TopLevelFolders.Add(Folder);
					}
				}
			}
			if (const TArray<TSharedPtr<FJsonValue>>* Prefs = nullptr;
				PT->TryGetArrayField(TEXT("preferredCreatePaths"), Prefs) && Prefs)
			{
				for (const TSharedPtr<FJsonValue>& V : *Prefs)
				{
					const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
					if (!V.IsValid() || !V->TryGetObject(ObjPtr) || !ObjPtr || !ObjPtr->IsValid())
					{
						continue;
					}
					FProjectTreePathPreference P;
					JsonToProjectTreePref(*ObjPtr, P);
					if (!P.AssetFamily.IsEmpty() && !P.PackagePath.IsEmpty())
					{
						OutState.ProjectTreeSummary.PreferredCreatePaths.Add(MoveTemp(P));
					}
				}
			}
		}

		return true;
	}
}
