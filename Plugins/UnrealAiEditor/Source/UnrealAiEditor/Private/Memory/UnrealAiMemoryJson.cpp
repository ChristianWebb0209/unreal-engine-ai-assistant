#include "Memory/UnrealAiMemoryJson.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealAiMemoryJson
{
namespace
{
	static const TCHAR* SchemaRecords = TEXT("unreal_ai.memory.index_v1");
	static const TCHAR* SchemaRecord = TEXT("unreal_ai.memory.item_v1");
	static const TCHAR* SchemaTombstones = TEXT("unreal_ai.memory.tombstones_v1");
	static const TCHAR* SchemaGenStatus = TEXT("unreal_ai.memory.gen_status_v1");

	static FString ScopeToString(const EUnrealAiMemoryScope Scope)
	{
		return Scope == EUnrealAiMemoryScope::Thread ? TEXT("thread") : TEXT("project");
	}

	static EUnrealAiMemoryScope StringToScope(const FString& S)
	{
		return S.Equals(TEXT("thread"), ESearchCase::IgnoreCase)
			? EUnrealAiMemoryScope::Thread
			: EUnrealAiMemoryScope::Project;
	}

	static FString StatusToString(const EUnrealAiMemoryStatus Status)
	{
		switch (Status)
		{
		case EUnrealAiMemoryStatus::Disabled: return TEXT("disabled");
		case EUnrealAiMemoryStatus::Archived: return TEXT("archived");
		default: return TEXT("active");
		}
	}

	static EUnrealAiMemoryStatus StringToStatus(const FString& S)
	{
		if (S.Equals(TEXT("disabled"), ESearchCase::IgnoreCase))
		{
			return EUnrealAiMemoryStatus::Disabled;
		}
		if (S.Equals(TEXT("archived"), ESearchCase::IgnoreCase))
		{
			return EUnrealAiMemoryStatus::Archived;
		}
		return EUnrealAiMemoryStatus::Active;
	}

	static FString GenStateToString(const EUnrealAiMemoryGenerationState S)
	{
		switch (S)
		{
		case EUnrealAiMemoryGenerationState::Running: return TEXT("running");
		case EUnrealAiMemoryGenerationState::Success: return TEXT("success");
		case EUnrealAiMemoryGenerationState::Failed: return TEXT("failed");
		default: return TEXT("idle");
		}
	}

	static EUnrealAiMemoryGenerationState StringToGenState(const FString& S)
	{
		if (S.Equals(TEXT("running"), ESearchCase::IgnoreCase)) return EUnrealAiMemoryGenerationState::Running;
		if (S.Equals(TEXT("success"), ESearchCase::IgnoreCase)) return EUnrealAiMemoryGenerationState::Success;
		if (S.Equals(TEXT("failed"), ESearchCase::IgnoreCase)) return EUnrealAiMemoryGenerationState::Failed;
		return EUnrealAiMemoryGenerationState::Idle;
	}

	static FString GenErrToString(const EUnrealAiMemoryGenerationErrorCode E)
	{
		switch (E)
		{
		case EUnrealAiMemoryGenerationErrorCode::MissingApiKey: return TEXT("missing_api_key");
		case EUnrealAiMemoryGenerationErrorCode::ProviderUnavailable: return TEXT("provider_unavailable");
		case EUnrealAiMemoryGenerationErrorCode::InvalidResponse: return TEXT("invalid_response");
		case EUnrealAiMemoryGenerationErrorCode::RateLimited: return TEXT("rate_limited");
		case EUnrealAiMemoryGenerationErrorCode::Unknown: return TEXT("unknown");
		default: return TEXT("none");
		}
	}

	static EUnrealAiMemoryGenerationErrorCode StringToGenErr(const FString& S)
	{
		if (S.Equals(TEXT("missing_api_key"), ESearchCase::IgnoreCase)) return EUnrealAiMemoryGenerationErrorCode::MissingApiKey;
		if (S.Equals(TEXT("provider_unavailable"), ESearchCase::IgnoreCase)) return EUnrealAiMemoryGenerationErrorCode::ProviderUnavailable;
		if (S.Equals(TEXT("invalid_response"), ESearchCase::IgnoreCase)) return EUnrealAiMemoryGenerationErrorCode::InvalidResponse;
		if (S.Equals(TEXT("rate_limited"), ESearchCase::IgnoreCase)) return EUnrealAiMemoryGenerationErrorCode::RateLimited;
		if (S.Equals(TEXT("unknown"), ESearchCase::IgnoreCase)) return EUnrealAiMemoryGenerationErrorCode::Unknown;
		return EUnrealAiMemoryGenerationErrorCode::None;
	}
}

bool RecordToJson(const FUnrealAiMemoryRecord& In, FString& OutJson)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), SchemaRecord);
	Root->SetStringField(TEXT("id"), In.Id);
	Root->SetStringField(TEXT("title"), In.Title);
	Root->SetStringField(TEXT("description"), In.Description);
	Root->SetStringField(TEXT("body"), In.Body);
	Root->SetStringField(TEXT("scope"), ScopeToString(In.Scope));
	Root->SetStringField(TEXT("status"), StatusToString(In.Status));
	Root->SetNumberField(TEXT("confidence"), In.Confidence);
	Root->SetNumberField(TEXT("ttlDays"), In.TtlDays);
	Root->SetNumberField(TEXT("useCount"), In.UseCount);
	Root->SetStringField(TEXT("createdAtUtc"), In.CreatedAtUtc.ToIso8601());
	Root->SetStringField(TEXT("updatedAtUtc"), In.UpdatedAtUtc.ToIso8601());
	Root->SetStringField(TEXT("lastUsedAtUtc"), In.LastUsedAtUtc.ToIso8601());

	TArray<TSharedPtr<FJsonValue>> Tags;
	for (const FString& Tag : In.Tags)
	{
		Tags.Add(MakeShared<FJsonValueString>(Tag));
	}
	Root->SetArrayField(TEXT("tags"), Tags);

	TArray<TSharedPtr<FJsonValue>> Sources;
	for (const FUnrealAiMemorySourceRef& Src : In.SourceRefs)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("kind"), Src.Kind);
		O->SetStringField(TEXT("value"), Src.Value);
		Sources.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("sourceRefs"), Sources);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	return FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
}

