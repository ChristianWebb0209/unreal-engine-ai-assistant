#include "Context/FUnrealAiContextService.h"

#include "Backend/IUnrealAiPersistence.h"
#include "Context/AgentContextFormat.h"
#include "Context/AgentContextJson.h"
#include "Context/UnrealAiEditorContextQueries.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#include "Selection.h"
#endif

#include "Containers/Ticker.h"
#include "Context/UnrealAiActiveTodoSummary.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Planning/UnrealAiComplexityAssessor.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAiCtxTodoPriv
{
	static int32 CountTodoStepsInJson(const FString& PlanJson)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PlanJson);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return 0;
		}
		const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
		if (!Root->TryGetArrayField(TEXT("steps"), Steps) || !Steps)
		{
			return 0;
		}
		return Steps->Num();
	}
}

FUnrealAiContextService::FUnrealAiContextService(IUnrealAiPersistence* InPersistence)
	: Persistence(InPersistence)
{
}

FString FUnrealAiContextService::SessionKey(const FString& ProjectId, const FString& ThreadId)
{
	return ProjectId + TEXT("|") + ThreadId;
}

FAgentContextState* FUnrealAiContextService::FindOrAddState(const FString& ProjectId, const FString& ThreadId)
{
	return &Sessions.FindOrAdd(SessionKey(ProjectId, ThreadId));
}

const FAgentContextState* FUnrealAiContextService::FindState(const FString& ProjectId, const FString& ThreadId) const
{
	return Sessions.Find(SessionKey(ProjectId, ThreadId));
}

void FUnrealAiContextService::LoadOrCreate(const FString& ProjectId, const FString& ThreadId)
{
	ActiveProjectId = ProjectId;
	ActiveThreadId = ThreadId;

	const FString Key = SessionKey(ProjectId, ThreadId);
	if (Sessions.Contains(Key))
	{
		return;
	}

	FString Json;
	if (Persistence && Persistence->LoadThreadContextJson(ProjectId, ThreadId, Json))
	{
		TArray<FString> Warnings;
		FAgentContextState Loaded;
		if (UnrealAiAgentContextJson::JsonToState(Json, Loaded, Warnings))
		{
			Sessions.Add(Key, MoveTemp(Loaded));
			return;
		}
	}

	Sessions.Add(Key, FAgentContextState());
}

void FUnrealAiContextService::ClearSession(const FString& ProjectId, const FString& ThreadId)
{
	Sessions.Remove(SessionKey(ProjectId, ThreadId));
	if (ActiveProjectId == ProjectId && ActiveThreadId == ThreadId)
	{
		ActiveProjectId.Reset();
		ActiveThreadId.Reset();
	}
}

void FUnrealAiContextService::AddAttachment(const FContextAttachment& Attachment)
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FString Warn;
	if (!ValidateAttachment(Attachment, &Warn) && !Warn.IsEmpty())
	{
		UE_LOG(LogCore, Warning, TEXT("Context attachment: %s"), *Warn);
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	S->Attachments.Add(Attachment);
	ScheduleSave(ActiveProjectId, ActiveThreadId);
}

bool FUnrealAiContextService::ValidateAttachment(const FContextAttachment& Attachment, FString* OutWarning) const
{
#if WITH_EDITOR
	if (Attachment.Type == EContextAttachmentType::AssetPath && !Attachment.Payload.IsEmpty())
	{
		FAssetRegistryModule& Reg =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		const FSoftObjectPath Path(Attachment.Payload);
		const FAssetData AD = Reg.Get().GetAssetByObjectPath(Path);
		if (!AD.IsValid())
		{
			if (OutWarning)
			{
				*OutWarning = FString::Printf(TEXT("Asset not found in registry: %s"), *Attachment.Payload);
			}
			return false;
		}
	}
#endif
	return true;
}

void FUnrealAiContextService::RemoveAttachment(int32 Index)
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	if (S->Attachments.IsValidIndex(Index))
	{
		S->Attachments.RemoveAt(Index);
		ScheduleSave(ActiveProjectId, ActiveThreadId);
	}
}

