#include "Retrieval/FOpenAiCompatibleEmbeddingProvider.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"

FOpenAiCompatibleEmbeddingProvider::FOpenAiCompatibleEmbeddingProvider(FUnrealAiModelProfileRegistry* InProfiles)
	: Profiles(InProfiles)
{
}

bool FOpenAiCompatibleEmbeddingProvider::EmbedOne(
	const FString& ModelId,
	const FUnrealAiEmbeddingRequest& Request,
	FUnrealAiEmbeddingResponse& OutResponse)
{
	if (!Profiles)
	{
		OutResponse.Error = TEXT("Embedding provider missing model profile registry.");
		return false;
	}

	FString BaseUrl;
	FString ApiKey;
	if (!Profiles->TryResolveApiForModel(Profiles->GetDefaultModelId(), BaseUrl, ApiKey) || ApiKey.IsEmpty())
	{
		OutResponse.Error = TEXT("No API key configured for embeddings.");
		return false;
	}
	while (!BaseUrl.IsEmpty() && BaseUrl.EndsWith(TEXT("/")))
	{
		BaseUrl.LeftChopInline(1, EAllowShrinking::No);
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), ModelId);
	TArray<TSharedPtr<FJsonValue>> Inputs;
	Inputs.Add(MakeShared<FJsonValueString>(Request.InputText));
	Root->SetArrayField(TEXT("input"), Inputs);

	FString Body;
	{
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
		FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	}

	const FString Url = BaseUrl + TEXT("/embeddings");
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(Url);
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	HttpRequest->SetContentAsString(Body);

	bool bDone = false;
	bool bOk = false;
	FString ResponseBody;
	FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(false);
	if (!DoneEvent)
	{
		OutResponse.Error = TEXT("Failed to allocate sync event for embeddings request.");
		return false;
	}
	HttpRequest->OnProcessRequestComplete().BindLambda(
		[&bDone, &bOk, &ResponseBody, DoneEvent](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedOk)
		{
			const int32 Status = Response.IsValid() ? Response->GetResponseCode() : 0;
			bOk = bConnectedOk && Response.IsValid() && Status >= 200 && Status < 300;
			ResponseBody = Response.IsValid() ? Response->GetContentAsString() : FString();
			bDone = true;
			if (DoneEvent)
			{
				DoneEvent->Trigger();
			}
		});

	if (!HttpRequest->ProcessRequest())
	{
		OutResponse.Error = TEXT("Failed to start embeddings request.");
		FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
		return false;
	}

	DoneEvent->Wait(30000);
	FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
	if (!bDone || !bOk)
	{
		OutResponse.Error = TEXT("Embeddings HTTP request failed.");
		return false;
	}

	TSharedPtr<FJsonObject> ResponseJson;
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
		if (!FJsonSerializer::Deserialize(Reader, ResponseJson) || !ResponseJson.IsValid())
		{
			OutResponse.Error = TEXT("Embeddings response JSON parse failed.");
			return false;
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* DataArray = nullptr;
	if (!ResponseJson->TryGetArrayField(TEXT("data"), DataArray) || !DataArray || DataArray->Num() == 0)
	{
		OutResponse.Error = TEXT("Embeddings response missing data.");
		return false;
	}
	const TSharedPtr<FJsonObject>* FirstObj = nullptr;
	if (!(*DataArray)[0].IsValid() || !(*DataArray)[0]->TryGetObject(FirstObj) || !FirstObj || !(*FirstObj).IsValid())
	{
		OutResponse.Error = TEXT("Embeddings response data[0] invalid.");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* EmbeddingArray = nullptr;
	if (!(*FirstObj)->TryGetArrayField(TEXT("embedding"), EmbeddingArray) || !EmbeddingArray)
	{
		OutResponse.Error = TEXT("Embeddings response missing embedding array.");
		return false;
	}
	OutResponse.Vector.Reset();
	OutResponse.Vector.Reserve(EmbeddingArray->Num());
	for (const TSharedPtr<FJsonValue>& Value : *EmbeddingArray)
	{
		const double Number = Value.IsValid() ? Value->AsNumber() : 0.0;
		OutResponse.Vector.Add(static_cast<float>(Number));
	}
	return OutResponse.Vector.Num() > 0;
}
