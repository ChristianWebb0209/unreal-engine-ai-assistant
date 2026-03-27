#include "Tools/UnrealAiToolDispatch_Search.h"

#include "Tools/UnrealAiFuzzySearch.h"
#include "Tools/UnrealAiToolJson.h"
#include "Tools/UnrealAiToolDispatch_ArgRepair.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformFilemanager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UnrealAiToolDispatchSearchInternal
{
	static void CollectProjectSourceRoots(const FString& ProjectDir, TArray<FString>& OutRoots)
	{
		const FString S = FPaths::Combine(ProjectDir, TEXT("Source"));
		if (FPaths::DirectoryExists(S))
		{
			OutRoots.Add(S);
		}
		const FString C = FPaths::Combine(ProjectDir, TEXT("Config"));
		if (FPaths::DirectoryExists(C))
		{
			OutRoots.Add(C);
		}
		const FString PPlugins = FPaths::Combine(ProjectDir, TEXT("Plugins"));
		if (FPaths::DirectoryExists(PPlugins))
		{
			TArray<FString> PluginNames;
			IFileManager::Get().FindFiles(PluginNames, *FPaths::Combine(PPlugins, TEXT("*")), false, true);
			for (const FString& PluginName : PluginNames)
			{
				const FString Src = FPaths::Combine(PPlugins, PluginName, TEXT("Source"));
				if (FPaths::DirectoryExists(Src) && !OutRoots.Contains(Src))
				{
					OutRoots.Add(Src);
				}
			}
			if (PluginNames.Num() == 0)
			{
				TArray<FString> PluginDirs;
				IFileManager::Get().FindFilesRecursive(PluginDirs, *PPlugins, TEXT(""), false, true);
				for (const FString& Dir : PluginDirs)
				{
					if (OutRoots.Contains(Dir))
					{
						continue;
					}
					FString Norm = Dir;
					Norm.ReplaceInline(TEXT("\\"), TEXT("/"));
					if (Norm.EndsWith(TEXT("/Source")))
					{
						OutRoots.Add(Dir);
					}
				}
			}
		}

		// Fallback roots: even if ProjectDir is mis-resolved during harness runs,
		// include this plugin's own Source/ (and Config/) and a limited set of engine plugin Source/ dirs.
		{
			const TSharedPtr<IPlugin> Plug = IPluginManager::Get().FindPlugin(TEXT("UnrealAiEditor"));
			if (Plug.IsValid())
			{
				const FString BaseDir = Plug->GetBaseDir();
				const FString PSrc = FPaths::Combine(BaseDir, TEXT("Source"));
				if (FPaths::DirectoryExists(PSrc) && !OutRoots.Contains(PSrc))
				{
					OutRoots.Add(PSrc);
				}
				const FString PCfg = FPaths::Combine(BaseDir, TEXT("Config"));
				if (FPaths::DirectoryExists(PCfg) && !OutRoots.Contains(PCfg))
				{
					OutRoots.Add(PCfg);
				}
			}
		}
		{
			const FString EPlugins = FPaths::EnginePluginsDir();
			if (FPaths::DirectoryExists(EPlugins))
			{
				TArray<FString> EnginePluginNames;
				IFileManager::Get().FindFiles(EnginePluginNames, *FPaths::Combine(EPlugins, TEXT("*")), false, true);
				for (const FString& PluginName : EnginePluginNames)
				{
					const FString Src = FPaths::Combine(EPlugins, PluginName, TEXT("Source"));
					if (FPaths::DirectoryExists(Src) && !OutRoots.Contains(Src))
					{
						OutRoots.Add(Src);
					}
				}
			}
		}
	}

	static void ParseGlobList(const FString& Glob, TArray<FString>& OutExts)
	{
		OutExts.Reset();
		if (Glob.IsEmpty())
		{
			return;
		}
		TArray<FString> Parts;
		Glob.ParseIntoArray(Parts, TEXT(";"), true);
		for (FString P : Parts)
		{
			P.TrimStartAndEndInline();
			if (P.StartsWith(TEXT("*.")))
			{
				OutExts.Add(P.Mid(2));
			}
		}
	}

	static bool FileMatchesExtList(const FString& File, const TArray<FString>& Exts)
	{
		if (Exts.Num() == 0)
		{
			return true;
		}
		const FString E = FPaths::GetExtension(File);
		for (const FString& X : Exts)
		{
			if (E.Equals(X, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	struct FPathHit
	{
		float Score = 0.f;
		FString RelativePath;
		FString FileName;
	};

	struct FLineHit
	{
		float Score = 0.f;
		FString RelativePath;
		int32 LineNumber = 0;
		FString LineText;
	};
}

using namespace UnrealAiToolDispatchSearchInternal;

FUnrealAiToolInvocationResult UnrealAiDispatch_SceneFuzzySearch(const TSharedPtr<FJsonObject>& Args)
{
	FString Query;
	if (!Args->TryGetStringField(TEXT("query"), Query) || Query.IsEmpty())
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("query"), TEXT("PlayerStart"));
		SuggestedArgs->SetNumberField(TEXT("max_results"), 20.0);
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("scene_fuzzy_search: non-empty query string is required."),
			TEXT("scene_fuzzy_search"),
			SuggestedArgs);
	}
	int32 MaxResults = 50;
	double MR = 0.0;
	if (Args->TryGetNumberField(TEXT("max_results"), MR))
	{
		MaxResults = FMath::Clamp(static_cast<int32>(MR), 1, 500);
	}
	bool bIncludeHidden = false;
	Args->TryGetBoolField(TEXT("include_hidden"), bIncludeHidden);

	if (!GEditor)
	{
		return UnrealAiToolJson::Error(TEXT("scene_fuzzy_search: Unreal Editor API unavailable (GEditor is null)."));
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return UnrealAiToolJson::Error(TEXT("scene_fuzzy_search: no editor world loaded; open a level in the editor."));
	}

	struct FHit
	{
		float Score = 0.f;
		FString ActorPath;
		FString Label;
		FString ClassName;
		FString MatchedField;
	};

	TArray<FHit> Hits;
	Hits.Reserve(256);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A)
		{
			continue;
		}
		if (!bIncludeHidden && A->IsHiddenEd())
		{
			continue;
		}
		FString TagsJoined;
		for (const FName& Tg : A->Tags)
		{
			if (!TagsJoined.IsEmpty())
			{
				TagsJoined += TEXT(" ");
			}
			TagsJoined += Tg.ToString();
		}
		float Best = 0.f;
		FString BestField = TEXT("path");
		auto Consider = [&](const FString& S, const TCHAR* FieldName)
		{
			if (S.IsEmpty())
			{
				return;
			}
			const float Ss = UnrealAiFuzzySearch::Score(Query, S);
			if (Ss > Best)
			{
				Best = Ss;
				BestField = FieldName;
			}
		};
		Consider(A->GetActorLabel(), TEXT("label"));
		Consider(A->GetName(), TEXT("name"));
		Consider(A->GetClass()->GetName(), TEXT("class"));
		Consider(A->GetPathName(), TEXT("path"));
		Consider(TagsJoined, TEXT("tags"));

		if (Best < 1.f)
		{
			continue;
		}
		FHit H;
		H.Score = Best;
		H.ActorPath = A->GetPathName();
		H.Label = A->GetActorLabel();
		H.ClassName = A->GetClass()->GetName();
		H.MatchedField = BestField;
		Hits.Add(MoveTemp(H));
	}

	Hits.Sort([](const FHit& A, const FHit& B) { return A.Score > B.Score; });
	const bool bLowConfidence = (Hits.Num() == 0 || Hits[0].Score < 40.f);
	if (Hits.Num() > MaxResults)
	{
		Hits.SetNum(MaxResults);
	}

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FHit& H : Hits)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("score"), static_cast<double>(H.Score));
		Row->SetStringField(TEXT("actor_path"), H.ActorPath);
		Row->SetStringField(TEXT("label"), H.Label);
		Row->SetStringField(TEXT("class"), H.ClassName);
		Row->SetStringField(TEXT("matched_field"), H.MatchedField);
		Arr.Add(MakeShareable(new FJsonValueObject(Row.ToSharedRef())));
	}

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("tool"), TEXT("scene_fuzzy_search"));
	O->SetStringField(TEXT("query"), Query);
	O->SetArrayField(TEXT("matches"), Arr);
	O->SetNumberField(TEXT("count"), static_cast<double>(Arr.Num()));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_AssetIndexFuzzySearch(const TSharedPtr<FJsonObject>& Args)
{
	FString Query;
	{
		const TArray<const TCHAR*> Aliases = { TEXT("search_string"), TEXT("filter"), TEXT("name_prefix") };
		UnrealAiToolDispatchArgRepair::TryGetStringFieldCanonical(
			Args,
			TEXT("query"),
			Aliases,
			Query);
	}
	if (Query.IsEmpty())
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("query"), TEXT("coin"));
		SuggestedArgs->SetStringField(TEXT("path_prefix"), TEXT("/Game"));
		SuggestedArgs->SetNumberField(TEXT("max_results"), 20.0);
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("asset_index_fuzzy_search: non-empty query string is required (preferred key: 'query'; accepted aliases: 'search_string', 'filter', 'name_prefix')."),
			TEXT("asset_index_fuzzy_search"),
			SuggestedArgs);
	}
	FString PathPrefix(TEXT("/Game"));
	Args->TryGetStringField(TEXT("path_prefix"), PathPrefix);
	if (!PathPrefix.StartsWith(TEXT("/")))
	{
		PathPrefix = TEXT("/") + PathPrefix;
	}
	int32 MaxResults = 80;
	double MR = 0.0;
	if (Args->TryGetNumberField(TEXT("max_results"), MR))
	{
		MaxResults = FMath::Clamp(static_cast<int32>(MR), 1, 500);
	}
	int32 MaxAssetsToScan = 25000;
	double MS = 0.0;
	if (Args->TryGetNumberField(TEXT("max_assets_to_scan"), MS))
	{
		MaxAssetsToScan = FMath::Clamp(static_cast<int32>(MS), 100, 500000);
	}
	FString ClassSubstring;
	Args->TryGetStringField(TEXT("class_name_substring"), ClassSubstring);
	const FString QLower = Query.ToLower();
	const FString ClassSubstringLower = ClassSubstring.ToLower();
	const bool bHasClassFilter = !ClassSubstringLower.IsEmpty();

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AR = ARM.Get();

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*PathPrefix));
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AR.GetAssets(Filter, Assets);
	const bool bTruncated = Assets.Num() > MaxAssetsToScan;
	if (bTruncated)
	{
		Assets.SetNum(MaxAssetsToScan);
	}

	struct FHit
	{
		float Score = 0.f;
		FString ObjectPath;
		FString AssetName;
		FString ClassPath;
	};

	TArray<FHit> Hits;
	Hits.Reserve(FMath::Min(Assets.Num(), MaxResults * 8));
	for (const FAssetData& AD : Assets)
	{
		if (bHasClassFilter)
		{
			const FString Cls = AD.AssetClassPath.ToString();
			if (!Cls.Contains(ClassSubstringLower, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}
		const FString AssetName = AD.AssetName.ToString();
		const FString ObjectPath = AD.GetObjectPathString();
		const FString ClassPath = AD.AssetClassPath.ToString();
		const FString NameLower = AssetName.ToLower();
		const FString PathLower = ObjectPath.ToLower();
		const FString ClassLower = ClassPath.ToLower();
		TArray<FString> Candidates;
		Candidates.Reserve(3);
		Candidates.Add(AssetName);
		Candidates.Add(ObjectPath);
		Candidates.Add(ClassPath);
		float WeightedScore = UnrealAiFuzzySearch::ScoreAgainstCandidates(Query, Candidates);
		// Prefer deterministic lexical wins for constrained projects.
		if (NameLower.Equals(QLower))
		{
			WeightedScore += 120.f;
		}
		else if (NameLower.StartsWith(QLower))
		{
			WeightedScore += 80.f;
		}
		else if (NameLower.Contains(QLower))
		{
			WeightedScore += 35.f;
		}
		if (PathLower.Contains(TEXT("/") + QLower) || PathLower.Contains(QLower + TEXT(".")))
		{
			WeightedScore += 25.f;
		}
		if (bHasClassFilter && ClassLower.Contains(ClassSubstringLower))
		{
			WeightedScore += 15.f;
		}
		const float S = WeightedScore;
		if (S < 1.f)
		{
			continue;
		}
		FHit H;
		H.Score = S;
		H.ObjectPath = AD.GetObjectPathString();
		H.AssetName = AD.AssetName.ToString();
		H.ClassPath = AD.AssetClassPath.ToString();
		Hits.Add(MoveTemp(H));
	}

	Hits.Sort([](const FHit& A, const FHit& B) { return A.Score > B.Score; });
	const bool bLowConfidence = (Hits.Num() == 0 || Hits[0].Score < 40.f);
	if (Hits.Num() > MaxResults)
	{
		Hits.SetNum(MaxResults);
	}

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FHit& H : Hits)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("score"), static_cast<double>(H.Score));
		Row->SetStringField(TEXT("object_path"), H.ObjectPath);
		Row->SetStringField(TEXT("asset_name"), H.AssetName);
		Row->SetStringField(TEXT("class_path"), H.ClassPath);
		Arr.Add(MakeShareable(new FJsonValueObject(Row.ToSharedRef())));
	}

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("tool"), TEXT("asset_index_fuzzy_search"));
	O->SetStringField(TEXT("query"), Query);
	O->SetStringField(TEXT("path_prefix"), PathPrefix);
	O->SetBoolField(TEXT("truncated"), bTruncated);
	if (bLowConfidence)
	{
		O->SetStringField(TEXT("low_confidence_notice"), TEXT("Top matches are low confidence. Try a narrower query or set class_name_substring."));
		O->SetBoolField(TEXT("low_confidence"), true);
	}
	O->SetArrayField(TEXT("matches"), Arr);
	O->SetNumberField(TEXT("count"), static_cast<double>(Arr.Num()));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_SourceSearchFuzzy(const TSharedPtr<FJsonObject>& Args)
{
	FString Query;
	{
		const TArray<const TCHAR*> Aliases = { TEXT("symbol"), TEXT("term"), TEXT("needle"), TEXT("name"), TEXT("search") };
		UnrealAiToolDispatchArgRepair::TryGetStringFieldCanonical(
			Args,
			TEXT("query"),
			Aliases,
			Query);
	}
	if (Query.IsEmpty())
	{
		TSharedPtr<FJsonObject> SuggestedArgs = MakeShared<FJsonObject>();
		SuggestedArgs->SetStringField(TEXT("query"), TEXT("RunAgentTurn"));
		SuggestedArgs->SetStringField(TEXT("glob"), TEXT("*.cpp;*.h"));
		return UnrealAiToolJson::ErrorWithSuggestedCall(
			TEXT("source_search_symbol: non-empty query string is required (aliases: symbol, term, needle, name). Do not retry with empty args; provide a concrete symbol/function/file token."),
			TEXT("source_search_symbol"),
			SuggestedArgs);
	}
	FString Glob;
	Args->TryGetStringField(TEXT("glob"), Glob);
	if (Glob.IsEmpty())
	{
		Glob = TEXT("*.cpp;*.h;*.hpp;*.cs;*.ini;*.json;*.md");
	}
	int32 MaxHits = 60;
	double MH = 0.0;
	if (Args->TryGetNumberField(TEXT("max_hits"), MH))
	{
		MaxHits = FMath::Clamp(static_cast<int32>(MH), 1, 500);
	}
	int32 MaxFilesToScan = 4000;
	double MF = 0.0;
	if (Args->TryGetNumberField(TEXT("max_files_to_scan"), MF))
	{
		MaxFilesToScan = FMath::Clamp(static_cast<int32>(MF), 50, 100000);
	}
	bool bIncludeLineMatches = true;
	Args->TryGetBoolField(TEXT("include_line_matches"), bIncludeLineMatches);

	TArray<FString> ExtAllow;
	ParseGlobList(Glob, ExtAllow);

	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	TArray<FString> Roots;
	CollectProjectSourceRoots(ProjectDir, Roots);
	TArray<FString> AllFiles;
	for (const FString& Root : Roots)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*"), true, false);
		AllFiles.Append(Files);
	}

	TArray<FPathHit> PathHits;
	for (const FString& Full : AllFiles)
	{
		if (!FileMatchesExtList(Full, ExtAllow))
		{
			continue;
		}
		const FString FullNorm = FPaths::ConvertRelativePathToFull(Full);
		const FString PDNorm = FPaths::ConvertRelativePathToFull(ProjectDir);
		FString CleanRel = FullNorm;
		if (CleanRel.StartsWith(PDNorm))
		{
			CleanRel = CleanRel.Mid(PDNorm.Len());
			CleanRel.TrimStartAndEndInline();
			while (CleanRel.StartsWith(TEXT("/")) || CleanRel.StartsWith(TEXT("\\")))
			{
				CleanRel = CleanRel.Mid(1);
			}
		}
		CleanRel.ReplaceInline(TEXT("\\"), TEXT("/"));
		const FString Base = FPaths::GetCleanFilename(Full);
		const float Sr = UnrealAiFuzzySearch::Score(Query, CleanRel);
		const float Sb = UnrealAiFuzzySearch::Score(Query, Base);
		const float S = FMath::Max(Sr, Sb);
		if (S < 1.f)
		{
			continue;
		}
		FPathHit Ph;
		Ph.Score = S;
		Ph.RelativePath = CleanRel;
		Ph.FileName = Base;
		PathHits.Add(MoveTemp(Ph));
	}

	PathHits.Sort([](const FPathHit& A, const FPathHit& B) { return A.Score > B.Score; });

	TArray<TSharedPtr<FJsonValue>> PathArr;
	const int32 PathCap = FMath::Min(MaxHits, PathHits.Num());
	for (int32 I = 0; I < PathCap; ++I)
	{
		const FPathHit& H = PathHits[I];
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("score"), static_cast<double>(H.Score));
		Row->SetStringField(TEXT("relative_path"), H.RelativePath);
		Row->SetStringField(TEXT("file_name"), H.FileName);
		PathArr.Add(MakeShareable(new FJsonValueObject(Row.ToSharedRef())));
	}

	TArray<TSharedPtr<FJsonValue>> LineArr;
	if (bIncludeLineMatches && PathHits.Num() > 0)
	{
		TArray<FLineHit> LineHits;
		const int32 FilesToRead = FMath::Min(MaxFilesToScan, PathHits.Num());
		const int32 MaxLineHits = FMath::Max(10, MaxHits);
		for (int32 Fi = 0; Fi < FilesToRead && LineHits.Num() < MaxLineHits; ++Fi)
		{
			FString RelComb = PathHits[Fi].RelativePath;
			RelComb.TrimStartAndEndInline();
			while (RelComb.StartsWith(TEXT("/")))
			{
				RelComb = RelComb.Mid(1);
			}
			const FString Full = FPaths::Combine(ProjectDir, RelComb);
			FString Content;
			if (!FFileHelper::LoadFileToString(Content, *Full))
			{
				continue;
			}
			if (Content.Len() > 512 * 1024)
			{
				Content.LeftInline(512 * 1024);
			}
			TArray<FString> Lines;
			Content.ParseIntoArrayLines(Lines);
			const int32 MaxLinesPerFile = 400;
			for (int32 Li = 0; Li < Lines.Num() && Li < MaxLinesPerFile; ++Li)
			{
				const float Ls = UnrealAiFuzzySearch::Score(Query, Lines[Li]);
				if (Ls < 35.f)
				{
					continue;
				}
				FLineHit Lh;
				Lh.Score = Ls;
				Lh.RelativePath = PathHits[Fi].RelativePath;
				Lh.LineNumber = Li + 1;
				Lh.LineText = Lines[Li].Left(400);
				LineHits.Add(MoveTemp(Lh));
			}
		}
		LineHits.Sort([](const FLineHit& A, const FLineHit& B) { return A.Score > B.Score; });
		const int32 Ln = FMath::Min(MaxLineHits, LineHits.Num());
		for (int32 I = 0; I < Ln; ++I)
		{
			const FLineHit& H = LineHits[I];
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetNumberField(TEXT("score"), static_cast<double>(H.Score));
			Row->SetStringField(TEXT("relative_path"), H.RelativePath);
			Row->SetNumberField(TEXT("line"), static_cast<double>(H.LineNumber));
			Row->SetStringField(TEXT("line_text"), H.LineText);
			LineArr.Add(MakeShareable(new FJsonValueObject(Row.ToSharedRef())));
		}
	}

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("tool"), TEXT("source_search_symbol"));
	O->SetStringField(TEXT("query"), Query);
	O->SetStringField(TEXT("glob"), Glob);
	O->SetArrayField(TEXT("path_matches"), PathArr);
	O->SetArrayField(TEXT("line_matches"), LineArr);
	O->SetNumberField(TEXT("files_considered"), static_cast<double>(AllFiles.Num()));
	O->SetNumberField(TEXT("path_candidates"), static_cast<double>(PathHits.Num()));
	return UnrealAiToolJson::Ok(O);
}
