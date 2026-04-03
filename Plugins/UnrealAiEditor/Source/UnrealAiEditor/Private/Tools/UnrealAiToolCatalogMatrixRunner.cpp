#include "Tools/UnrealAiToolCatalogMatrixRunner.h"

#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "Tools/UnrealAiToolCatalog.h"
#include "Tools/UnrealAiToolDispatch.h"
#include "Tools/UnrealAiToolResolver.h"

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

	static TArray<FString> GetRequiredParameterKeys(const TSharedPtr<FJsonObject>& ToolDef)
	{
		TArray<FString> Required;
		if (!ToolDef.IsValid())
		{
			return Required;
		}
		const TSharedPtr<FJsonObject>* Parameters = nullptr;
		if (!ToolDef->TryGetObjectField(TEXT("parameters"), Parameters) || !Parameters || !(*Parameters).IsValid())
		{
			return Required;
		}
		const TArray<TSharedPtr<FJsonValue>>* RequiredArray = nullptr;
		if (!(*Parameters)->TryGetArrayField(TEXT("required"), RequiredArray) || !RequiredArray)
		{
			return Required;
		}
		for (const TSharedPtr<FJsonValue>& Value : *RequiredArray)
		{
			if (Value.IsValid())
			{
				const FString Key = Value->AsString();
				if (!Key.IsEmpty())
				{
					Required.Add(Key);
				}
			}
		}
		return Required;
	}

	static TSharedPtr<FJsonObject> GetParameterPropertiesObject(const TSharedPtr<FJsonObject>& ToolDef)
	{
		const TSharedPtr<FJsonObject>* Parameters = nullptr;
		if (!ToolDef.IsValid() || !ToolDef->TryGetObjectField(TEXT("parameters"), Parameters) || !Parameters || !(*Parameters).IsValid())
		{
			return nullptr;
		}
		const TSharedPtr<FJsonObject>* Properties = nullptr;
		if (!(*Parameters)->TryGetObjectField(TEXT("properties"), Properties) || !Properties || !(*Properties).IsValid())
		{
			return nullptr;
		}
		return *Properties;
	}

	static TSharedPtr<FJsonValue> BuildExampleJsonValueForSchema(const TSharedPtr<FJsonObject>& PropertySchema)
	{
		if (!PropertySchema.IsValid())
		{
			return MakeShared<FJsonValueString>(TEXT("<required>"));
		}
		const TArray<TSharedPtr<FJsonValue>>* EnumValues = nullptr;
		if (PropertySchema->TryGetArrayField(TEXT("enum"), EnumValues) && EnumValues && EnumValues->Num() > 0)
		{
			return (*EnumValues)[0];
		}
		FString Type;
		PropertySchema->TryGetStringField(TEXT("type"), Type);
		Type = Type.ToLower();
		if (Type == TEXT("string"))
		{
			return MakeShared<FJsonValueString>(TEXT("<required>"));
		}
		if (Type == TEXT("boolean"))
		{
			return MakeShared<FJsonValueBoolean>(false);
		}
		if (Type == TEXT("integer") || Type == TEXT("number"))
		{
			return MakeShared<FJsonValueNumber>(0);
		}
		if (Type == TEXT("array"))
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			const TSharedPtr<FJsonObject>* Items = nullptr;
			if (PropertySchema->TryGetObjectField(TEXT("items"), Items) && Items && (*Items).IsValid())
			{
				Arr.Add(BuildExampleJsonValueForSchema(*Items));
			}
			return MakeShared<FJsonValueArray>(Arr);
		}
		if (Type == TEXT("object"))
		{
			return MakeShared<FJsonValueObject>(MakeShared<FJsonObject>());
		}
		return MakeShared<FJsonValueString>(TEXT("<required>"));
	}

	/** Minimal args so the resolver can validate strict composite tools (no inference). */
	static TSharedPtr<FJsonObject> BuildMinimalSmokeArgsForTool(const FString& ToolId, const TSharedPtr<FJsonObject>& ToolDef)
	{
		if (ToolId == TEXT("setting_query"))
		{
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("domain"), TEXT("editor_preference"));
			A->SetStringField(TEXT("key"), TEXT("editor_focus"));
			return A;
		}
		if (ToolId == TEXT("setting_apply"))
		{
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("domain"), TEXT("editor_preference"));
			A->SetStringField(TEXT("key"), TEXT("editor_focus"));
			A->SetBoolField(TEXT("value"), false);
			return A;
		}
		if (ToolId == TEXT("viewport_camera_control"))
		{
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("operation"), TEXT("get_transform"));
			return A;
		}
		if (ToolId == TEXT("viewport_capture"))
		{
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("capture_kind"), TEXT("immediate_png"));
			return A;
		}
		if (ToolId == TEXT("viewport_frame"))
		{
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("target"), TEXT("selection"));
			return A;
		}
		if (ToolId == TEXT("material_instance_set_parameter"))
		{
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("value_kind"), TEXT("vector"));
			A->SetStringField(TEXT("material_path"), TEXT("/Game/Placeholder.Placeholder"));
			A->SetStringField(TEXT("parameter_name"), TEXT("Param"));
			TArray<TSharedPtr<FJsonValue>> LC;
			LC.Add(MakeShared<FJsonValueNumber>(1.0));
			LC.Add(MakeShared<FJsonValueNumber>(1.0));
			LC.Add(MakeShared<FJsonValueNumber>(1.0));
			A->SetArrayField(TEXT("linear_color"), LC);
			return A;
		}
		if (ToolId == TEXT("asset_graph_query"))
		{
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("relation"), TEXT("referencers"));
			A->SetStringField(TEXT("object_path"), TEXT("/Game/Placeholder.Placeholder"));
			return A;
		}

		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		const TArray<FString> Required = GetRequiredParameterKeys(ToolDef);
		const TSharedPtr<FJsonObject> Properties = GetParameterPropertiesObject(ToolDef);
		for (const FString& Key : Required)
		{
			const TSharedPtr<FJsonObject>* PropSchema = nullptr;
			if (Properties.IsValid() && Properties->TryGetObjectField(Key, PropSchema) && PropSchema && (*PropSchema).IsValid())
			{
				Out->SetField(Key, BuildExampleJsonValueForSchema(*PropSchema));
			}
			else
			{
				Out->SetStringField(Key, TEXT("<required>"));
			}
		}
		return Out;
	}

}

