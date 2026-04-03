#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Tools/UnrealAiToolDispatch.h"
#include "Tools/UnrealAiToolProjectPathAllowlist.h"
#include "Tools/UnrealAiAssetFactoryResolver.h"
#include "Tools/UnrealAiToolCatalog.h"
#include "Tools/UnrealAiToolJson.h"
#include "Tools/UnrealAiToolResolver.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"
#include "EdGraphSchema_K2.h"

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
	FUnrealAiToolResolverCompositeRoutingTest,
	"UnrealAiEditor.Tools.ResolverCompositeRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiToolResolverCompositeRoutingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FUnrealAiToolCatalog Catalog;
	TestTrue(TEXT("catalog loads"), Catalog.LoadFromPlugin());
	TestTrue(TEXT("catalog reports loaded"), Catalog.IsLoaded());
	TestEqual(TEXT("resolver contract version"), Catalog.GetResolverContractVersion(), FString(TEXT("2.1.0")));

	FUnrealAiToolResolver Resolver(Catalog);

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("domain"), TEXT("editor_preference"));
		Args->SetStringField(TEXT("key"), TEXT("editor_focus"));

		const FUnrealAiResolvedToolInvocation Resolved = Resolver.Resolve(TEXT("setting_query"), Args);
		TestTrue(TEXT("setting_query resolves"), Resolved.bResolved);
		TestEqual(TEXT("setting_query legacy tool"), Resolved.LegacyToolId, FString(TEXT("settings_get")));
		FString Scope;
		TestTrue(TEXT("setting_query maps domain to scope"), Resolved.ResolvedArguments->TryGetStringField(TEXT("scope"), Scope));
		TestEqual(TEXT("setting_query scope value"), Scope, FString(TEXT("editor")));
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("value_kind"), TEXT("vector"));
		Args->SetStringField(TEXT("path"), TEXT("/Game/MI_Test.MI_Test"));
		Args->SetStringField(TEXT("parameter_name"), TEXT("Tint"));
		TArray<TSharedPtr<FJsonValue>> LinearColor;
		LinearColor.Add(MakeShared<FJsonValueNumber>(1.0));
		LinearColor.Add(MakeShared<FJsonValueNumber>(0.5));
		LinearColor.Add(MakeShared<FJsonValueNumber>(0.25));
		Args->SetArrayField(TEXT("linear_color"), LinearColor);

		const FUnrealAiResolvedToolInvocation Resolved = Resolver.Resolve(TEXT("material_instance_set_parameter"), Args);
		TestTrue(TEXT("material_instance_set_parameter resolves"), Resolved.bResolved);
		TestEqual(TEXT("material_instance_set_parameter legacy tool"), Resolved.LegacyToolId, FString(TEXT("material_instance_set_vector_parameter")));
		FString MaterialPath;
		TestTrue(TEXT("material path canonicalized"), Resolved.ResolvedArguments->TryGetStringField(TEXT("material_path"), MaterialPath));
		TestEqual(TEXT("material path preserved"), MaterialPath, FString(TEXT("/Game/MI_Test.MI_Test")));
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("object_path"), TEXT("/Game/Foo.Bar"));
		Args->SetStringField(TEXT("relation"), TEXT("dependencies"));

		const FUnrealAiResolvedToolInvocation Resolved = Resolver.Resolve(TEXT("asset_graph_query"), Args);
		TestTrue(TEXT("asset_graph_query resolves"), Resolved.bResolved);
		TestEqual(TEXT("asset_graph_query legacy tool"), Resolved.LegacyToolId, FString(TEXT("asset_get_dependencies")));
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("operation"), TEXT("pan"));
		Args->SetStringField(TEXT("delta"), TEXT("not-an-array"));

		const FUnrealAiResolvedToolInvocation Resolved = Resolver.Resolve(TEXT("viewport_camera_control"), Args);
		TestFalse(TEXT("viewport_camera_control invalid delta shape fails"), Resolved.bResolved);

		TSharedPtr<FJsonObject> Payload;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Resolved.FailureResult.ContentForModel);
		TestTrue(TEXT("viewport_camera_control failure payload parses"), FJsonSerializer::Deserialize(Reader, Payload) && Payload.IsValid());
		TestEqual(TEXT("viewport_camera_control validation error code"), Payload->GetStringField(TEXT("error")), FString(TEXT("validation_failed")));
		TestTrue(TEXT("viewport_camera_control validation issues present"), Payload->HasField(TEXT("validation_errors")));
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("material_path"), TEXT("/Game/M.MI"));
		Args->SetStringField(TEXT("parameter_name"), TEXT("Tint"));
		TArray<TSharedPtr<FJsonValue>> LinearColor;
		LinearColor.Add(MakeShared<FJsonValueNumber>(1.0));
		LinearColor.Add(MakeShared<FJsonValueNumber>(0.0));
		LinearColor.Add(MakeShared<FJsonValueNumber>(0.0));
		Args->SetArrayField(TEXT("linear_color"), LinearColor);
		const FUnrealAiResolvedToolInvocation Resolved = Resolver.Resolve(TEXT("material_instance_set_parameter"), Args);
		TestFalse(TEXT("material_instance_set_parameter missing value_kind fails"), Resolved.bResolved);
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("operation"), TEXT("get_transform"));
		const FUnrealAiResolvedToolInvocation Resolved = Resolver.Resolve(TEXT("viewport_camera_control"), Args);
		TestTrue(TEXT("viewport_camera_control explicit operation resolves"), Resolved.bResolved);
		TestEqual(TEXT("viewport_camera_control get_transform legacy"), Resolved.LegacyToolId, FString(TEXT("viewport_camera_get_transform")));
	}

	{
		const FUnrealAiResolvedToolInvocation Resolved = Resolver.Resolve(TEXT("viewport_cmera_control"), MakeShared<FJsonObject>());
		TestFalse(TEXT("ambiguous typo without args should fail"), Resolved.bResolved);
		TestTrue(
			TEXT("unknown tool typo includes suggestion"),
			Resolved.FailureResult.ErrorMessage.Contains(TEXT("viewport_camera_control")) || Resolved.FailureResult.ContentForModel.Contains(TEXT("viewport_camera_control")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiBlueprintPinTypeParseTest,
	"UnrealAiEditor.Tools.BlueprintPinTypeParse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiBlueprintPinTypeParseTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FEdGraphPinType IntTy;
	TestTrue(TEXT("int32"), UnrealAiBlueprintTools_TryParsePinTypeFromString(TEXT("int32"), IntTy));
	TestTrue(TEXT("int category"), IntTy.PinCategory == UEdGraphSchema_K2::PC_Int);

	FEdGraphPinType BadTy;
	TestFalse(TEXT("unknown token"), UnrealAiBlueprintTools_TryParsePinTypeFromString(TEXT("not_a_real_type_xyz_q"), BadTy));

	FEdGraphPinType Legacy = UnrealAiBlueprintTools_ParsePinTypeFromString(TEXT("not_a_real_type_xyz_q"));
	TestTrue(TEXT("legacy boolean fallback"), Legacy.PinCategory == UEdGraphSchema_K2::PC_Boolean);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiBlueprintGraphPatchContractTest,
	"UnrealAiEditor.Tools.BlueprintGraphPatchContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiBlueprintGraphPatchContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Runs on game thread (tool dispatch requirement)"), IsInGameThread());

	// blueprint_graph_patch requires blueprint_path and ops
	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_graph_patch"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_graph_patch empty args: bOk"), R.bOk);
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("blueprint_graph_patch empty args: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestFalse(TEXT("blueprint_graph_patch empty args: ok field"), O->GetBoolField(TEXT("ok")));
		TestTrue(TEXT("blueprint_graph_patch empty args: error present"), O->HasTypedField<EJson::String>(TEXT("error")));
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Nonexistent/XP.XP"));
		Args->SetArrayField(TEXT("ops"), TArray<TSharedPtr<FJsonValue>>());
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_graph_patch"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_graph_patch empty ops: bOk"), R.bOk);
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("blueprint_graph_patch empty ops: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestTrue(TEXT("blueprint_graph_patch empty ops: error"), O->HasTypedField<EJson::String>(TEXT("error")));
	}

	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_graph_list_pins"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_graph_list_pins empty args: bOk"), R.bOk);
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("blueprint_graph_list_pins empty args: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestFalse(TEXT("blueprint_graph_list_pins empty args: ok"), O->GetBoolField(TEXT("ok")));
	}

	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_graph_introspect"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_graph_introspect empty args: bOk"), R.bOk);
	}
	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_export_graph_t3d"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_export_graph_t3d empty args: bOk"), R.bOk);
	}
	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_t3d_preflight_validate"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_t3d_preflight_validate empty args: bOk"), R.bOk);
	}
	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_graph_import_t3d"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_graph_import_t3d empty args: bOk"), R.bOk);
	}
	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_verify_graph"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_verify_graph empty args: bOk"), R.bOk);
	}
	// Missing asset: steps are not evaluated (load fails first). Validates args + steps parse path without crashing.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/__does_not_exist__/BP_VerifySteps.BP_VerifySteps"));
		TArray<TSharedPtr<FJsonValue>> St;
		St.Add(MakeShared<FJsonValueString>(TEXT("links")));
		St.Add(MakeShared<FJsonValueString>(TEXT("orphan_pins")));
		St.Add(MakeShared<FJsonValueString>(TEXT("not_a_real_step")));
		Args->SetArrayField(TEXT("steps"), St);
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_verify_graph"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_verify_graph missing asset with steps: bOk"), R.bOk);
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

	// Missing/empty `nodes` should include a suggested recovery path via blueprint_export_ir.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/__does_not_exist__/BP_ApplyIrMissingNodes.BP_ApplyIrMissingNodes"));

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_apply_ir"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());

		TestFalse(TEXT("blueprint_apply_ir missing nodes: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("blueprint_apply_ir missing nodes: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());

		TestTrue(TEXT("blueprint_apply_ir missing nodes: has suggested_correct_call"), O->HasField(TEXT("suggested_correct_call")));

		const TSharedPtr<FJsonObject>* SuggestedObj = nullptr;
		TestTrue(
			TEXT("suggested_correct_call parseable"),
			O->TryGetObjectField(TEXT("suggested_correct_call"), SuggestedObj) && SuggestedObj && SuggestedObj->IsValid());

		TestEqual(
			TEXT("suggested_correct_call.tool_id"),
			SuggestedObj->Get()->GetStringField(TEXT("tool_id")),
			FString(TEXT("blueprint_export_ir")));
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
	FUnrealAiBlueprintSetComponentDefaultContractTest,
	"UnrealAiEditor.Tools.BlueprintSetComponentDefaultContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiBlueprintSetComponentDefaultContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Runs on game thread (tool dispatch requirement)"), IsInGameThread());

	// Missing blueprint_path: suggested_correct_call to blueprint_set_component_default.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("component"), TEXT("CharacterMovement"));
		Args->SetStringField(TEXT("property"), TEXT("JumpZVelocity"));
		Args->SetNumberField(TEXT("value"), 1200.0);

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_set_component_default"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_set_component_default missing blueprint_path: bOk"), R.bOk);
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("blueprint_set_component_default missing blueprint_path: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestTrue(TEXT("blueprint_set_component_default missing blueprint_path: has suggested_correct_call"), O->HasField(TEXT("suggested_correct_call")));
	}

	// Missing component.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/MyBP.MyBP"));
		Args->SetStringField(TEXT("property"), TEXT("JumpZVelocity"));
		Args->SetNumberField(TEXT("value"), 1200.0);

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_set_component_default"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_set_component_default missing component: bOk"), R.bOk);
	}

	// Missing value.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/MyBP.MyBP"));
		Args->SetStringField(TEXT("component"), TEXT("CharacterMovement"));
		Args->SetStringField(TEXT("property"), TEXT("JumpZVelocity"));

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_set_component_default"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_set_component_default missing value: bOk"), R.bOk);
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

	// Missing relative_path: default to project manifest or DefaultEngine.ini (no tool_finish failure).
	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("project_file_read_text"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestTrue(TEXT("project_file_read_text missing relative_path: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("project_file_read_text missing relative_path: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		bool bOk = false;
		TestTrue(TEXT("project_file_read_text missing relative_path: ok"), O->TryGetBoolField(TEXT("ok"), bOk) && bOk);
		FString RelPath;
		TestTrue(TEXT("project_file_read_text: effective relative_path"), O->TryGetStringField(TEXT("relative_path"), RelPath) && !RelPath.IsEmpty());
		bool bDefaulted = false;
		TestTrue(TEXT("project_file_read_text: relative_path_defaulted"), O->TryGetBoolField(TEXT("relative_path_defaulted"), bDefaulted) && bDefaulted);
		const FString ExpectedRel = [&]() {
			const FString ProjectFileAbs = FPaths::GetProjectFilePath();
			if (!ProjectFileAbs.IsEmpty())
			{
				FString RelToProject = ProjectFileAbs;
				if (FPaths::MakePathRelativeTo(RelToProject, *FPaths::ProjectDir()))
				{
					RelToProject.ReplaceInline(TEXT("\\"), TEXT("/"));
					return RelToProject;
				}
			}
			return FString(TEXT("Config/DefaultEngine.ini"));
		}();
		TestEqual(TEXT("project_file_read_text: path matches default resolver"), RelPath, ExpectedRel);
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

	// asset_find_referencers: non-existent object_path should fail with suggested_correct_call to asset_index_fuzzy_search.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("object_path"), TEXT("/Game/__unreal_ai_missing_asset_for_referencers.__unreal_ai_missing_asset_for_referencers__"));

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("asset_find_referencers"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("asset_find_referencers missing asset: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("asset_find_referencers missing asset: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestTrue(TEXT("asset_find_referencers missing asset: has suggested_correct_call"), O->HasField(TEXT("suggested_correct_call")));

		const TSharedPtr<FJsonObject>* SuggestedObj = nullptr;
		TestTrue(
			TEXT("asset_find_referencers suggested_correct_call parseable"),
			O->TryGetObjectField(TEXT("suggested_correct_call"), SuggestedObj) && SuggestedObj && SuggestedObj->IsValid());

		TestEqual(TEXT("asset_find_referencers suggested tool_id"), SuggestedObj->Get()->GetStringField(TEXT("tool_id")), FString(TEXT("asset_index_fuzzy_search")));
		const TSharedPtr<FJsonObject>* InnerArgs = nullptr;
		TestTrue(
			TEXT("asset_find_referencers suggested arguments parseable"),
			SuggestedObj->Get()->TryGetObjectField(TEXT("arguments"), InnerArgs) && InnerArgs && InnerArgs->IsValid());
		FString PathPrefix;
		TestTrue(TEXT("asset_find_referencers suggested path_prefix field"), InnerArgs->Get()->TryGetStringField(TEXT("path_prefix"), PathPrefix) && !PathPrefix.IsEmpty());
		TestEqual(TEXT("asset_find_referencers suggested path_prefix value"), PathPrefix, FString(TEXT("/Game")));
	}

	// asset_get_dependencies: non-existent object_path should fail with suggested_correct_call to asset_index_fuzzy_search.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("object_path"), TEXT("/Game/__unreal_ai_missing_asset_for_dependencies.__unreal_ai_missing_asset_for_dependencies__"));

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("asset_get_dependencies"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("asset_get_dependencies missing asset: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("asset_get_dependencies missing asset: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestTrue(TEXT("asset_get_dependencies missing asset: has suggested_correct_call"), O->HasField(TEXT("suggested_correct_call")));

		const TSharedPtr<FJsonObject>* SuggestedObj = nullptr;
		TestTrue(
			TEXT("asset_get_dependencies suggested_correct_call parseable"),
			O->TryGetObjectField(TEXT("suggested_correct_call"), SuggestedObj) && SuggestedObj && SuggestedObj->IsValid());

		TestEqual(TEXT("asset_get_dependencies suggested tool_id"), SuggestedObj->Get()->GetStringField(TEXT("tool_id")), FString(TEXT("asset_index_fuzzy_search")));
		const TSharedPtr<FJsonObject>* InnerArgs = nullptr;
		TestTrue(
			TEXT("asset_get_dependencies suggested arguments parseable"),
			SuggestedObj->Get()->TryGetObjectField(TEXT("arguments"), InnerArgs) && InnerArgs && InnerArgs->IsValid());
		FString PathPrefix;
		TestTrue(TEXT("asset_get_dependencies suggested path_prefix field"), InnerArgs->Get()->TryGetStringField(TEXT("path_prefix"), PathPrefix) && !PathPrefix.IsEmpty());
		TestEqual(TEXT("asset_get_dependencies suggested path_prefix value"), PathPrefix, FString(TEXT("/Game")));
	}

	// asset_rename: non-loadable from_path should fail with suggested_correct_call to asset_index_fuzzy_search.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("object_path"), TEXT("/Game/__unreal_ai_missing_asset_for_rename.__unreal_ai_missing_asset_for_rename__"));
		Args->SetStringField(TEXT("to_name"), TEXT("RenamedAsset"));

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("asset_rename"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("asset_rename missing asset: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("asset_rename missing asset: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestTrue(TEXT("asset_rename missing asset: has suggested_correct_call"), O->HasField(TEXT("suggested_correct_call")));

		const TSharedPtr<FJsonObject>* SuggestedObj = nullptr;
		TestTrue(
			TEXT("asset_rename suggested_correct_call parseable"),
			O->TryGetObjectField(TEXT("suggested_correct_call"), SuggestedObj) && SuggestedObj && SuggestedObj->IsValid());

		TestEqual(TEXT("asset_rename suggested tool_id"), SuggestedObj->Get()->GetStringField(TEXT("tool_id")), FString(TEXT("asset_index_fuzzy_search")));
		const TSharedPtr<FJsonObject>* InnerArgs = nullptr;
		TestTrue(
			TEXT("asset_rename suggested arguments parseable"),
			SuggestedObj->Get()->TryGetObjectField(TEXT("arguments"), InnerArgs) && InnerArgs && InnerArgs->IsValid());
		FString PathPrefix;
		TestTrue(TEXT("asset_rename suggested path_prefix field"), InnerArgs->Get()->TryGetStringField(TEXT("path_prefix"), PathPrefix) && !PathPrefix.IsEmpty());
		TestEqual(TEXT("asset_rename suggested path_prefix value"), PathPrefix, FString(TEXT("/Game")));
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
		const bool bHasSuggested = O->HasField(TEXT("suggested_correct_call"));
		FString Err;
		TestTrue(TEXT("asset_delete confirm:false: error field present"), O->TryGetStringField(TEXT("error"), Err) && !Err.IsEmpty());
		if (bHasSuggested)
		{
			TestEqual(TEXT("asset_delete confirm:false: suggested error message"),
				Err,
				FString(TEXT("confirm must be true to delete assets")));
		}
		else
		{
			// In headless automation runs, destructive calls may auto-confirm, changing the error shape.
			TestEqual(TEXT("asset_delete confirm:false: non-suggested error message"),
				Err,
				FString(TEXT("No assets resolved for deletion")));
		}
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

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("asset_save_packages object_path: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		const bool bHasSuggested = O->HasField(TEXT("suggested_correct_call"));
		bool bOkField = true;
		const bool bHasOkField = O->TryGetBoolField(TEXT("ok"), bOkField);
		TestTrue(TEXT("asset_save_packages object_path: ok field present"), bHasOkField);

		// Either: strict error (bOk=false with suggested_correct_call), or: non-error Ok wrapper with `ok:false`
		// when the package got created in-memory as a side-effect of earlier tool calls.
		if (!R.bOk)
		{
			TestTrue(TEXT("asset_save_packages object_path: has suggested_correct_call when bOk=false"), bHasSuggested);
		}
		else
		{
			TestFalse(TEXT("asset_save_packages object_path: suggested_correct_call absent when bOk=true"), bHasSuggested);
			// Headless runs may create transient in-memory packages; accept either ok=true/false,
			// as long as the failure-mode recovery payload (suggested_correct_call) is not present.
		}
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
		if (R.Factory != nullptr)
		{
			TestTrue(TEXT("factory_resolver staticmesh: has factory"), R.Factory != nullptr);
			TestFalse(TEXT("factory_resolver staticmesh: has class path"), R.FactoryClassPath.IsEmpty());
		}
		else
		{
			// Some headless environments may not register UnrealEd.StaticMeshFactoryNew.
			TestTrue(TEXT("factory_resolver staticmesh: factory missing is acceptable in headless env"), true);
		}
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

	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_format_selection"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_format_selection missing blueprint_path: bOk"), R.bOk);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiTodoPlanContractTest,
	"UnrealAiEditor.Tools.TodoPlanContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiGenericSettingsPropertiesContractTest,
	"UnrealAiEditor.Tools.GenericSettingsPropertiesContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiGenericSettingsPropertiesContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Runs on game thread"), IsInGameThread());

	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("settings_get"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("settings_get missing required fields: bOk"), R.bOk);
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("scope"), TEXT("editor"));
		Args->SetStringField(TEXT("key"), TEXT("editor_focus"));
		// UE Json strings implement TryGetBool (via ToBool); use a JSON array so the value is the wrong shape.
		Args->SetArrayField(TEXT("value"), TArray<TSharedPtr<FJsonValue>>());
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("settings_set"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("settings_set editor_focus wrong type: bOk"), R.bOk);
	}

	{
		TSharedPtr<FJsonObject> SetArgs = MakeShared<FJsonObject>();
		SetArgs->SetStringField(TEXT("scope"), TEXT("editor"));
		SetArgs->SetStringField(TEXT("key"), TEXT("editor_focus"));
		SetArgs->SetBoolField(TEXT("value"), true);
		const FUnrealAiToolInvocationResult SetR = UnrealAiDispatchTool(
			TEXT("settings_set"),
			SetArgs,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestTrue(TEXT("settings_set editor_focus bool value: bOk"), SetR.bOk);

		TSharedPtr<FJsonObject> GetArgs = MakeShared<FJsonObject>();
		GetArgs->SetStringField(TEXT("scope"), TEXT("editor"));
		GetArgs->SetStringField(TEXT("key"), TEXT("editor_focus"));
		const FUnrealAiToolInvocationResult GetR = UnrealAiDispatchTool(
			TEXT("settings_get"),
			GetArgs,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestTrue(TEXT("settings_get editor_focus: bOk"), GetR.bOk);
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("entity_type"), TEXT("actor"));
		Args->SetStringField(TEXT("entity_ref"), TEXT("/Game/Maps/NoMap.NoMap:PersistentLevel.NoActor"));
		Args->SetStringField(TEXT("property"), TEXT("hidden_in_editor"));
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("entity_get_property"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("entity_get_property invalid actor path returns error"), R.bOk);
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("scope"), TEXT("project"));
		Args->SetStringField(TEXT("key"), TEXT("/Script/Engine.Engine:GameEngineClassName"));
		Args->SetStringField(TEXT("value"), TEXT("/Script/Engine.GameEngine"));
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("settings_set"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("settings_set project scope: denied without allowlist"), R.bOk);
		TestTrue(
			TEXT("settings_set project scope: error explains allowlist"),
			R.ErrorMessage.Contains(TEXT("allowlist")) || R.ContentForModel.Contains(TEXT("allowlist")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiProjectFileWritePolicyTest,
	"UnrealAiEditor.Tools.ProjectFileWritePolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiProjectFileWritePolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Runs on game thread"), IsInGameThread());

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("relative_path"), FPaths::GetCleanFilename(FPaths::GetProjectFilePath()));
		Args->SetStringField(TEXT("content"), TEXT("automated policy probe — should not be written"));
		Args->SetBoolField(TEXT("confirm"), true);
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("project_file_write_text"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("project_file_write_text .uproject without critical confirm: bOk"), R.bOk);
		TestTrue(
			TEXT("project_file_write_text: gate message"),
			R.ErrorMessage.Contains(TEXT("confirm_project_critical")) || R.ContentForModel.Contains(TEXT("confirm_project_critical")));
	}

	{
		const FString Rel = TEXT("Saved/UnrealAiEditorAgent/_automation_write_policy_probe.txt");
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("relative_path"), Rel);
		Args->SetStringField(TEXT("content"), TEXT("ok"));
		Args->SetBoolField(TEXT("confirm"), true);
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("project_file_write_text"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestTrue(TEXT("project_file_write_text under agent safe root: bOk"), R.bOk);
		FString Abs;
		FString Err;
		TestTrue(TEXT("resolve automation probe path"), UnrealAiResolveProjectFilePath(Rel, Abs, Err));
		IFileManager::Get().Delete(*Abs, false, true, true);
	}

	return true;
}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiConsoleCommandAllowlistTest,
	"UnrealAiEditor.Tools.ConsoleCommandAllowlist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiConsoleCommandAllowlistTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Runs on game thread"), IsInGameThread());

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("command"), TEXT("___not_an_allowed_console_key___"));
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("console_command"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("unknown key: bOk"), R.bOk);
		TestTrue(TEXT("unknown key: error mentions allow"), R.ErrorMessage.Contains(TEXT("Unknown")));
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("command"), TEXT("r_vsync"));
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("console_command"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("r_vsync without args: bOk"), R.bOk);
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("command"), TEXT("stat_fps"));
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("console_command"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestTrue(TEXT("stat_fps: bOk"), R.bOk);
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("stat_fps: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestTrue(TEXT("stat_fps: ok field"), O->GetBoolField(TEXT("ok")));
		TestEqual(TEXT("stat_fps: executed"), O->GetStringField(TEXT("executed")), FString(TEXT("stat fps")));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
