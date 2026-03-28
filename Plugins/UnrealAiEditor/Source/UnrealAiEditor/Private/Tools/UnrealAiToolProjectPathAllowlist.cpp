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
	if (!FPaths::IsRelative(N))
	{
		OutError = TEXT("Path must be relative to the project directory (no absolute paths)");
		return false;
	}
	OutError = FString();
	return true;
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
