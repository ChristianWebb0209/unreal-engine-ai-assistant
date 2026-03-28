#include "Widgets/UnrealAiPlanDraftPersist.h"

#include "Dom/JsonObject.h"
#include "Misc/DateTime.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FString UnrealAiPlanDraftPersist::WrapDraftFile(const FString& DagJson)
{
	const TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("version"), 1);
	O->SetStringField(TEXT("dagJson"), DagJson);
	O->SetStringField(TEXT("updatedUtc"), FDateTime::UtcNow().ToIso8601());
	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(O.ToSharedRef(), Writer);
	return Out;
}

bool UnrealAiPlanDraftPersist::TryUnwrapDraftFile(const FString& FileBody, FString& OutDagJson)
{
	OutDagJson.Reset();
	if (FileBody.TrimStartAndEnd().IsEmpty())
	{
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FileBody);
	if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
	{
		FString Dag;
		if (Root->TryGetStringField(TEXT("dagJson"), Dag) && !Dag.IsEmpty())
		{
			OutDagJson = MoveTemp(Dag);
			return true;
		}
	}
	OutDagJson = FileBody;
	return !OutDagJson.TrimStartAndEnd().IsEmpty();
}
