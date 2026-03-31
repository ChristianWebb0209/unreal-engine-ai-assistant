#include "Tools/UnrealAiToolProjectPathAllowlist.h"

#include "Harness/UnrealAiHarnessTurnPaths.h"

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
	FString N = RelativePath;
	NormalizeSlashes(N);

	static const FString HarnessPrefix = TEXT("harness_step/");
	if (N.StartsWith(HarnessPrefix, ESearchCase::IgnoreCase))
	{
		const FString Tail = N.Mid(HarnessPrefix.Len());
		if (Tail.IsEmpty())
		{
			OutError = TEXT("harness_step/ requires a path after the prefix");
			return false;
		}
		if (HasDangerousSegment(Tail))
		{
			OutError = TEXT("Path contains invalid segments");
			return false;
		}
		if (!FUnrealAiHarnessTurnPaths::HasCurrentStepOutputDir())
		{
			OutError = TEXT("harness_step/ is only valid during a headed harness turn (output directory active)");
			return false;
		}
		const FString Base = FPaths::ConvertRelativePathToFull(FUnrealAiHarnessTurnPaths::GetCurrentStepOutputDir());
		OutAbsolute = FPaths::ConvertRelativePathToFull(FPaths::Combine(Base, Tail));
		const FString BaseNorm = Base.Replace(TEXT("\\"), TEXT("/"));
		const FString AbsNorm = OutAbsolute.Replace(TEXT("\\"), TEXT("/"));
		if (AbsNorm == BaseNorm)
		{
			return true;
		}
		const FString BaseWithSlash = BaseNorm.EndsWith(TEXT("/")) ? BaseNorm : (BaseNorm + TEXT("/"));
		if (!AbsNorm.StartsWith(BaseWithSlash))
		{
			OutError = TEXT("Resolved path escapes harness step directory");
			return false;
		}
		return true;
	}

	if (!UnrealAiIsAllowedProjectRelativePath(RelativePath, OutError))
	{
		return false;
	}
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
