#include "Tools/UnrealAiToolDispatch_Context.h"

#include "UnrealAiEditorModule.h"
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
		Args->TryGetStringField(TEXT("path"), PathFilter);
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
	UnrealAiToolDispatchArgRepair::SanitizeUnrealPathString(PathFilter);
	ClassName.TrimStartAndEndInline();
	UnrealAiToolDispatchArgRepair::SanitizeUnrealPathString(ClassName);
	if (!ClassName.IsEmpty() && !ClassName.StartsWith(TEXT("/Script/"), ESearchCase::IgnoreCase))
	{
		ClassName = FString::Printf(TEXT("/Script/Engine.%s"), *ClassName);
	}

	if (PathFilter.IsEmpty() && ClassName.IsEmpty())
	{
		// Deterministic one-shot recovery: keep the query bounded without forcing a tool failure.
		PathFilter = TEXT("/Game");
		if (Args.IsValid())
		{
			Args->SetStringField(TEXT("path_filter"), PathFilter);
		}
	}

	int32 MaxResults = 100;
	double MR = 0.0;
	if (Args->TryGetNumberField(TEXT("max_results"), MR))
	{
		MaxResults = FMath::Clamp(static_cast<int32>(MR), 1, 5000);
	}
	else
	{
		double Limit = 0.0;
		if (Args->TryGetNumberField(TEXT("limit"), Limit))
		{
			MaxResults = FMath::Clamp(static_cast<int32>(Limit), 1, 5000);
		}
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
		SuggestedArgs->SetStringField(TEXT("query"), TEXT("MyAsset"));
		SuggestedArgs->SetStringField(TEXT("path_prefix"), TEXT("/Game"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("content_browser_sync_asset: path/object_path/asset_path is required; empty arguments are invalid. "
				 "Resolve a concrete asset object path (for example via asset_index_fuzzy_search or asset_registry_query, "
				 "or from context/selection), then call again with that path."),
			TEXT("asset_index_fuzzy_search"),
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

	bool bUiSuppressed = false;
	if (FUnrealAiEditorModule::IsEditorFocusEnabled())
	{
		FContentBrowserModule& CBM = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		TArray<FAssetData> List;
		List.Add(AD);
		CBM.Get().SyncBrowserToAssets(List);
	}
	else
	{
		bUiSuppressed = true;
	}

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("synced_path"), Path);
	if (bUiSuppressed)
	{
		O->SetBoolField(TEXT("ui_suppressed"), true);
	}
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
