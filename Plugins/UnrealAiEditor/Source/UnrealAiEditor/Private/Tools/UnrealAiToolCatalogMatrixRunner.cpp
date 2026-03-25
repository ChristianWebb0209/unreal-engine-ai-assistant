#include "Tools/UnrealAiToolCatalogMatrixRunner.h"

#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "Tools/UnrealAiToolCatalog.h"
#include "Tools/UnrealAiToolDispatch.h"

namespace UnrealAiToolCatalogMatrixRunnerPriv
{
	static bool IsBannedTool(const TSharedPtr<FJsonObject>& Def)
	{
		if (!Def.IsValid())
		{
			return false;
		}
		FString Cat;
		if (Def->TryGetStringField(TEXT("category"), Cat) && Cat == TEXT("banned"))
		{
			return true;
		}
		FString Status;
		if (Def->TryGetStringField(TEXT("status"), Status) && Status == TEXT("banned_v1"))
		{
			return true;
		}
		return false;
	}

	static bool ResponseLooksStructured(const TSharedPtr<FJsonObject>& O)
	{
		if (!O.IsValid())
		{
			return false;
		}
		return O->HasField(TEXT("ok")) || O->HasField(TEXT("status")) || O->HasField(TEXT("error"));
	}

	static void AppendViolation(
		TArray<TSharedPtr<FJsonValue>>& ContractViolations,
		const FString& ToolId,
		const FString& Reason)
	{
		TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>();
		V->SetStringField(TEXT("tool_id"), ToolId);
		V->SetStringField(TEXT("reason"), Reason);
		ContractViolations.Add(MakeShared<FJsonValueObject>(V.ToSharedRef()));
	}

	static bool TryLoadFixtureArgs(const FString& ToolId, TSharedPtr<FJsonObject>& OutArgs, FString& OutViolationReason)
	{
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("tests"), TEXT("fixtures"), ToolId + TEXT(".json"));
		if (!FPaths::FileExists(Path))
		{
			OutArgs = MakeShared<FJsonObject>();
			OutViolationReason.Reset();
			return true;
		}
		FString JsonStr;
		if (!FFileHelper::LoadFileToString(JsonStr, *Path))
		{
			OutViolationReason = TEXT("fixture: failed to read file");
			return false;
		}
		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
		if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
		{
			OutViolationReason = TEXT("fixture: invalid JSON");
			return false;
		}
		OutArgs = Parsed;
		OutViolationReason.Reset();
		return true;
	}
}