bool UnrealAiToolCatalogMatrixRunner::RunAndWriteJson(const FString& MatrixFilter, TArray<FString>* OutViolationMessages)
{
	using namespace UnrealAiToolCatalogMatrixRunnerPriv;

	const FString GitCommit; // optional CI metadata; not required for matrix output

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
	FUnrealAiToolResolver Resolver(Cat);

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

		const TSharedPtr<FJsonObject> Args = BuildMinimalSmokeArgsForTool(ToolId, Def);
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("tool_id"), ToolId);
		Row->SetBoolField(TEXT("skipped"), false);

		++Invoked;

		const double T0 = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Warning, TEXT("CatalogMatrix: invoking %s (%d/%d)"), *ToolId, Invoked, Cat.GetToolCount());
		FUnrealAiToolInvocationResult R;
		const FUnrealAiResolvedToolInvocation Resolved = Resolver.Resolve(ToolId, Args);
		if (!Resolved.bResolved)
		{
			R = Resolved.FailureResult;
		}
		else
		{
			R = UnrealAiDispatchTool(Resolved.LegacyToolId, Resolved.ResolvedArguments, Resolved.LegacyToolDefinition, nullptr, FString(), FString());
			FUnrealAiToolResolver::AttachAuditToResult(Resolved.Audit, R);
			Row->SetStringField(TEXT("resolved_tool_id"), Resolved.LegacyToolId);
		}
		const double T1 = FPlatformTime::Seconds();
		const double Ms = (T1 - T0) * 1000.0;
		UE_LOG(LogTemp, Warning, TEXT("CatalogMatrix: done %s ok=%s duration_ms=%.3f"),
			*ToolId,
			R.bOk ? TEXT("true") : TEXT("false"),
			Ms);

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
