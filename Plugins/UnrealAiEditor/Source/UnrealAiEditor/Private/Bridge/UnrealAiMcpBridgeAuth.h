#pragma once

#include "CoreMinimal.h"

class IUnrealAiPersistence;

namespace UnrealAiMcpBridgeAuth
{
	/** Ensures %DataRoot%/mcp/bridge.token exists; returns token or empty on failure. */
	FString EnsureTokenFile(IUnrealAiPersistence* Persistence, FString& OutTokenFilePath);

	bool ValidateBearerToken(const FString& AuthorizationHeader, const FString& ExpectedToken);

	FString GetTokenFilePath(IUnrealAiPersistence* Persistence);
}
