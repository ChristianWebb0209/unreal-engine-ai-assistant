#pragma once

#include "CoreMinimal.h"

struct FUnrealAiRetrievalDiagnosticsRow
{
	FString SourceId;
	int32 ChunkCount = 0;
};

namespace UnrealAiRetrievalDiagnostics
{
	/** Writes lightweight diagnostics under the project vector folder; never writes raw chunk text. */
	void WriteIndexDiagnostics(
		const FString& ProjectId,
		const FString& State,
		int32 FilesIndexed,
		int32 ChunksIndexed,
		const TArray<FUnrealAiRetrievalDiagnosticsRow>& TopSources);
}
