#include "Tools/UnrealAiToolResolver.h"

#include "Tools/UnrealAiToolCatalog.h"
#include "Tools/UnrealAiToolJson.h"

#include "Algo/LevenshteinDistance.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAiToolResolverPriv
{
	struct FValidationIssue
	{
		FString Path;
		FString Message;
	};

	struct FToolIdCandidate
	{
		FString ToolId;
		int32 Distance = MAX_int32;
	};

	static FString NormalizeToolId(const FString& In)
	{
		FString Out = In.ToLower();
		while (Out.ReplaceInline(TEXT("__"), TEXT("_")) > 0)
		{
		}
		Out.TrimStartAndEndInline();
		return Out;
	}

	static FString ValueTypeToString(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return TEXT("null");
		}

		switch (Value->Type)
		{
		case EJson::None:
		case EJson::Null:
			return TEXT("null");
		case EJson::String:
			return TEXT("string");
		case EJson::Number:
			return TEXT("number");
		case EJson::Boolean:
			return TEXT("boolean");
		case EJson::Array:
			return TEXT("array");
		case EJson::Object:
			return TEXT("object");
		default:
			return TEXT("unknown");
		}
	}

	static void AddValidationIssue(TArray<FValidationIssue>& Issues, const FString& Path, const FString& Message)
	{
		FValidationIssue& Issue = Issues.AddDefaulted_GetRef();
		Issue.Path = Path;
		Issue.Message = Message;
	}

	static TSharedPtr<FJsonObject> CloneObject(const TSharedPtr<FJsonObject>& Source)
	{
		TSharedPtr<FJsonObject> Copy = MakeShared<FJsonObject>();
		if (!Source.IsValid())
		{
			return Copy;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Source->Values)
		{
			Copy->SetField(Pair.Key, Pair.Value);
		}
		return Copy;
	}

	static void EnsureAuditArrays(const TSharedPtr<FJsonObject>& Audit)
	{
		if (!Audit.IsValid())
		{
			return;
		}
		if (!Audit->HasField(TEXT("transforms")))
		{
			Audit->SetArrayField(TEXT("transforms"), {});
		}
		if (!Audit->HasField(TEXT("warnings")))
		{
			Audit->SetArrayField(TEXT("warnings"), {});
		}
	}

	static void AppendAuditString(const TSharedPtr<FJsonObject>& Audit, const TCHAR* Field, const FString& Value)
	{
		if (!Audit.IsValid() || Value.IsEmpty())
		{
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
		TArray<TSharedPtr<FJsonValue>> Values;
		if (Audit->TryGetArrayField(Field, Existing) && Existing)
		{
			Values = *Existing;
		}
		Values.Add(MakeShared<FJsonValueString>(Value));
		Audit->SetArrayField(Field, Values);
	}

	static void AddTransform(const TSharedPtr<FJsonObject>& Audit, const FString& Value)
	{
		AppendAuditString(Audit, TEXT("transforms"), Value);
	}

	static void AddWarning(const TSharedPtr<FJsonObject>& Audit, const FString& Value)
	{
		AppendAuditString(Audit, TEXT("warnings"), Value);
	}

	static TArray<FString> GetRequiredFields(const TSharedPtr<FJsonObject>& ToolDef)
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

	static TSharedPtr<FJsonObject> GetParameterProperties(const TSharedPtr<FJsonObject>& ToolDef)
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

	static bool HasAnyNonNullField(const TSharedPtr<FJsonObject>& Object)
	{
		return Object.IsValid() && Object->Values.Num() > 0;
	}

	static TSharedPtr<FJsonValue> BuildExampleValueForSchema(const TSharedPtr<FJsonObject>& PropertySchema)
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
				Arr.Add(BuildExampleValueForSchema(*Items));
			}
			return MakeShared<FJsonValueArray>(Arr);
		}
		if (Type == TEXT("object"))
		{
			return MakeShared<FJsonValueObject>(MakeShared<FJsonObject>());
		}

		return MakeShared<FJsonValueString>(TEXT("<required>"));
	}

	static TSharedPtr<FJsonObject> BuildSuggestedArguments(const TSharedPtr<FJsonObject>& ToolDef, const TSharedPtr<FJsonObject>& ExistingArgs)
	{
		TSharedPtr<FJsonObject> Suggested = MakeShared<FJsonObject>();
		if (ExistingArgs.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ExistingArgs->Values)
			{
				Suggested->SetField(Pair.Key, Pair.Value);
			}
		}

		const TArray<FString> Required = GetRequiredFields(ToolDef);
		const TSharedPtr<FJsonObject> Properties = GetParameterProperties(ToolDef);
		for (const FString& Key : Required)
		{
			if (Suggested->HasField(Key))
			{
				continue;
			}

			const TSharedPtr<FJsonObject>* PropSchema = nullptr;
			if (Properties.IsValid() && Properties->TryGetObjectField(Key, PropSchema) && PropSchema && (*PropSchema).IsValid())
			{
				Suggested->SetField(Key, BuildExampleValueForSchema(*PropSchema));
			}
			else
			{
				Suggested->SetStringField(Key, TEXT("<required>"));
			}
		}
		return Suggested;
	}

	static FUnrealAiToolInvocationResult BuildResolverError(
		const TSharedPtr<FJsonObject>& Audit,
		const FString& Message,
		const FString& SuggestedToolId,
		const TSharedPtr<FJsonObject>& SuggestedArguments)
	{
		FUnrealAiToolInvocationResult Result = SuggestedArguments.IsValid()
			? UnrealAiToolJson::ErrorWithSuggestedCall(Message, SuggestedToolId, SuggestedArguments)
			: UnrealAiToolJson::Error(Message);
		FUnrealAiToolResolver::AttachAuditToResult(Audit, Result);
		return Result;
	}

	static FUnrealAiToolInvocationResult BuildResolverValidationError(
		const TSharedPtr<FJsonObject>& Audit,
		const FString& Message,
		const FString& SuggestedToolId,
		const TSharedPtr<FJsonObject>& SuggestedArguments,
		const TArray<FValidationIssue>& Issues)
	{
		FUnrealAiToolInvocationResult Result;
		Result.bOk = false;
		Result.ErrorMessage = Message;

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), false);
		Root->SetStringField(TEXT("error"), TEXT("validation_failed"));
		Root->SetStringField(TEXT("message"), Message);

		TArray<TSharedPtr<FJsonValue>> ValidationIssues;
		for (const FValidationIssue& Issue : Issues)
		{
			TSharedPtr<FJsonObject> IssueObject = MakeShared<FJsonObject>();
			IssueObject->SetStringField(TEXT("path"), Issue.Path);
			IssueObject->SetStringField(TEXT("message"), Issue.Message);
			ValidationIssues.Add(MakeShared<FJsonValueObject>(IssueObject));
		}
		if (ValidationIssues.Num() > 0)
		{
			Root->SetArrayField(TEXT("validation_errors"), ValidationIssues);
		}

		if (!SuggestedToolId.IsEmpty() && SuggestedArguments.IsValid())
		{
			TSharedPtr<FJsonObject> Suggested = MakeShared<FJsonObject>();
			Suggested->SetStringField(TEXT("tool_id"), SuggestedToolId);
			Suggested->SetObjectField(TEXT("arguments"), SuggestedArguments);
			Root->SetObjectField(TEXT("suggested_correct_call"), Suggested);
		}

		Result.ContentForModel = UnrealAiToolJson::SerializeObject(Root);
		FUnrealAiToolResolver::AttachAuditToResult(Audit, Result);
		return Result;
	}

	static void CanonicalizeAliasKeys(
		const TSharedPtr<FJsonObject>& Args,
		const TSharedPtr<FJsonObject>& Audit,
		const FString& CanonicalKey,
		const TArray<FString>& Aliases)
	{
		if (!Args.IsValid())
		{
			return;
		}

		bool bCanonicalPresent = Args->HasField(CanonicalKey);
		for (const FString& Alias : Aliases)
		{
			if (!Args->HasField(Alias))
			{
				continue;
			}

			const TSharedPtr<FJsonValue> AliasValue = Args->TryGetField(Alias);
			if (!AliasValue.IsValid())
			{
				continue;
			}

			if (bCanonicalPresent)
			{
				AddWarning(Audit, FString::Printf(TEXT("alias '%s' ignored because canonical '%s' is already present"), *Alias, *CanonicalKey));
			}
			else
			{
				Args->SetField(CanonicalKey, AliasValue);
				bCanonicalPresent = true;
				AddTransform(Audit, FString::Printf(TEXT("canonicalized '%s' -> '%s'"), *Alias, *CanonicalKey));
			}
		}
	}

	static void CanonicalizeSingleStringToArray(
		const TSharedPtr<FJsonObject>& Args,
		const TSharedPtr<FJsonObject>& Audit,
		const FString& ArrayKey,
		const TArray<FString>& SingleKeys)
	{
		if (!Args.IsValid() || Args->HasField(ArrayKey))
		{
			return;
		}

		for (const FString& Key : SingleKeys)
		{
			FString Value;
			if (!Args->TryGetStringField(Key, Value) || Value.IsEmpty())
			{
				continue;
			}

			TArray<TSharedPtr<FJsonValue>> Values;
			Values.Add(MakeShared<FJsonValueString>(Value));
			Args->SetArrayField(ArrayKey, Values);
			AddTransform(Audit, FString::Printf(TEXT("promoted '%s' to '%s'[]"), *Key, *ArrayKey));
			return;
		}
	}

	static void CanonicalizeToolArguments(
		const FString& ToolId,
		const TSharedPtr<FJsonObject>& Args,
		const TSharedPtr<FJsonObject>& Audit)
	{
		CanonicalizeAliasKeys(Args, Audit, TEXT("object_path"), {TEXT("path"), TEXT("asset_path")});

		if (ToolId == TEXT("asset_registry_query"))
		{
			CanonicalizeAliasKeys(Args, Audit, TEXT("path_filter"), {TEXT("filter"), TEXT("path_prefix"), TEXT("path")});
			CanonicalizeAliasKeys(Args, Audit, TEXT("class_name"), {TEXT("class_path"), TEXT("asset_class")});
		}
		else if (ToolId == TEXT("asset_open_editor") || ToolId == TEXT("asset_export_properties"))
		{
			CanonicalizeAliasKeys(Args, Audit, TEXT("object_path"), {TEXT("path"), TEXT("asset_path")});
		}
		else if (ToolId == TEXT("blueprint_add_variable"))
		{
			CanonicalizeAliasKeys(Args, Audit, TEXT("name"), {TEXT("variable_name")});
			CanonicalizeAliasKeys(Args, Audit, TEXT("type"), {TEXT("variable_type")});
		}
		else if (ToolId == TEXT("blueprint_open_graph_tab"))
		{
			CanonicalizeAliasKeys(Args, Audit, TEXT("blueprint_path"), {TEXT("object_path"), TEXT("path")});
			CanonicalizeAliasKeys(Args, Audit, TEXT("graph_name"), {TEXT("graph")});
		}
		else if (ToolId == TEXT("content_browser_sync_asset"))
		{
			CanonicalizeAliasKeys(Args, Audit, TEXT("path"), {TEXT("object_path"), TEXT("asset_path")});
		}
		else if (ToolId == TEXT("editor_set_selection"))
		{
			CanonicalizeSingleStringToArray(Args, Audit, TEXT("actor_paths"), {TEXT("actor_path"), TEXT("path")});
		}
		else if (ToolId == TEXT("viewport_frame_actors"))
		{
			CanonicalizeSingleStringToArray(Args, Audit, TEXT("actor_paths"), {TEXT("actor_path"), TEXT("path")});
		}
		else if (ToolId == TEXT("asset_save_packages"))
		{
			CanonicalizeSingleStringToArray(Args, Audit, TEXT("package_paths"), {TEXT("package_path")});
		}
	}

	static bool EnumContainsValue(
		const TArray<TSharedPtr<FJsonValue>>& EnumValues,
		const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& EnumValue : EnumValues)
		{
			if (!EnumValue.IsValid() || EnumValue->Type != Value->Type)
			{
				continue;
			}

			switch (Value->Type)
			{
			case EJson::String:
				if (EnumValue->AsString() == Value->AsString())
				{
					return true;
				}
				break;
			case EJson::Boolean:
				if (EnumValue->AsBool() == Value->AsBool())
				{
					return true;
				}
				break;
			case EJson::Number:
				if (EnumValue->AsNumber() == Value->AsNumber())
				{
					return true;
				}
				break;
			default:
				break;
			}
		}
		return false;
	}

	static void ValidateValueAgainstSchema(
		const FString& Path,
		const TSharedPtr<FJsonValue>& Value,
		const TSharedPtr<FJsonObject>& Schema,
		int32 Depth,
		TArray<FValidationIssue>& Issues);

	static void ValidateObjectAgainstSchema(
		const FString& Path,
		const TSharedPtr<FJsonObject>& ObjectValue,
		const TSharedPtr<FJsonObject>& Schema,
		int32 Depth,
		TArray<FValidationIssue>& Issues)
	{
		if (!ObjectValue.IsValid() || !Schema.IsValid())
		{
			return;
		}

		bool bAllowAdditionalProperties = true;
		Schema->TryGetBoolField(TEXT("additionalProperties"), bAllowAdditionalProperties);

		const TSharedPtr<FJsonObject>* Properties = nullptr;
		const bool bHasProperties =
			Schema->TryGetObjectField(TEXT("properties"), Properties) && Properties && (*Properties).IsValid();

		if (!bAllowAdditionalProperties && bHasProperties)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ObjectValue->Values)
			{
				if (!(*Properties)->HasField(Pair.Key))
				{
					AddValidationIssue(
						Issues,
						FString::Printf(TEXT("%s.%s"), *Path, *Pair.Key),
						TEXT("unknown field"));
				}
			}
		}

		if (!bHasProperties || Depth >= 2)
		{
			return;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ObjectValue->Values)
		{
			const TSharedPtr<FJsonObject>* PropertySchema = nullptr;
			if (!(*Properties)->TryGetObjectField(Pair.Key, PropertySchema) || !PropertySchema || !(*PropertySchema).IsValid())
			{
				continue;
			}
			ValidateValueAgainstSchema(
				FString::Printf(TEXT("%s.%s"), *Path, *Pair.Key),
				Pair.Value,
				*PropertySchema,
				Depth + 1,
				Issues);
		}
	}

	static void ValidateValueAgainstSchema(
		const FString& Path,
		const TSharedPtr<FJsonValue>& Value,
		const TSharedPtr<FJsonObject>& Schema,
		int32 Depth,
		TArray<FValidationIssue>& Issues)
	{
		if (!Schema.IsValid() || !Value.IsValid())
		{
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* OneOfSchemas = nullptr;
		if (Schema->TryGetArrayField(TEXT("oneOf"), OneOfSchemas) && OneOfSchemas && OneOfSchemas->Num() > 0)
		{
			for (const TSharedPtr<FJsonValue>& CandidateValue : *OneOfSchemas)
			{
				const TSharedPtr<FJsonObject> CandidateSchema = CandidateValue.IsValid() ? CandidateValue->AsObject() : nullptr;
				if (!CandidateSchema.IsValid())
				{
					continue;
				}

				TArray<FValidationIssue> CandidateIssues;
				ValidateValueAgainstSchema(Path, Value, CandidateSchema, Depth, CandidateIssues);
				if (CandidateIssues.Num() == 0)
				{
					return;
				}
			}

			AddValidationIssue(Issues, Path, TEXT("value does not match any allowed schema shape"));
			return;
		}

		FString ExpectedType;
		if (Schema->TryGetStringField(TEXT("type"), ExpectedType))
		{
			ExpectedType = ExpectedType.ToLower();
			if (ExpectedType == TEXT("string") && Value->Type != EJson::String)
			{
				AddValidationIssue(Issues, Path, FString::Printf(TEXT("expected string but got %s"), *ValueTypeToString(Value)));
				return;
			}
			if ((ExpectedType == TEXT("number") || ExpectedType == TEXT("integer")) && Value->Type != EJson::Number)
			{
				AddValidationIssue(Issues, Path, FString::Printf(TEXT("expected %s but got %s"), *ExpectedType, *ValueTypeToString(Value)));
				return;
			}
			if (ExpectedType == TEXT("boolean") && Value->Type != EJson::Boolean)
			{
				AddValidationIssue(Issues, Path, FString::Printf(TEXT("expected boolean but got %s"), *ValueTypeToString(Value)));
				return;
			}
			if (ExpectedType == TEXT("object"))
			{
				if (Value->Type != EJson::Object)
				{
					AddValidationIssue(Issues, Path, FString::Printf(TEXT("expected object but got %s"), *ValueTypeToString(Value)));
					return;
				}
				ValidateObjectAgainstSchema(Path, Value->AsObject(), Schema, Depth, Issues);
			}
			if (ExpectedType == TEXT("array"))
			{
				if (Value->Type != EJson::Array)
				{
					AddValidationIssue(Issues, Path, FString::Printf(TEXT("expected array but got %s"), *ValueTypeToString(Value)));
					return;
				}

				const TArray<TSharedPtr<FJsonValue>>& ArrayValues = Value->AsArray();
				int32 MinItems = 0;
				if (Schema->TryGetNumberField(TEXT("minItems"), MinItems) && ArrayValues.Num() < MinItems)
				{
					AddValidationIssue(Issues, Path, FString::Printf(TEXT("expected at least %d items but got %d"), MinItems, ArrayValues.Num()));
				}

				int32 MaxItems = 0;
				if (Schema->TryGetNumberField(TEXT("maxItems"), MaxItems) && ArrayValues.Num() > MaxItems)
				{
					AddValidationIssue(Issues, Path, FString::Printf(TEXT("expected at most %d items but got %d"), MaxItems, ArrayValues.Num()));
				}

				if (Depth < 2)
				{
					const TSharedPtr<FJsonObject>* ItemSchema = nullptr;
					if (Schema->TryGetObjectField(TEXT("items"), ItemSchema) && ItemSchema && (*ItemSchema).IsValid())
					{
						for (int32 Index = 0; Index < ArrayValues.Num(); ++Index)
						{
							ValidateValueAgainstSchema(
								FString::Printf(TEXT("%s[%d]"), *Path, Index),
								ArrayValues[Index],
								*ItemSchema,
								Depth + 1,
								Issues);
						}
					}
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* EnumValues = nullptr;
		if (Schema->TryGetArrayField(TEXT("enum"), EnumValues) && EnumValues && EnumValues->Num() > 0 && !EnumContainsValue(*EnumValues, Value))
		{
			AddValidationIssue(Issues, Path, TEXT("value is not one of the allowed enum members"));
		}
	}

	static bool ValidateSchemaShape(
		const FString& ToolId,
		const TSharedPtr<FJsonObject>& ToolDef,
		const TSharedPtr<FJsonObject>& Args,
		const TSharedPtr<FJsonObject>& Audit,
		const FString& SuggestedToolId,
		FUnrealAiToolInvocationResult& OutFailure)
	{
		const TSharedPtr<FJsonObject>* Parameters = nullptr;
		if (!ToolDef.IsValid() || !ToolDef->TryGetObjectField(TEXT("parameters"), Parameters) || !Parameters || !(*Parameters).IsValid())
		{
			return true;
		}

		TArray<FValidationIssue> Issues;
		ValidateObjectAgainstSchema(TEXT("arguments"), Args, *Parameters, 0, Issues);
		if (Issues.Num() == 0)
		{
			return true;
		}

		AddWarning(Audit, FString::Printf(TEXT("schema validation failed for %s"), *ToolId));
		OutFailure = BuildResolverValidationError(
			Audit,
			FString::Printf(TEXT("%s arguments failed schema validation"), *ToolId),
			SuggestedToolId,
			BuildSuggestedArguments(ToolDef, Args),
			Issues);
		return false;
	}

	static TArray<FToolIdCandidate> BuildToolIdCandidates(const FUnrealAiToolCatalog& Catalog, const FString& RequestedToolId)
	{
		TArray<FToolIdCandidate> Candidates;
		TArray<FString> ToolIds;
		Catalog.GetAllToolIds(ToolIds);
		for (const FString& ToolId : ToolIds)
		{
			FToolIdCandidate Candidate;
			Candidate.ToolId = ToolId;
			Candidate.Distance = Algo::LevenshteinDistance(NormalizeToolId(ToolId), RequestedToolId);
			Candidates.Add(Candidate);
		}

		Candidates.Sort([](const FToolIdCandidate& A, const FToolIdCandidate& B)
		{
			if (A.Distance != B.Distance)
			{
				return A.Distance < B.Distance;
			}
			return A.ToolId < B.ToolId;
		});
		return Candidates;
	}

	static bool ValidateRequiredFields(
		const FString& ToolId,
		const TSharedPtr<FJsonObject>& ToolDef,
		const TSharedPtr<FJsonObject>& Args,
		const TSharedPtr<FJsonObject>& Audit,
		const FString& SuggestedToolId,
		FUnrealAiToolInvocationResult& OutFailure)
	{
		const TArray<FString> Required = GetRequiredFields(ToolDef);
		if (Required.Num() == 0)
		{
			return true;
		}

		if (!HasAnyNonNullField(Args))
		{
			AddWarning(Audit, TEXT("rejected empty arguments for required-arg tool"));
			OutFailure = BuildResolverError(
				Audit,
				FString::Printf(TEXT("%s requires arguments; do not call it with {}."), *ToolId),
				SuggestedToolId,
				BuildSuggestedArguments(ToolDef, Args));
			return false;
		}

		TArray<FString> Missing;
		for (const FString& Key : Required)
		{
			if (!Args.IsValid() || !Args->HasField(Key))
			{
				Missing.Add(Key);
				continue;
			}

			const TSharedPtr<FJsonValue> Value = Args->TryGetField(Key);
			if (!Value.IsValid() || Value->IsNull())
			{
				Missing.Add(Key);
				continue;
			}

			if (Value->Type == EJson::String && Value->AsString().TrimStartAndEnd().IsEmpty())
			{
				Missing.Add(Key);
			}
		}

		if (Missing.Num() == 0)
		{
			return true;
		}

		AddWarning(Audit, FString::Printf(TEXT("missing required fields: %s"), *FString::Join(Missing, TEXT(", "))));
		OutFailure = BuildResolverError(
			Audit,
			FString::Printf(TEXT("%s is missing required fields: %s"), *ToolId, *FString::Join(Missing, TEXT(", "))),
			SuggestedToolId,
			BuildSuggestedArguments(ToolDef, Args));
		return false;
	}

	static TSharedPtr<FJsonObject> BuildAudit(const FUnrealAiToolCatalog& Catalog, const FString& RequestedToolId)
	{
		TSharedPtr<FJsonObject> Audit = MakeShared<FJsonObject>();
		Audit->SetStringField(TEXT("resolver_version"), Catalog.GetResolverContractVersion());
		Audit->SetStringField(TEXT("requested_tool_id"), RequestedToolId);
		EnsureAuditArrays(Audit);
		return Audit;
	}

	static TSharedPtr<FJsonObject> ProjectArguments(
		const TSharedPtr<FJsonObject>& Source,
		const TArray<FString>& Keys,
		const TSharedPtr<FJsonObject>& Audit,
		const FString& Reason)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		if (Source.IsValid())
		{
			for (const FString& Key : Keys)
			{
				const TSharedPtr<FJsonValue> Value = Source->TryGetField(Key);
				if (Value.IsValid())
				{
					Out->SetField(Key, Value);
				}
			}
		}
		if (!Reason.IsEmpty())
		{
			AddTransform(Audit, Reason);
		}
		return Out;
	}
}

FUnrealAiToolResolver::FUnrealAiToolResolver(const FUnrealAiToolCatalog& InCatalog)
	: Catalog(InCatalog)
{
}

FUnrealAiResolvedToolInvocation FUnrealAiToolResolver::Resolve(const FString& ToolId, const TSharedPtr<FJsonObject>& Arguments) const
{
	using namespace UnrealAiToolResolverPriv;

	const double ResolveStartSeconds = FPlatformTime::Seconds();

	FUnrealAiResolvedToolInvocation Result;
	Result.RequestedToolId = ToolId;
	Result.CanonicalToolId = NormalizeToolId(ToolId);
	Result.Audit = BuildAudit(Catalog, ToolId);
	Result.ResolvedArguments = CloneObject(Arguments);

	if (Result.CanonicalToolId != ToolId)
	{
		AddTransform(Result.Audit, FString::Printf(TEXT("normalized tool_id '%s' -> '%s'"), *ToolId, *Result.CanonicalToolId));
	}

	const TMap<FString, FString> ToolAliases = {
		{TEXT("scene_fuzzy_search.query"), TEXT("scene_fuzzy_search")},
		{TEXT("asset_destroy"), TEXT("asset_delete")}
	};

	if (const FString* Alias = ToolAliases.Find(Result.CanonicalToolId))
	{
		AddTransform(Result.Audit, FString::Printf(TEXT("aliased tool_id '%s' -> '%s'"), *Result.CanonicalToolId, **Alias));
		Result.CanonicalToolId = *Alias;
	}

	Result.RequestedToolDefinition = Catalog.FindToolDefinition(Result.CanonicalToolId);
	if (!Result.RequestedToolDefinition.IsValid())
	{
		Result.RequestedToolDefinition = Catalog.FindToolDefinition(ToolId);
	}

	if (!Result.RequestedToolDefinition.IsValid())
	{
		TArray<FString> Suggestions;
		const TArray<FToolIdCandidate> Candidates = BuildToolIdCandidates(Catalog, Result.CanonicalToolId);
		for (int32 Index = 0; Index < FMath::Min(3, Candidates.Num()); ++Index)
		{
			Suggestions.Add(Candidates[Index].ToolId);
		}

		const FString Message = Suggestions.Num() > 0
			? FString::Printf(TEXT("Unknown tool_id '%s'. Did you mean: %s?"), *ToolId, *FString::Join(Suggestions, TEXT(", ")))
			: FString::Printf(TEXT("Unknown tool_id '%s'."), *ToolId);
		const FString SuggestedToolId = Suggestions.Num() > 0 ? Suggestions[0] : FString();
		const TSharedPtr<FJsonObject> SuggestedDef = !SuggestedToolId.IsEmpty() ? Catalog.FindToolDefinition(SuggestedToolId) : nullptr;
		Result.FailureResult = BuildResolverError(
			Result.Audit,
			Message,
			SuggestedToolId,
			SuggestedDef.IsValid() ? BuildSuggestedArguments(SuggestedDef, MakeShared<FJsonObject>()) : nullptr);
		Result.Audit->SetNumberField(TEXT("latency_ms"), (FPlatformTime::Seconds() - ResolveStartSeconds) * 1000.0);
		return Result;
	}

	CanonicalizeToolArguments(Result.CanonicalToolId, Result.ResolvedArguments, Result.Audit);

	if (Result.CanonicalToolId == TEXT("settings_get") || Result.CanonicalToolId == TEXT("settings_set"))
	{
		CanonicalizeAliasKeys(Result.ResolvedArguments, Result.Audit, TEXT("scope"), {TEXT("domain")});
	}

	TSharedPtr<FJsonObject> LegacyArgs = Result.ResolvedArguments;
	FString LegacyToolId = Result.CanonicalToolId;

	static auto MapSettingsDomainToLegacyScope = [](const FString& Domain) -> FString
	{
		static const TMap<FString, FString> DomainToScope = {
			{TEXT("editor_preference"), TEXT("editor")},
			{TEXT("plugin_setting"), TEXT("editor")},
			{TEXT("viewport_session"), TEXT("viewport")},
			{TEXT("project_setting"), TEXT("project")},
			{TEXT("cvar"), TEXT("editor")},
			{TEXT("map_world_settings"), TEXT("project")}
		};
		if (const FString* Scope = DomainToScope.Find(Domain.ToLower()))
		{
			return *Scope;
		}
		return FString();
	};

	if (Result.CanonicalToolId == TEXT("setting_query"))
	{
		FString Domain;
		FString Key;
		Result.ResolvedArguments->TryGetStringField(TEXT("domain"), Domain);
		Result.ResolvedArguments->TryGetStringField(TEXT("key"), Key);

		LegacyToolId = TEXT("settings_get");
		LegacyArgs = MakeShared<FJsonObject>();
		const FString ScopeValue = MapSettingsDomainToLegacyScope(Domain);
		if (!ScopeValue.IsEmpty())
		{
			LegacyArgs->SetStringField(TEXT("scope"), ScopeValue);
			AddTransform(Result.Audit, FString::Printf(TEXT("mapped setting domain '%s' -> scope '%s'"), *Domain, *ScopeValue));
		}
		LegacyArgs->SetStringField(TEXT("key"), Key);
		AddTransform(Result.Audit, TEXT("family dispatch: setting_query -> settings_get"));
	}
	else if (Result.CanonicalToolId == TEXT("setting_apply"))
	{
		FString Domain;
		FString Key;
		Result.ResolvedArguments->TryGetStringField(TEXT("domain"), Domain);
		Result.ResolvedArguments->TryGetStringField(TEXT("key"), Key);

		LegacyToolId = TEXT("settings_set");
		LegacyArgs = MakeShared<FJsonObject>();
		const FString ScopeValue = MapSettingsDomainToLegacyScope(Domain);
		if (!ScopeValue.IsEmpty())
		{
			LegacyArgs->SetStringField(TEXT("scope"), ScopeValue);
			AddTransform(Result.Audit, FString::Printf(TEXT("mapped setting domain '%s' -> scope '%s'"), *Domain, *ScopeValue));
		}
		LegacyArgs->SetStringField(TEXT("key"), Key);

		const TSharedPtr<FJsonValue> ValueField = Result.ResolvedArguments->TryGetField(TEXT("value"));
		if (ValueField.IsValid())
		{
			LegacyArgs->SetField(TEXT("value"), ValueField);
		}
		bool bConfirm = false;
		if (Result.ResolvedArguments->TryGetBoolField(TEXT("confirm"), bConfirm))
		{
			LegacyArgs->SetBoolField(TEXT("confirm"), bConfirm);
		}
		bool bDryRun = false;
		if (Result.ResolvedArguments->TryGetBoolField(TEXT("dry_run"), bDryRun))
		{
			LegacyArgs->SetBoolField(TEXT("dry_run"), bDryRun);
		}
		AddTransform(Result.Audit, TEXT("family dispatch: setting_apply -> settings_set"));
	}
	else if (Result.CanonicalToolId == TEXT("viewport_camera_control"))
	{
		FString Operation;
		Result.ResolvedArguments->TryGetStringField(TEXT("operation"), Operation);
		Operation = Operation.ToLower();

		if (Operation == TEXT("dolly"))
		{
			LegacyToolId = TEXT("viewport_camera_dolly");
			LegacyArgs = ProjectArguments(Result.ResolvedArguments, {TEXT("dolly_units"), TEXT("fov_delta"), TEXT("mode")}, Result.Audit, TEXT("family dispatch: viewport_camera_control -> viewport_camera_dolly"));
		}
		else if (Operation == TEXT("orbit"))
		{
			LegacyToolId = TEXT("viewport_camera_orbit");
			LegacyArgs = ProjectArguments(Result.ResolvedArguments, {TEXT("pivot"), TEXT("yaw_deg"), TEXT("pitch_deg"), TEXT("radius"), TEXT("relative")}, Result.Audit, TEXT("family dispatch: viewport_camera_control -> viewport_camera_orbit"));
		}
		else if (Operation == TEXT("pan"))
		{
			LegacyToolId = TEXT("viewport_camera_pan");
			LegacyArgs = ProjectArguments(Result.ResolvedArguments, {TEXT("delta"), TEXT("space")}, Result.Audit, TEXT("family dispatch: viewport_camera_control -> viewport_camera_pan"));
		}
		else if (Operation == TEXT("pilot"))
		{
			LegacyToolId = TEXT("viewport_camera_pilot");
			LegacyArgs = ProjectArguments(Result.ResolvedArguments, {TEXT("actor_path"), TEXT("unpilot")}, Result.Audit, TEXT("family dispatch: viewport_camera_control -> viewport_camera_pilot"));
		}
		else if (Operation == TEXT("get_transform"))
		{
			LegacyToolId = TEXT("viewport_camera_get_transform");
			LegacyArgs = ProjectArguments(Result.ResolvedArguments, {TEXT("viewport_index")}, Result.Audit, TEXT("family dispatch: viewport_camera_control -> viewport_camera_get_transform"));
		}
		else if (Operation == TEXT("set_transform"))
		{
			LegacyToolId = TEXT("viewport_camera_set_transform");
			LegacyArgs = ProjectArguments(Result.ResolvedArguments, {TEXT("location"), TEXT("rotation"), TEXT("b_snap")}, Result.Audit, TEXT("family dispatch: viewport_camera_control -> viewport_camera_set_transform"));
		}
		else
		{
			TSharedPtr<FJsonObject> Suggested = MakeShared<FJsonObject>();
			Suggested->SetStringField(TEXT("operation"), TEXT("get_transform"));
			Result.FailureResult = BuildResolverError(
				Result.Audit,
				TEXT("viewport_camera_control requires operation: dolly, orbit, pan, pilot, get_transform, or set_transform."),
				TEXT("viewport_camera_control"),
				Suggested);
			Result.Audit->SetNumberField(TEXT("latency_ms"), (FPlatformTime::Seconds() - ResolveStartSeconds) * 1000.0);
			return Result;
		}
	}
	else if (Result.CanonicalToolId == TEXT("viewport_capture"))
	{
		FString CaptureKind;
		Result.ResolvedArguments->TryGetStringField(TEXT("capture_kind"), CaptureKind);
		CaptureKind = CaptureKind.ToLower();

		if (CaptureKind == TEXT("immediate_png"))
		{
			LegacyToolId = TEXT("viewport_capture_png");
			LegacyArgs = ProjectArguments(Result.ResolvedArguments, {TEXT("filename_slug"), TEXT("resolution_scale"), TEXT("include_ui"), TEXT("delay_frames")}, Result.Audit, TEXT("family dispatch: viewport_capture -> viewport_capture_png"));
		}
		else if (CaptureKind == TEXT("after_frames"))
		{
			LegacyToolId = TEXT("viewport_capture_delayed");
			LegacyArgs = ProjectArguments(Result.ResolvedArguments, {TEXT("filename_slug"), TEXT("resolution_scale"), TEXT("include_ui"), TEXT("delay_frames")}, Result.Audit, TEXT("family dispatch: viewport_capture -> viewport_capture_delayed"));
		}
		else
		{
			TSharedPtr<FJsonObject> Suggested = MakeShared<FJsonObject>();
			Suggested->SetStringField(TEXT("capture_kind"), TEXT("immediate_png"));
			Result.FailureResult = BuildResolverError(
				Result.Audit,
				TEXT("viewport_capture requires capture_kind: immediate_png or after_frames."),
				TEXT("viewport_capture"),
				Suggested);
			Result.Audit->SetNumberField(TEXT("latency_ms"), (FPlatformTime::Seconds() - ResolveStartSeconds) * 1000.0);
			return Result;
		}
	}
	else if (Result.CanonicalToolId == TEXT("viewport_frame"))
	{
		FString Target;
		Result.ResolvedArguments->TryGetStringField(TEXT("target"), Target);
		Target = Target.ToLower();

		if (Target == TEXT("actors"))
		{
			LegacyToolId = TEXT("viewport_frame_actors");
			LegacyArgs = ProjectArguments(Result.ResolvedArguments, {TEXT("actor_paths"), TEXT("margin_scale"), TEXT("pitch_bias"), TEXT("yaw_bias"), TEXT("orthographic")}, Result.Audit, TEXT("family dispatch: viewport_frame -> viewport_frame_actors"));
		}
		else if (Target == TEXT("selection"))
		{
			LegacyToolId = TEXT("viewport_frame_selection");
			LegacyArgs = ProjectArguments(Result.ResolvedArguments, {TEXT("margin_scale")}, Result.Audit, TEXT("family dispatch: viewport_frame -> viewport_frame_selection"));
		}
		else
		{
			TSharedPtr<FJsonObject> Suggested = MakeShared<FJsonObject>();
			Suggested->SetStringField(TEXT("target"), TEXT("selection"));
			Result.FailureResult = BuildResolverError(
				Result.Audit,
				TEXT("viewport_frame requires target: selection or actors."),
				TEXT("viewport_frame"),
				Suggested);
			Result.Audit->SetNumberField(TEXT("latency_ms"), (FPlatformTime::Seconds() - ResolveStartSeconds) * 1000.0);
			return Result;
		}
	}
	else if (Result.CanonicalToolId == TEXT("asset_graph_query"))
	{
		FString Relation;
		Result.ResolvedArguments->TryGetStringField(TEXT("relation"), Relation);
		Relation = Relation.ToLower();
		if (Relation == TEXT("referencers"))
		{
			LegacyToolId = TEXT("asset_find_referencers");
			LegacyArgs = ProjectArguments(Result.ResolvedArguments, {TEXT("object_path")}, Result.Audit, TEXT("family dispatch: asset_graph_query -> asset_find_referencers"));
		}
		else if (Relation == TEXT("dependencies"))
		{
			LegacyToolId = TEXT("asset_get_dependencies");
			LegacyArgs = ProjectArguments(Result.ResolvedArguments, {TEXT("object_path")}, Result.Audit, TEXT("family dispatch: asset_graph_query -> asset_get_dependencies"));
		}
		else
		{
			TSharedPtr<FJsonObject> Suggested = MakeShared<FJsonObject>();
			Suggested->SetStringField(TEXT("relation"), TEXT("referencers"));
			Suggested->SetStringField(TEXT("object_path"), TEXT("<required>"));
			Result.FailureResult = BuildResolverError(
				Result.Audit,
				TEXT("asset_graph_query requires relation: referencers or dependencies."),
				TEXT("asset_graph_query"),
				Suggested);
			Result.Audit->SetNumberField(TEXT("latency_ms"), (FPlatformTime::Seconds() - ResolveStartSeconds) * 1000.0);
			return Result;
		}
	}
	else if (Result.CanonicalToolId == TEXT("material_instance_set_parameter"))
	{
		CanonicalizeAliasKeys(Result.ResolvedArguments, Result.Audit, TEXT("material_path"), {TEXT("path"), TEXT("object_path")});
		// Global CanonicalizeToolArguments maps path -> object_path; composite schema uses material_path only (additionalProperties: false).
		Result.ResolvedArguments->RemoveField(TEXT("path"));
		Result.ResolvedArguments->RemoveField(TEXT("object_path"));
		Result.ResolvedArguments->RemoveField(TEXT("asset_path"));

		FString ValueKind;
		Result.ResolvedArguments->TryGetStringField(TEXT("value_kind"), ValueKind);
		ValueKind = ValueKind.ToLower();

		if (ValueKind == TEXT("scalar"))
		{
			LegacyToolId = TEXT("material_instance_set_scalar_parameter");
			LegacyArgs = ProjectArguments(Result.ResolvedArguments, {TEXT("material_path"), TEXT("parameter_name"), TEXT("value")}, Result.Audit, TEXT("family dispatch: material_instance_set_parameter -> material_instance_set_scalar_parameter"));
		}
		else if (ValueKind == TEXT("vector"))
		{
			LegacyToolId = TEXT("material_instance_set_vector_parameter");
			LegacyArgs = ProjectArguments(Result.ResolvedArguments, {TEXT("material_path"), TEXT("parameter_name"), TEXT("linear_color")}, Result.Audit, TEXT("family dispatch: material_instance_set_parameter -> material_instance_set_vector_parameter"));
		}
		else
		{
			TSharedPtr<FJsonObject> Suggested = MakeShared<FJsonObject>();
			Suggested->SetStringField(TEXT("value_kind"), TEXT("scalar"));
			Suggested->SetStringField(TEXT("material_path"), TEXT("<required>"));
			Suggested->SetStringField(TEXT("parameter_name"), TEXT("<required>"));
			Suggested->SetNumberField(TEXT("value"), 0.0);
			Result.FailureResult = BuildResolverError(
				Result.Audit,
				TEXT("material_instance_set_parameter requires value_kind: scalar or vector."),
				TEXT("material_instance_set_parameter"),
				Suggested);
			Result.Audit->SetNumberField(TEXT("latency_ms"), (FPlatformTime::Seconds() - ResolveStartSeconds) * 1000.0);
			return Result;
		}
	}

	const TSharedPtr<FJsonObject> CanonicalDef = Catalog.FindToolDefinition(Result.CanonicalToolId);
	if (CanonicalDef.IsValid())
	{
		FUnrealAiToolInvocationResult Failure;
		if (!ValidateRequiredFields(Result.CanonicalToolId, CanonicalDef, Result.ResolvedArguments, Result.Audit, Result.CanonicalToolId, Failure))
		{
			Result.FailureResult = Failure;
			Result.Audit->SetNumberField(TEXT("latency_ms"), (FPlatformTime::Seconds() - ResolveStartSeconds) * 1000.0);
			return Result;
		}
		if (!ValidateSchemaShape(Result.CanonicalToolId, CanonicalDef, Result.ResolvedArguments, Result.Audit, Result.CanonicalToolId, Failure))
		{
			Result.FailureResult = Failure;
			Result.Audit->SetNumberField(TEXT("latency_ms"), (FPlatformTime::Seconds() - ResolveStartSeconds) * 1000.0);
			return Result;
		}
	}

	Result.LegacyToolId = LegacyToolId;
	Result.LegacyToolDefinition = Catalog.FindToolDefinition(Result.LegacyToolId);
	if (Result.LegacyToolDefinition.IsValid())
	{
		FUnrealAiToolInvocationResult Failure;
		const FString SuggestedToolId = Result.CanonicalToolId.IsEmpty() ? Result.LegacyToolId : Result.CanonicalToolId;
		if (!ValidateRequiredFields(Result.LegacyToolId, Result.LegacyToolDefinition, LegacyArgs, Result.Audit, SuggestedToolId, Failure))
		{
			Result.FailureResult = Failure;
			Result.Audit->SetNumberField(TEXT("latency_ms"), (FPlatformTime::Seconds() - ResolveStartSeconds) * 1000.0);
			return Result;
		}
		if (!ValidateSchemaShape(Result.LegacyToolId, Result.LegacyToolDefinition, LegacyArgs, Result.Audit, SuggestedToolId, Failure))
		{
			Result.FailureResult = Failure;
			Result.Audit->SetNumberField(TEXT("latency_ms"), (FPlatformTime::Seconds() - ResolveStartSeconds) * 1000.0);
			return Result;
		}
	}

	Result.ResolvedArguments = LegacyArgs.IsValid() ? LegacyArgs : MakeShared<FJsonObject>();
	Result.bResolved = true;
	Result.Audit->SetStringField(TEXT("canonical_tool_id"), Result.CanonicalToolId);
	Result.Audit->SetStringField(TEXT("legacy_tool_id"), Result.LegacyToolId);
	Result.Audit->SetNumberField(TEXT("latency_ms"), (FPlatformTime::Seconds() - ResolveStartSeconds) * 1000.0);
	UE_LOG(
		LogTemp,
		Verbose,
		TEXT("UnrealAiToolResolver: requested=%s canonical=%s legacy=%s latency_ms=%.2f"),
		*Result.RequestedToolId,
		*Result.CanonicalToolId,
		*Result.LegacyToolId,
		Result.Audit->GetNumberField(TEXT("latency_ms")));
	return Result;
}

void FUnrealAiToolResolver::AttachAuditToResult(const TSharedPtr<FJsonObject>& Audit, FUnrealAiToolInvocationResult& InOutResult)
{
	if (!Audit.IsValid() || InOutResult.ContentForModel.IsEmpty())
	{
		return;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InOutResult.ContentForModel);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}

	Root->SetObjectField(TEXT("resolver_audit"), Audit);
	InOutResult.ContentForModel = UnrealAiToolJson::SerializeObject(Root);
}
