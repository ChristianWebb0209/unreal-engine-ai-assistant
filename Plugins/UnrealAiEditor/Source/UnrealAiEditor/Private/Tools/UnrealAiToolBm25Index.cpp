#include "Tools/UnrealAiToolBm25Index.h"

#include "Harness/UnrealAiAgentTypes.h"
#include "Tools/UnrealAiToolCatalog.h"

namespace UnrealAiToolBm25IndexPriv
{
	static void Tokenize(const FString& S, TArray<FString>& OutTerms)
	{
		OutTerms.Reset();
		const FString L = S.ToLower();
		FString Cur;
		for (int32 I = 0; I < L.Len(); ++I)
		{
			const TCHAR C = L[I];
			if (FChar::IsAlnum(C))
			{
				Cur.AppendChar(C);
			}
			else
			{
				if (Cur.Len() >= 2)
				{
					OutTerms.Add(Cur);
				}
				Cur.Reset();
			}
		}
		if (Cur.Len() >= 2)
		{
			OutTerms.Add(Cur);
		}
	}

	static FString BuildDocText(const TSharedPtr<FJsonObject>& Obj, const FString& ToolId)
	{
		FString Cat, Summary;
		Obj->TryGetStringField(TEXT("category"), Cat);
		Obj->TryGetStringField(TEXT("summary"), Summary);
		const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
		FString TagStr;
		if (Obj->TryGetArrayField(TEXT("tags"), Tags) && Tags)
		{
			for (const TSharedPtr<FJsonValue>& V : *Tags)
			{
				if (V.IsValid() && V->Type == EJson::String)
				{
					TagStr += V->AsString();
					TagStr += TEXT(' ');
				}
			}
		}
		const TSharedPtr<FJsonObject>* Ts = nullptr;
		if (Obj->TryGetObjectField(TEXT("tool_surface"), Ts) && Ts && (*Ts).IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Dt = nullptr;
			if ((*Ts)->TryGetArrayField(TEXT("domain_tags"), Dt) && Dt)
			{
				for (const TSharedPtr<FJsonValue>& V : *Dt)
				{
					if (V.IsValid() && V->Type == EJson::String)
					{
						TagStr += V->AsString();
						TagStr += TEXT(' ');
					}
				}
			}
		}
		return FString::Printf(TEXT("%s %s %s %s"), *ToolId, *Summary, *Cat, *TagStr);
	}
} // namespace UnrealAiToolBm25IndexPriv

void FUnrealAiToolBm25Index::RebuildForEnabledTools(
	const FUnrealAiToolCatalog& Catalog,
	EUnrealAiAgentMode Mode,
	const FUnrealAiModelCapabilities& Caps,
	const FUnrealAiToolPackOptions* PackOptions)
{
	RebuildForEnabledTools(Catalog, Mode, Caps, PackOptions, [](const FString&) { return true; });
}

void FUnrealAiToolBm25Index::RebuildForEnabledTools(
	const FUnrealAiToolCatalog& Catalog,
	EUnrealAiAgentMode Mode,
	const FUnrealAiModelCapabilities& Caps,
	const FUnrealAiToolPackOptions* PackOptions,
	TFunctionRef<bool(const FString& ToolId)> ToolIdFilter)
{
	Docs.Reset();
	Idf.Reset();
	NumDocs = 0;
	AvgDocLen = 1.f;

	TMap<FString, int32> DocFreq;
	int32 TotalLen = 0;

	Catalog.ForEachEnabledToolForMode(
		Mode,
		Caps,
		PackOptions,
		ToolIdFilter,
		[&](const FString& Tid, const TSharedPtr<FJsonObject>& Obj)
		{
			const FString DocText = UnrealAiToolBm25IndexPriv::BuildDocText(Obj, Tid);
			TArray<FString> Terms;
			UnrealAiToolBm25IndexPriv::Tokenize(DocText, Terms);
			FDoc D;
			for (const FString& T : Terms)
			{
				const int32 C = D.TermFreq.FindRef(T) + 1;
				D.TermFreq.Add(T, C);
			}
			D.Len = Terms.Num();
			if (D.Len == 0)
			{
				D.Len = 1;
			}
			TotalLen += D.Len;
			for (const auto& Pair : D.TermFreq)
			{
				DocFreq.FindOrAdd(Pair.Key)++;
			}
			Docs.Add(Tid, D);
			NumDocs++;
		});

	if (NumDocs <= 0)
	{
		return;
	}
	AvgDocLen = static_cast<float>(TotalLen) / static_cast<float>(NumDocs);
	const float N = static_cast<float>(NumDocs);
	for (const auto& Pair : DocFreq)
	{
		const float Df = static_cast<float>(Pair.Value);
		Idf.Add(Pair.Key, FMath::Loge(1.f + (N - Df + 0.5f) / (Df + 0.5f)));
	}
}

void FUnrealAiToolBm25Index::ScoreQuery(const FString& QueryText, TMap<FString, float>& OutScoresByToolId) const
{
	OutScoresByToolId.Reset();
	TArray<FString> QTerms;
	UnrealAiToolBm25IndexPriv::Tokenize(QueryText, QTerms);
	if (QTerms.Num() == 0 || NumDocs <= 0)
	{
		return;
	}
	const float K1 = 1.2f;
	const float B = 0.75f;

	for (const auto& DocPair : Docs)
	{
		const FString& Tid = DocPair.Key;
		const FDoc& D = DocPair.Value;
		float Score = 0.f;
		for (const FString& Q : QTerms)
		{
			const float* IdfPtr = Idf.Find(Q);
			if (!IdfPtr)
			{
				continue;
			}
			const int32 F = D.TermFreq.FindRef(Q);
			if (F <= 0)
			{
				continue;
			}
			const float Num = F * (K1 + 1.f);
			const float Den = F + K1 * (1.f - B + B * (static_cast<float>(D.Len) / AvgDocLen));
			Score += (*IdfPtr) * (Num / Den);
		}
		if (Score > 0.f)
		{
			OutScoresByToolId.Add(Tid, Score);
		}
	}
}
