#include "Backend/FUnrealAiModelConnectorStub.h"

#include "Containers/Ticker.h"

void FUnrealAiModelConnectorStub::TestConnection(FUnrealAiModelTestResultDelegate OnDone)
{
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda(
			[OnDone](float DeltaTime) -> bool
			{
				if (OnDone.IsBound())
				{
					OnDone.Execute(true);
				}
				return false;
			}),
		0.5f);
}
