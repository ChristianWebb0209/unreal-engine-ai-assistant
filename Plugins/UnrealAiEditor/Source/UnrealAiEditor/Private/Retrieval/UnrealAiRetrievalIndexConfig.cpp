#include "Retrieval/UnrealAiRetrievalIndexConfig.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Logging/LogMacros.h"

namespace UnrealAiRetrievalIndexConfig
{
	TArray<FString> GetLegacyDefaultIndexedExtensions()
	{
		return TArray<FString>{
			TEXT(".h"),
			TEXT(".hpp"),
			TEXT(".cpp"),
			TEXT(".md"),
			TEXT(".txt"),
			TEXT(".ini"),
		};
	}

	void NormalizeIndexedExtensions(TArray<FString>& InOutExtensions)
	{
		for (FString& Ext : InOutExtensions)
		{
			Ext.TrimStartAndEndInline();
			Ext = Ext.ToLower();
			if (!Ext.IsEmpty() && !Ext.StartsWith(TEXT(".")))
			{
				Ext = TEXT(".") + Ext;
			}
		}
		InOutExtensions.RemoveAll([](const FString& E) { return E.IsEmpty(); });
	}

	void GetEffectiveIndexedExtensions(const FUnrealAiRetrievalSettings& Settings, TArray<FString>& OutExtLowerWithDot)
	{
		OutExtLowerWithDot.Reset();
		if (Settings.IndexedExtensions.Num() > 0)
		{
			OutExtLowerWithDot = Settings.IndexedExtensions;
			NormalizeIndexedExtensions(OutExtLowerWithDot);
		}
		else
		{
			OutExtLowerWithDot = GetLegacyDefaultIndexedExtensions();
		}
	}

	static void AddRootIfExists(const FString& Abs, TArray<FString>& OutRoots)
	{
		if (!Abs.IsEmpty() && FPaths::DirectoryExists(Abs))
		{
			OutRoots.AddUnique(Abs);
		}
	}

	void AppendIndexRootsForPreset(const FString& ProjectDirAbs, const EUnrealAiRetrievalRootPreset Preset, TArray<FString>& OutRoots)
	{
		const FString ProjectDir = FPaths::ConvertRelativePathToFull(ProjectDirAbs);
		AddRootIfExists(FPaths::Combine(ProjectDir, TEXT("Source")), OutRoots);
		AddRootIfExists(FPaths::Combine(ProjectDir, TEXT("docs")), OutRoots);

		if (Preset == EUnrealAiRetrievalRootPreset::Minimal)
		{
			return;
		}

		AddRootIfExists(FPaths::Combine(ProjectDir, TEXT("Config")), OutRoots);

		const FString PluginsDir = FPaths::Combine(ProjectDir, TEXT("Plugins"));
		if (FPaths::DirectoryExists(PluginsDir))
		{
			TArray<FString> PluginFolders;
			IFileManager::Get().FindFiles(PluginFolders, *FPaths::Combine(PluginsDir, TEXT("*")), false, true);
			for (const FString& Folder : PluginFolders)
			{
				const FString PluginSource = FPaths::Combine(PluginsDir, Folder, TEXT("Source"));
				AddRootIfExists(PluginSource, OutRoots);
			}
		}

		if (Preset == EUnrealAiRetrievalRootPreset::ExtendedRoots)
		{
			AddRootIfExists(FPaths::Combine(ProjectDir, TEXT("Content")), OutRoots);
		}

		const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
		if (!ProjectFile.IsEmpty() && FPaths::FileExists(ProjectFile))
		{
			OutRoots.AddUnique(FPaths::GetPath(ProjectFile));
		}
	}

	void CollectFilesystemIndexPaths(
		const FString& ProjectDirAbs,
		const FUnrealAiRetrievalSettings& Settings,
		TArray<FString>& OutAbsolutePathsSorted,
		int32& OutSkippedFilesDueToCap)
	{
		OutAbsolutePathsSorted.Reset();
		OutSkippedFilesDueToCap = 0;

		TArray<FString> ExtWhitelist;
		GetEffectiveIndexedExtensions(Settings, ExtWhitelist);
		TSet<FString> ExtSet(ExtWhitelist);

		TArray<FString> Roots;
		AppendIndexRootsForPreset(ProjectDirAbs, Settings.RootPreset, Roots);

		TArray<FString> Candidates;
		for (const FString& Root : Roots)
		{
			IFileManager::Get().FindFilesRecursive(Candidates, *Root, TEXT("*.*"), true, false);
		}

		for (FString& Path : Candidates)
		{
			Path = FPaths::ConvertRelativePathToFull(Path);
		}

		Candidates.Sort();

		const int32 MaxFiles = Settings.MaxFilesPerRebuild;
		int32 Taken = 0;
		for (const FString& Path : Candidates)
		{
			const FString Ext = FPaths::GetExtension(Path, true).ToLower();
			if (!ExtSet.Contains(Ext))
			{
				continue;
			}
			if (MaxFiles > 0 && Taken >= MaxFiles)
			{
				++OutSkippedFilesDueToCap;
				continue;
			}
			OutAbsolutePathsSorted.Add(Path);
			++Taken;
		}
	}

	static FString NormalizePathSlashes(FString P)
	{
		P.ReplaceInline(TEXT("\\"), TEXT("/"));
		return P;
	}

