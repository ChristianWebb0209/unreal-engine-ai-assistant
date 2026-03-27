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
	for (const FUnrealAiRetrievalDiagnosticsRow& Row : TopSources)
	{
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("source_id"), Row.SourceId);
		R->SetNumberField(TEXT("chunk_count"), Row.ChunkCount);
		SourcesArr.Add(MakeShared<FJsonValueObject>(R));
	}
	Obj->SetArrayField(TEXT("top_sources"), SourcesArr);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer))
	{
		return;
	}
	const FString Path = FPaths::Combine(JobsDir, TEXT("latest_diagnostics.json"));
	FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

