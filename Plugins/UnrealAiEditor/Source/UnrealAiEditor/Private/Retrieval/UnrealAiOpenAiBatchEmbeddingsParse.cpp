#include "Retrieval/UnrealAiOpenAiBatchEmbeddingsParse.h"

#include "Harness/UnrealAiHarnessTpmThrottle.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAiOpenAiBatchEmbeddingsParse
{
	bool ParseEmbeddingsDataFromResponse(
		const FString& ResponseBody,
		const int32 ExpectedCount,
		TArray<TArray<float>>& OutVectors,
		FString& OutError)
	{
		OutVectors.Reset();
		if (ExpectedCount <= 0)
		{
			OutError = TEXT("Embeddings parse: invalid expected count.");
			return false;
		}

		TSharedPtr<FJsonObject> ResponseJson;
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
			if (!FJsonSerializer::Deserialize(Reader, ResponseJson) || !ResponseJson.IsValid())
			{
				OutError = TEXT("Embeddings response JSON parse failed.");
				return false;
			}
		}

		{
			const TSharedPtr<FJsonObject>* UsageObj = nullptr;
			if (ResponseJson->TryGetObjectField(TEXT("usage"), UsageObj) && UsageObj && (*UsageObj).IsValid())
			{
				double TotalTok = 0.0;
				double PromptTok = 0.0;
				if ((*UsageObj)->TryGetNumberField(TEXT("total_tokens"), TotalTok) && TotalTok > 0.0)
				{
					UnrealAiHarnessTpmThrottle::RecordEmbeddingTokens(static_cast<int32>(TotalTok));
				}
				else if ((*UsageObj)->TryGetNumberField(TEXT("prompt_tokens"), PromptTok) && PromptTok > 0.0)
				{
					UnrealAiHarnessTpmThrottle::RecordEmbeddingTokens(static_cast<int32>(PromptTok));
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* DataArray = nullptr;
		if (!ResponseJson->TryGetArrayField(TEXT("data"), DataArray) || !DataArray || DataArray->Num() == 0)
		{
			OutError = TEXT("Embeddings response missing data.");
			return false;
		}
		if (DataArray->Num() != ExpectedCount)
		{
			OutError = FString::Printf(
				TEXT("Embeddings response data length mismatch (got %d, expected %d)."),
				DataArray->Num(),
				ExpectedCount);
			return false;
		}

		struct FIndexedVec
		{
			int32 Index = 0;
			TArray<float> Vec;
		};
		TArray<FIndexedVec> Indexed;
		Indexed.Reserve(DataArray->Num());

		for (const TSharedPtr<FJsonValue>& DataVal : *DataArray)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!DataVal.IsValid() || !DataVal->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
			{
				OutError = TEXT("Embeddings response data item invalid.");
				return false;
			}
			int32 Idx = 0;
			if (!(*Obj)->TryGetNumberField(TEXT("index"), Idx))
			{
				Idx = Indexed.Num();
			}
			const TArray<TSharedPtr<FJsonValue>>* EmbeddingArray = nullptr;
			if (!(*Obj)->TryGetArrayField(TEXT("embedding"), EmbeddingArray) || !EmbeddingArray)
			{
				OutError = TEXT("Embeddings response missing embedding array.");
				return false;
			}
			FIndexedVec Row;
			Row.Index = Idx;
			Row.Vec.Reserve(EmbeddingArray->Num());
			for (const TSharedPtr<FJsonValue>& Value : *EmbeddingArray)
			{
				const double Number = Value.IsValid() ? Value->AsNumber() : 0.0;
				Row.Vec.Add(static_cast<float>(Number));
			}
			if (Row.Vec.Num() == 0)
			{
				OutError = TEXT("Embeddings response empty embedding vector.");
				return false;
			}
			Indexed.Add(MoveTemp(Row));
		}

		Indexed.Sort([](const FIndexedVec& A, const FIndexedVec& B) { return A.Index < B.Index; });

		OutVectors.SetNum(ExpectedCount);
		for (int32 i = 0; i < Indexed.Num(); ++i)
		{
			const int32 Idx = Indexed[i].Index;
			if (Idx < 0 || Idx >= ExpectedCount || OutVectors[Idx].Num() > 0)
			{
				OutError = FString::Printf(TEXT("Embeddings response invalid or duplicate index %d."), Idx);
				return false;
			}
			OutVectors[Idx] = MoveTemp(Indexed[i].Vec);
		}
		for (int32 i = 0; i < ExpectedCount; ++i)
		{
			if (OutVectors[i].Num() == 0)
			{
				OutError = FString::Printf(TEXT("Embeddings response missing index %d."), i);
				return false;
			}
		}
		return true;
	}
}
