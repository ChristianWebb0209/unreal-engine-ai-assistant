#include "Bridge/UnrealAiMcpBridge.h"

#include "Bridge/UnrealAiMcpBridgeAuth.h"
#include "Bridge/UnrealAiMcpInvoke.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Tools/UnrealAiToolCatalog.h"
#include "UnrealAiEditorSettings.h"

namespace UnrealAiMcpBridgePrivate
{
	static FString BodyBytesToString(const TArray<uint8>& Body)
	{
		if (Body.Num() == 0)
		{
			return FString();
		}
		FUTF8ToTCHAR Convert(reinterpret_cast<const ANSICHAR*>(Body.GetData()), Body.Num());
		return FString(Convert.Length(), Convert.Get());
	}

	static FString FindHeader(const FHttpServerRequest& Request, const FString& HeaderName)
	{
		for (const TPair<FString, TArray<FString>>& H : Request.Headers)
		{
			if (H.Key.Equals(HeaderName, ESearchCase::IgnoreCase) && H.Value.Num() > 0)
			{
				return H.Value[0];
			}
		}
		return FString();
	}

	static TUniquePtr<FHttpServerResponse> MakeJsonResponse(const FString& JsonBody, EHttpServerResponseCodes Code)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(JsonBody, TEXT("application/json"));
		Response->Code = Code;
		return Response;
	}
}

void FUnrealAiMcpBridge::Stop()
{
	UnbindRoutes();
	HttpRouter.Reset();
	AuthToken.Reset();
	bRunning = false;
}

void FUnrealAiMcpBridge::Start(FUnrealAiBackendRegistry* InRegistry, const UUnrealAiEditorSettings* Settings)
{
	Stop();

	if (!Settings || !Settings->bMcpBridgeEnabled || !InRegistry)
	{
		return;
	}

	Registry = InRegistry;
	BoundPort = static_cast<uint32>(FMath::Clamp(Settings->McpBridgePort, 1024, 65535));
	InvokeTimeoutSeconds = FMath::Clamp(static_cast<double>(Settings->McpBridgeInvokeTimeoutSeconds), 5.0, 600.0);

	if (!ToolPack.LoadFromPlugin())
	{
		UE_LOG(LogTemp, Error, TEXT("UnrealAi MCP Bridge: failed to load mcp-tool-pack.v1.json"));
		return;
	}

	IUnrealAiPersistence* Persistence = Registry->GetPersistence();
	AuthToken = UnrealAiMcpBridgeAuth::EnsureTokenFile(Persistence, TokenFilePath);
	if (AuthToken.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("UnrealAi MCP Bridge: failed to create auth token file"));
		return;
	}

	FHttpServerModule& HttpServerModule = FHttpServerModule::Get();
	HttpRouter = HttpServerModule.GetHttpRouter(BoundPort);
	if (!HttpRouter.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("UnrealAi MCP Bridge: GetHttpRouter failed for port %u"), BoundPort);
		return;
	}

	if (!BindRoutes())
	{
		UE_LOG(LogTemp, Error, TEXT("UnrealAi MCP Bridge: failed to bind routes on port %u"), BoundPort);
		HttpRouter.Reset();
		return;
	}

	HttpServerModule.StartAllListeners();

	bRunning = true;
	UE_LOG(LogTemp, Display,
		TEXT("UnrealAi MCP Bridge: listening on 127.0.0.1:%u (token file: %s, tools: %d)"),
		BoundPort,
		*TokenFilePath,
		ToolPack.GetToolCount());
}

bool FUnrealAiMcpBridge::BindRoutes()
{
	UnbindRoutes();
	if (!HttpRouter.IsValid())
	{
		return false;
	}

	const FHttpRequestHandler HealthHandler =
		FHttpRequestHandler::CreateRaw(this, &FUnrealAiMcpBridge::HandleHealth);
	const FHttpRequestHandler ListToolsHandler =
		FHttpRequestHandler::CreateRaw(this, &FUnrealAiMcpBridge::HandleListTools);
	const FHttpRequestHandler InvokeHandler =
		FHttpRequestHandler::CreateRaw(this, &FUnrealAiMcpBridge::HandleInvoke);

	RouteHandles.Add(HttpRouter->BindRoute(FHttpPath(TEXT("/health")), EHttpServerRequestVerbs::VERB_GET, HealthHandler));
	RouteHandles.Add(HttpRouter->BindRoute(
		FHttpPath(TEXT("/v1/tools")),
		EHttpServerRequestVerbs::VERB_GET,
		ListToolsHandler));
	RouteHandles.Add(HttpRouter->BindRoute(
		FHttpPath(TEXT("/v1/tools/invoke")),
		EHttpServerRequestVerbs::VERB_POST,
		InvokeHandler));

	for (const FHttpRouteHandle& H : RouteHandles)
	{
		if (!H.IsValid())
		{
			return false;
		}
	}
	return true;
}

