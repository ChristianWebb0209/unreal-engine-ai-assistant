#include "Tools/UnrealAiToolProjectPathAllowlist.h"

#include "Misc/Paths.h"

static void NormalizeSlashes(FString& S)
{
	S.ReplaceInline(TEXT("\\"), TEXT("/"));
	while (S.StartsWith(TEXT("/")))
	{
		S = S.Mid(1);
	}
}

static bool HasDangerousSegment(const FString& N)
{
	return N.Contains(TEXT("..")) || N.Contains(TEXT(":/")) || N.Contains(TEXT(":\\"));
}

/** Single path segment, lowercased — e.g. readme.md */
static bool IsBlockedRootLevelSensitiveName(const FString& SegmentLower)
{
	if (SegmentLower.Contains(TEXT("/")))
	{
		return false;
	}
	if (SegmentLower == TEXT(".env") || SegmentLower.EndsWith(TEXT(".pem")) || SegmentLower.EndsWith(TEXT(".pfx"))
		|| SegmentLower.EndsWith(TEXT(".p12")))
	{
		return true;
	}
	// Private keys often use .key; allow .keywords etc. by only blocking common secret filenames
	if (SegmentLower == TEXT("id_rsa") || SegmentLower == TEXT("id_ed25519") || SegmentLower.EndsWith(TEXT("_rsa"))
		|| SegmentLower.EndsWith(TEXT("_ed25519")))
	{
		return true;
	}
	return false;
}

/** Root-only files: README.md, LICENSE, Dockerfile, etc. */
static bool IsAllowedRootLevelDocOrMetaFile(const FString& SegmentLower)
{
	if (SegmentLower.Contains(TEXT("/")))
	{
		return false;
	}
	if (IsBlockedRootLevelSensitiveName(SegmentLower))
	{
		return false;
	}
	static const TCHAR* TextishExt[] = { TEXT(".md"),   TEXT(".txt"),  TEXT(".rst"),  TEXT(".toml"), TEXT(".yaml"),
		                                 TEXT(".yml"),   TEXT(".json"), TEXT(".csv"),  TEXT(".tsv"),  TEXT(".ini"),
		                                 TEXT(".cfg"),  TEXT(".in"),   TEXT(".cmake") };
	for (const TCHAR* Ext : TextishExt)
	{
		if (SegmentLower.EndsWith(Ext))
		{
			return true;
		}
	}
	static const TCHAR* DotMeta[] = { TEXT(".editorconfig"), TEXT(".gitattributes"), TEXT(".gitignore"),
		                              TEXT(".gitmodules"),  TEXT(".clang-format"),   TEXT(".clang-tidy"),
		                              TEXT(".cursorignore"), TEXT(".dockerignore") };
	for (const TCHAR* Name : DotMeta)
	{
		if (SegmentLower == Name)
		{
			return true;
		}
	}
	if (SegmentLower == TEXT("license") || SegmentLower.StartsWith(TEXT("license.")) || SegmentLower == TEXT("copying")
		|| SegmentLower == TEXT("authors") || SegmentLower == TEXT("dockerfile") || SegmentLower == TEXT("makefile")
		|| SegmentLower == TEXT("cmakelists.txt") || SegmentLower == TEXT("contributing")
		|| SegmentLower.StartsWith(TEXT("contributing.")) || SegmentLower == TEXT("changelog")
		|| SegmentLower.StartsWith(TEXT("changelog.")) || SegmentLower == TEXT("readme"))
	{
		return true;
	}
	return false;
}

bool UnrealAiIsAllowedProjectRelativePath(const FString& RelativePath, FString& OutError)
{
	FString N = RelativePath;
	NormalizeSlashes(N);
	if (N.IsEmpty())
	{
		OutError = TEXT("Path is empty");
		return false;
	}
	if (HasDangerousSegment(N))
	{
		OutError = TEXT("Path contains invalid segments");
		return false;
	}
	const FString Lower = N.ToLower();
	if (Lower.StartsWith(TEXT("saved/")) || Lower.StartsWith(TEXT("intermediate/")) || Lower.StartsWith(TEXT("binaries/"))
		|| Lower.StartsWith(TEXT("deriveddatacache/")))
	{
		OutError = TEXT("Path under blocked project folder");
		return false;
	}
	if (Lower.StartsWith(TEXT("source/")) || Lower.StartsWith(TEXT("config/")) || Lower.StartsWith(TEXT("content/"))
		|| Lower.StartsWith(TEXT("plugins/")))
	{
		OutError = FString();
		return true;
	}
	// Common repo / UE project roots next to Source (README, docs, CI, build scripts)
	if (Lower.StartsWith(TEXT("docs/")) || Lower.StartsWith(TEXT(".github/")) || Lower.StartsWith(TEXT("scripts/"))
		|| Lower.StartsWith(TEXT("tools/")) || Lower.StartsWith(TEXT("build/")))
	{
		OutError = FString();
		return true;
	}
	if (!N.Contains(TEXT("/")) && Lower.EndsWith(TEXT(".uproject")))
	{
		OutError = FString();
		return true;
	}
	if (IsAllowedRootLevelDocOrMetaFile(Lower))
	{
		OutError = FString();
		return true;
	}
	OutError = TEXT(
		"Path must be under Source/, Config/, Content/, Plugins/, docs/, .github/, scripts/, tools/, build/, a root "
		"*.uproject, or a root-level doc/meta file (e.g. README.md, LICENSE)");
	return false;
}

bool UnrealAiResolveProjectFilePath(const FString& RelativePath, FString& OutAbsolute, FString& OutError)
{
	if (!UnrealAiIsAllowedProjectRelativePath(RelativePath, OutError))
	{
		return false;
	}
	FString N = RelativePath;
	NormalizeSlashes(N);
	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	OutAbsolute = FPaths::ConvertRelativePathToFull(FPaths::Combine(ProjectDir, N));
	const FString ProjectDirNorm = ProjectDir.Replace(TEXT("\\"), TEXT("/"));
	const FString AbsNorm = OutAbsolute.Replace(TEXT("\\"), TEXT("/"));
	if (!AbsNorm.StartsWith(ProjectDirNorm))
	{
		OutError = TEXT("Resolved path escapes project directory");
		return false;
	}
	return true;
}
