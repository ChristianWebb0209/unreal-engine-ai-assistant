#include "Observability/UnrealAiGameThreadPerf.h"

#include "UnrealAiEditorSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CoreMisc.h"

DEFINE_LOG_CATEGORY_STATIC(LogUnrealAiPerf, Log, All);

static TAutoConsoleVariable<int32> CVarUnrealAiGtPerf(
	TEXT("unrealai.GtPerf"),
	1,
	TEXT("Unreal AI: game-thread perf scopes. Default on. 0 = off, 1 = on, -1 = follow Editor settings (Plugins → Unreal AI → Diagnostics)."),
	ECVF_Default);

namespace UnrealAiGameThreadPerf
{
	namespace
	{
		/** Include essentially all scopes in the ring when perf is enabled (sub-ms noise is still filtered). */
		static constexpr float GMinRecordMs = 0.05f;

		struct FRecord
		{
			double WallSeconds = 0.0;
			float DurationMs = 0.f;
			FString Scope;
			FString Parent;
			int32 LlmRound = -1;
			bool bGameThread = true;
		};

		static FCriticalSection GMutex;
		static TArray<FRecord> GRing;
		static int32 GRingMax = 256;
		static std::atomic<int32> GLlmRound{-1};

		static thread_local TArray<const TCHAR*> GScopeStack;

		static bool CheckEnabled()
		{
			const int32 V = CVarUnrealAiGtPerf.GetValueOnAnyThread();
			if (V == 0)
			{
				return false;
			}
			if (V == 1)
			{
				return true;
			}
			if (const UUnrealAiEditorSettings* S = GetDefault<UUnrealAiEditorSettings>())
			{
				return S->bGameThreadPerfLogging;
			}
			return false;
		}

		static void GetThresholds(float& OutLogMs, float& OutHitchMs, int32& OutRingMax)
		{
			OutLogMs = 0.f;
			OutHitchMs = 1000.f;
			OutRingMax = 256;
			if (const UUnrealAiEditorSettings* S = GetDefault<UUnrealAiEditorSettings>())
			{
				OutLogMs = FMath::Max(1.f, S->GameThreadPerfLogThresholdMs);
				OutHitchMs = FMath::Max(OutLogMs, S->GameThreadPerfHitchThresholdMs);
				OutRingMax = FMath::Clamp(S->GameThreadPerfRingMax, 32, 4096);
			}
		}
	} // namespace

	bool IsEnabled()
	{
		return CheckEnabled();
	}

	void SetLlmRoundCorrelation(const int32 LlmRound)
	{
		GLlmRound.store(LlmRound, std::memory_order_relaxed);
	}

	void ResetRecords()
	{
		FScopeLock Lock(&GMutex);
		GRing.Reset();
	}

	void PushScope(const TCHAR* ScopeName)
	{
		GScopeStack.Add(ScopeName);
	}

	const TCHAR* PopScope()
	{
		const TCHAR* Parent = nullptr;
		if (GScopeStack.Num() >= 2)
		{
			Parent = GScopeStack[GScopeStack.Num() - 2];
		}
		if (GScopeStack.Num() > 0)
		{
			GScopeStack.Pop(EAllowShrinking::No);
		}
		return Parent;
	}

	void RecordScope(
		const float DurationMs,
		FString ScopeName,
		FString ParentScopeName,
		const bool bOnGameThread)
	{
		if (!CheckEnabled())
		{
			return;
		}
		float LogMs = 50.f;
		float HitchMs = 1000.f;
		int32 RingMax = 256;
		GetThresholds(LogMs, HitchMs, RingMax);

		const int32 Round = GLlmRound.load(std::memory_order_relaxed);
		const double Wall = FPlatformTime::Seconds();

		if (DurationMs >= GMinRecordMs)
		{
			FScopeLock Lock(&GMutex);
			GRingMax = RingMax;
			while (GRing.Num() >= GRingMax)
			{
				GRing.RemoveAt(0, 1, EAllowShrinking::No);
			}
			FRecord R;
			R.WallSeconds = Wall;
			R.DurationMs = DurationMs;
			R.Scope = ScopeName;
			R.Parent = ParentScopeName;
			R.LlmRound = Round;
			R.bGameThread = bOnGameThread;
			GRing.Add(MoveTemp(R));
		}

		const bool bHitch = DurationMs >= HitchMs;
		const bool bExtraSlow = !bHitch && LogMs > 0.f && DurationMs >= LogMs;
		if (bHitch)
		{
			UE_LOG(
				LogUnrealAiPerf,
				Warning,
				TEXT("HITCH UnrealAiPerf: scope=%s parent=%s ms=%.2f round=%d gt=%s"),
				ScopeName.IsEmpty() ? TEXT("?") : *ScopeName,
				ParentScopeName.IsEmpty() ? TEXT("") : *ParentScopeName,
				DurationMs,
				Round,
				bOnGameThread ? TEXT("1") : TEXT("0"));
		}
		else if (bExtraSlow)
		{
			UE_LOG(
				LogUnrealAiPerf,
				Warning,
				TEXT("UnrealAiPerf: scope=%s parent=%s ms=%.2f round=%d gt=%s"),
				ScopeName.IsEmpty() ? TEXT("?") : *ScopeName,
				ParentScopeName.IsEmpty() ? TEXT("") : *ParentScopeName,
				DurationMs,
				Round,
				bOnGameThread ? TEXT("1") : TEXT("0"));
		}
		else
		{
			UE_LOG(
				LogUnrealAiPerf,
				Display,
				TEXT("UnrealAiPerf: scope=%s parent=%s ms=%.2f round=%d gt=%s"),
				ScopeName.IsEmpty() ? TEXT("?") : *ScopeName,
				ParentScopeName.IsEmpty() ? TEXT("") : *ParentScopeName,
				DurationMs,
				Round,
				bOnGameThread ? TEXT("1") : TEXT("0"));
		}
	}

