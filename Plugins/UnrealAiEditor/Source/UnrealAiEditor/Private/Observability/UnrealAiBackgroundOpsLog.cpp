#include "Observability/UnrealAiBackgroundOpsLog.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealAiBackgroundOpsLog
{
	FString BuildDetailJson(
		const FString& OpType,
		const FString& Status,
		const FString& ProjectId,
		const FString& ThreadId,
		const double DurationMs,
		const TFunctionRef<void(const TSharedPtr<FJsonObject>&)>& FillExtra)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("op_type"), OpType);
		O->SetStringField(TEXT("status"), Status);
		O->SetStringField(TEXT("project_id"), ProjectId);
		O->SetStringField(TEXT("thread_id"), ThreadId);
		O->SetNumberField(TEXT("duration_ms"), DurationMs);
		O->SetStringField(TEXT("utc_iso8601"), FDateTime::UtcNow().ToIso8601());
		FillExtra(O);
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(O.ToSharedRef(), Writer);
		return Out;
	}

	void EmitLogLine(const FString& OpType, const FString& Status, const double DurationMs, const FString& ExtraSummary)
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT("UnrealAi background_op: type=%s status=%s duration_ms=%.1f %s"),
			*OpType,
			*Status,
			DurationMs,
			ExtraSummary.IsEmpty() ? TEXT("") : *ExtraSummary);
	}
}
