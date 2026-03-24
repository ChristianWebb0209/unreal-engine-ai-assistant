#include "Tools/UnrealAiToolInvocationArgs.h"

bool UnrealAiConsumeFocusedFlag(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return false;
	}
	bool bFocused = false;
	if (!Args->TryGetBoolField(TEXT("focused"), bFocused))
	{
		return false;
	}
	Args->RemoveField(TEXT("focused"));
	return bFocused;
}
