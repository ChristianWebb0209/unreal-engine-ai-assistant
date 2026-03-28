#pragma once

#include "CoreMinimal.h"

/** §P0 — append-only JSONL for offline usage prior training. */
namespace UnrealAiToolUsageEventLogger
{
	void AppendOperationalEvent(const FString& QueryHash, const FString& ToolId, bool bOperationalOk, const FString& ThreadId);
}
