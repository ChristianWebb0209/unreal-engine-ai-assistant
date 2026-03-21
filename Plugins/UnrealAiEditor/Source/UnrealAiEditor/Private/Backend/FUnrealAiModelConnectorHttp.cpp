#include "Backend/FUnrealAiModelConnectorHttp.h"

#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

FUnrealAiModelConnectorHttp::FUnrealAiModelConnectorHttp(FUnrealAiModelProfileRegistry* InProfiles)
	: Profiles(InProfiles)
{
}

void FUnrealAiModelConnectorHttp::TestConnection(FUnrealAiModelTestResultDelegate OnDone)
{
	if (!Profiles)
	{
		OnDone.ExecuteIfBound(false);
		return;
	}
	FString BaseUrl;
	FString Key;
	if (!Profiles->TryResolveApiForModel(Profiles->GetDefaultModelId(), BaseUrl, Key) || Key.IsEmpty())
	{
		OnDone.ExecuteIfBound(false);
		return;
	}
	while (BaseUrl.Len() > 0 && BaseUrl[BaseUrl.Len() - 1] == TEXT('/'))
	{
		BaseUrl.LeftChopInline(1, EAllowShrinking::No);
	}
	const FString Url = BaseUrl + TEXT("/models");
	TSharedRef<IHttpRequest> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(Url);
	Req->SetVerb(TEXT("GET"));
	Req->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Key));
	Req->OnProcessRequestComplete().BindLambda(
		[OnDone](FHttpRequestPtr /*R*/, FHttpResponsePtr Resp, bool bConnectedOk)
		{
			const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : 0;
			const bool bOk = bConnectedOk && Resp.IsValid() && Code >= 200 && Code < 300;
			OnDone.ExecuteIfBound(bOk);
		});
	Req->ProcessRequest();
}