bool JsonToRecord(const FString& Json, FUnrealAiMemoryRecord& Out, TArray<FString>& OutWarnings)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutWarnings.Add(TEXT("memory item: invalid JSON"));
		return false;
	}
	Root->TryGetStringField(TEXT("id"), Out.Id);
	Root->TryGetStringField(TEXT("title"), Out.Title);
	Root->TryGetStringField(TEXT("description"), Out.Description);
	Root->TryGetStringField(TEXT("body"), Out.Body);
	FString Scope;
	Root->TryGetStringField(TEXT("scope"), Scope);
	Out.Scope = StringToScope(Scope);
	FString Status;
	Root->TryGetStringField(TEXT("status"), Status);
	Out.Status = StringToStatus(Status);
	Root->TryGetNumberField(TEXT("confidence"), Out.Confidence);
	Out.TtlDays = static_cast<int32>(Root->GetNumberField(TEXT("ttlDays")));
	Out.UseCount = static_cast<int32>(Root->GetNumberField(TEXT("useCount")));
	FString Time;
	if (Root->TryGetStringField(TEXT("createdAtUtc"), Time))
	{
		FDateTime::ParseIso8601(*Time, Out.CreatedAtUtc);
	}
	if (Root->TryGetStringField(TEXT("updatedAtUtc"), Time))
	{
		FDateTime::ParseIso8601(*Time, Out.UpdatedAtUtc);
	}
	if (Root->TryGetStringField(TEXT("lastUsedAtUtc"), Time))
	{
		FDateTime::ParseIso8601(*Time, Out.LastUsedAtUtc);
	}

	const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
	if (Root->TryGetArrayField(TEXT("tags"), Tags) && Tags)
	{
		for (const TSharedPtr<FJsonValue>& V : *Tags)
		{
			if (V.IsValid() && V->Type == EJson::String)
			{
				Out.Tags.Add(V->AsString());
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Sources = nullptr;
	if (Root->TryGetArrayField(TEXT("sourceRefs"), Sources) && Sources)
	{
		for (const TSharedPtr<FJsonValue>& V : *Sources)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid())
			{
				continue;
			}
			FUnrealAiMemorySourceRef Src;
			O->TryGetStringField(TEXT("kind"), Src.Kind);
			O->TryGetStringField(TEXT("value"), Src.Value);
			Out.SourceRefs.Add(MoveTemp(Src));
		}
	}
	return !Out.Id.IsEmpty();
}

bool IndexToJson(const TArray<FUnrealAiMemoryIndexRow>& Rows, FString& OutJson)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), SchemaRecords);
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FUnrealAiMemoryIndexRow& Row : Rows)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), Row.Id);
		O->SetStringField(TEXT("title"), Row.Title);
		O->SetStringField(TEXT("description"), Row.Description);
		O->SetStringField(TEXT("scope"), ScopeToString(Row.Scope));
		O->SetStringField(TEXT("status"), StatusToString(Row.Status));
		O->SetNumberField(TEXT("confidence"), Row.Confidence);
		O->SetNumberField(TEXT("useCount"), Row.UseCount);
		O->SetStringField(TEXT("updatedAtUtc"), Row.UpdatedAtUtc.ToIso8601());
		O->SetStringField(TEXT("lastUsedAtUtc"), Row.LastUsedAtUtc.ToIso8601());
		TArray<TSharedPtr<FJsonValue>> Tags;
		for (const FString& Tag : Row.Tags)
		{
			Tags.Add(MakeShared<FJsonValueString>(Tag));
		}
		O->SetArrayField(TEXT("tags"), Tags);
		Arr.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("rows"), Arr);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	return FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
}

