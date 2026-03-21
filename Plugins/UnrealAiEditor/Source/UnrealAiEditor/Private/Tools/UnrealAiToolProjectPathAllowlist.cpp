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
	if (!N.Contains(TEXT("/")) && Lower.EndsWith(TEXT(".uproject")))
	{
		OutError = FString();
		return true;
	}
	OutError = TEXT("Path must be under Source/, Config/, Content/, Plugins/, or a *.uproject at project root");
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
	const FString ProjectDir = FPaths::ConvertRelativeToPath(FPaths::ProjectDir());
	OutAbsolute = FPaths::ConvertRelativeToPath(FPaths::Combine(ProjectDir, N));
	const FString ProjectDirNorm = ProjectDir.Replace(TEXT("\\"), TEXT("/"));
	const FString AbsNorm = OutAbsolute.Replace(TEXT("\\"), TEXT("/"));
	if (!AbsNorm.StartsWith(ProjectDirNorm))
	{
		OutError = TEXT("Resolved path escapes project directory");
		return false;
	}
	return true;
}
