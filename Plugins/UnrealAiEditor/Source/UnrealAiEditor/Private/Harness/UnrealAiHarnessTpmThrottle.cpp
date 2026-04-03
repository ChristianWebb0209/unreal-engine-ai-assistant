#include "Harness/UnrealAiHarnessTpmThrottle.h"

#include "Harness/ILlmTransport.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Misc/UnrealAiRuntimeDefaults.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"

namespace UnrealAiHarnessTpmThrottle
{
	namespace
	{
		FCriticalSection GTpmMutex;
		TArray<TPair<double, int32>> GChatEvents;
		TArray<TPair<double, int32>> GEmbeddingEvents;

		static double GetWindowSeconds()
		{
			return static_cast<double>(UnrealAiRuntimeDefaults::HarnessTpmWindowSec);
		}

		static bool IsStrictTpmEnabled()
		{
			return UnrealAiRuntimeDefaults::HarnessTpmStrict;
		}

		static int32 GetSafetyBasisPoints(const bool bStrict)
		{
			return bStrict ? UnrealAiRuntimeDefaults::HarnessTpmEstimateSafetyBpStrict
						   : UnrealAiRuntimeDefaults::HarnessTpmEstimateSafetyBpLoose;
		}

		static int32 GetAdmissionHeadroomTokens()
		{
			return UnrealAiRuntimeDefaults::HarnessTpmHeadroomTokens;
		}

		static int32 EffectiveBudgetTpm(const int32 BudgetTpm)
		{
			if (BudgetTpm <= 0)
			{
				return 0;
			}
			const int32 H = GetAdmissionHeadroomTokens();
			return FMath::Max(0, BudgetTpm - H);
		}

		static void GetBudgets(int32& OutChatTpm, int32& OutEmbedTpm)
		{
			OutChatTpm = UnrealAiRuntimeDefaults::HarnessTpmPerMinute;
			const int32 EmbedRaw = UnrealAiRuntimeDefaults::HarnessEmbeddingTpmPerMinute;
			if (EmbedRaw > 0)
			{
				OutEmbedTpm = EmbedRaw;
			}
			else if (OutChatTpm > 0)
			{
				OutEmbedTpm = OutChatTpm;
			}
			else
			{
				OutEmbedTpm = 0;
			}
		}

		static int32 ApplySafetyToPromptTokens(const int32 PromptTokensEst, const bool bStrict)
		{
			const int32 Bp = GetSafetyBasisPoints(bStrict);
			const int64 Scaled = (static_cast<int64>(PromptTokensEst) * static_cast<int64>(Bp) + 99LL) / 100LL;
			return static_cast<int32>(FMath::Clamp(Scaled, 1LL, static_cast<int64>(INT32_MAX)));
		}

		static void PruneAndSum(double Now, double WindowSec, TArray<TPair<double, int32>>& Events, int32& OutSum)
		{
			const double Cutoff = Now - WindowSec;
			int32 Sum = 0;
			for (int32 i = 0; i < Events.Num();)
			{
				if (Events[i].Key < Cutoff)
				{
					Events.RemoveAtSwap(i);
					continue;
				}
				Sum += Events[i].Value;
				++i;
			}
			OutSum = Sum;
		}

		static double OldestEventTime(const TArray<TPair<double, int32>>& Events)
		{
			if (Events.Num() == 0)
			{
				return 0.0;
			}
			double T = Events[0].Key;
			for (int32 i = 1; i < Events.Num(); ++i)
			{
				T = FMath::Min(T, Events[i].Key);
			}
			return T;
		}

