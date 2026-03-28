#include "Tools/UnrealAiToolUsageEventLogger.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

void UnrealAiToolUsageEventLogger::AppendOperationalEvent(
	const FString& QueryHash,
	const FString& ToolId,
	const bool bOperationalOk,
	const FString& ThreadId)
{
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAi"));
	const FString Path = FPaths::Combine(Dir, TEXT("tool_usage_events.jsonl"));
	IFileManager::Get().MakeDirectory(*Dir, true);

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("ts"), FDateTime::UtcNow().ToIso8601());
	O->SetStringField(TEXT("query_hash"), QueryHash);
	O->SetStringField(TEXT("tool_id"), ToolId);
	O->SetBoolField(TEXT("operational_ok"), bOperationalOk);
	O->SetStringField(TEXT("thread_id"), ThreadId);

	FString Line;
	{
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Line);
		if (!FJsonSerializer::Serialize(O.ToSharedRef(), W))
		{
			return;
		}
	}
	Line += LINE_TERMINATOR;
	FFileHelper::SaveStringToFile(Line, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
}
