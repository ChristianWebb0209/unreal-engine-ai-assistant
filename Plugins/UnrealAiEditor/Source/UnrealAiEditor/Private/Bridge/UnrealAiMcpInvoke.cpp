#include "Bridge/UnrealAiMcpInvoke.h"

#include "Async/Async.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/UnrealAiProjectId.h"
#include "HAL/Event.h"
#include "Harness/IToolExecutionHost.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAiMcpInvoke
{
	struct FInvokeSharedState
	{
		FCriticalSection Mutex;
		bool bDone = false;
		FUnrealAiToolInvocationResult ToolResult;
	};

	static void RunInvoke(
		FUnrealAiBackendRegistry* Registry,
		const FString& ToolId,
		const FString& ArgumentsJson,
		FInvokeSharedState& Shared)
	{
		if (!Registry)
		{
			FScopeLock Lock(&Shared.Mutex);
			Shared.bDone = true;
			Shared.ToolResult = FUnrealAiToolInvocationResult();
			Shared.ToolResult.bOk = false;
			Shared.ToolResult.ErrorMessage = TEXT("Backend registry unavailable");
			return;
		}

		IToolExecutionHost* Host = Registry->GetToolExecutionHost();
		if (!Host)
		{
			FScopeLock Lock(&Shared.Mutex);
			Shared.bDone = true;
			Shared.ToolResult.bOk = false;
			Shared.ToolResult.ErrorMessage = TEXT("Tool execution host unavailable");
			return;
		}

		Host->SetToolSession(UnrealAiProjectId::GetCurrentProjectId(), TEXT("mcp-bridge"));
		const FUnrealAiToolInvocationResult R = Host->InvokeTool(ToolId, ArgumentsJson, TEXT("mcp"));

		FScopeLock Lock(&Shared.Mutex);
		Shared.bDone = true;
		Shared.ToolResult = R;
	}

	FInvokeOutcome InvokeOnGameThread(
		FUnrealAiBackendRegistry* Registry,
		const FString& ToolId,
		const TSharedPtr<FJsonObject>& Arguments,
		double TimeoutSeconds)
	{
		FInvokeOutcome Outcome;
		const double StartSec = FPlatformTime::Seconds();

		FString ArgumentsJson = TEXT("{}");
		if (Arguments.IsValid())
		{
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ArgumentsJson);
			FJsonSerializer::Serialize(Arguments.ToSharedRef(), Writer);
		}

		FInvokeSharedState Shared;
		if (IsInGameThread())
		{
			RunInvoke(Registry, ToolId, ArgumentsJson, Shared);
		}
		else
		{
			FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(false);
			AsyncTask(ENamedThreads::GameThread, [Registry, ToolId, ArgumentsJson, &Shared, DoneEvent]()
			{
				RunInvoke(Registry, ToolId, ArgumentsJson, Shared);
				DoneEvent->Trigger();
			});

			const uint32 TimeoutMs = FMath::Max(
				1u,
				static_cast<uint32>(FMath::RoundToInt(TimeoutSeconds * 1000.0)));
			if (!DoneEvent->Wait(TimeoutMs))
			{
				Outcome.bBridgeOk = false;
				Outcome.ErrorCode = TEXT("bridge_timeout");
				Outcome.ErrorMessage = FString::Printf(
					TEXT("Tool invoke timed out after %.1fs on game thread"), TimeoutSeconds);
				Outcome.DurationMs = FMath::RoundToInt((FPlatformTime::Seconds() - StartSec) * 1000.0);
				FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
				return Outcome;
			}
			FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
		}

		FUnrealAiToolInvocationResult ToolResult;
		{
			FScopeLock Lock(&Shared.Mutex);
			ToolResult = Shared.ToolResult;
		}

		Outcome.DurationMs = FMath::RoundToInt((FPlatformTime::Seconds() - StartSec) * 1000.0);
		Outcome.bBridgeOk = true;

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ToolResult.ContentForModel);
		TSharedPtr<FJsonObject> Parsed;
		if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
		{
			Outcome.ResultObject = Parsed;
		}
		else
		{
			Outcome.ResultObject = MakeShared<FJsonObject>();
			Outcome.ResultObject->SetBoolField(TEXT("ok"), ToolResult.bOk);
			if (!ToolResult.bOk)
			{
				Outcome.ResultObject->SetStringField(TEXT("error"), ToolResult.ErrorMessage);
			}
			Outcome.ResultObject->SetStringField(TEXT("raw"), ToolResult.ContentForModel);
		}

		return Outcome;
	}
}
