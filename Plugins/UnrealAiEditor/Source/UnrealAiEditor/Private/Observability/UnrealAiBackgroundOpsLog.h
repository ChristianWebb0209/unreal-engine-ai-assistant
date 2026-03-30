#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealAiBackgroundOpsLog
{
	/** Build compact structured JSON detail for run.jsonl enforcement_event payloads. */
	FString BuildDetailJson(
		const FString& OpType,
		const FString& Status,
		const FString& ProjectId,
		const FString& ThreadId,
		double DurationMs,
		const TFunctionRef<void(const TSharedPtr<FJsonObject>&)>& FillExtra);

	/** Log one-line background operation summary for editor diagnostics. */
	void EmitLogLine(const FString& OpType, const FString& Status, double DurationMs, const FString& ExtraSummary);
}