bool JsonToIndex(const FString& Json, TArray<FUnrealAiMemoryIndexRow>& OutRows, TArray<FString>& OutWarnings)
{
	OutRows.Reset();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutWarnings.Add(TEXT("memory index: invalid JSON"));
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Root->TryGetArrayField(TEXT("rows"), Arr) || !Arr)
	{
		return true;
	}
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
		if (!O.IsValid())
		{
			continue;
		}
		FUnrealAiMemoryIndexRow Row;
		O->TryGetStringField(TEXT("id"), Row.Id);
		O->TryGetStringField(TEXT("title"), Row.Title);
		O->TryGetStringField(TEXT("description"), Row.Description);
		FString Scope;
		O->TryGetStringField(TEXT("scope"), Scope);
		Row.Scope = StringToScope(Scope);
		FString Status;
		O->TryGetStringField(TEXT("status"), Status);
		Row.Status = StringToStatus(Status);
		O->TryGetNumberField(TEXT("confidence"), Row.Confidence);
		Row.UseCount = static_cast<int32>(O->GetNumberField(TEXT("useCount")));
		FString Time;
		if (O->TryGetStringField(TEXT("updatedAtUtc"), Time))
		{
			FDateTime::ParseIso8601(*Time, Row.UpdatedAtUtc);
		}
		if (O->TryGetStringField(TEXT("lastUsedAtUtc"), Time))
		{
			FDateTime::ParseIso8601(*Time, Row.LastUsedAtUtc);
		}
		const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
		if (O->TryGetArrayField(TEXT("tags"), Tags) && Tags)
		{
			for (const TSharedPtr<FJsonValue>& Tag : *Tags)
			{
				if (Tag.IsValid() && Tag->Type == EJson::String)
				{
					Row.Tags.Add(Tag->AsString());
				}
			}
		}
		if (!Row.Id.IsEmpty())
		{
			OutRows.Add(MoveTemp(Row));
		}
	}
	return true;
}

bool TombstonesToJson(const TArray<FUnrealAiMemoryTombstone>& In, FString& OutJson)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), SchemaTombstones);
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FUnrealAiMemoryTombstone& T : In)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), T.Id);
		O->SetStringField(TEXT("deletedAtUtc"), T.DeletedAtUtc.ToIso8601());
		Arr.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("rows"), Arr);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	return FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
}

bool JsonToTombstones(const FString& Json, TArray<FUnrealAiMemoryTombstone>& Out, TArray<FString>& OutWarnings)
{
	Out.Reset();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutWarnings.Add(TEXT("memory tombstones: invalid JSON"));
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Root->TryGetArrayField(TEXT("rows"), Arr) || !Arr)
	{
		return true;
	}
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
		if (!O.IsValid())
		{
			continue;
		}
		FUnrealAiMemoryTombstone T;
		O->TryGetStringField(TEXT("id"), T.Id);
		FString Time;
		if (O->TryGetStringField(TEXT("deletedAtUtc"), Time))
		{
			FDateTime::ParseIso8601(*Time, T.DeletedAtUtc);
		}
		if (!T.Id.IsEmpty())
		{
			Out.Add(MoveTemp(T));
		}
	}
	return true;
}

bool GenerationStatusToJson(const FUnrealAiMemoryGenerationStatus& In, FString& OutJson)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), SchemaGenStatus);
	Root->SetStringField(TEXT("state"), GenStateToString(In.State));
	Root->SetStringField(TEXT("errorCode"), GenErrToString(In.ErrorCode));
	Root->SetStringField(TEXT("errorMessage"), In.ErrorMessage);
	Root->SetStringField(TEXT("lastAttemptAtUtc"), In.LastAttemptAtUtc.ToIso8601());
	if (In.LastSuccessAtUtc.GetTicks() > 0)
	{
		Root->SetStringField(TEXT("lastSuccessAtUtc"), In.LastSuccessAtUtc.ToIso8601());
	}
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	return FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
}

bool JsonToGenerationStatus(const FString& Json, FUnrealAiMemoryGenerationStatus& Out, TArray<FString>& OutWarnings)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutWarnings.Add(TEXT("memory generation status: invalid JSON"));
		return false;
	}
	FString State;
	Root->TryGetStringField(TEXT("state"), State);
	Out.State = StringToGenState(State);
	FString Err;
	Root->TryGetStringField(TEXT("errorCode"), Err);
	Out.ErrorCode = StringToGenErr(Err);
	Root->TryGetStringField(TEXT("errorMessage"), Out.ErrorMessage);
	FString Time;
	if (Root->TryGetStringField(TEXT("lastAttemptAtUtc"), Time))
	{
		FDateTime::ParseIso8601(*Time, Out.LastAttemptAtUtc);
	}
	if (Root->TryGetStringField(TEXT("lastSuccessAtUtc"), Time))
	{
		FDateTime::ParseIso8601(*Time, Out.LastSuccessAtUtc);
	}
	return true;
}
}
