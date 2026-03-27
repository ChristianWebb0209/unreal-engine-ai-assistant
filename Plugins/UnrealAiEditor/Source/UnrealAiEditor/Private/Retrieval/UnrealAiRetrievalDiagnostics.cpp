#include "Retrieval/UnrealAiRetrievalDiagnostics.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	static FString GetVectorProjectRoot(const FString& ProjectId)
	{
		const FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
		const FString Base = LocalAppData.IsEmpty()
			? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAiEditor"))
			: FPaths::Combine(LocalAppData, TEXT("UnrealAiEditor"));
		return FPaths::Combine(Base, TEXT("vector"), ProjectId);
	}
}

void UnrealAiRetrievalDiagnostics::WriteIndexDiagnostics(
	const FString& ProjectId,
	const FString& State,
	const int32 FilesIndexed,
	const int32 ChunksIndexed,
	const TArray<FUnrealAiRetrievalDiagnosticsRow>& TopSources)
{
	if (ProjectId.IsEmpty())
	{
		return;
	}
	const FString Root = GetVectorProjectRoot(ProjectId);
	const FString JobsDir = FPaths::Combine(Root, TEXT("jobs"));
	IFileManager::Get().MakeDirectory(*JobsDir, true);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("project_id"), ProjectId);
	Obj->SetStringField(TEXT("state"), State);
	Obj->SetNumberField(TEXT("files_indexed"), FilesIndexed);
	Obj->SetNumberField(TEXT("chunks_indexed"), ChunksIndexed);
	Obj->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());

	TArray<TSharedPtr<FJsonValue>> SourcesArr;
	auto AppendRowsToArray = [](const TArray<FUnrealAiRetrievalDiagnosticsRow>& Rows, TArray<TSharedPtr<FJsonValue>>& OutArray)
	{
		for (const FUnrealAiRetrievalDiagnosticsRow& Row : Rows)
		{
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("source_id"), Row.SourceId);
			R->SetNumberField(TEXT("chunk_count"), Row.ChunkCount);
			OutArray.Add(MakeShared<FJsonValueObject>(R));
		}
	};
	AppendRowsToArray(TopSources, SourcesArr);

	// Preserve the last known non-empty top_sources snapshot if this write has no rows.
	// This avoids noisy empty diagnostics during transient phases while keeping
	// counts/state/timestamp fresh for the current write.
	const FString Path = FPaths::Combine(JobsDir, TEXT("latest_diagnostics.json"));
	if (SourcesArr.Num() == 0 && FPaths::FileExists(Path))
	{
		FString ExistingJson;
		if (FFileHelper::LoadFileToString(ExistingJson, *Path))
		{
			TSharedPtr<FJsonObject> ExistingObj;
			const TSharedRef<TJsonReader<>> ExistingReader = TJsonReaderFactory<>::Create(ExistingJson);
			if (FJsonSerializer::Deserialize(ExistingReader, ExistingObj) && ExistingObj.IsValid())
			{
				const TArray<TSharedPtr<FJsonValue>>* ExistingSources = nullptr;
				if (ExistingObj->TryGetArrayField(TEXT("top_sources"), ExistingSources) && ExistingSources && ExistingSources->Num() > 0)
				{
					SourcesArr = *ExistingSources;
				}
			}
		}
	}
	Obj->SetArrayField(TEXT("top_sources"), SourcesArr);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer))
	{
		return;
	}
	FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

