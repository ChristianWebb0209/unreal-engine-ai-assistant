#include "Tools/UnrealAiToolDispatch_Console.h"

#include "Tools/UnrealAiToolJson.h"
#include "UnrealAiEditorSettings.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "HAL/PlatformMisc.h"
#include "UnrealEdGlobals.h"

namespace UnrealAiConsoleCommandInternal
{
	static bool LegacyWideExecEnabled()
	{
		const FString Env = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_CONSOLE_COMMAND_LEGACY_EXEC")).TrimStartAndEnd();
		if (Env == TEXT("1") || Env.Equals(TEXT("true"), ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (Env == TEXT("0") || Env.Equals(TEXT("false"), ESearchCase::IgnoreCase))
		{
			return false;
		}
		if (const UUnrealAiEditorSettings* S = GetDefault<UUnrealAiEditorSettings>())
		{
			return S->bConsoleCommandLegacyWideExec;
		}
		return false;
	}

	static bool IsBlockedLegacySubstring(const FString& Cmd)
	{
		const FString L = Cmd.ToLower();
		if (L.Contains(TEXT("quit")) || L.Contains(TEXT("exit")) || L.Contains(TEXT("r.recompileshaders")) || L.Contains(TEXT("crash")))
		{
			return true;
		}
		return false;
	}

	static const TCHAR* AllowedKeysHint =
		TEXT("stat_fps, stat_unit, stat_gpu; r_vsync with args 0|1; viewmode_lit, viewmode_unlit, viewmode_wireframe");

	static bool BuildExecFromAllowList(const FString& KeyIn, const FString& ArgsIn, FString& OutExec, FString& OutError)
	{
		FString Key = KeyIn;
		Key.TrimStartAndEndInline();
		const FString KeyLower = Key.ToLower();
		FString Args = ArgsIn;
		Args.TrimStartAndEndInline();

		if (KeyLower == TEXT("stat_fps"))
		{
			if (!Args.IsEmpty())
			{
				OutError = TEXT("stat_fps takes no args (omit args)");
				return false;
			}
			OutExec = TEXT("stat fps");
			return true;
		}
		if (KeyLower == TEXT("stat_unit"))
		{
			if (!Args.IsEmpty())
			{
				OutError = TEXT("stat_unit takes no args (omit args)");
				return false;
			}
			OutExec = TEXT("stat unit");
			return true;
		}
		if (KeyLower == TEXT("stat_gpu"))
		{
			if (!Args.IsEmpty())
			{
				OutError = TEXT("stat_gpu takes no args (omit args)");
				return false;
			}
			OutExec = TEXT("stat gpu");
			return true;
		}
		if (KeyLower == TEXT("r_vsync"))
		{
			if (Args != TEXT("0") && Args != TEXT("1"))
			{
				OutError = TEXT("r_vsync requires args \"0\" or \"1\" in the args field");
				return false;
			}
			OutExec = FString::Printf(TEXT("r.VSync %s"), *Args);
			return true;
		}
		if (KeyLower == TEXT("viewmode_lit"))
		{
			if (!Args.IsEmpty())
			{
				OutError = TEXT("viewmode_lit takes no args");
				return false;
			}
			OutExec = TEXT("viewmode lit");
			return true;
		}
		if (KeyLower == TEXT("viewmode_unlit"))
		{
			if (!Args.IsEmpty())
			{
				OutError = TEXT("viewmode_unlit takes no args");
				return false;
			}
			OutExec = TEXT("viewmode unlit");
			return true;
		}
		if (KeyLower == TEXT("viewmode_wireframe"))
		{
			if (!Args.IsEmpty())
			{
				OutError = TEXT("viewmode_wireframe takes no args");
				return false;
			}
			OutExec = TEXT("viewmode wireframe");
			return true;
		}

		OutError = FString::Printf(TEXT("Unknown command key. Allowed: %s"), AllowedKeysHint);
		return false;
	}
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ConsoleCommand(const TSharedPtr<FJsonObject>& Args)
{
	using namespace UnrealAiConsoleCommandInternal;

	FString CmdKey;
	if (!Args->TryGetStringField(TEXT("command"), CmdKey) || CmdKey.IsEmpty())
	{
		Args->TryGetStringField(TEXT("cmd"), CmdKey);
	}
	if (CmdKey.IsEmpty())
	{
		return UnrealAiToolJson::Error(
			TEXT("command is required: an allow-list key (e.g. stat_fps). See catalog for keys. Legacy wide exec: enable editor setting or UNREAL_AI_CONSOLE_COMMAND_LEGACY_EXEC=1"));
	}

	FString ArgsField;
	Args->TryGetStringField(TEXT("args"), ArgsField);

	if (LegacyWideExecEnabled())
	{
		FString Cmd = CmdKey;
		Cmd.TrimStartAndEndInline();
		ArgsField.TrimStartAndEndInline();
		if (!ArgsField.IsEmpty())
		{
			Cmd.AppendChar(TEXT(' '));
			Cmd += ArgsField;
		}
		if (IsBlockedLegacySubstring(Cmd))
		{
			return UnrealAiToolJson::Error(TEXT("Command blocked by policy (quit/exit/shader rebuild/crash)"));
		}
		if (!GEngine)
		{
			return UnrealAiToolJson::Error(TEXT("GEngine not available"));
		}
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		const bool bOk = GEngine->Exec(World, *Cmd, *GLog);
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), bOk);
		O->SetStringField(TEXT("command"), Cmd);
		O->SetBoolField(TEXT("legacy_wide_exec"), true);
		return UnrealAiToolJson::Ok(O);
	}

	FString ExecLine;
	FString Err;
	if (!BuildExecFromAllowList(CmdKey, ArgsField, ExecLine, Err))
	{
		return UnrealAiToolJson::Error(Err);
	}
	if (!GEngine)
	{
		return UnrealAiToolJson::Error(TEXT("GEngine not available"));
	}
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	const bool bOk = GEngine->Exec(World, *ExecLine, *GLog);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), bOk);
	O->SetStringField(TEXT("command_key"), CmdKey);
	O->SetStringField(TEXT("executed"), ExecLine);
	return UnrealAiToolJson::Ok(O);
}