void FUnrealAiMcpBridge::UnbindRoutes()
{
	if (HttpRouter.IsValid())
	{
		for (const FHttpRouteHandle& H : RouteHandles)
		{
			if (H.IsValid())
			{
				HttpRouter->UnbindRoute(H);
			}
		}
	}
	RouteHandles.Empty();
}

bool FUnrealAiMcpBridge::AuthorizeRequest(const FHttpServerRequest& Request, FString& OutErrorCode) const
{
	const FString AuthHeader = UnrealAiMcpBridgePrivate::FindHeader(Request, TEXT("Authorization"));
	if (!UnrealAiMcpBridgeAuth::ValidateBearerToken(AuthHeader, AuthToken))
	{
		OutErrorCode = TEXT("auth_failed");
		return false;
	}
	return true;
}

FString FUnrealAiMcpBridge::BuildJsonEnvelope(
	bool bOk,
	const FString& ToolId,
	int32 DurationMs,
	const FString& ErrorCode,
	const FString& ErrorMessage,
	const TSharedPtr<FJsonObject>& Result) const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), bOk);
	if (!ToolId.IsEmpty())
	{
		Root->SetStringField(TEXT("tool_id"), ToolId);
	}
	Root->SetNumberField(TEXT("duration_ms"), DurationMs);
	if (!ErrorCode.IsEmpty())
	{
		Root->SetStringField(TEXT("error_code"), ErrorCode);
	}
	if (!ErrorMessage.IsEmpty())
	{
		Root->SetStringField(TEXT("error"), ErrorMessage);
	}
	if (Result.IsValid())
	{
		Root->SetObjectField(TEXT("result"), Result);
	}

	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return Out;
}

FString FUnrealAiMcpBridge::SerializeJsonObject(const TSharedPtr<FJsonObject>& Obj)
{
	FString Out;
	if (!Obj.IsValid())
	{
		return Out;
	}
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	return Out;
}

bool FUnrealAiMcpBridge::HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AuthErr;
	if (!AuthorizeRequest(Request, AuthErr))
	{
		const FString Body = BuildJsonEnvelope(false, FString(), 0, AuthErr, TEXT("Invalid or missing Bearer token"), nullptr);
		OnComplete(UnrealAiMcpBridgePrivate::MakeJsonResponse(Body, EHttpServerResponseCodes::Denied));
		return true;
	}

	TSharedPtr<FJsonObject> Health = MakeShared<FJsonObject>();
	Health->SetBoolField(TEXT("ok"), true);
	Health->SetStringField(TEXT("bridge_version"), TEXT("1.0.0"));
	Health->SetStringField(TEXT("pack_version"), ToolPack.GetPackVersion());
	Health->SetStringField(TEXT("catalog_version_pin"), ToolPack.GetCatalogVersionPin());
	Health->SetNumberField(TEXT("tool_count"), ToolPack.GetToolCount());
	Health->SetStringField(TEXT("project_name"), FPaths::GetCleanFilename(FPaths::GetProjectFilePath()));

	const FString Body = BuildJsonEnvelope(true, FString(), 0, FString(), FString(), Health);
	OnComplete(UnrealAiMcpBridgePrivate::MakeJsonResponse(Body, EHttpServerResponseCodes::Ok));
	return true;
}