void FUnrealAiContextService::ClearAttachments()
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	S->Attachments.Reset();
	ScheduleSave(ActiveProjectId, ActiveThreadId);
}

void FUnrealAiContextService::RecordToolResult(
	FName ToolName,
	const FString& Result,
	const FContextRecordPolicy& Policy)
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	FToolContextEntry E;
	E.ToolName = ToolName.ToString();
	E.TruncatedResult = Result;
	if (E.TruncatedResult.Len() > Policy.MaxStoredCharsPerResult)
	{
		E.TruncatedResult = E.TruncatedResult.Left(Policy.MaxStoredCharsPerResult);
	}
	E.Timestamp = FDateTime::UtcNow();
	S->ToolResults.Add(MoveTemp(E));
	ScheduleSave(ActiveProjectId, ActiveThreadId);
}

void FUnrealAiContextService::SetActiveTodoPlan(const FString& PlanJson)
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	S->ActiveTodoPlanJson = PlanJson;
	const int32 N = UnrealAiCtxTodoPriv::CountTodoStepsInJson(PlanJson);
	S->TodoStepsDone.Init(false, N);
	ScheduleSave(ActiveProjectId, ActiveThreadId);
}

void FUnrealAiContextService::SetTodoStepDone(int32 StepIndex, bool bDone)
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	if (!S->TodoStepsDone.IsValidIndex(StepIndex))
	{
		return;
	}
	S->TodoStepsDone[StepIndex] = bDone;
	ScheduleSave(ActiveProjectId, ActiveThreadId);
}

void FUnrealAiContextService::SetEditorSnapshot(const FEditorContextSnapshot& Snapshot)
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	S->EditorSnapshot = Snapshot;
	ScheduleSave(ActiveProjectId, ActiveThreadId);
}

void FUnrealAiContextService::ClearEditorSnapshot()
{
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		return;
	}
	FAgentContextState* S = FindOrAddState(ActiveProjectId, ActiveThreadId);
	S->EditorSnapshot.Reset();
	ScheduleSave(ActiveProjectId, ActiveThreadId);
}

void FUnrealAiContextService::RefreshEditorSnapshotFromEngine()
{
#if WITH_EDITOR
	FEditorContextSnapshot Snap;
	Snap.bValid = true;
	if (GEditor)
	{
		USelection* SelectedActors = GEditor->GetSelectedActors();
		if (SelectedActors)
		{
			TArray<FString> Names;
			for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
			{
				if (AActor* A = Cast<AActor>(*It))
				{
					Names.Add(A->GetActorLabel());
				}
			}
			Snap.SelectedActorsSummary = FString::Join(Names, TEXT(", "));
		}
	}
	UnrealAiEditorContextQueries::PopulateContentBrowserAndSelection(Snap);
	UnrealAiEditorContextQueries::PopulateOpenEditorAssets(Snap);
	SetEditorSnapshot(Snap);
#endif
}

