#pragma once

#include "CoreMinimal.h"
#include "Observability/UnrealAiBackgroundOpsLog.h"
#include "Retrieval/UnrealAiRetrievalTypes.h"

namespace UnrealAiRetrievalObservability
{
	inline void EmitQueryStart(const FUnrealAiRetrievalQuery& Query)
	{
		UnrealAiBackgroundOpsLog::EmitLogLine(
			TEXT("retrieval_query_start"),
			TEXT("begin"),
			0.0,
			FString::Printf(TEXT("project=%s thread=%s"), *Query.ProjectId, *Query.ThreadId));
	}

	inline void EmitQueryEnd(const FUnrealAiRetrievalQuery& Query, const FUnrealAiRetrievalQueryResult& Result)
	{
		UnrealAiBackgroundOpsLog::EmitLogLine(
			TEXT("retrieval_query_end"),
			TEXT("done"),
			0.0,
			FString::Printf(
				TEXT("project=%s thread=%s snippets=%d warnings=%d"),
				*Query.ProjectId,
				*Query.ThreadId,
				Result.Snippets.Num(),
				Result.Warnings.Num()));
	}
}
