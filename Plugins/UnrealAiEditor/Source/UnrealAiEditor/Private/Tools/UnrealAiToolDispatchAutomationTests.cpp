#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Tools/UnrealAiToolDispatch.h"
#include "Tools/UnrealAiAssetFactoryResolver.h"
#include "Tools/UnrealAiToolJson.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"

/**
 * Contract tests for tool JSON helpers (no Editor / world required).
 * Catches regressions in the shared serialization path used by every tool.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiToolJsonHelpersTest,
	"UnrealAiEditor.Tools.JsonHelpers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiToolJsonHelpersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetBoolField(TEXT("ok"), true);
	Payload->SetStringField(TEXT("hello"), TEXT("world"));

	const FUnrealAiToolInvocationResult OkRes = UnrealAiToolJson::Ok(Payload);
	TestTrue(TEXT("Ok.bOk"), OkRes.bOk);

	TSharedPtr<FJsonObject> ParsedOk;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OkRes.ContentForModel);
		TestTrue(TEXT("Ok JSON parse"), FJsonSerializer::Deserialize(Reader, ParsedOk) && ParsedOk.IsValid());
	}
	TestTrue(TEXT("Ok payload.ok"), ParsedOk->GetBoolField(TEXT("ok")));
	TestEqual(TEXT("Ok payload.hello"), ParsedOk->GetStringField(TEXT("hello")), FString(TEXT("world")));

	const FUnrealAiToolInvocationResult ErrRes = UnrealAiToolJson::Error(TEXT("boom"));
	TestFalse(TEXT("Err.bOk"), ErrRes.bOk);
	TestEqual(TEXT("Err.ErrorMessage"), ErrRes.ErrorMessage, FString(TEXT("boom")));

	TSharedPtr<FJsonObject> ParsedErr;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ErrRes.ContentForModel);
		TestTrue(TEXT("Err JSON parse"), FJsonSerializer::Deserialize(Reader, ParsedErr) && ParsedErr.IsValid());
	}
	TestFalse(TEXT("Err payload.ok"), ParsedErr->GetBoolField(TEXT("ok")));
	TestEqual(TEXT("Err payload.error"), ParsedErr->GetStringField(TEXT("error")), FString(TEXT("boom")));

	return true;
}

/**
 * Smoke test for the router + real Editor APIs (GEditor selection).
 * Run from Session Frontend (Editor) — validates the UE5 "connection point" for dispatch.
 * This does not replace per-tool integration tests; it catches "nothing is wired" failures early.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiToolDispatchEditorSmokeTest,
	"UnrealAiEditor.Tools.DispatchEditorSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUnrealAiToolDispatchEditorSmokeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Runs on game thread (tool dispatch requirement)"), IsInGameThread());

	// Unknown tool id -> structured not_implemented (router default path)
	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("___unreal_ai_automation_unknown_tool___"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());

		TestFalse(TEXT("Unknown tool: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("Unknown tool: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());

		FString Status;
		TestTrue(TEXT("Unknown tool: status field"), O->TryGetStringField(TEXT("status"), Status));
		TestEqual(TEXT("Unknown tool: status value"), Status, FString(TEXT("not_implemented")));
	}

	// Implemented tool: editor_get_selection -> GEditor / USelection (may be empty)
	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("editor_get_selection"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());

		TestTrue(TEXT("editor_get_selection: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("editor_get_selection: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());

		TestTrue(TEXT("editor_get_selection: ok field"), O->GetBoolField(TEXT("ok")));
		TestTrue(TEXT("editor_get_selection: has count"), O->HasField(TEXT("count")));
		TestTrue(TEXT("editor_get_selection: has actor_paths"), O->HasField(TEXT("actor_paths")));
		TestTrue(TEXT("editor_get_selection: has labels"), O->HasField(TEXT("labels")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiBlueprintApplyIrContractTest,
	"UnrealAiEditor.Tools.BlueprintApplyIrContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiBlueprintApplyIrContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Runs on game thread (tool dispatch requirement)"), IsInGameThread());

	// Missing required fields should return structured invalid_ir.
	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_apply_ir"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_apply_ir missing required: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("blueprint_apply_ir missing required: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestFalse(TEXT("blueprint_apply_ir missing required: ok field"), O->GetBoolField(TEXT("ok")));

		FString Status;
		TestTrue(TEXT("blueprint_apply_ir missing required: status"), O->TryGetStringField(TEXT("status"), Status));
		TestEqual(TEXT("blueprint_apply_ir missing required: status value"), Status, FString(TEXT("invalid_ir")));
		TestTrue(TEXT("blueprint_apply_ir missing required: errors present"), O->HasTypedField<EJson::Array>(TEXT("errors")));
	}

	// Deprecated op normalization: add_movement_input -> call_function (+ class_path/function_name)
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/__does_not_exist__/BP_ResolverTest.BP_ResolverTest"));

		TArray<TSharedPtr<FJsonValue>> Nodes;
		{
			TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
			N->SetStringField(TEXT("node_id"), TEXT("n1"));
			N->SetStringField(TEXT("op"), TEXT("add_movement_input"));
			Nodes.Add(MakeShareable(new FJsonValueObject(N.ToSharedRef())));
		}
		Args->SetArrayField(TEXT("nodes"), Nodes);

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_apply_ir"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("deprecated op normalization should not succeed"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("blueprint_apply_ir deprecated op: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());

		TestTrue(TEXT("blueprint_apply_ir deprecated op: normalization_applied present"), O->HasField(TEXT("normalization_applied")));
		TestTrue(TEXT("blueprint_apply_ir deprecated op: normalization_applied true"), O->GetBoolField(TEXT("normalization_applied")));
		TestTrue(TEXT("blueprint_apply_ir deprecated op: has deprecated_fields_seen"), O->HasField(TEXT("deprecated_fields_seen")));

		const TArray<TSharedPtr<FJsonValue>>* DepArr = nullptr;
		TestTrue(TEXT("deprecated_fields_seen parseable"), O->TryGetArrayField(TEXT("deprecated_fields_seen"), DepArr) && DepArr);

		bool bFoundDeprecated = false;
		if (DepArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *DepArr)
			{
				if (V.IsValid() && V->AsString().Contains(TEXT("op:add_movement_input")))
				{
					bFoundDeprecated = true;
					break;
				}
			}
		}
		TestTrue(TEXT("deprecated_fields_seen contains op:add_movement_input"), bFoundDeprecated);
	}

	// custom_event name normalization: implicit name from node_id (required by UK2Node_CustomEvent)
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/__does_not_exist__/BP_CustomEventName.BP_CustomEventName"));

		TArray<TSharedPtr<FJsonValue>> Nodes;
		{
			// event nodes are fine without extra metadata; they only exist to make the apply shape realistic.
			TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
			N->SetStringField(TEXT("node_id"), TEXT("begin_play"));
			N->SetStringField(TEXT("op"), TEXT("event_begin_play"));
			Nodes.Add(MakeShareable(new FJsonValueObject(N.ToSharedRef())));
		}
		{
			// LLMs commonly omit `name` and emit only `node_id` + `op:custom_event`.
			TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
			N->SetStringField(TEXT("node_id"), TEXT("coin_hover"));
			N->SetStringField(TEXT("op"), TEXT("custom_event"));
			Nodes.Add(MakeShareable(new FJsonValueObject(N.ToSharedRef())));
		}
		Args->SetArrayField(TEXT("nodes"), Nodes);

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_apply_ir"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("custom_event name normalization should still fail due to missing blueprint"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("blueprint_apply_ir custom_event name normalization: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());

		TestTrue(TEXT("custom_event name normalization: has normalization_applied"), O->HasField(TEXT("normalization_applied")));
		TestTrue(TEXT("custom_event name normalization: normalization_applied true"), O->GetBoolField(TEXT("normalization_applied")));

		TestTrue(TEXT("custom_event name normalization: has deprecated_fields_seen"), O->HasField(TEXT("deprecated_fields_seen")));
		const TArray<TSharedPtr<FJsonValue>>* DepArr = nullptr;
		TestTrue(TEXT("custom_event name normalization: deprecated_fields_seen parseable"), O->TryGetArrayField(TEXT("deprecated_fields_seen"), DepArr) && DepArr);

		bool bFoundMapping = false;
		if (DepArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *DepArr)
			{
				if (V.IsValid() && V->AsString().Contains(TEXT("node_id_for_custom_event_name")))
				{
					bFoundMapping = true;
					break;
				}
			}
		}
		TestTrue(TEXT("deprecated_fields_seen contains mapping node_id_for_custom_event_name"), bFoundMapping);
	}

	// Alias repair + suggested_correct_call: nodeId repaired -> invalid_ir still returned (missing op)
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/__does_not_exist__/BP_ResolverTest2.BP_ResolverTest2"));

		TArray<TSharedPtr<FJsonValue>> Nodes;
		{
			TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
			N->SetStringField(TEXT("nodeId"), TEXT("n1"));
			// op intentionally omitted to trigger invalid_ir after normalization
			Nodes.Add(MakeShareable(new FJsonValueObject(N.ToSharedRef())));
		}
		Args->SetArrayField(TEXT("nodes"), Nodes);

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_apply_ir"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());

		TestFalse(TEXT("alias repair but still invalid_ir"), R.bOk);
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("blueprint_apply_ir alias repair: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());

		TestEqual(TEXT("blueprint_apply_ir alias repair: status"), O->GetStringField(TEXT("status")), FString(TEXT("invalid_ir")));
		TestTrue(TEXT("blueprint_apply_ir alias repair: has suggested_correct_call"), O->HasField(TEXT("suggested_correct_call")));

		const TSharedPtr<FJsonObject>* SuggestedObj = nullptr;
		TestTrue(
			TEXT("suggested_correct_call parseable"),
			O->TryGetObjectField(TEXT("suggested_correct_call"), SuggestedObj) && SuggestedObj && SuggestedObj->IsValid());
		TestEqual(TEXT("suggested_correct_call.tool_id"), SuggestedObj->Get()->GetStringField(TEXT("tool_id")), FString(TEXT("blueprint_apply_ir")));
	}

	// Link pin defaulting: from/to without pin are normalized (notes emitted) before asset loading fails.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/__does_not_exist__/BP_ResolverTest3.BP_ResolverTest3"));

		TArray<TSharedPtr<FJsonValue>> Nodes;
		{
			TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
			N->SetStringField(TEXT("node_id"), TEXT("n1"));
			N->SetStringField(TEXT("op"), TEXT("event_begin_play"));
			Nodes.Add(MakeShareable(new FJsonValueObject(N.ToSharedRef())));
		}
		Args->SetArrayField(TEXT("nodes"), Nodes);

		TArray<TSharedPtr<FJsonValue>> Links;
		{
			TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
			L->SetStringField(TEXT("from"), TEXT("n1"));
			L->SetStringField(TEXT("to"), TEXT("n1"));
			Links.Add(MakeShareable(new FJsonValueObject(L.ToSharedRef())));
		}
		Args->SetArrayField(TEXT("links"), Links);

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_apply_ir"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());

		TestFalse(TEXT("link defaulting should still fail due to missing blueprint"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("blueprint_apply_ir link defaulting: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());

		TestTrue(TEXT("blueprint_apply_ir link defaulting: has normalization_notes"), O->HasField(TEXT("normalization_notes")));
		const TArray<TSharedPtr<FJsonValue>>* NotesArr = nullptr;
		TestTrue(TEXT("normalization_notes parseable"), O->TryGetArrayField(TEXT("normalization_notes"), NotesArr) && NotesArr);

		bool bFoundFromNote = false;
		bool bFoundToNote = false;
		if (NotesArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *NotesArr)
			{
				if (V.IsValid() && V->AsString().Contains(TEXT("Normalized link.from without pin to .Then")))
				{
					bFoundFromNote = true;
				}
				if (V.IsValid() && V->AsString().Contains(TEXT("Normalized link.to without pin to .Execute")))
				{
					bFoundToNote = true;
				}
			}
		}
		TestTrue(TEXT("normalization_notes includes from-pin default"), bFoundFromNote);
		TestTrue(TEXT("normalization_notes includes to-pin default"), bFoundToNote);
	}

	// Unsupported op should fail deterministically with apply_failed.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/__does_not_exist__.__does_not_exist__"));
		TArray<TSharedPtr<FJsonValue>> Nodes;
		{
			TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
			N->SetStringField(TEXT("node_id"), TEXT("n1"));
			N->SetStringField(TEXT("op"), TEXT("totally_unknown_op"));
			Nodes.Add(MakeShareable(new FJsonValueObject(N.ToSharedRef())));
		}
		Args->SetArrayField(TEXT("nodes"), Nodes);

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_apply_ir"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_apply_ir unsupported op: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("blueprint_apply_ir unsupported op: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestFalse(TEXT("blueprint_apply_ir unsupported op: ok field"), O->GetBoolField(TEXT("ok")));
		TestTrue(TEXT("blueprint_apply_ir unsupported op: has status"), O->HasTypedField<EJson::String>(TEXT("status")));
	}

	// Invalid merge_policy should fail parse (invalid_ir) before asset load.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/__unreal_ai_missing_bp__.__unreal_ai_missing_bp__"));
		Args->SetStringField(TEXT("merge_policy"), TEXT("not_a_real_policy"));
		TArray<TSharedPtr<FJsonValue>> Nodes;
		{
			TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
			N->SetStringField(TEXT("node_id"), TEXT("n1"));
			N->SetStringField(TEXT("op"), TEXT("event_begin_play"));
			Nodes.Add(MakeShareable(new FJsonValueObject(N.ToSharedRef())));
		}
		Args->SetArrayField(TEXT("nodes"), Nodes);

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_apply_ir"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_apply_ir invalid merge_policy: bOk"), R.bOk);
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("blueprint_apply_ir invalid merge_policy: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		FString Status;
		TestTrue(TEXT("blueprint_apply_ir invalid merge_policy: status"), O->TryGetStringField(TEXT("status"), Status));
		TestEqual(TEXT("blueprint_apply_ir invalid merge_policy: status value"), Status, FString(TEXT("invalid_ir")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiGenericAssetToolsContractTest,
	"UnrealAiEditor.Tools.GenericAssetToolsContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiGenericAssetToolsContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Runs on game thread"), IsInGameThread());

	// Missing required string fields should include suggested_correct_call for deterministic repair.
	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("project_file_read_text"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("project_file_read_text missing relative_path: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("project_file_read_text missing relative_path: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestTrue(TEXT("project_file_read_text missing relative_path: has suggested_correct_call"), O->HasField(TEXT("suggested_correct_call")));
		const TSharedPtr<FJsonObject>* Suggested = nullptr;
		TestTrue(TEXT("project_file_read_text: suggested_correct_call object"), O->TryGetObjectField(TEXT("suggested_correct_call"), Suggested) && Suggested && Suggested->IsValid());
		const TSharedPtr<FJsonObject>* InnerArgs = nullptr;
		TestTrue(TEXT("project_file_read_text: suggested arguments"), (*Suggested)->TryGetObjectField(TEXT("arguments"), InnerArgs) && InnerArgs && InnerArgs->IsValid());
		FString RelPath;
		TestTrue(TEXT("project_file_read_text: suggested relative_path"), (*InnerArgs)->TryGetStringField(TEXT("relative_path"), RelPath));
		const FString ExpectedUProj = FPaths::GetCleanFilename(FPaths::GetProjectFilePath());
		if (!ExpectedUProj.IsEmpty())
		{
			TestEqual(TEXT("project_file_read_text: suggested path matches project .uproject"), RelPath, ExpectedUProj);
		}
		else
		{
			TestTrue(TEXT("project_file_read_text: suggested path non-empty fallback"), !RelPath.IsEmpty());
		}
	}

	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("asset_export_properties"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("asset_export_properties missing object_path: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("asset_export_properties missing object_path: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestTrue(TEXT("asset_export_properties missing object_path: has suggested_correct_call"), O->HasField(TEXT("suggested_correct_call")));
	}

	// asset_delete: missing object_paths should return suggested_correct_call (deterministic repair).
	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("asset_delete"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("asset_delete missing object_paths: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("asset_delete missing object_paths: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestTrue(TEXT("asset_delete missing object_paths: has suggested_correct_call"), O->HasField(TEXT("suggested_correct_call")));
	}

	// asset_delete: confirm:false should also return suggested_correct_call.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Paths;
		Paths.Add(MakeShared<FJsonValueString>(TEXT("/Game/__unreal_ai_missing_asset.__unreal_ai_missing_asset__")));
		Args->SetArrayField(TEXT("object_paths"), Paths);
		Args->SetBoolField(TEXT("confirm"), false);

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("asset_delete"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("asset_delete confirm:false: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("asset_delete confirm:false: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestTrue(TEXT("asset_delete confirm:false: has suggested_correct_call"), O->HasField(TEXT("suggested_correct_call")));
	}

	// asset_save_packages: non-resolvable object_path should fail with suggested_correct_call (normalization + resolve failure).
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("object_path"), TEXT("/Game/__unreal_ai_missing_asset.__unreal_ai_missing_asset__"));

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("asset_save_packages"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("asset_save_packages missing resolvable package from object_path: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("asset_save_packages object_path: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestTrue(TEXT("asset_save_packages object_path: has suggested_correct_call"), O->HasField(TEXT("suggested_correct_call")));
	}

	// asset_save_packages: non-resolvable folder path should fail with suggested_correct_call.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> PackagePaths;
		PackagePaths.Add(MakeShared<FJsonValueString>(TEXT("/Game/__unreal_ai_missing_folder__/")));
		Args->SetArrayField(TEXT("package_paths"), PackagePaths);

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("asset_save_packages"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("asset_save_packages folder path: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("asset_save_packages folder path: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestTrue(TEXT("asset_save_packages folder path: has suggested_correct_call"), O->HasField(TEXT("suggested_correct_call")));
	}

	// source_search_symbol: should consider files (fallback roots) even when project roots are mis-resolved.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("query"), TEXT("UnrealAiDispatchTool"));
		Args->SetStringField(TEXT("glob"), TEXT("*.cpp"));
		Args->SetBoolField(TEXT("include_line_matches"), false);
		Args->SetNumberField(TEXT("max_hits"), 5.0);

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("source_search_symbol"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestTrue(TEXT("source_search_symbol fallback roots: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("source_search_symbol fallback roots: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestTrue(TEXT("source_search_symbol fallback roots: has files_considered"), O->HasField(TEXT("files_considered")));
		// Value is a number field (double in JSON); accept > 0 as signal that roots were found.
		const double FilesConsidered = O->GetNumberField(TEXT("files_considered"));
		TestTrue(TEXT("source_search_symbol fallback roots: files_considered > 0"), FilesConsidered > 0.0);
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("object_path"), TEXT("/Game/__unreal_ai_missing_asset__.__unreal_ai_missing_asset__"));
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("asset_export_properties"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("asset_export_properties missing asset: bOk"), R.bOk);
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("object_path"), TEXT("/Game/Test.Test"));
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("asset_apply_properties"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("asset_apply_properties missing properties field: bOk"), R.bOk);
	}

	// Factory resolver: should resolve common asset classes without requiring explicit factory_class.
	{
		const FUnrealAiAssetFactoryResolver::FResolveResult R = FUnrealAiAssetFactoryResolver::Resolve(
			UBlueprint::StaticClass(),
			FString());
		TestTrue(TEXT("factory_resolver blueprint: has factory"), R.Factory != nullptr);
		TestFalse(TEXT("factory_resolver blueprint: has class path"), R.FactoryClassPath.IsEmpty());
	}
	{
		const FUnrealAiAssetFactoryResolver::FResolveResult R = FUnrealAiAssetFactoryResolver::Resolve(
			UStaticMesh::StaticClass(),
			FString());
		TestTrue(TEXT("factory_resolver staticmesh: has factory"), R.Factory != nullptr);
		TestFalse(TEXT("factory_resolver staticmesh: has class path"), R.FactoryClassPath.IsEmpty());
	}

	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_export_ir"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_export_ir missing blueprint_path: bOk"), R.bOk);
	}

	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_format_graph"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_format_graph missing blueprint_path: bOk"), R.bOk);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiTodoPlanContractTest,
	"UnrealAiEditor.Tools.TodoPlanContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiTodoPlanContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Runs on game thread"), IsInGameThread());

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("title"), TEXT("Plan title"));
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("agent_emit_todo_plan"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("agent_emit_todo_plan missing steps: bOk"), R.bOk);
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("agent_emit_todo_plan missing steps: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestTrue(TEXT("agent_emit_todo_plan missing steps: has suggestion"), O->HasField(TEXT("suggested_correct_call")));
	}

	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("agent_emit_todo_plan"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("agent_emit_todo_plan missing title: bOk"), R.bOk);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
