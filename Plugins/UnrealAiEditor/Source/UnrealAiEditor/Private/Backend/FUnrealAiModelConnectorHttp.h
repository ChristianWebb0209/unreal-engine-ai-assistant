#pragma once

#include "Backend/IUnrealAiModelConnector.h"

class FUnrealAiModelProfileRegistry;

/** GET {base}/models with bearer key — verifies API reachability. */
class FUnrealAiModelConnectorHttp final : public IUnrealAiModelConnector
{
public:
	explicit FUnrealAiModelConnectorHttp(FUnrealAiModelProfileRegistry* InProfiles);

	virtual void TestConnection(FUnrealAiModelTestResultDelegate OnDone) override;

private:
	FUnrealAiModelProfileRegistry* Profiles = nullptr;
};
