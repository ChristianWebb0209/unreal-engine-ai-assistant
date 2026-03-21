#include "Tools/UnrealAiFuzzySearch.h"

namespace UnrealAiFuzzySearchInternal
{
	static int32 LevenshteinBounded(const FString& A, const FString& B, int32 MaxDim)
	{
		const int32 M = FMath::Min(A.Len(), MaxDim);
		const int32 N = FMath::Min(B.Len(), MaxDim);
		if (M == 0)
		{
			return N;
		}
		if (N == 0)
		{
			return M;
		}
		TArray<int32> Prev;
		TArray<int32> Cur;
		Prev.SetNumUninitialized(N + 1);
		Cur.SetNumUninitialized(N + 1);
		for (int32 J = 0; J <= N; ++J)
		{
			Prev[J] = J;
		}
		for (int32 I = 1; I <= M; ++I)
		{
			Cur[0] = I;
			for (int32 J = 1; J <= N; ++J)
			{
				const int32 Cost = (A[I - 1] == B[J - 1]) ? 0 : 1;
				Cur[J] = FMath::Min3(Prev[J] + 1, Cur[J - 1] + 1, Prev[J - 1] + Cost);
			}
			Swap(Prev, Cur);
		}
		return Prev[N];
	}

	/** All query chars appear in order (Cursor-style). */
	static float SubsequenceMatchScore(const FString& Q, const FString& C)
	{
		int32 Qi = 0;
		for (int32 I = 0; I < C.Len() && Qi < Q.Len(); ++I)
		{
			if (C[I] == Q[Qi])
			{
				++Qi;
			}
		}
		if (Qi < Q.Len())
		{
			return 0.f;
		}
		const float Density = static_cast<float>(Q.Len()) / static_cast<float>(FMath::Max(1, C.Len()));
		return 82.f + 18.f * FMath::Clamp(Density * 3.f, 0.f, 1.f);
	}
}

void UnrealAiFuzzySearch::FoldCaseInline(FString& S)
{
	S = S.ToLower();
}

float UnrealAiFuzzySearch::Score(const FString& QueryIn, const FString& CandidateIn)
{
	FString Q = QueryIn;
	FString C = CandidateIn;
	Q.TrimStartAndEndInline();
	C.TrimStartAndEndInline();
	Q = Q.ToLower();
	C = C.ToLower();
	if (Q.IsEmpty())
	{
		return 0.f;
	}
	if (C.IsEmpty())
	{
		return 0.f;
	}
	if (C.Contains(Q))
	{
		return 100.f;
	}
	const float Sub = UnrealAiFuzzySearchInternal::SubsequenceMatchScore(Q, C);
	if (Sub > 0.f)
	{
		return Sub;
	}
	const int32 MaxDim = 96;
	if (Q.Len() > MaxDim || C.Len() > MaxDim)
	{
		const FString Qs = Q.Left(MaxDim);
		const FString Cs = C.Left(MaxDim);
		const int32 D = UnrealAiFuzzySearchInternal::LevenshteinBounded(Qs, Cs, MaxDim);
		const int32 Mx = FMath::Max3(Qs.Len(), Cs.Len(), 1);
		return 55.f * (1.f - static_cast<float>(D) / static_cast<float>(Mx));
	}
	const int32 D = UnrealAiFuzzySearchInternal::LevenshteinBounded(Q, C, MaxDim);
	const int32 Mx = FMath::Max3(Q.Len(), C.Len(), 1);
	return 65.f * (1.f - static_cast<float>(D) / static_cast<float>(Mx));
}

float UnrealAiFuzzySearch::ScoreAgainstCandidates(const FString& Query, const TArray<FString>& Candidates)
{
	float Best = 0.f;
	for (const FString& S : Candidates)
	{
		Best = FMath::Max(Best, Score(Query, S));
	}
	return Best;
}