bool UnrealAiToolCatalogMatrixRunner::RunAndWriteJson(const FString& MatrixFilter, TArray<FString>* OutViolationMessages)
{
	using namespace UnrealAiToolCatalogMatrixRunnerPriv;

	const FString GitCommit = FPlatformMisc::GetEnvironmentVariable(TEXT("GIT_COMMIT"));

	FUnrealAiToolCatalog Cat;
	if (!Cat.LoadFromPlugin() || !Cat.IsLoaded())
	{
		if (OutViolationMessages)
		{
			OutViolationMessages->Add(TEXT("CatalogMatrix: failed to load UnrealAiToolCatalog.json"));
		}
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> ContractViolations;
	TArray<TSharedPtr<FJsonValue>> ResultRows;

	int32 SkippedBanned = 0;
	int32 SkippedFilter = 0;
	int32 Invoked = 0;

	Cat.ForEachTool([&](const FString& ToolId, const TSharedPtr<FJsonObject>& Def) {
		if (!MatrixFilter.IsEmpty() && !ToolId.Contains(MatrixFilter))
		{
			++SkippedFilter;
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("tool_id"), ToolId);
			Row->SetBoolField(TEXT("skipped"), true);
			Row->SetStringField(TEXT("skip_reason"), TEXT("matrix_filter"));
			ResultRows.Add(MakeShared<FJsonValueObject>(Row.ToSharedRef()));
			return;
		}

		if (IsBannedTool(Def))
		{
			++SkippedBanned;
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("tool_id"), ToolId);
			Row->SetBoolField(TEXT("skipped"), true);
			Row->SetStringField(TEXT("skip_reason"), TEXT("banned"));
			ResultRows.Add(MakeShared<FJsonValueObject>(Row.ToSharedRef()));
			return;
		}

		TSharedPtr<FJsonObject> Args;
		FString FixtureErr;
		if (!TryLoadFixtureArgs(ToolId, Args, FixtureErr))
		{
			AppendViolation(ContractViolations, ToolId, FixtureErr);
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("tool_id"), ToolId);
			Row->SetBoolField(TEXT("skipped"), false);
			Row->SetStringField(TEXT("tier"), TEXT("fixture_error"));
			Row->SetStringField(TEXT("error_message"), FixtureErr);
			ResultRows.Add(MakeShared<FJsonValueObject>(Row.ToSharedRef()));
			return;
		}

		++Invoked;

		const double T0 = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Warning, TEXT("CatalogMatrix: invoking %s (%d/%d)"), *ToolId, Invoked, Cat.GetToolCount());
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(ToolId, Args, Def, nullptr, FString(), FString());
		const double T1 = FPlatformTime::Seconds();
		const double Ms = (T1 - T0) * 1000.0;
		UE_LOG(LogTemp, Warning, TEXT("CatalogMatrix: done %s ok=%s duration_ms=%.3f"),
			*ToolId,
			R.bOk ? TEXT("true") : TEXT("false"),
			Ms);

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("tool_id"), ToolId);
		Row->SetBoolField(TEXT("skipped"), false);
		Row->SetNumberField(TEXT("duration_ms"), Ms);
		Row->SetBoolField(TEXT("bOk"), R.bOk);
		if (!R.ErrorMessage.IsEmpty())
		{
			Row->SetStringField(TEXT("error_message"), R.ErrorMessage);
		}

		if (R.ContentForModel.IsEmpty())
		{
			AppendViolation(ContractViolations, ToolId, TEXT("empty ContentForModel"));
			Row->SetStringField(TEXT("tier"), TEXT("contract_fail"));
			ResultRows.Add(MakeShared<FJsonValueObject>(Row.ToSharedRef()));
			return;
		}

		TSharedPtr<FJsonObject> ParsedBody;
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
			if (!FJsonSerializer::Deserialize(Reader, ParsedBody) || !ParsedBody.IsValid())
			{
				AppendViolation(ContractViolations, ToolId, TEXT("ContentForModel not valid JSON"));
				Row->SetStringField(TEXT("tier"), TEXT("contract_fail"));
				ResultRows.Add(MakeShared<FJsonValueObject>(Row.ToSharedRef()));
				return;
			}
		}

		if (!ResponseLooksStructured(ParsedBody))
		{
			AppendViolation(ContractViolations, ToolId, TEXT("JSON missing ok/status/error"));
			Row->SetStringField(TEXT("tier"), TEXT("contract_fail"));
		}
		else
		{
			Row->SetStringField(TEXT("tier"), TEXT("contract_ok"));
		}

		FString Status;
		if (ParsedBody->TryGetStringField(TEXT("status"), Status))
		{
			Row->SetStringField(TEXT("parsed_status"), Status);
		}

		FString Preview = R.ContentForModel;
		if (Preview.Len() > 500)
		{
			Preview.LeftInline(500);
			Preview += TEXT("...");
		}
		Row->SetStringField(TEXT("response_preview"), Preview);

		ResultRows.Add(MakeShared<FJsonValueObject>(Row.ToSharedRef()));
	});

	TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetNumberField(TEXT("tools_total"), Cat.GetToolCount());
	Summary->SetNumberField(TEXT("skipped_banned"), SkippedBanned);
	Summary->SetNumberField(TEXT("skipped_filter"), SkippedFilter);
	Summary->SetNumberField(TEXT("invoked"), Invoked);
	Summary->SetNumberField(TEXT("contract_violations"), ContractViolations.Num());
	Summary->SetNumberField(TEXT("unexpected_exceptions"), 0);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema_version"), TEXT("1"));
	Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	if (!GitCommit.IsEmpty())
	{
		Root->SetStringField(TEXT("git_commit"), GitCommit);
	}
	Root->SetObjectField(TEXT("summary"), Summary);
	Root->SetArrayField(TEXT("contract_violations"), ContractViolations);
	Root->SetArrayField(TEXT("results"), ResultRows);

	const FString OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAiEditor"), TEXT("Automation"));
	IFileManager::Get().MakeDirectory(*OutDir, true);
	const FString OutPath = FPaths::Combine(OutDir, TEXT("tool_matrix_last.json"));
	FString OutStr;
	{
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutStr);
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
		{
			if (OutViolationMessages)
			{
				OutViolationMessages->Add(TEXT("CatalogMatrix: failed to serialize tool_matrix_last.json"));
			}
			return false;
		}
	}
	if (!FFileHelper::SaveStringToFile(OutStr, *OutPath))
	{
		if (OutViolationMessages)
		{
			OutViolationMessages->Add(FString::Printf(TEXT("CatalogMatrix: failed to write %s"), *OutPath));
		}
		return false;
	}

	if (OutViolationMessages)
	{
		for (const TSharedPtr<FJsonValue>& V : ContractViolations)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (V.IsValid() && V->TryGetObject(O) && O->IsValid())
			{
				FString Tid, Reason;
				(*O)->TryGetStringField(TEXT("tool_id"), Tid);
				(*O)->TryGetStringField(TEXT("reason"), Reason);
				OutViolationMessages->Add(FString::Printf(TEXT("CatalogMatrix contract: %s — %s"), *Tid, *Reason));
			}
		}
	}

	return ContractViolations.Num() == 0;
}
