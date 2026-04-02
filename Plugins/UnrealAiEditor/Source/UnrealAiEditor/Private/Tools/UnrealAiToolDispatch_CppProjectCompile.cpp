#include "Tools/UnrealAiToolDispatch_CppProjectCompile.h"

#include "Tools/UnrealAiToolJson.h"

#include "HAL/PlatformProcess.h"
#include "Misc/Parse.h"
#include "CoreGlobals.h"
#include "Misc/Paths.h"

namespace UnrealAiCppCompilePriv
{
	static void TryParseLineLocation(const FString& Line, TSharedPtr<FJsonObject>& M)
	{
		const int32 LParen = Line.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (LParen == INDEX_NONE || LParen == 0)
		{
			return;
		}
		const int32 RParen = Line.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, LParen + 1);
		if (RParen == INDEX_NONE)
		{
			return;
		}
		FString MaybeLine = Line.Mid(LParen + 1, RParen - LParen - 1);
		MaybeLine.TrimStartAndEndInline();
		if (!MaybeLine.IsNumeric())
		{
			return;
		}
		M->SetNumberField(TEXT("line"), static_cast<double>(FCString::Atoi(*MaybeLine)));
		FString File = Line.Left(LParen);
		File.TrimEndInline();
		if (!File.IsEmpty())
		{
			M->SetStringField(TEXT("file"), File);
		}
	}
}

FUnrealAiToolInvocationResult UnrealAiDispatch_CppProjectCompile(const TSharedPtr<FJsonObject>& Args)
{
#if PLATFORM_WINDOWS
	const bool bUnattendedCli = FParse::Param(FCommandLine::Get(), TEXT("unattended"));
	bool bConfirmExternalRebuild = false;
	if (Args.IsValid())
	{
		Args->TryGetBoolField(TEXT("confirm_external_rebuild"), bConfirmExternalRebuild);
	}
	if (!bUnattendedCli && !GIsAutomationTesting && !bConfirmExternalRebuild)
	{
		return UnrealAiToolJson::Error(
			TEXT("cpp_project_compile is blocked in interactive editor sessions unless confirm_external_rebuild:true is set "
				 "(external Build.bat can desync Live Coding and loaded modules). Prefer closing the editor and running "
				 "your build script, or use -unattended / automation for CI-style builds."));
	}

	const FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	if (ProjectPath.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("Could not resolve project .uproject path."));
	}

	FString Target;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("target_name"), Target);
	}
	if (Target.IsEmpty())
	{
		Target = FPaths::GetBaseFilename(ProjectPath) + TEXT("Editor");
	}

	FString Platform = TEXT("Win64");
	FString Config = TEXT("Development");
	if (Args.IsValid())
	{
		FString Tmp;
		if (Args->TryGetStringField(TEXT("platform"), Tmp) && !Tmp.IsEmpty())
		{
			Platform = Tmp;
		}
		if (Args->TryGetStringField(TEXT("configuration"), Tmp) && !Tmp.IsEmpty())
		{
			Config = Tmp;
		}
	}

	const FString EngineDir = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
	const FString BuildBat = FPaths::Combine(EngineDir, TEXT("Build/BatchFiles/Build.bat"));
	if (!FPaths::FileExists(BuildBat))
	{
		return UnrealAiToolJson::Error(
			FString::Printf(TEXT("Build.bat not found (is EngineDir correct?): %s"), *BuildBat));
	}

	const FString Params = FString::Printf(
		TEXT("%s %s %s \"-project=%s\" -waitmutex -NoHotReloadFromIDE"),
		*Target,
		*Platform,
		*Config,
		*ProjectPath);

	FString StdOut;
	FString StdErr;
	int32 ReturnCode = 0;
	const bool bRan = FPlatformProcess::ExecProcess(*BuildBat, *Params, &ReturnCode, &StdOut, &StdErr);
	if (!bRan)
	{
		return UnrealAiToolJson::Error(TEXT("Failed to execute Build.bat (ExecProcess returned false)."));
	}

	FString Combined = MoveTemp(StdOut);
	if (!StdErr.IsEmpty())
	{
		Combined += TEXT("\n--- stderr ---\n");
		Combined += StdErr;
	}

	TArray<TSharedPtr<FJsonValue>> Msgs;
	TArray<FString> Lines;
	Combined.ParseIntoArrayLines(Lines, /*CullEmpty*/ false);
	int32 ErrCount = 0;
	int32 WarnCount = 0;
	for (FString& Line : Lines)
	{
		Line.TrimStartAndEndInline();
		if (Line.IsEmpty())
		{
			continue;
		}
		const FString Lower = Line.ToLower();
		const bool bErr = Lower.Contains(TEXT(" error "))
			|| Lower.Contains(TEXT(": error:"))
			|| Lower.Contains(TEXT(": fatal error "))
			|| Lower.StartsWith(TEXT("error "));
		const bool bWarn = !bErr
			&& (Lower.Contains(TEXT(" warning "))
				|| Lower.Contains(TEXT(": warning "))
				|| Lower.Contains(TEXT(": note:")));
		if (bErr)
		{
			++ErrCount;
		}
		else if (bWarn)
		{
			++WarnCount;
		}
		if (bErr || bWarn)
		{
			TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
			M->SetStringField(TEXT("text"), Line);
			// Rough mapping: Error=3, Warning=2 (matches EMessageSeverity ordering used elsewhere).
			M->SetNumberField(TEXT("severity"), bErr ? 3.0 : 2.0);
			UnrealAiCppCompilePriv::TryParseLineLocation(Line, M);
			Msgs.Add(MakeShareable(new FJsonValueObject(M.ToSharedRef())));
		}
	}

	const int32 MaxRaw = 120000;
	const FString RawTail = Combined.Len() > static_cast<int32>(MaxRaw) ? Combined.Right(MaxRaw) : Combined;

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), ReturnCode == 0);
	O->SetNumberField(TEXT("exit_code"), static_cast<double>(ReturnCode));
	O->SetStringField(TEXT("target_name"), Target);
	O->SetArrayField(TEXT("messages"), Msgs);
	O->SetNumberField(TEXT("compiler_error_count"), static_cast<double>(ErrCount));
	O->SetNumberField(TEXT("compiler_warning_count"), static_cast<double>(WarnCount));
	O->SetStringField(TEXT("raw_log_tail"), RawTail);
	return UnrealAiToolJson::Ok(O);
#else
	(void)Args;
	return UnrealAiToolJson::Error(TEXT("cpp_project_compile is implemented for Windows only in this build."));
#endif
}
