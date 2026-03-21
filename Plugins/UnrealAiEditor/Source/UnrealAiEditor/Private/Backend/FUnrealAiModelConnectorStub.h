#pragma once

#include "Backend/IUnrealAiModelConnector.h"

class FUnrealAiModelConnectorStub final : public IUnrealAiModelConnector
{
public:
	virtual void TestConnection(FUnrealAiModelTestResultDelegate OnDone) override;
};
