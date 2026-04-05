#pragma once

#include "CoreMinimal.h"
#include "HAL/Platform.h"
#include "Misc/CoreMisc.h"

/**
 * Opt-in game-thread (and selected any-thread) scope timing for diagnosing editor hitches.
 * Default on (`unrealai.GtPerf` defaults to 1). Disable: `unrealai.GtPerf 0` or turn off in Editor Settings → Plugins → Unreal AI Editor → Diagnostics.
 * Dump ring buffer: `UnrealAi.GtPerf.Dump` (writes Saved/UnrealAiPerf/*.jsonl). Reset: `UnrealAi.GtPerf.Reset`.
 */
namespace UnrealAiGameThreadPerf
{
	bool IsEnabled();

	/** Current LLM round for correlation (harness sets each DispatchLlm). */
	void SetLlmRoundCorrelation(int32 LlmRound);

	void ResetRecords();

	/** Writes JSONL to Saved/UnrealAiPerf/; returns full path or empty on failure. */
	FString DumpRecordsToSavedDir();

	void PushScope(const TCHAR* ScopeName);
	/** Returns parent scope name (weak pointer to literal) after popping. */
	const TCHAR* PopScope();

	void RecordScope(float DurationMs, FString ScopeName, FString ParentScopeName, bool bOnGameThread);

	class FUnrealAiGtPerfScope
	{
	public:
		enum class EPolicy : uint8
		{
			/** No-op when not on the game thread. */
			GameThreadOnly,
			/** Records from HTTP / worker threads (e.g. SSE parse); does not use the game-thread parent stack. */
			AnyThread
		};

		FUnrealAiGtPerfScope(const TCHAR* InScopeName, EPolicy InPolicy);
		~FUnrealAiGtPerfScope();

		FUnrealAiGtPerfScope(const FUnrealAiGtPerfScope&) = delete;
		FUnrealAiGtPerfScope& operator=(const FUnrealAiGtPerfScope&) = delete;

	private:
		const TCHAR* ScopeName = nullptr;
		EPolicy Policy = EPolicy::GameThreadOnly;
		double StartSeconds = 0.0;
		bool bActive = false;
		bool bPushedStack = false;
	};

	/** Game-thread scope with a dynamic name (copied into the perf ring buffer; safe for tool_id, etc.). */
	class FUnrealAiGtPerfScopeDynamic
	{
	public:
		explicit FUnrealAiGtPerfScopeDynamic(FString InScopeName);
		~FUnrealAiGtPerfScopeDynamic();

		FUnrealAiGtPerfScopeDynamic(const FUnrealAiGtPerfScopeDynamic&) = delete;
		FUnrealAiGtPerfScopeDynamic& operator=(const FUnrealAiGtPerfScopeDynamic&) = delete;

	private:
		FString ScopeName;
		double StartSeconds = 0.0;
		bool bActive = false;
		bool bPushedStack = false;
	};
} // namespace UnrealAiGameThreadPerf

/** Game-thread-only scope; no-op when perf is disabled. */
#define UNREALAI_GT_PERF_SCOPE(ScopeLiteral) \
	::UnrealAiGameThreadPerf::FUnrealAiGtPerfScope PREPROCESSOR_JOIN(UnrealAiGtPerf_, __LINE__)( \
		TEXT(ScopeLiteral), ::UnrealAiGameThreadPerf::FUnrealAiGtPerfScope::EPolicy::GameThreadOnly)

/** Dynamic game-thread scope name (FString expression). */
#define UNREALAI_GT_PERF_SCOPE_DYNAMIC(ScopeFStringExpr) \
	::UnrealAiGameThreadPerf::FUnrealAiGtPerfScopeDynamic PREPROCESSOR_JOIN(UnrealAiGtDyn_, __LINE__)(ScopeFStringExpr)

/** Records from any thread; parent stack only when on game thread. */
#define UNREALAI_ANYTHREAD_PERF_SCOPE(ScopeLiteral) \
	::UnrealAiGameThreadPerf::FUnrealAiGtPerfScope PREPROCESSOR_JOIN(UnrealAiGtPerf_, __LINE__)( \
		TEXT(ScopeLiteral), ::UnrealAiGameThreadPerf::FUnrealAiGtPerfScope::EPolicy::AnyThread)
