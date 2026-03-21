#pragma once

#include "CoreMinimal.h"

DECLARE_DELEGATE_OneParam(FUnrealAiModelTestResultDelegate, bool /*bSuccess*/);

class IUnrealAiModelConnector
{
public:
	virtual ~IUnrealAiModelConnector() = default;

	virtual void TestConnection(FUnrealAiModelTestResultDelegate OnDone) = 0;
};
