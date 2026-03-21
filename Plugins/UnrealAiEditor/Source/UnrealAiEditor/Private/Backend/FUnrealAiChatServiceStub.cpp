#include "Backend/FUnrealAiChatServiceStub.h"

#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/AgentContextTypes.h"
#include "Context/IAgentContextService.h"
#include "Containers/Ticker.h"

FUnrealAiChatServiceStub::FUnrealAiChatServiceStub(FUnrealAiBackendRegistry* InRegistry)
	: Registry(InRegistry)
	, bConnected(true)
{
}

void FUnrealAiChatServiceStub::Connect(FUnrealAiChatSimpleDelegate OnConnected)
{
	bConnected = true;
	if (OnConnected.IsBound())
	{
		OnConnected.Execute();
	}
}

void FUnrealAiChatServiceStub::Disconnect()
{
	bConnected = false;
}

bool FUnrealAiChatServiceStub::IsConnected() const
{
	return bConnected;
}

void FUnrealAiChatServiceStub::SendMessage(
	const FString& Prompt,
	FUnrealAiChatTokenDelegate OnToken,
	FUnrealAiChatCompleteDelegate OnComplete,
	const FUnrealAiChatSendMeta* Meta)
{
	static const FString FakeResponse =
		TEXT("This is a **stub** streamed response; real LLM integration will replace this. You asked: ");

	int32 TokenIndex = 0;
	const FString Full = FakeResponse + Prompt;

	FUnrealAiBackendRegistry* Reg = Registry;
	TOptional<FUnrealAiChatSendMeta> OptMeta;
	if (Meta)
	{
		OptMeta = *Meta;
	}

	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda(
			[OnToken, OnComplete, Full, TokenIndex, Reg, OptMeta](float DeltaTime) mutable -> bool
			{
				if (TokenIndex < Full.Len())
				{
					const int32 ChunkSize = FMath::Min(3, Full.Len() - TokenIndex);
					const FString Chunk = Full.Mid(TokenIndex, ChunkSize);
					TokenIndex += ChunkSize;
					if (OnToken.IsBound())
					{
						OnToken.Execute(Chunk);
					}
					return true;
				}
				if (OptMeta.IsSet() && OptMeta->bRecordStubToolResult && Reg)
				{
					if (IAgentContextService* Ctx = Reg->GetContextService())
					{
						Ctx->LoadOrCreate(OptMeta->ProjectId, OptMeta->ThreadId);
						FContextRecordPolicy Policy;
						Ctx->RecordToolResult(FName(TEXT("assistant_stub")), Full, Policy);
					}
				}
				if (OnComplete.IsBound())
				{
					OnComplete.Execute();
				}
				return false;
			}),
		0.05f);
}
