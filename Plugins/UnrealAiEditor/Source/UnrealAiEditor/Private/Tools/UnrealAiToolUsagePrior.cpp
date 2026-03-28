#include "Tools/UnrealAiToolUsagePrior.h"

#include "HAL/CriticalSection.h"

namespace UnrealAiToolUsagePriorPriv
{
	static FCriticalSection Mutex;
	static TMap<FString, TPair<int32, int32>> OkFailByTool;

	static TPair<int32, int32>& GetOrAdd(const FString& ToolId)
	{
		return OkFailByTool.FindOrAdd(ToolId);
	}
} // namespace UnrealAiToolUsagePriorPriv

float UnrealAiToolUsagePrior::GetOperationalPrior01(const FString& ToolId)
{
	FScopeLock Lock(&UnrealAiToolUsagePriorPriv::Mutex);
	const TPair<int32, int32> P = UnrealAiToolUsagePriorPriv::OkFailByTool.FindRef(ToolId);
	const int32 Ok = P.Key;
	const int32 Fail = P.Value;
	const int32 Tot = Ok + Fail;
	if (Tot < 2)
	{
		return 0.5f;
	}
	return static_cast<float>(Ok) / static_cast<float>(Tot);
}

void UnrealAiToolUsagePrior::NoteSessionOutcome(const FString& ToolId, bool bOperationalOk)
{
	FScopeLock Lock(&UnrealAiToolUsagePriorPriv::Mutex);
	TPair<int32, int32>& P = UnrealAiToolUsagePriorPriv::GetOrAdd(ToolId);
	if (bOperationalOk)
	{
		++P.Key;
	}
	else
	{
		++P.Value;
	}
}
