#include "Tools/UnrealAiToolDispatch_BuildPackaging.h"

#include "Tools/UnrealAiToolJson.h"

#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

static bool LaunchUatNonBlocking(const FString& UatArguments, uint32& OutPid)
{
	const FString RunUAT = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Build/BatchFiles/RunUAT.bat"));
	if (!FPaths::FileExists(RunUAT))
	{
		return false;
	}
	const FProcHandle H = FPlatformProcess::CreateProc(*RunUAT, *UatArguments, true, false, false, &OutPid, 0, nullptr, nullptr);
	return H.IsValid();
}

FUnrealAiToolInvocationResult UnrealAiDispatch_CookContentForPlatform(const TSharedPtr<FJsonObject>& Args)
{
	FString Platform = TEXT("Win64");
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("platform"), Platform);
	}
	const FString Proj = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString UatArgs = FString::Printf(
		TEXT("BuildCookRun -project=\"%s\" -noP4 -clientconfig=Development -platform=%s -cook -skipbuild"),
		*Proj,
		*Platform);
	uint32 Pid = 0;
	const bool bStarted = LaunchUatNonBlocking(UatArgs, Pid);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), bStarted);
	O->SetNumberField(TEXT("process_id"), static_cast<double>(Pid));
	O->SetStringField(TEXT("note"), TEXT("UAT launched asynchronously; monitor Output Log / Saved/Logs."));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_PackageProject(const TSharedPtr<FJsonObject>& Args)
{
	FString Config = TEXT("Development");
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("configuration"), Config);
	}
	const FString Proj = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	const FString UatArgs = FString::Printf(
		TEXT("BuildCookRun -project=\"%s\" -noP4 -clientconfig=%s -platform=Win64 -build -cook -stage -pak"),
		*Proj,
		*Config);
	uint32 Pid = 0;
	const bool bStarted = LaunchUatNonBlocking(UatArgs, Pid);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), bStarted);
	O->SetNumberField(TEXT("process_id"), static_cast<double>(Pid));
	O->SetStringField(TEXT("note"), TEXT("Packaging started asynchronously; this can take a long time."));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ShaderCompileWait(const TSharedPtr<FJsonObject>& Args)
{
	(void)Args;
#if WITH_EDITOR
	if (GShaderCompilingManager)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}
#endif
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	return UnrealAiToolJson::Ok(O);
}
