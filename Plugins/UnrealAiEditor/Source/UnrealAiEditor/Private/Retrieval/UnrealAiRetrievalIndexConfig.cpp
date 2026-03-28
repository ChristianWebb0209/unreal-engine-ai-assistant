#include "Retrieval/UnrealAiRetrievalIndexConfig.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

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
}
