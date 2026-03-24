#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Harness/IToolExecutionHost.h"

/** After a successful tool run, optionally raise Blueprint editors, asset editors, or the IDE. */
void UnrealAiApplyPostToolEditorFocus(
	const FString& ToolId,
	const TSharedPtr<FJsonObject>& Args,
	const FUnrealAiToolInvocationResult& Result);
