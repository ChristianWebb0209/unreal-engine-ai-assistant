#include "Context/UnrealAiProjectId.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace UnrealAiProjectId
{
	FString GetCurrentProjectId()
	{
		const FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
		if (ProjectPath.IsEmpty())
		{
			return TEXT("no_project");
		}
		uint32 Hash = GetTypeHash(ProjectPath);
		return FString::Printf(TEXT("%08x"), Hash);
	}
}