	static FString RelativePathUnderProject(const FString& AbsolutePath, const FString& ProjectDirFull)
	{
		FString Abs = NormalizePathSlashes(FPaths::ConvertRelativePathToFull(AbsolutePath));
		FString Base = NormalizePathSlashes(FPaths::ConvertRelativePathToFull(ProjectDirFull));
		if (!Base.EndsWith(TEXT("/")))
		{
			Base += TEXT("/");
		}
		if (Abs.StartsWith(Base))
		{
			return Abs.Mid(Base.Len());
		}
		return Abs;
	}

	static int32 BuildPriorityBucket(const FString& RelPath)
	{
		if (RelPath.StartsWith(TEXT("Source/"), ESearchCase::IgnoreCase)
			&& !RelPath.Contains(TEXT("/Plugins/"), ESearchCase::IgnoreCase))
		{
			return 0;
		}
		if (RelPath.Contains(TEXT("/Plugins/"), ESearchCase::IgnoreCase)
			&& RelPath.Contains(TEXT("/Source/"), ESearchCase::IgnoreCase))
		{
			return 1;
		}
		if (RelPath.StartsWith(TEXT("Config/"), ESearchCase::IgnoreCase))
		{
			return 2;
		}
		if (RelPath.StartsWith(TEXT("docs/"), ESearchCase::IgnoreCase))
		{
			return 3;
		}
		if (RelPath.StartsWith(TEXT("Content/"), ESearchCase::IgnoreCase))
		{
			return 4;
		}
		return 5;
	}

	void SortFilesystemIndexPathsForBuildPriority(const FString& ProjectDirAbs, TArray<FString>& InOutAbsolutePaths)
	{
		const FString ProjectDirFull = FPaths::ConvertRelativePathToFull(ProjectDirAbs);
		InOutAbsolutePaths.Sort([&ProjectDirFull](const FString& A, const FString& B)
		{
			const FString RelA = RelativePathUnderProject(A, ProjectDirFull);
			const FString RelB = RelativePathUnderProject(B, ProjectDirFull);
			const int32 Pa = BuildPriorityBucket(RelA);
			const int32 Pb = BuildPriorityBucket(RelB);
			if (Pa != Pb)
			{
				return Pa < Pb;
			}
			return RelA < RelB;
		});
	}

	namespace ChunkingDetail
	{
		static void ClampChunkParamsLocal(int32& ChunkChars, int32& ChunkOverlap)
		{
			ChunkChars = FMath::Clamp(ChunkChars, 128, 32000);
			ChunkOverlap = FMath::Max(0, ChunkOverlap);
			if (ChunkOverlap >= ChunkChars)
			{
				ChunkOverlap = FMath::Max(0, ChunkChars - 1);
			}
		}

		static FString MakeChunkIdLocal(const FString& SourcePath, const int32 ChunkStart, const FString& ChunkText)
		{
			return SourcePath + TEXT(":") + FString::FromInt(ChunkStart) + TEXT(":") + FMD5::HashAnsiString(*ChunkText);
		}
	}

	void ChunkTextFixedWindow(
		const FString& RelativePath,
		const FString& Text,
		int32 ChunkChars,
		int32 OverlapChars,
		const int32 MaxChunksPerSource,
		TArray<FUnrealAiVectorChunkRow>& OutChunks)
	{
		using namespace ChunkingDetail;
		ClampChunkParamsLocal(ChunkChars, OverlapChars);
		OutChunks.Reset();
		if (Text.IsEmpty())
		{
			return;
		}
		int32 Start = 0;
		while (Start < Text.Len())
		{
			if (MaxChunksPerSource > 0 && OutChunks.Num() >= MaxChunksPerSource)
			{
				UE_LOG(
					LogTemp,
					Verbose,
					TEXT("Retrieval index: chunk cap reached for source %s (%d chunks)."),
					*RelativePath,
					MaxChunksPerSource);
				break;
			}
			const int32 Len = FMath::Min(ChunkChars, Text.Len() - Start);
			const FString ChunkText = Text.Mid(Start, Len);
			FUnrealAiVectorChunkRow Row;
			Row.SourcePath = RelativePath;
			Row.Text = ChunkText;
			Row.ContentHash = FMD5::HashAnsiString(*ChunkText);
			Row.ChunkId = MakeChunkIdLocal(RelativePath, Start, ChunkText);
			OutChunks.Add(MoveTemp(Row));
			if (Start + Len >= Text.Len())
			{
				break;
			}
			Start += (ChunkChars - OverlapChars);
		}
	}

	int32 GetIndexBuildWaveCount()
	{
		return 5;
	}

	int32 GetIndexBuildWaveForSource(const FString& ProjectDirAbs, const FString& SourceKey)
	{
		(void)ProjectDirAbs;
		FString Key = SourceKey;
		Key.ReplaceInline(TEXT("\\"), TEXT("/"));

		if (Key.StartsWith(TEXT("virtual://"), ESearchCase::IgnoreCase))
		{
			return 4;
		}
		if (Key.StartsWith(TEXT("memory:"), ESearchCase::IgnoreCase))
		{
			return 4;
		}
		if (Key.StartsWith(TEXT("/Game/"), ESearchCase::IgnoreCase) || Key.StartsWith(TEXT("/Engine/"), ESearchCase::IgnoreCase))
		{
			return 4;
		}

		while (Key.StartsWith(TEXT("/")))
		{
			Key.RightChopInline(1, EAllowShrinking::No);
		}

		const int32 B = BuildPriorityBucket(Key);
		switch (B)
		{
		case 0:
			return 0;
		case 1:
			return 1;
		case 2:
		case 3:
			return 2;
		case 4:
			return 3;
		default:
			return 4;
		}
	}
}