FAgentContextBuildResult FUnrealAiContextService::BuildContextWindow(const FAgentContextBuildOptions& Options)
{
	FAgentContextBuildResult Result;
	if (ActiveProjectId.IsEmpty() || ActiveThreadId.IsEmpty())
	{
		Result.Warnings.Add(TEXT("No active thread — call LoadOrCreate first."));
		return Result;
	}

	const FAgentContextState* Found = FindState(ActiveProjectId, ActiveThreadId);
	if (!Found)
	{
		Result.Warnings.Add(TEXT("No context state for active thread."));
		return Result;
	}

	FAgentContextState Working = *Found;
	UnrealAiAgentContextFormat::ApplyModeToStateForBuild(Working, Options);

	int32 Budget = Options.MaxContextChars;
	if (Working.MaxContextChars > 0)
	{
		Budget = Working.MaxContextChars;
	}

	FString Block = UnrealAiAgentContextFormat::FormatContextBlock(Working, Options);
	const int32 RawLen = Block.Len();
	Block = UnrealAiAgentContextFormat::TruncateToBudget(Block, Budget, Result.Warnings);
	Result.bTruncated = Block.Len() < RawLen;

	if (!Options.StaticSystemPrefix.IsEmpty())
	{
		Result.SystemOrDeveloperBlock = Options.StaticSystemPrefix;
	}

	Result.ContextBlock = MoveTemp(Block);

	Result.ActiveTodoSummaryText = UnrealAiFormatActiveTodoSummary(Working.ActiveTodoPlanJson, Working.TodoStepsDone);

	FUnrealAiComplexityAssessorInputs Cin;
	Cin.UserMessage = Options.UserMessageForComplexity;
	Cin.AttachmentCount = Found->Attachments.Num();
	Cin.ToolResultCount = Found->ToolResults.Num();
	Cin.ContextBlockCharCount = Result.ContextBlock.Len();
	Cin.Mode = Options.Mode;
	if (Found->EditorSnapshot.IsSet() && Found->EditorSnapshot.GetValue().bValid)
	{
		Cin.ContentBrowserSelectionCount = Found->EditorSnapshot.GetValue().ContentBrowserSelectedAssets.Num();
	}
	const FUnrealAiComplexityAssessment Ca = UnrealAiComplexityAssessor::Assess(Cin);
	Result.ComplexityLabel = Ca.Label;
	Result.ComplexityScoreNormalized = Ca.ScoreNormalized;
	Result.bRecommendPlanGate = Ca.bRecommendPlanGate;
	Result.ComplexitySignals = Ca.Signals;
	Result.ComplexityBlock = Ca.FormatBlock();

	if (Options.Mode == EUnrealAiAgentMode::Ask && Found->ToolResults.Num() > 0)
	{
		Result.Warnings.Add(TEXT("Ask mode: tool results omitted from context."));
	}

	return Result;
}

void FUnrealAiContextService::SaveNow(const FString& ProjectId, const FString& ThreadId)
{
	FlushSave(ProjectId, ThreadId);
}

const FAgentContextState* FUnrealAiContextService::GetState(const FString& ProjectId, const FString& ThreadId) const
{
	return FindState(ProjectId, ThreadId);
}

void FUnrealAiContextService::ScheduleSave(const FString& ProjectId, const FString& ThreadId)
{
	if (!Persistence)
	{
		return;
	}
	const FString Key = SessionKey(ProjectId, ThreadId);
	PendingSaveKeys.Add(Key);

	if (!SaveTickerHandle.IsValid())
	{
		SaveTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda(
				[this](float) -> bool
				{
					TSet<FString> Copy = MoveTemp(PendingSaveKeys);
					PendingSaveKeys.Reset();
					for (const FString& K : Copy)
					{
						int32 Bar = INDEX_NONE;
						if (K.FindChar(TEXT('|'), Bar))
						{
							const FString Pid = K.Left(Bar);
							const FString Tid = K.Mid(Bar + 1);
							FlushSave(Pid, Tid);
						}
					}
					SaveTickerHandle = FDelegateHandle();
					return false;
				}),
			0.25f);
	}
}

void FUnrealAiContextService::FlushSave(const FString& ProjectId, const FString& ThreadId)
{
	FlushSaveBySessionKey(SessionKey(ProjectId, ThreadId));
}

void FUnrealAiContextService::FlushSaveBySessionKey(const FString& Key)
{
	if (!Persistence)
	{
		return;
	}
	const FAgentContextState* S = Sessions.Find(Key);
	if (!S)
	{
		return;
	}
	int32 Bar = INDEX_NONE;
	if (!Key.FindChar(TEXT('|'), Bar))
	{
		return;
	}
	const FString Pid = Key.Left(Bar);
	const FString Tid = Key.Mid(Bar + 1);
	FString Json;
	if (UnrealAiAgentContextJson::StateToJson(*S, Json))
	{
		Persistence->SaveThreadContextJson(Pid, Tid, Json);
	}
}

void FUnrealAiContextService::FlushAllSessionsToDisk()
{
	for (const auto& Pair : Sessions)
	{
		FlushSaveBySessionKey(Pair.Key);
	}
}
