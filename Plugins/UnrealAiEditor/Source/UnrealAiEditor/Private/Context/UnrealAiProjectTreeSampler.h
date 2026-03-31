#pragma once

#include "CoreMinimal.h"
#include "Context/AgentContextTypes.h"

namespace UnrealAiProjectTreeSampler
{
	/** Refresh interval for project tree sampling in minutes. */
	inline constexpr int32 RefreshMinutes = 5;

	/** Best-effort update from Asset Registry; returns true when a new sample was taken. */
	bool MaybeRefreshProjectTreeSummary(
		const FString& ProjectId,
		FProjectTreeSummary& InOutSummary,
		bool bForceRefresh = false,
		const FString& RefreshReason = FString());
	/** Shared per-project cache used by callers outside context-service instance boundaries. */
	const FProjectTreeSummary& GetOrRefreshProjectSummary(
		const FString& ProjectId,
		bool bForceRefresh = false,
		const FString& RefreshReason = FString());

	/** Determine preferred package path for a known asset family key (for example blueprint). */
	FString GetPreferredPackagePath(const FProjectTreeSummary& Summary, const FString& AssetFamily, const FString& FallbackPath);
	FString GetPreferredPackagePathForProject(
		const FString& ProjectId,
		const FString& AssetFamily,
		const FString& FallbackPath,
		bool bForceRefresh = false);

	/** Build compact context block that helps path grounding for create flows. */
	FString BuildContextBlurb(const FProjectTreeSummary& Summary);

	/** Footer indicator shown in context + chat/logs. */
	FString BuildFooterLine(const FProjectTreeSummary& Summary);
}
