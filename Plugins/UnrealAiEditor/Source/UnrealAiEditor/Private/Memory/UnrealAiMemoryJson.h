#pragma once

#include "CoreMinimal.h"
#include "Memory/UnrealAiMemoryTypes.h"

namespace UnrealAiMemoryJson
{
	bool RecordToJson(const FUnrealAiMemoryRecord& In, FString& OutJson);
	bool JsonToRecord(const FString& Json, FUnrealAiMemoryRecord& Out, TArray<FString>& OutWarnings);

	bool IndexToJson(const TArray<FUnrealAiMemoryIndexRow>& Rows, FString& OutJson);
	bool JsonToIndex(const FString& Json, TArray<FUnrealAiMemoryIndexRow>& OutRows, TArray<FString>& OutWarnings);

	bool TombstonesToJson(const TArray<FUnrealAiMemoryTombstone>& In, FString& OutJson);
	bool JsonToTombstones(const FString& Json, TArray<FUnrealAiMemoryTombstone>& Out, TArray<FString>& OutWarnings);

	bool GenerationStatusToJson(const FUnrealAiMemoryGenerationStatus& In, FString& OutJson);
	bool JsonToGenerationStatus(const FString& Json, FUnrealAiMemoryGenerationStatus& Out, TArray<FString>& OutWarnings);
}
