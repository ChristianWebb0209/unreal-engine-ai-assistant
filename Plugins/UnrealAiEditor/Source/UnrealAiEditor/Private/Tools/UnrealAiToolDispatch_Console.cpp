#include "Tools/UnrealAiToolDispatch_Console.h"

#include "Tools/UnrealAiToolJson.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "UnrealEdGlobals.h"

static bool UnrealAiIsConsoleCommandBlocked(const FString& Cmd)
{
	const FString L = Cmd.ToLower();
	if (L.Contains(TEXT("quit")) || L.Contains(TEXT("exit")) || L.Contains(TEXT("r.recompileshaders"))
		|| L.Contains(TEXT("crash")))
	{
		return true;
	}
	return false;
}

FUnrealAiToolInvocationResult UnrealAiDispatch_ConsoleCommand(const TSharedPtr<FJsonObject>& Args)
{
	FString Cmd;
	if (!Args->TryGetStringField(TEXT("command"), Cmd) || Cmd.IsEmpty())
	{
		Args->TryGetStringField(TEXT("cmd"), Cmd);
	}
	if (Cmd.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("command is required"));
	}
	if (UnrealAiIsConsoleCommandBlocked(Cmd))
	{
		return UnrealAiToolJson::Error(TEXT("Command blocked by allow-list policy"));
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
	return UnrealAiToolJson::Ok(O);
}
