#include "Tools/UnrealAiToolDispatch_Context.h"

#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/AgentContextTypes.h"
#include "Context/IAgentContextService.h"
#include "Tools/UnrealAiToolJson.h"
#include "Tools/UnrealAiToolDispatch_ArgRepair.h"

#include "Dom/JsonObject.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/TopLevelAssetPath.h"
#include "HAL/FileManager.h"
#include "Harness/FUnrealAiWorkerOrchestrator.h"

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetRegistryQuery(const TSharedPtr<FJsonObject>& Args)
{
	FString PathFilter;
	Args->TryGetStringField(TEXT("path_filter"), PathFilter);
	if (PathFilter.IsEmpty())
	{
		Args->TryGetStringField(TEXT("filter"), PathFilter);
	}
	if (PathFilter.IsEmpty())
	{
		FString ObjectPath;
		Args->TryGetStringField(TEXT("object_path"), ObjectPath);
		if (!ObjectPath.IsEmpty())
		{
			PathFilter = FPackageName::ObjectPathToPackageName(ObjectPath);
		}
	}
	FString ClassName;
	Args->TryGetStringField(TEXT("class_name"), ClassName);
	const TArray<TSharedPtr<FJsonValue>>* ClassPaths = nullptr;
	if (ClassName.IsEmpty() && Args->TryGetArrayField(TEXT("class_paths"), ClassPaths) && ClassPaths && ClassPaths->Num() > 0)
	{
		FString FirstClass;
		if ((*ClassPaths)[0].IsValid() && (*ClassPaths)[0]->TryGetString(FirstClass))
		{
			ClassName = FirstClass;
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* ClassFilters = nullptr;
	if (ClassName.IsEmpty() && Args->TryGetArrayField(TEXT("class_filters"), ClassFilters) && ClassFilters && ClassFilters->Num() > 0)
	{
		FString FirstClass;
		if ((*ClassFilters)[0].IsValid() && (*ClassFilters)[0]->TryGetString(FirstClass))
		{
			ClassName = FirstClass;
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* ClassNames = nullptr;
	if (ClassName.IsEmpty() && Args->TryGetArrayField(TEXT("class_names"), ClassNames) && ClassNames && ClassNames->Num() > 0)
	{
		FString FirstClass;
		if ((*ClassNames)[0].IsValid() && (*ClassNames)[0]->TryGetString(FirstClass))
		{
			ClassName = FirstClass;
		}
	}
	if (!ClassName.IsEmpty() && !ClassName.StartsWith(TEXT("/Script/"), ESearchCase::IgnoreCase))
	{
		ClassName = FString::Printf(TEXT("/Script/Engine.%s"), *ClassName);
	}
	const TArray<TSharedPtr<FJsonValue>>* Filters = nullptr;
	if ((PathFilter.IsEmpty() || ClassName.IsEmpty()) && Args->TryGetArrayField(TEXT("filters"), Filters) && Filters)
	{
		for (const TSharedPtr<FJsonValue>& V : *Filters)
		{
			const TSharedPtr<FJsonObject>* F = nullptr;
			if (!V.IsValid() || !V->TryGetObject(F) || !F || !(*F).IsValid())
			{
				continue;
			}
			FString Key;
			FString Value;
			(*F)->TryGetStringField(TEXT("key"), Key);
			(*F)->TryGetStringField(TEXT("value"), Value);
			if (Value.IsEmpty())
			{
				continue;
			}
			if (PathFilter.IsEmpty() && (Key.Equals(TEXT("path"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("path_filter"), ESearchCase::IgnoreCase)))
			{
				PathFilter = Value;
			}
			else if (ClassName.IsEmpty()
				&& (Key.Equals(TEXT("class"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("class_name"), ESearchCase::IgnoreCase)))
			{
				ClassName = Value;
			}
		}
	}
	if (PathFilter.IsEmpty())
	{
		const TSharedPtr<FJsonObject>* FiltersObj = nullptr;
		if (Args->TryGetObjectField(TEXT("filters"), FiltersObj) && FiltersObj && (*FiltersObj).IsValid())
		{
			(*FiltersObj)->TryGetStringField(TEXT("path_prefix"), PathFilter);
			if (PathFilter.IsEmpty())
			{
				(*FiltersObj)->TryGetStringField(TEXT("path_filter"), PathFilter);
			}
		}
	}
	int32 MaxResults = 100;
	double MR = 0.0;
	if (Args->TryGetNumberField(TEXT("max_results"), MR))
	{
		MaxResults = FMath::Clamp(static_cast<int32>(MR), 1, 5000);
	}

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AR = ARM.Get();

	FARFilter Filter;
	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(*PathFilter);
		Filter.bRecursivePaths = true;
	}
	if (!ClassName.IsEmpty())
	{
		Filter.ClassPaths.Add(FTopLevelAssetPath(*ClassName));
	}

	TArray<FAssetData> Assets;
	AR.GetAssets(Filter, Assets);

	if (Assets.Num() > MaxResults)
	{
		Assets.SetNum(MaxResults);
	}

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FAssetData& AD : Assets)
	{
		TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("object_path"), AD.GetObjectPathString());
		Item->SetStringField(TEXT("package_name"), AD.PackageName.ToString());
		Item->SetStringField(TEXT("asset_name"), AD.AssetName.ToString());
		Item->SetStringField(TEXT("class"), AD.AssetClassPath.ToString());
		Arr.Add(MakeShareable(new FJsonValueObject(Item.ToSharedRef())));
	}

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetArrayField(TEXT("assets"), Arr);
	O->SetNumberField(TEXT("count"), static_cast<double>(Arr.Num()));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_EditorStateSnapshotRead(
	FUnrealAiBackendRegistry* Registry,
	const FString& ProjectId,
	const FString& ThreadId,
	const TSharedPtr<FJsonObject>& Args)
{
	(void)Args;
	IAgentContextService* Ctx = Registry ? Registry->GetContextService() : nullptr;
	if (!Ctx)
	{
		return UnrealAiToolJson::Error(TEXT("Context service not available"));
	}

	Ctx->LoadOrCreate(ProjectId, ThreadId);
	Ctx->RefreshEditorSnapshotFromEngine();

	FAgentContextBuildOptions Opt;
	Opt.Mode = EUnrealAiAgentMode::Agent;
	Opt.ContextBuildInvocationReason = TEXT("tool_editor_state_snapshot_read");
	const FAgentContextBuildResult Built = Ctx->BuildContextWindow(Opt);

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("context_block"), Built.ContextBlock);
	O->SetBoolField(TEXT("truncated"), Built.bTruncated);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_AgentEmitTodoPlan(
	FUnrealAiBackendRegistry* Registry,
	const FString& ProjectId,
	const FString& ThreadId,
	const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return UnrealAiToolJson::Error(TEXT("invalid arguments"));
	}
	FString Title;
	{
		const TArray<const TCHAR*> Aliases = { TEXT("definitionOfDone") };
		if (!UnrealAiToolDispatchArgRepair::TryGetStringFieldCanonical(
				Args,
				TEXT("title"),
				Aliases,
				Title)
			|| Title.IsEmpty())
		{
			TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
			SuggestedArgs->SetStringField(TEXT("title"), TEXT("Implement requested task safely"));
			const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
			if (Args->TryGetArrayField(TEXT("steps"), Steps) && Steps)
			{
				SuggestedArgs->SetArrayField(TEXT("steps"), *Steps);
			}
			return UnrealAiToolJson::ErrorWithSuggestedCall(
				TEXT("title is required (alias: definitionOfDone)."),
				TEXT("agent_emit_todo_plan"),
				SuggestedArgs);
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
	if (!Args->TryGetArrayField(TEXT("steps"), Steps) || !Steps || Steps->Num() == 0)
	{
		TSharedPtr<FJsonObject> Suggested = MakeShared<FJsonObject>();
		Suggested->SetStringField(TEXT("title"), TEXT("Implement requested task safely"));
		TArray<TSharedPtr<FJsonValue>> SuggestedSteps;
		TSharedPtr<FJsonObject> S0 = MakeShared<FJsonObject>();
		S0->SetStringField(TEXT("id"), TEXT("step_1"));
		S0->SetStringField(TEXT("title"), TEXT("Inspect current state"));
		S0->SetStringField(TEXT("detail"), TEXT("Use read/search tools to resolve required paths."));
		S0->SetArrayField(TEXT("dependsOn"), TArray<TSharedPtr<FJsonValue>>());
		S0->SetStringField(TEXT("status"), TEXT("pending"));
		SuggestedSteps.Add(MakeShared<FJsonValueObject>(S0));
		Suggested->SetArrayField(TEXT("steps"), SuggestedSteps);
		return UnrealAiToolJson::ErrorWithSuggestedCall(TEXT("steps array is required"), TEXT("agent_emit_todo_plan"), Suggested);
	}
	IAgentContextService* Ctx = Registry ? Registry->GetContextService() : nullptr;
	if (!Ctx)
	{
		return UnrealAiToolJson::Error(TEXT("Context service not available"));
	}
	Ctx->LoadOrCreate(ProjectId, ThreadId);
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), TEXT("unreal_ai.todo_plan"));
	Root->SetStringField(TEXT("title"), Title);
	const int32 MaxPlanSteps = 12;
	if (Steps->Num() > MaxPlanSteps)
	{
		return UnrealAiToolJson::Error(
			FString::Printf(TEXT("steps exceeds max allowed (%d). Split into phases or reduce scope."), MaxPlanSteps));
	}
	TArray<TSharedPtr<FJsonValue>> StepArr;
	TSet<FString> StepIds;
	for (const TSharedPtr<FJsonValue>& V : *Steps)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (!V.IsValid() || !V->TryGetObject(O) || !O->IsValid())
		{
			continue;
		}
		const TSharedPtr<FJsonObject> InStep = *O;
		TSharedPtr<FJsonObject> OutStep = MakeShared<FJsonObject>();
		FString Id;
		InStep->TryGetStringField(TEXT("id"), Id);
		if (Id.IsEmpty())
		{
			Id = FString::Printf(TEXT("step_%d"), StepArr.Num() + 1);
		}
		if (StepIds.Contains(Id))
		{
			return UnrealAiToolJson::Error(FString::Printf(TEXT("duplicate step id: %s"), *Id));
		}
		StepIds.Add(Id);
		OutStep->SetStringField(TEXT("id"), Id);

		FString StepTitle;
		InStep->TryGetStringField(TEXT("title"), StepTitle);
		if (StepTitle.IsEmpty())
		{
			InStep->TryGetStringField(TEXT("detail"), StepTitle);
		}
		if (StepTitle.IsEmpty())
		{
			return UnrealAiToolJson::Error(FString::Printf(TEXT("step '%s' requires title/detail"), *Id));
		}
		OutStep->SetStringField(TEXT("title"), StepTitle);

		FString Detail;
		InStep->TryGetStringField(TEXT("detail"), Detail);
		if (!Detail.IsEmpty())
		{
			OutStep->SetStringField(TEXT("detail"), Detail);
		}
		FString Status;
		InStep->TryGetStringField(TEXT("status"), Status);
		if (Status.IsEmpty())
		{
			Status = TEXT("pending");
		}
		const FString StatusNorm = Status.ToLower();
		if (StatusNorm != TEXT("pending")
			&& StatusNorm != TEXT("in_progress")
			&& StatusNorm != TEXT("completed")
			&& StatusNorm != TEXT("blocked")
			&& StatusNorm != TEXT("cancelled"))
		{
			return UnrealAiToolJson::Error(FString::Printf(TEXT("step '%s' has invalid status '%s'"), *Id, *Status));
		}
		OutStep->SetStringField(TEXT("status"), StatusNorm);
		const TArray<TSharedPtr<FJsonValue>>* Depends = nullptr;
		if (InStep->TryGetArrayField(TEXT("dependsOn"), Depends) && Depends)
		{
			TArray<TSharedPtr<FJsonValue>> OutDepends;
			for (const TSharedPtr<FJsonValue>& DepVal : *Depends)
			{
				FString DepId;
				if (DepVal.IsValid() && DepVal->TryGetString(DepId) && !DepId.IsEmpty())
				{
					OutDepends.Add(MakeShared<FJsonValueString>(DepId));
				}
			}
			OutStep->SetArrayField(TEXT("dependsOn"), OutDepends);
		}
		StepArr.Add(MakeShared<FJsonValueObject>(OutStep));
	}
	if (StepArr.Num() == 0)
	{
		return UnrealAiToolJson::Error(TEXT("steps must contain objects"));
	}
	for (const TSharedPtr<FJsonValue>& StepVal : StepArr)
	{
		const TSharedPtr<FJsonObject>* StepObj = nullptr;
		if (!StepVal.IsValid() || !StepVal->TryGetObject(StepObj) || !StepObj || !(*StepObj).IsValid())
		{
			continue;
		}
		const FString ThisId = (*StepObj)->GetStringField(TEXT("id"));
		const TArray<TSharedPtr<FJsonValue>>* Depends = nullptr;
		if ((*StepObj)->TryGetArrayField(TEXT("dependsOn"), Depends) && Depends)
		{
			for (const TSharedPtr<FJsonValue>& DepVal : *Depends)
			{
				FString DepId;
				if (DepVal.IsValid() && DepVal->TryGetString(DepId) && !DepId.IsEmpty() && !StepIds.Contains(DepId))
				{
					return UnrealAiToolJson::Error(
						FString::Printf(TEXT("step '%s' dependsOn unknown step id '%s'"), *ThisId, *DepId));
				}
			}
		}
	}
	Root->SetArrayField(TEXT("steps"), StepArr);
	const FString Json = UnrealAiToolJson::SerializeObject(Root);
	Ctx->SetActiveTodoPlan(Json);
	Ctx->SaveNow(ProjectId, ThreadId);
	return UnrealAiToolJson::Ok(Root);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ContentBrowserSyncAsset(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	if (!Args->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
	{
		if (!Args->TryGetStringField(TEXT("object_path"), Path) || Path.IsEmpty())
		{
			Args->TryGetStringField(TEXT("asset_path"), Path);
		}
	}
	if (Path.IsEmpty())
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("path"), TEXT("/Game/Blueprints/MyBP.MyBP"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("path is required (aliases: object_path, asset_path)."),
			TEXT("content_browser_sync_asset"),
			SuggestedArgs);
	}

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AR = ARM.Get();

	const FSoftObjectPath SOP(Path);
	FAssetData AD = AR.GetAssetByObjectPath(SOP);
	if (!AD.IsValid())
	{
		return UnrealAiToolJson::Error(FString::Printf(TEXT("Asset not found: %s"), *Path));
	}

	FContentBrowserModule& CBM = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FAssetData> List;
	List.Add(AD);
	CBM.Get().SyncBrowserToAssets(List);

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("synced_path"), Path);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_EngineMessageLogRead(const TSharedPtr<FJsonObject>& Args)
{
	int32 MaxLines = 80;
	double ML = 0.0;
	if (Args->TryGetNumberField(TEXT("max_lines"), ML))
	{
		MaxLines = FMath::Clamp(static_cast<int32>(ML), 1, 5000);
	}

	FString Category;
	Args->TryGetStringField(TEXT("category"), Category);

	const FString LogDir = FPaths::ProjectLogDir();
	const FString Latest = FPaths::Combine(LogDir, TEXT("UnrealEditor.log"));
	FString Content;
	if (!FPaths::FileExists(Latest) || !FFileHelper::LoadFileToString(Content, *Latest))
	{
		return UnrealAiToolJson::Error(TEXT("Could not read UnrealEditor.log"));
	}

	TArray<FString> Lines;
	Content.ParseIntoArrayLines(Lines);
	int32 Start = FMath::Max(0, Lines.Num() - MaxLines);
	TArray<FString> Tail;
	for (int32 i = Start; i < Lines.Num(); ++i)
	{
		if (!Category.IsEmpty() && !Lines[i].Contains(Category))
		{
			continue;
		}
		Tail.Add(Lines[i]);
	}

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FString& L : Tail)
	{
		Arr.Add(MakeShareable(new FJsonValueString(L)));
	}

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetArrayField(TEXT("lines"), Arr);
	O->SetStringField(TEXT("source"), Latest);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_WorkerMergeResults(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return UnrealAiToolJson::Error(TEXT("invalid arguments"));
	}
	FString ParentRunId;
	const TArray<TSharedPtr<FJsonValue>>* WorkerArr = nullptr;
	if (!Args->TryGetStringField(TEXT("parent_run_id"), ParentRunId) || ParentRunId.IsEmpty()
		|| !Args->TryGetArrayField(TEXT("worker_results"), WorkerArr) || !WorkerArr || WorkerArr->Num() == 0)
	{
		return UnrealAiToolJson::Error(TEXT("parent_run_id and worker_results[] are required"));
	}
	TArray<FUnrealAiWorkerResult> Workers;
	for (const TSharedPtr<FJsonValue>& V : *WorkerArr)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (!V.IsValid() || !V->TryGetObject(O) || !O->IsValid())
		{
			continue;
		}
		FUnrealAiWorkerResult R;
		(*O)->TryGetStringField(TEXT("status"), R.Status);
		(*O)->TryGetStringField(TEXT("summary"), R.Summary);
		const TArray<TSharedPtr<FJsonValue>>* Errs = nullptr;
		if ((*O)->TryGetArrayField(TEXT("errors"), Errs) && Errs)
		{
			for (const TSharedPtr<FJsonValue>& E : *Errs)
			{
				FString S;
				if (E.IsValid() && E->TryGetString(S))
				{
					R.Errors.Add(S);
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Arts = nullptr;
		if ((*O)->TryGetArrayField(TEXT("artifacts"), Arts) && Arts)
		{
			for (const TSharedPtr<FJsonValue>& E : *Arts)
			{
				FString S;
				if (E.IsValid() && E->TryGetString(S))
				{
					R.Artifacts.Add(S);
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Fuq = nullptr;
		if ((*O)->TryGetArrayField(TEXT("follow_up_questions"), Fuq) && Fuq)
		{
			for (const TSharedPtr<FJsonValue>& E : *Fuq)
			{
				FString S;
				if (E.IsValid() && E->TryGetString(S))
				{
					R.FollowUpQuestions.Add(S);
				}
			}
		}
		Workers.Add(R);
	}
	if (Workers.Num() == 0)
	{
		return UnrealAiToolJson::Error(TEXT("No valid worker_results objects"));
	}
	const FUnrealAiWorkerResult M = FUnrealAiWorkerOrchestrator::MergeDeterministic(Workers);
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("ok"), true);
	Out->SetStringField(TEXT("parent_run_id"), ParentRunId);
	Out->SetStringField(TEXT("merged_status"), M.Status);
	Out->SetStringField(TEXT("merged_summary"), M.Summary);
	TArray<TSharedPtr<FJsonValue>> ErrArr;
	for (const FString& E : M.Errors)
	{
		ErrArr.Add(MakeShareable(new FJsonValueString(E)));
	}
	Out->SetArrayField(TEXT("merged_errors"), ErrArr);
	TArray<TSharedPtr<FJsonValue>> ArtArr;
	for (const FString& A : M.Artifacts)
	{
		ArtArr.Add(MakeShareable(new FJsonValueString(A)));
	}
	Out->SetArrayField(TEXT("merged_artifacts"), ArtArr);
	TArray<TSharedPtr<FJsonValue>> FuqArr;
	for (const FString& Q : M.FollowUpQuestions)
	{
		FuqArr.Add(MakeShareable(new FJsonValueString(Q)));
	}
	Out->SetArrayField(TEXT("merged_follow_up_questions"), FuqArr);
	return UnrealAiToolJson::Ok(Out);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ToolAuditAppend(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return UnrealAiToolJson::Error(TEXT("invalid arguments"));
	}
	FString ToolId;
	FString Status;
	FString Summary;
	if (!Args->TryGetStringField(TEXT("tool_id"), ToolId) || ToolId.IsEmpty()
		|| !Args->TryGetStringField(TEXT("status"), Status) || Status.IsEmpty()
		|| !Args->TryGetStringField(TEXT("summary"), Summary) || Summary.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("tool_id, status, summary are required"));
	}
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("UnrealAiEditor");
	const FString AuditPath = Dir / TEXT("tool_audit.log");
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Line = FString::Printf(
		TEXT("%s | %s | %s | %s | %s\n"),
		*FDateTime::UtcNow().ToIso8601(),
		*ToolId,
		*Status,
		*Summary.Replace(TEXT("\n"), TEXT(" ")),
		*AssetPath);
	FFileHelper::SaveStringToFile(Line, *AuditPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(),
		FILEWRITE_Append);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("audit_path"), AuditPath);
	return UnrealAiToolJson::Ok(O);
}