		static void WaitForWindowRoom(
			const TCHAR* LogLabel,
			TArray<TPair<double, int32>>& Events,
			const int32 BudgetTpm,
			const int32 EstimateTokens,
			const double WindowSec)
		{
			const int32 Cap = EffectiveBudgetTpm(BudgetTpm);
			if (Cap <= 0 || EstimateTokens <= 0)
			{
				return;
			}

			const double kMaxSleepSec = 120.0;
			const double kMinSlice = 0.05;
			double LogAccum = 0.0;

			for (int32 Iter = 0; Iter < 100000; ++Iter)
			{
				const double Now = FPlatformTime::Seconds();
				int32 Sum = 0;
				PruneAndSum(Now, WindowSec, Events, Sum);

				if (Sum + EstimateTokens <= Cap)
				{
					if (LogAccum > 0.5)
					{
						UE_LOG(
							LogTemp,
							Display,
							TEXT("UnrealAi harness TPM: %s budget ok after %.1fs wait (window_sum=%d est=%d budget=%d)."),
							LogLabel,
							LogAccum,
							Sum,
							EstimateTokens,
							Cap);
					}
					return;
				}

				if (Events.Num() == 0)
				{
					UE_LOG(
						LogTemp,
						Warning,
						TEXT("UnrealAi harness TPM: %s estimate %d exceeds admission cap %d (budget=%d) with empty window; proceeding."),
						LogLabel,
						EstimateTokens,
						Cap,
						BudgetTpm);
					return;
				}

				const double Oldest = OldestEventTime(Events);
				double WaitSec = (Oldest + WindowSec) - Now + 0.001;
				if (WaitSec < kMinSlice)
				{
					WaitSec = kMinSlice;
				}
				WaitSec = FMath::Min(WaitSec, kMaxSleepSec);

				if (LogAccum < 0.01)
				{
					UE_LOG(
						LogTemp,
						Warning,
						TEXT("UnrealAi harness TPM: %s waiting %.2fs (window_sum=%d + est=%d > cap=%d, budget=%d)."),
						LogLabel,
						WaitSec,
						Sum,
						EstimateTokens,
						Cap,
						BudgetTpm);
				}
				LogAccum += WaitSec;
				FPlatformProcess::SleepNoStats(static_cast<float>(WaitSec));
			}

			UE_LOG(LogTemp, Error, TEXT("UnrealAi harness TPM: %s wait loop exceeded iteration cap."), LogLabel);
		}

		static void RecordEvent(TArray<TPair<double, int32>>& Events, int32 Tokens)
		{
			if (Tokens <= 0)
			{
				return;
			}
			const double Now = FPlatformTime::Seconds();
			Events.Emplace(Now, Tokens);
		}

		/** Token estimate for one embedding `input` string (matches MaybeWaitBeforeEmbeddingRequest before WaitForWindowRoom). */
		static int32 EstimateEmbeddingInputTokens(const int32 InputUtf16CharCount)
		{
			const bool bStrict = IsStrictTpmEnabled();
			const int64 Chars = static_cast<int64>(FMath::Max(0, InputUtf16CharCount));
			int32 PromptEst = 1;
			if (bStrict)
			{
				const int32 Div = UnrealAiRuntimeDefaults::HarnessTpmPromptDivisor;
				const int64 Ceil = (Chars + static_cast<int64>(Div) - 1) / static_cast<int64>(FMath::Max(1, Div));
				PromptEst = static_cast<int32>(FMath::Min<int64>(FMath::Max(1LL, Ceil), INT32_MAX));
				const int32 Overhead = UnrealAiRuntimeDefaults::HarnessTpmEmbedOverheadTokens;
				PromptEst = FMath::Min(INT32_MAX, PromptEst + Overhead);
			}
			else
			{
				const int32 Div = 4;
				const int64 Ceil = (Chars + static_cast<int64>(Div) - 1) / static_cast<int64>(Div);
				PromptEst = static_cast<int32>(FMath::Min<int64>(FMath::Max(1LL, Ceil), INT32_MAX));
			}
			return ApplySafetyToPromptTokens(PromptEst, bStrict);
		}
	}

