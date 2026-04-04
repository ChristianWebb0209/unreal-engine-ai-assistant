#include "Tools/UnrealAiBlueprintFunctionResolve.h"

#include "UObject/Class.h"

void UnrealAiBlueprintFunctionResolve::SplitCombinedClassPathAndFunctionName(
	FString& InOutClassPath,
	FString& InOutFunctionName)
{
	if (!InOutFunctionName.IsEmpty())
	{
		return;
	}
	const int32 Pos = InOutClassPath.Find(TEXT("::"), ESearchCase::IgnoreCase);
	if (Pos == INDEX_NONE)
	{
		return;
	}
	InOutFunctionName = InOutClassPath.Mid(Pos + 2);
	InOutClassPath = InOutClassPath.Left(Pos);
	InOutClassPath.TrimStartAndEndInline();
	InOutFunctionName.TrimStartAndEndInline();
}

UFunction* UnrealAiBlueprintFunctionResolve::ResolveCallFunction(UClass* Class, FString& InOutFunctionName)
{
	if (!Class)
	{
		return nullptr;
	}
	FString& N = InOutFunctionName;
	N.TrimStartAndEndInline();
	if (N.IsEmpty())
	{
		return nullptr;
	}
	// "AActor::GetActorLocation" or "/Script/Engine.Actor::GetActorLocation" stuffed into function_name
	if (const int32 DoubleColon = N.Find(TEXT("::"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		DoubleColon != INDEX_NONE)
	{
		N = N.Mid(DoubleColon + 2);
		N.TrimStartAndEndInline();
	}
	if (N.IsEmpty())
	{
		return nullptr;
	}
	const FName FnName(*N);
	if (UFunction* Fn = Class->FindFunctionByName(FnName, EIncludeSuperFlag::IncludeSuper))
	{
		return Fn;
	}
	return nullptr;
}

bool UnrealAiBlueprintFunctionResolve::TryDefaultOuterClassPathForK2Event(
	const FString& FunctionName,
	FString& OutOuterClassPath)
{
	const FName F(*FunctionName);
	static const FName NBegin(TEXT("ReceiveBeginPlay"));
	static const FName NTick(TEXT("ReceiveTick"));
	static const FName NEnd(TEXT("ReceiveEndPlay"));
	static const FName NDestroyed(TEXT("ReceiveDestroyed"));
	static const FName NActorBeginOverlap(TEXT("ReceiveActorBeginOverlap"));
	static const FName NActorEndOverlap(TEXT("ReceiveActorEndOverlap"));
	if (F == NBegin || F == NTick || F == NEnd || F == NDestroyed || F == NActorBeginOverlap || F == NActorEndOverlap)
	{
		OutOuterClassPath = TEXT("/Script/Engine.Actor");
		return true;
	}
	static const FName NPossessed(TEXT("ReceivePossessed"));
	static const FName NUnpossessed(TEXT("ReceiveUnpossessed"));
	if (F == NPossessed || F == NUnpossessed)
	{
		OutOuterClassPath = TEXT("/Script/Engine.Pawn");
		return true;
	}
	static const FName NJump(TEXT("Jump"));
	static const FName NLanded(TEXT("Landed"));
	static const FName NReceiveJump(TEXT("ReceiveJump"));
	if (F == NJump || F == NLanded || F == NReceiveJump)
	{
		OutOuterClassPath = TEXT("/Script/Engine.Character");
		return true;
	}
	return false;
}
