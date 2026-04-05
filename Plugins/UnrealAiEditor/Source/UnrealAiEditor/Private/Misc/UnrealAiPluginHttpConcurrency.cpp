#include "Misc/UnrealAiPluginHttpConcurrency.h"

#include "HAL/PlatformProcess.h"
#include "Misc/AssertionMacros.h"
#include <atomic>

namespace
{
	std::atomic<int32> GOutboundInflight{0};
}

void UnrealAiPluginHttpConcurrency::AcquireOutboundSlot()
{
	for (;;)
	{
		int32 Cur = GOutboundInflight.load(std::memory_order_acquire);
		if (Cur >= kMaxConcurrentOutboundRequests)
		{
			FPlatformProcess::Sleep(0.002f);
			continue;
		}
		if (GOutboundInflight.compare_exchange_weak(Cur, Cur + 1, std::memory_order_acq_rel))
		{
			return;
		}
	}
}

void UnrealAiPluginHttpConcurrency::ReleaseOutboundSlot()
{
	const int32 Prev = GOutboundInflight.fetch_sub(1, std::memory_order_acq_rel);
	ensure(Prev > 0);
}
