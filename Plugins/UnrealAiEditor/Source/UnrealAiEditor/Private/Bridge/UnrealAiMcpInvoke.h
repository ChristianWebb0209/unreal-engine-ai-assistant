#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FUnrealAiBackendRegistry;

namespace UnrealAiMcpInvoke
{
	struct FInvokeOutcome
	{
		bool bBridgeOk = false;
		FString ErrorCode;
		FString ErrorMessage;
		TSharedPtr<FJsonObject> ResultObject;
		int32 DurationMs = 0;
	};

	FInvokeOutcome InvokeOnGameThread(
		FUnrealAiBackendRegistry* Registry,
		const FString& ToolId,
		const TSharedPtr<FJsonObject>& Arguments,
		double TimeoutSeconds);
}
