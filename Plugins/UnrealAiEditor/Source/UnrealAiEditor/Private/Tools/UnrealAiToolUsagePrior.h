#pragma once

#include "CoreMinimal.h"

/** §2.5 — session operational prior; file-backed decay is deferred (P3). */
namespace UnrealAiToolUsagePrior
{
	float GetOperationalPrior01(const FString& ToolId);
	void NoteSessionOutcome(const FString& ToolId, bool bOperationalOk);
}
