#pragma once

#include "CoreMinimal.h"
#include "Bridge/UnrealAiMcpToolPack.h"
#include "Dom/JsonObject.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "HttpServerRequest.h"
#include "IHttpRouter.h"

class FUnrealAiBackendRegistry;
class UUnrealAiEditorSettings;

/** Localhost HTTP bridge (Function B) for external MCP clients. */
class FUnrealAiMcpBridge
{
public:
	void Start(FUnrealAiBackendRegistry* InRegistry, const UUnrealAiEditorSettings* Settings);
	void Stop();

	bool IsRunning() const { return bRunning; }

	uint32 GetPort() const { return BoundPort; }

	const FString& GetTokenFilePath() const { return TokenFilePath; }

	const FUnrealAiMcpToolPack& GetToolPack() const { return ToolPack; }

	FUnrealAiBackendRegistry* GetRegistry() const { return Registry; }

	const FString& GetAuthToken() const { return AuthToken; }

	double GetInvokeTimeoutSeconds() const { return InvokeTimeoutSeconds; }

private:
	bool BindRoutes();
	void UnbindRoutes();

	bool HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleListTools(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleInvoke(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	static FString SerializeJsonObject(const TSharedPtr<FJsonObject>& Obj);

	bool AuthorizeRequest(const FHttpServerRequest& Request, FString& OutErrorCode) const;

	FString BuildJsonEnvelope(
		bool bOk,
		const FString& ToolId,
		int32 DurationMs,
		const FString& ErrorCode,
		const FString& ErrorMessage,
		const TSharedPtr<FJsonObject>& Result) const;

	FUnrealAiBackendRegistry* Registry = nullptr;
	FUnrealAiMcpToolPack ToolPack;
	FString AuthToken;
	FString TokenFilePath;
	TSharedPtr<class IHttpRouter> HttpRouter;
	TArray<FHttpRouteHandle> RouteHandles;
	uint32 BoundPort = 0;
	double InvokeTimeoutSeconds = 120.0;
	bool bRunning = false;
};
