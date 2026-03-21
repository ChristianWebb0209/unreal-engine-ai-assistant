#include "Context/AgentContextJson.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealAiAgentContextJson
{
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
		if (!State.ActiveOrchestrateDagJson.IsEmpty())
		{
			Root->SetStringField(TEXT("activeOrchestrateDag"), State.ActiveOrchestrateDagJson);
		}
		TArray<TSharedPtr<FJsonValue>> OrchStatusArr;
		for (const TPair<FString, FString>& Pair : State.OrchestrateNodeStatusById)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("nodeId"), Pair.Key);
			O->SetStringField(TEXT("status"), Pair.Value);
			if (const FString* Summary = State.OrchestrateNodeSummaryById.Find(Pair.Key))
			{
				O->SetStringField(TEXT("summary"), *Summary);
			}
			OrchStatusArr.Add(MakeShared<FJsonValueObject>(O.ToSharedRef()));
		}
		Root->SetArrayField(TEXT("orchestrateNodeStatus"), OrchStatusArr);

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
		Root->TryGetStringField(TEXT("activeOrchestrateDag"), OutState.ActiveOrchestrateDagJson);
		const TArray<TSharedPtr<FJsonValue>>* OrchStatusArr = nullptr;
		if (Root->TryGetArrayField(TEXT("orchestrateNodeStatus"), OrchStatusArr) && OrchStatusArr)
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
				OutState.OrchestrateNodeStatusById.Add(NodeId, Status);
				FString Summary;
				if ((*ObjPtr)->TryGetStringField(TEXT("summary"), Summary) && !Summary.IsEmpty())
				{
					OutState.OrchestrateNodeSummaryById.Add(NodeId, Summary);
				}
			}
		}

		return true;
	}
}