	FString DumpRecordsToSavedDir()
	{
		const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAiPerf"));
		IFileManager::Get().MakeDirectory(*Dir, true);
		const FString Path = FPaths::Combine(
			Dir,
			FString::Printf(
				TEXT("gt-perf-%s.jsonl"),
				*FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));

		TArray<FString> Lines;
		{
			FScopeLock Lock(&GMutex);
			Lines.Reserve(GRing.Num() + 1);
			Lines.Add(FString::Printf(
				TEXT("{\"kind\":\"unreal_ai_gt_perf_header\",\"engine_pid\":%d,\"count\":%d}"),
				FPlatformProcess::GetCurrentProcessId(),
				GRing.Num()));
			for (const FRecord& R : GRing)
			{
				auto EscStr = [](const FString& S) -> FString
				{
					FString T(S);
					T.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
					T.ReplaceInline(TEXT("\""), TEXT("\\\""));
					return T;
				};
				Lines.Add(FString::Printf(
					TEXT("{\"wall_s\":%.6f,\"ms\":%.3f,\"scope\":\"%s\",\"parent\":\"%s\",\"llm_round\":%d,\"gt\":%s}"),
					R.WallSeconds,
					R.DurationMs,
					*EscStr(R.Scope),
					*EscStr(R.Parent),
					R.LlmRound,
					R.bGameThread ? TEXT("true") : TEXT("false")));
			}
		}
		if (FFileHelper::SaveStringArrayToFile(Lines, *Path))
		{
			return Path;
		}
		return FString();
	}

	FUnrealAiGtPerfScope::FUnrealAiGtPerfScope(const TCHAR* InScopeName, const EPolicy InPolicy)
		: ScopeName(InScopeName)
		, Policy(InPolicy)
	{
		if (!CheckEnabled())
		{
			return;
		}
		if (InPolicy == EPolicy::GameThreadOnly && !IsInGameThread())
		{
			return;
		}
		bActive = true;
		StartSeconds = FPlatformTime::Seconds();
		if (IsInGameThread())
		{
			PushScope(ScopeName);
			bPushedStack = true;
		}
	}

	FUnrealAiGtPerfScope::~FUnrealAiGtPerfScope()
	{
		if (!bActive)
		{
			return;
		}
		const double End = FPlatformTime::Seconds();
		const float Ms = static_cast<float>((End - StartSeconds) * 1000.0);
		const TCHAR* Parent = nullptr;
		if (bPushedStack)
		{
			Parent = PopScope();
		}
		RecordScope(
			Ms,
			ScopeName ? FString(ScopeName) : FString(),
			Parent ? FString(Parent) : FString(),
			IsInGameThread());
	}

	FUnrealAiGtPerfScopeDynamic::FUnrealAiGtPerfScopeDynamic(FString InScopeName)
		: ScopeName(MoveTemp(InScopeName))
	{
		if (!CheckEnabled())
		{
			return;
		}
		if (!IsInGameThread())
		{
			return;
		}
		bActive = true;
		StartSeconds = FPlatformTime::Seconds();
		PushScope(*ScopeName);
		bPushedStack = true;
	}

	FUnrealAiGtPerfScopeDynamic::~FUnrealAiGtPerfScopeDynamic()
	{
		if (!bActive)
		{
			return;
		}
		const double End = FPlatformTime::Seconds();
		const float Ms = static_cast<float>((End - StartSeconds) * 1000.0);
		const TCHAR* ParentPtr = nullptr;
		if (bPushedStack)
		{
			ParentPtr = PopScope();
		}
		RecordScope(
			Ms,
			FString(ScopeName),
			ParentPtr ? FString(ParentPtr) : FString(),
			IsInGameThread());
	}
} // namespace UnrealAiGameThreadPerf
