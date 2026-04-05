#pragma once

#include "CoreMinimal.h"

namespace UnrealAiPluginHttpConcurrency
{
	/** Soft cap on simultaneous outbound HTTP requests started by Unreal AI Editor (LLM, embeddings, probes). */
	constexpr int32 kMaxConcurrentOutboundRequests = 4;

	/** Game thread: blocks briefly until a slot is available, then ProcessRequest may start. */
	void AcquireOutboundSlot();

	/** Call once per successful Acquire when the request lifecycle ends (or failed to start). */
	void ReleaseOutboundSlot();
}
