#include "Tools/UnrealAiToolKPolicy.h"

int32 UnrealAiToolKPolicy::ComputeEffectiveK(const TArray<float>& SortedDescScoresNonGuard, int32 KMin, int32 KMax)
{
	KMin = FMath::Max(1, KMin);
	KMax = FMath::Max(KMin, KMax);
	const int32 N = SortedDescScoresNonGuard.Num();
	if (N == 0)
	{
		return KMin;
	}
	if (N == 1)
	{
		return 1;
	}
	const float S0 = SortedDescScoresNonGuard[0];
	const float S1 = SortedDescScoresNonGuard[1];
	const float Den = FMath::Max(1.e-6f, S0);
	const float Margin = (S0 - S1) / Den;
	int32 K = KMax;
	// Prefer a tight tool set when the top match is clearly ahead (saves appendix tokens).
	if (Margin > 0.14f)
	{
		K = KMin;
	}
	else if (Margin > 0.06f)
	{
		K = (KMin + KMax) / 2;
	}
	K = FMath::Clamp(K, KMin, KMax);
	return FMath::Min(K, N);
}