bool FUnrealAiMcpBridge::HandleListTools(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AuthErr;
	if (!AuthorizeRequest(Request, AuthErr))
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("ok"), false);
		Err->SetStringField(TEXT("error_code"), AuthErr);
		Err->SetStringField(TEXT("error"), TEXT("Invalid or missing Bearer token"));
		OnComplete(UnrealAiMcpBridgePrivate::MakeJsonResponse(
			SerializeJsonObject(Err),
			EHttpServerResponseCodes::Denied));
		return true;
	}

	if (!Registry)
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("ok"), false);
		Err->SetStringField(TEXT("error"), TEXT("Backend registry unavailable"));
		OnComplete(UnrealAiMcpBridgePrivate::MakeJsonResponse(
			SerializeJsonObject(Err),
			EHttpServerResponseCodes::ServerError));
		return true;
	}

	FUnrealAiToolCatalog* Catalog = Registry->GetToolCatalog();
	if (!Catalog || !Catalog->IsLoaded())
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("ok"), false);
		Err->SetStringField(TEXT("error"), TEXT("Tool catalog not loaded"));
		OnComplete(UnrealAiMcpBridgePrivate::MakeJsonResponse(
			SerializeJsonObject(Err),
			EHttpServerResponseCodes::ServerError));
		return true;
	}

	TSharedPtr<FJsonObject> ListRoot;
	ToolPack.BuildToolListJson(*Catalog, ListRoot);
	const EHttpServerResponseCodes Code =
		ListRoot.IsValid() && ListRoot->GetBoolField(TEXT("ok"))
			? EHttpServerResponseCodes::Ok
			: EHttpServerResponseCodes::ServerError;
	OnComplete(UnrealAiMcpBridgePrivate::MakeJsonResponse(SerializeJsonObject(ListRoot), Code));
	return true;
}

bool FUnrealAiMcpBridge::HandleInvoke(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString AuthErr;
	if (!AuthorizeRequest(Request, AuthErr))
	{
		const FString Body = BuildJsonEnvelope(false, FString(), 0, AuthErr, TEXT("Invalid or missing Bearer token"), nullptr);
		OnComplete(UnrealAiMcpBridgePrivate::MakeJsonResponse(Body, EHttpServerResponseCodes::Denied));
		return true;
	}

	const FString BodyStr = UnrealAiMcpBridgePrivate::BodyBytesToString(Request.Body);
	TSharedPtr<FJsonObject> Req;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyStr);
	if (!FJsonSerializer::Deserialize(Reader, Req) || !Req.IsValid())
	{
		const FString Out = BuildJsonEnvelope(
			false, FString(), 0, TEXT("invalid_request"), TEXT("Request body must be JSON object"), nullptr);
		OnComplete(UnrealAiMcpBridgePrivate::MakeJsonResponse(Out, EHttpServerResponseCodes::BadRequest));
		return true;
	}

	FString ToolId;
	if (!Req->TryGetStringField(TEXT("tool_id"), ToolId) || ToolId.IsEmpty())
	{
		const FString Out = BuildJsonEnvelope(
			false, FString(), 0, TEXT("invalid_request"), TEXT("Missing tool_id"), nullptr);
		OnComplete(UnrealAiMcpBridgePrivate::MakeJsonResponse(Out, EHttpServerResponseCodes::BadRequest));
		return true;
	}

	if (!ToolPack.IsAllowed(ToolId))
	{
		const FString Out = BuildJsonEnvelope(
			false,
			ToolId,
			0,
			TEXT("tool_not_in_mcp_pack"),
			FString::Printf(TEXT("Tool '%s' is not in the MCP v1 allowlist"), *ToolId),
			nullptr);
		OnComplete(UnrealAiMcpBridgePrivate::MakeJsonResponse(Out, EHttpServerResponseCodes::Denied));
		return true;
	}

	const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	if (Req->TryGetObjectField(TEXT("arguments"), ArgsObj) && ArgsObj && ArgsObj->IsValid())
	{
		Args = *ArgsObj;
	}

	const UnrealAiMcpInvoke::FInvokeOutcome Outcome =
		UnrealAiMcpInvoke::InvokeOnGameThread(Registry, ToolId, Args, InvokeTimeoutSeconds);

	if (!Outcome.bBridgeOk)
	{
		const FString Out = BuildJsonEnvelope(
			false,
			ToolId,
			Outcome.DurationMs,
			Outcome.ErrorCode,
			Outcome.ErrorMessage,
			nullptr);
		OnComplete(UnrealAiMcpBridgePrivate::MakeJsonResponse(Out, EHttpServerResponseCodes::ServerError));
		return true;
	}

	const FString Out = BuildJsonEnvelope(true, ToolId, Outcome.DurationMs, FString(), FString(), Outcome.ResultObject);
	OnComplete(UnrealAiMcpBridgePrivate::MakeJsonResponse(Out, EHttpServerResponseCodes::Ok));
	return true;
}
