#pragma once

#include "CoreMinimal.h"

class UClass;
class UFunction;

/** Shared Blueprint call_function resolution for graph_patch and blueprint_apply_ir. */
namespace UnrealAiBlueprintFunctionResolve
{
	/**
	 * Resolve a Blueprint-exposed UFunction on Class (including super types).
	 * Normalizes InOutFunctionName: strips mistaken "SomeClass::Member" or "/Script/...::Member" fragments
	 * often emitted by LLMs as a single string.
	 */
	UFunction* ResolveCallFunction(UClass* Class, FString& InOutFunctionName);

	/**
	 * When K2Node_Event event_override omits outer_class_path, supply the declaring class for common
	 * Actor/Pawn lifecycle overrides (ReceiveBeginPlay, ReceiveTick, etc.).
	 */
	bool TryDefaultOuterClassPathForK2Event(const FString& FunctionName, FString& OutOuterClassPath);

	/** If function_name is empty and class_path looks like "/Script/Engine.Actor::GetActorLocation", split it. */
	void SplitCombinedClassPathAndFunctionName(FString& InOutClassPath, FString& InOutFunctionName);
}