	int32 EstimateChatFootprintTokens(const FUnrealAiLlmRequest& Req, int32 CharPerTokenApprox)
	{
		int64 Chars = 0;
		for (const FUnrealAiConversationMessage& M : Req.Messages)
		{
			Chars += static_cast<int64>(M.Content.Len());
			for (const FUnrealAiToolCallSpec& Tc : M.ToolCalls)
			{
				Chars += static_cast<int64>(Tc.Name.Len());
				Chars += static_cast<int64>(Tc.ArgumentsJson.Len());
				Chars += static_cast<int64>(Tc.Id.Len());
			}
			Chars += static_cast<int64>(M.ToolCallId.Len());
		}
		Chars += static_cast<int64>(Req.ToolsJsonArray.Len());
		Chars += static_cast<int64>(Req.ApiModelName.Len());

		const bool bStrict = IsStrictTpmEnabled();
		int32 PromptEst = 1;
		if (bStrict)
		{
			// Pessimistic chars→tokens (smaller divisor ⇒ more tokens) + JSON/chat envelope overhead.
			const int32 Div = UnrealAiRuntimeDefaults::HarnessTpmPromptDivisor;
			const int64 Ceil = (Chars + static_cast<int64>(Div) - 1) / static_cast<int64>(Div);
			PromptEst = static_cast<int32>(FMath::Min<int64>(Ceil, INT32_MAX));
			const int32 Overhead = UnrealAiRuntimeDefaults::HarnessTpmChatOverheadTokens;
			PromptEst = FMath::Min(INT32_MAX, PromptEst + Overhead);
		}
		else
		{
			const int32 Div = FMath::Max(1, CharPerTokenApprox);
			const int64 Ceil = (Chars + static_cast<int64>(Div) - 1) / static_cast<int64>(Div);
			PromptEst = static_cast<int32>(FMath::Min<int64>(Ceil, INT32_MAX));
		}

		const int32 PromptSafe = ApplySafetyToPromptTokens(FMath::Max(1, PromptEst), bStrict);
		const int64 Total = static_cast<int64>(PromptSafe) + static_cast<int64>(FMath::Max(0, Req.MaxOutputTokens));
		return static_cast<int32>(FMath::Min<int64>(Total, INT32_MAX));
	}

	void MaybeWaitBeforeChatRequest(const FUnrealAiLlmRequest& Req, int32 CharPerTokenApprox)
	{
		int32 ChatBudget = 0;
		int32 EmbedBudget = 0;
		GetBudgets(ChatBudget, EmbedBudget);
		(void)EmbedBudget;
		if (ChatBudget <= 0)
		{
			return;
		}

		const int32 Est = EstimateChatFootprintTokens(Req, CharPerTokenApprox);
		FScopeLock Lock(&GTpmMutex);
		WaitForWindowRoom(TEXT("chat"), GChatEvents, ChatBudget, Est, GetWindowSeconds());
	}

	void RecordChatCompletionTokens(int32 TotalTokens)
	{
		int32 ChatBudget = 0;
		int32 EmbedBudget = 0;
		GetBudgets(ChatBudget, EmbedBudget);
		(void)EmbedBudget;
		if (ChatBudget <= 0 || TotalTokens <= 0)
		{
			return;
		}
		FScopeLock Lock(&GTpmMutex);
		RecordEvent(GChatEvents, TotalTokens);
	}

	void MaybeWaitBeforeEmbeddingRequest(int32 InputUtf16CharCount)
	{
		int32 ChatBudget = 0;
		int32 EmbedBudget = 0;
		GetBudgets(ChatBudget, EmbedBudget);
		(void)ChatBudget;
		if (EmbedBudget <= 0)
		{
			return;
		}
		const int32 Est = EstimateEmbeddingInputTokens(InputUtf16CharCount);
		FScopeLock Lock(&GTpmMutex);
		WaitForWindowRoom(TEXT("embedding"), GEmbeddingEvents, EmbedBudget, Est, GetWindowSeconds());
	}

	void MaybeWaitBeforeEmbeddingBatchRequest(const TArray<FString>& InputTexts)
	{
		int32 ChatBudget = 0;
		int32 EmbedBudget = 0;
		GetBudgets(ChatBudget, EmbedBudget);
		(void)ChatBudget;
		if (EmbedBudget <= 0 || InputTexts.Num() <= 0)
		{
			return;
		}
		int64 SumEst = 0;
		for (const FString& T : InputTexts)
		{
			SumEst += static_cast<int64>(EstimateEmbeddingInputTokens(T.Len()));
			if (SumEst >= INT32_MAX)
			{
				SumEst = INT32_MAX;
				break;
			}
		}
		const int32 Est = static_cast<int32>(FMath::Min<int64>(SumEst, INT32_MAX));
		FScopeLock Lock(&GTpmMutex);
		WaitForWindowRoom(TEXT("embedding_batch"), GEmbeddingEvents, EmbedBudget, FMath::Max(1, Est), GetWindowSeconds());
	}

	void RecordEmbeddingTokens(int32 TotalTokens)
	{
		int32 ChatBudget = 0;
		int32 EmbedBudget = 0;
		GetBudgets(ChatBudget, EmbedBudget);
		(void)ChatBudget;
		if (EmbedBudget <= 0 || TotalTokens <= 0)
		{
			return;
		}
		FScopeLock Lock(&GTpmMutex);
		RecordEvent(GEmbeddingEvents, TotalTokens);
	}
}
