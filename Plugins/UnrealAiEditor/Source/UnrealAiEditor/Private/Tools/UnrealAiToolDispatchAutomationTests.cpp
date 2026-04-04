#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Tools/UnrealAiToolDispatch.h"
#include "Tools/UnrealAiToolDispatch_ArgRepair.h"
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
#include "UObject/UObjectGlobals.h"
#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"
#include "Tools/UnrealAiBlueprintToolGate.h"
#include "Tools/UnrealAiToolSurfaceCompatibility.h"
#include "EdGraphSchema_K2.h"
#include "GraphBuilder/UnrealAiGraphEditDomain.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Materials/Material.h"
#include "Tools/UnrealAiToolDispatch_MaterialGraph.h"

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
	TestEqual(TEXT("resolver contract version"), Catalog.GetResolverContractVersion(), FString(TEXT("2.2.0")));

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
	FUnrealAiBlueprintGraphPatchArgRepairTest,
	"UnrealAiEditor.Tools.BlueprintGraphPatchArgRepair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiBlueprintGraphPatchArgRepairTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TSharedPtr<FJsonObject> Op = MakeShared<FJsonObject>();
	Op->SetStringField(TEXT("op"), TEXT("create_node"));
	Op->SetStringField(TEXT("patch_id"), TEXT("n1"));
	Op->SetStringField(TEXT("k2_class"), TEXT("UK2Node_IfThenElse"));
	Op->SetStringField(TEXT("node_guid"), TEXT("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));
	TArray<TSharedPtr<FJsonValue>> Ops;
	Ops.Add(MakeShared<FJsonValueObject>(Op.ToSharedRef()));
	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetArrayField(TEXT("ops"), Ops);
	UnrealAiToolDispatchArgRepair::RepairBlueprintGraphPatchToolArgs(Args);
	const TArray<TSharedPtr<FJsonValue>>* OutOps = nullptr;
	TestTrue(TEXT("ops array"), Args->TryGetArrayField(TEXT("ops"), OutOps) && OutOps && OutOps->Num() == 1);
	const TSharedPtr<FJsonObject>* Fixed = nullptr;
	TestTrue(TEXT("first op object"), (*OutOps)[0]->TryGetObject(Fixed) && Fixed && (*Fixed).IsValid());
	FString K2;
	TestTrue(TEXT("k2 normalized"), (*Fixed)->TryGetStringField(TEXT("k2_class"), K2));
	TestEqual(TEXT("k2_class path"), K2, FString(TEXT("/Script/BlueprintGraph.K2Node_IfThenElse")));
	TestFalse(TEXT("node_guid stripped"), (*Fixed)->HasField(TEXT("node_guid")));

	TSharedPtr<FJsonObject> Op2 = MakeShared<FJsonObject>();
	Op2->SetStringField(TEXT("op"), TEXT("create_node"));
	Op2->SetStringField(TEXT("patch_id"), TEXT("e1"));
	Op2->SetStringField(TEXT("class_name"), TEXT("K2Node_CallFunction"));
	TArray<TSharedPtr<FJsonValue>> Ops2;
	Ops2.Add(MakeShared<FJsonValueObject>(Op2.ToSharedRef()));
	TSharedPtr<FJsonObject> Args2 = MakeShared<FJsonObject>();
	Args2->SetArrayField(TEXT("ops"), Ops2);
	UnrealAiToolDispatchArgRepair::RepairBlueprintGraphPatchToolArgs(Args2);
	const TArray<TSharedPtr<FJsonValue>>* OutOps2 = nullptr;
	TestTrue(TEXT("ops2"), Args2->TryGetArrayField(TEXT("ops"), OutOps2) && OutOps2 && OutOps2->Num() == 1);
	const TSharedPtr<FJsonObject>* Fixed2 = nullptr;
	TestTrue(TEXT("op2 object"), (*OutOps2)[0]->TryGetObject(Fixed2) && Fixed2 && (*Fixed2).IsValid());
	FString K2b;
	TestTrue(TEXT("class_name -> k2"), (*Fixed2)->TryGetStringField(TEXT("k2_class"), K2b));
	TestEqual(TEXT("bare K2Node expanded"), K2b, FString(TEXT("/Script/BlueprintGraph.K2Node_CallFunction")));
	TestFalse(TEXT("class_name removed"), (*Fixed2)->HasField(TEXT("class_name")));

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

	// T3D __UAI_G_* tokens are rejected in graph_patch with an explicit recovery hint (StrictTests harness only).
	{
		UBlueprint* Bp = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
		if (Bp)
		{
			TSharedPtr<FJsonObject> Op = MakeShared<FJsonObject>();
			Op->SetStringField(TEXT("op"), TEXT("move_node"));
			Op->SetStringField(TEXT("node_guid"), TEXT("__UAI_G_000001__"));
			Op->SetNumberField(TEXT("x"), 0);
			Op->SetNumberField(TEXT("y"), 0);
			TArray<TSharedPtr<FJsonValue>> Ops;
			Ops.Add(MakeShared<FJsonValueObject>(Op.ToSharedRef()));
			TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
			Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
			Args->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
			Args->SetArrayField(TEXT("ops"), Ops);
			const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
				TEXT("blueprint_graph_patch"),
				Args,
				nullptr,
				nullptr,
				FString(),
				FString());
			TestFalse(TEXT("graph_patch UAI placeholder: bOk"), R.bOk);
			TSharedPtr<FJsonObject> O;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
			TestTrue(TEXT("graph_patch UAI placeholder: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
			const TArray<TSharedPtr<FJsonValue>>* Errs = nullptr;
			TestTrue(TEXT("graph_patch UAI placeholder: errors array"), O->TryGetArrayField(TEXT("errors"), Errs) && Errs && Errs->Num() > 0);
			FString FirstErr;
			TestTrue(TEXT("graph_patch UAI placeholder: first error string"), (*Errs)[0]->TryGetString(FirstErr));
			TestTrue(TEXT("graph_patch UAI placeholder: mentions T3D placeholder"), FirstErr.Contains(TEXT("T3D placeholder")));
			TestTrue(TEXT("graph_patch UAI placeholder: mentions introspect"), FirstErr.Contains(TEXT("blueprint_graph_introspect")));
		}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiBlueprintVerifyGraphHappyPathTest,
	"UnrealAiEditor.Tools.BlueprintVerifyGraphHappyPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiBlueprintVerifyGraphHappyPathTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Runs on game thread (tool dispatch requirement)"), IsInGameThread());

	// Sample TP_FirstPerson harness asset used by strict suites; not shipped with plugin sources (Content gitignored at repo root).
	UBlueprint* Bp = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
	if (!Bp)
	{
		UE_LOG(LogTemp, Display, TEXT("UnrealAiEditor.Tools.BlueprintVerifyGraphHappyPath: skip — Strict_BP02 not in project"));
		return true;
	}
	(void)Bp;

	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
	Args->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
	TArray<TSharedPtr<FJsonValue>> St;
	St.Add(MakeShared<FJsonValueString>(TEXT("links")));
	St.Add(MakeShared<FJsonValueString>(TEXT("orphan_pins")));
	St.Add(MakeShared<FJsonValueString>(TEXT("duplicate_node_guids")));
	Args->SetArrayField(TEXT("steps"), St);

	const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
		TEXT("blueprint_verify_graph"),
		Args,
		nullptr,
		nullptr,
		FString(),
		FString());

	TestTrue(TEXT("blueprint_verify_graph Strict_BP02: bOk"), R.bOk);
	TSharedPtr<FJsonObject> O;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
	TestTrue(TEXT("blueprint_verify_graph Strict_BP02: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
	TestTrue(TEXT("blueprint_verify_graph Strict_BP02: passed"), O->GetBoolField(TEXT("passed")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiBlueprintGraphIntrospectLinkedToTest,
	"UnrealAiEditor.Tools.BlueprintGraphIntrospectLinkedTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiBlueprintGraphIntrospectLinkedToTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Runs on game thread (tool dispatch requirement)"), IsInGameThread());

	UBlueprint* Bp = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
	if (!Bp)
	{
		UE_LOG(LogTemp, Display, TEXT("UnrealAiEditor.Tools.BlueprintGraphIntrospectLinkedTo: skip — Strict_BP02 not in project"));
		return true;
	}
	(void)Bp;

	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
	Args->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));

	const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
		TEXT("blueprint_graph_introspect"),
		Args,
		nullptr,
		nullptr,
		FString(),
		FString());

	TestTrue(TEXT("blueprint_graph_introspect Strict_BP02: bOk"), R.bOk);
	TSharedPtr<FJsonObject> O;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
	TestTrue(TEXT("blueprint_graph_introspect Strict_BP02: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
	TestTrue(TEXT("blueprint_graph_introspect Strict_BP02: ok"), O->GetBoolField(TEXT("ok")));

	const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
	TestTrue(TEXT("blueprint_graph_introspect Strict_BP02: has nodes"), O->TryGetArrayField(TEXT("nodes"), Nodes) && Nodes && Nodes->Num() > 0);

	bool bAnyLink = false;
	for (const TSharedPtr<FJsonValue>& Nv : *Nodes)
	{
		const TSharedPtr<FJsonObject>* No = nullptr;
		if (!Nv.IsValid() || !Nv->TryGetObject(No) || !No->IsValid())
		{
			continue;
		}
		const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
		if (!(*No)->TryGetArrayField(TEXT("pins"), Pins) || !Pins)
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& Pv : *Pins)
		{
			const TSharedPtr<FJsonObject>* Po = nullptr;
			if (!Pv.IsValid() || !Pv->TryGetObject(Po) || !Po->IsValid())
			{
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>* Linked = nullptr;
			TestTrue(TEXT("blueprint_graph_introspect pin has linked_to array"), (*Po)->TryGetArrayField(TEXT("linked_to"), Linked) && Linked);
			if (Linked && Linked->Num() > 0)
			{
				bAnyLink = true;
				const TSharedPtr<FJsonObject>* L0 = nullptr;
				TestTrue(TEXT("blueprint_graph_introspect linked_to entry is object"), (*Linked)[0]->TryGetObject(L0) && L0->IsValid());
				TestTrue(TEXT("blueprint_graph_introspect linked_to has node_guid"), (*L0)->HasTypedField<EJson::String>(TEXT("node_guid")));
				TestTrue(TEXT("blueprint_graph_introspect linked_to has pin_name"), (*L0)->HasTypedField<EJson::String>(TEXT("pin_name")));
			}
		}
	}
	TestTrue(TEXT("blueprint_graph_introspect Strict_BP02: at least one pin connection reported"), bAnyLink);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiBlueprintCompositeLifecyclePrintContractTest,
	"UnrealAiEditor.Tools.BlueprintCompositeLifecyclePrintContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiBlueprintCompositeLifecyclePrintContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Runs on game thread (tool dispatch requirement)"), IsInGameThread());

	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_composite_lifecycle_print"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("composite_lifecycle_print empty args: bOk"), R.bOk);
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("composite_lifecycle_print empty args: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestEqual(TEXT("composite_lifecycle_print empty args: error"), O->GetStringField(TEXT("error")), FString(TEXT("blueprint_path is required")));
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
		Args->SetStringField(TEXT("lifecycle"), TEXT("not_a_real_lifecycle"));
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_composite_lifecycle_print"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("composite_lifecycle_print bad lifecycle: bOk"), R.bOk);
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("composite_lifecycle_print bad lifecycle: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestEqual(TEXT("composite_lifecycle_print bad lifecycle: error"), O->GetStringField(TEXT("error")), FString(TEXT("lifecycle must be begin_play or tick")));
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/__does_not_exist__/UaiCompositeMissing.UaiCompositeMissing"));
		Args->SetStringField(TEXT("lifecycle"), TEXT("begin_play"));
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_composite_lifecycle_print"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("composite_lifecycle_print missing asset: bOk"), R.bOk);
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("composite_lifecycle_print missing asset: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestEqual(TEXT("composite_lifecycle_print missing asset: status"), O->GetStringField(TEXT("status")), FString(TEXT("asset_not_found")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiBlueprintIrTier1GoldenRoundTripTest,
	"UnrealAiEditor.Tools.BlueprintIrTier1GoldenRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiBlueprintIrTier1GoldenRoundTripTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Runs on game thread (tool dispatch requirement)"), IsInGameThread());

	// Golden Tier-1 IR (event_begin_play + PrintString + link + default) must parse; failure must be asset load, not invalid_ir.
	{
		TSharedPtr<FJsonObject> Ir = MakeShared<FJsonObject>();
		Ir->SetStringField(TEXT("blueprint_path"), TEXT("/Game/__Tier1Golden__/BP_Tier1Golden.BP_Tier1Golden"));
		Ir->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
		Ir->SetStringField(TEXT("merge_policy"), TEXT("append_to_existing"));
		Ir->SetBoolField(TEXT("auto_layout"), true);
		TArray<TSharedPtr<FJsonValue>> Nodes;
		{
			TSharedPtr<FJsonObject> N1 = MakeShared<FJsonObject>();
			N1->SetStringField(TEXT("node_id"), TEXT("golden_ev"));
			N1->SetStringField(TEXT("op"), TEXT("event_begin_play"));
			N1->SetNumberField(TEXT("x"), 0);
			N1->SetNumberField(TEXT("y"), 0);
			Nodes.Add(MakeShared<FJsonValueObject>(N1.ToSharedRef()));
			TSharedPtr<FJsonObject> N2 = MakeShared<FJsonObject>();
			N2->SetStringField(TEXT("node_id"), TEXT("golden_print"));
			N2->SetStringField(TEXT("op"), TEXT("call_function"));
			N2->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.KismetSystemLibrary"));
			N2->SetStringField(TEXT("function_name"), TEXT("PrintString"));
			N2->SetNumberField(TEXT("x"), 400);
			N2->SetNumberField(TEXT("y"), 0);
			Nodes.Add(MakeShared<FJsonValueObject>(N2.ToSharedRef()));
		}
		Ir->SetArrayField(TEXT("nodes"), Nodes);
		TArray<TSharedPtr<FJsonValue>> Links;
		{
			TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
			L->SetStringField(TEXT("from"), TEXT("golden_ev.Then"));
			L->SetStringField(TEXT("to"), TEXT("golden_print.execute"));
			Links.Add(MakeShared<FJsonValueObject>(L.ToSharedRef()));
		}
		Ir->SetArrayField(TEXT("links"), Links);
		TArray<TSharedPtr<FJsonValue>> Defs;
		{
			TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
			D->SetStringField(TEXT("node_id"), TEXT("golden_print"));
			D->SetStringField(TEXT("pin"), TEXT("InString"));
			D->SetStringField(TEXT("value"), TEXT("Tier-1 golden"));
			Defs.Add(MakeShared<FJsonValueObject>(D.ToSharedRef()));
		}
		Ir->SetArrayField(TEXT("defaults"), Defs);

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("blueprint_apply_ir"),
			Ir,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("Tier-1 golden IR missing asset: bOk"), R.bOk);
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("Tier-1 golden IR missing asset: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestEqual(TEXT("Tier-1 golden IR missing asset: status"), O->GetStringField(TEXT("status")), FString(TEXT("asset_not_found")));
	}

	// Export stability: two consecutive blueprint_export_ir calls return the same node count (StrictTests harness only).
	{
		UBlueprint* Bp = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
		if (!Bp)
		{
			UE_LOG(LogTemp, Display, TEXT("UnrealAiEditor.Tools.BlueprintIrTier1GoldenRoundTrip: skip export stability — Strict_BP02 not in project"));
			return true;
		}
		(void)Bp;

		TSharedPtr<FJsonObject> ExportArgs = MakeShared<FJsonObject>();
		ExportArgs->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
		ExportArgs->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));

		const FUnrealAiToolInvocationResult R1 = UnrealAiDispatchTool(
			TEXT("blueprint_export_ir"),
			ExportArgs,
			nullptr,
			nullptr,
			FString(),
			FString());
		const FUnrealAiToolInvocationResult R2 = UnrealAiDispatchTool(
			TEXT("blueprint_export_ir"),
			ExportArgs,
			nullptr,
			nullptr,
			FString(),
			FString());

		TestTrue(TEXT("export stability R1: bOk"), R1.bOk);
		TestTrue(TEXT("export stability R2: bOk"), R2.bOk);

		auto ParseIrNodeCount = [](const FString& Json, int32& OutCount) -> bool
		{
			OutCount = 0;
			TSharedPtr<FJsonObject> Root;
			TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Json);
			if (!FJsonSerializer::Deserialize(Rd, Root) || !Root.IsValid())
			{
				return false;
			}
			const TSharedPtr<FJsonObject>* IrObj = nullptr;
			if (!Root->TryGetObjectField(TEXT("ir"), IrObj) || !IrObj || !(*IrObj).IsValid())
			{
				return false;
			}
			const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
			if (!(*IrObj)->TryGetArrayField(TEXT("nodes"), Nodes) || !Nodes)
			{
				return false;
			}
			OutCount = Nodes->Num();
			return true;
		};

		int32 C1 = 0;
		int32 C2 = 0;
		TestTrue(TEXT("export stability: parse R1"), ParseIrNodeCount(R1.ContentForModel, C1));
		TestTrue(TEXT("export stability: parse R2"), ParseIrNodeCount(R2.ContentForModel, C2));
		TestEqual(TEXT("export stability: node count"), C1, C2);
		TestTrue(TEXT("export stability: non-empty graph"), C1 > 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiGraphEditDomainRegistryTest,
	"UnrealAiEditor.Tools.GraphEditDomainRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiGraphEditDomainRegistryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const TArray<EUnrealAiBlueprintBuilderTargetKind> Kinds = {
		EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint,
		EUnrealAiBlueprintBuilderTargetKind::AnimBlueprint,
		EUnrealAiBlueprintBuilderTargetKind::MaterialInstance,
		EUnrealAiBlueprintBuilderTargetKind::MaterialGraph,
		EUnrealAiBlueprintBuilderTargetKind::Niagara,
		EUnrealAiBlueprintBuilderTargetKind::WidgetBlueprint,
	};
	for (const EUnrealAiBlueprintBuilderTargetKind K : Kinds)
	{
		IUnrealAiGraphEditDomain* D = FUnrealAiGraphEditDomainRegistry::Get(K);
		TestNotNull(TEXT("registry returns domain"), D);
		TestEqual(TEXT("kind roundtrip"), D->GetKind(), K);
	}
	TestTrue(
		TEXT("script surface exposes Kismet IR capability"),
		FUnrealAiGraphEditDomainRegistry::Get(EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint)->GetCapabilities().bSupportsKismetIR);
	TestTrue(
		TEXT("material instance surface exposes parameter capability"),
		FUnrealAiGraphEditDomainRegistry::Get(EUnrealAiBlueprintBuilderTargetKind::MaterialInstance)->GetCapabilities().bSupportsParameterOnlyMutation);
	TestTrue(
		TEXT("material graph domain implementation complete"),
		FUnrealAiGraphEditDomainRegistry::Get(EUnrealAiBlueprintBuilderTargetKind::MaterialGraph)->GetCapabilities().bImplementationComplete);

	FUnrealAiAgentTurnRequest Req;
	Req.bBlueprintBuilderTurn = false;
	FUnrealAiToolInvocationResult Block;
	TestFalse(
		TEXT("preflight does not block when not builder turn"),
		UnrealAiGraphEditDomainPreflight_ShouldBlockInvocation(Req, TEXT("blueprint_compile"), TEXT("{\"blueprint_path\":\"/Game/Missing.Missing\"}"), Block));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiToolSurfaceCompatibilityTest,
	"UnrealAiEditor.Tools.SurfaceCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiToolSurfaceCompatibilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	{
		TSharedPtr<FJsonObject> Tool = MakeShared<FJsonObject>();
		TSet<FString> Tokens;
		bool bAll = false;
		UnrealAiToolSurfaceCompatibility::ParseAgentSurfaces(*Tool, Tokens, bAll);
		TestTrue(TEXT("missing agent_surfaces => all"), bAll);
		TestEqual(TEXT("missing => empty token set"), Tokens.Num(), 0);
	}

	{
		TSharedPtr<FJsonObject> Tool = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShared<FJsonValueString>(TEXT("ALL")));
		Tool->SetArrayField(TEXT("agent_surfaces"), Arr);
		TSet<FString> Tokens;
		bool bAll = false;
		UnrealAiToolSurfaceCompatibility::ParseAgentSurfaces(*Tool, Tokens, bAll);
		TestTrue(TEXT("single all token => all"), bAll);
	}

	{
		TSharedPtr<FJsonObject> Tool = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShared<FJsonValueString>(TEXT("Main_Agent")));
		Arr.Add(MakeShared<FJsonValueString>(TEXT("blueprint_builder")));
		Tool->SetArrayField(TEXT("agent_surfaces"), Arr);
		TSet<FString> Tokens;
		bool bAll = false;
		UnrealAiToolSurfaceCompatibility::ParseAgentSurfaces(*Tool, Tokens, bAll);
		TestFalse(TEXT("dual tag => not all"), bAll);
		TestTrue(TEXT("dual main_agent"), Tokens.Contains(UnrealAiToolSurfaceCompatibility::GAgentSurfaceToken_MainAgent));
		TestTrue(TEXT("dual blueprint_builder"), Tokens.Contains(UnrealAiToolSurfaceCompatibility::GAgentSurfaceToken_BlueprintBuilder));
		TestTrue(
			TEXT("main surface allowed"),
			UnrealAiToolSurfaceCompatibility::ToolAllowedOnSurface(Tokens, bAll, EUnrealAiToolSurfaceKind::MainAgent));
		TestTrue(
			TEXT("builder surface allowed"),
			UnrealAiToolSurfaceCompatibility::ToolAllowedOnSurface(Tokens, bAll, EUnrealAiToolSurfaceKind::BlueprintBuilder));
	}

	{
		TSet<FString> BuilderOnly;
		BuilderOnly.Add(UnrealAiToolSurfaceCompatibility::GAgentSurfaceToken_BlueprintBuilder);
		TestFalse(
			TEXT("builder-only on main agent"),
			UnrealAiToolSurfaceCompatibility::ToolAllowedOnSurface(BuilderOnly, false, EUnrealAiToolSurfaceKind::MainAgent));
		TestTrue(
			TEXT("builder-only on builder"),
			UnrealAiToolSurfaceCompatibility::ToolAllowedOnSurface(BuilderOnly, false, EUnrealAiToolSurfaceKind::BlueprintBuilder));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiBlueprintToolGateSurfaceTest,
	"UnrealAiEditor.Tools.BlueprintToolGateSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiBlueprintToolGateSurfaceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FUnrealAiToolCatalog Catalog;
	TestTrue(TEXT("catalog loads"), Catalog.LoadFromPlugin());
	TestTrue(TEXT("catalog reports loaded"), Catalog.IsLoaded());

	FUnrealAiAgentTurnRequest Req;

	Req.Mode = EUnrealAiAgentMode::Ask;
	TestTrue(
		TEXT("ask mode passes surface gate"),
		UnrealAiBlueprintToolGate::PassesToolSurfaceFilter(Req, TEXT("blueprint_graph_patch"), &Catalog));

	Req.Mode = EUnrealAiAgentMode::Agent;
	Req.bOmitMainAgentBlueprintMutationTools = false;
	TestTrue(
		TEXT("omit flag off bypasses gate"),
		UnrealAiBlueprintToolGate::PassesToolSurfaceFilter(Req, TEXT("blueprint_graph_patch"), &Catalog));

	Req.bOmitMainAgentBlueprintMutationTools = true;
	Req.bBlueprintBuilderTurn = false;
	TestFalse(
		TEXT("builder-only tool blocked on main agent"),
		UnrealAiBlueprintToolGate::PassesToolSurfaceFilter(Req, TEXT("blueprint_graph_patch"), &Catalog));

	Req.bBlueprintBuilderTurn = true;
	TestTrue(
		TEXT("builder-only tool allowed on builder turn"),
		UnrealAiBlueprintToolGate::PassesToolSurfaceFilter(Req, TEXT("blueprint_graph_patch"), &Catalog));

	Req.bBlueprintBuilderTurn = false;
	TestTrue(
		TEXT("default surfaces (no field) passes on main"),
		UnrealAiBlueprintToolGate::PassesToolSurfaceFilter(Req, TEXT("editor_get_selection"), &Catalog));

	Req.bOmitMainAgentBlueprintMutationTools = true;
	Req.bBlueprintBuilderTurn = false;
	TestFalse(
		TEXT("material_graph_patch blocked on main agent"),
		UnrealAiBlueprintToolGate::PassesToolSurfaceFilter(Req, TEXT("material_graph_patch"), &Catalog));
	TestTrue(
		TEXT("material_graph_export allowed on main agent"),
		UnrealAiBlueprintToolGate::PassesToolSurfaceFilter(Req, TEXT("material_graph_export"), &Catalog));
	Req.bBlueprintBuilderTurn = true;
	TestTrue(
		TEXT("material_graph_patch allowed on builder turn"),
		UnrealAiBlueprintToolGate::PassesToolSurfaceFilter(Req, TEXT("material_graph_patch"), &Catalog));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiMaterialGraphToolsTest,
	"UnrealAiEditor.Tools.MaterialGraphRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiMaterialGraphToolsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UMaterial* Mat = NewObject<UMaterial>(
		GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UMaterial::StaticClass(), TEXT("UaiMatGraphTest")),
		RF_Transactional);
	TestNotNull(TEXT("transient UMaterial"), Mat);
	const FString MatPath = Mat->GetPathName();

	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("material_path"), MatPath);
		const FUnrealAiToolInvocationResult R = UnrealAiDispatch_MaterialGraphExport(A);
		TestTrue(TEXT("material_graph_export.bOk"), R.bOk);
	}

	FString NewGuidStr;
	{
		TSharedPtr<FJsonObject> Patch = MakeShared<FJsonObject>();
		Patch->SetStringField(TEXT("material_path"), MatPath);
		TArray<TSharedPtr<FJsonValue>> Ops;
		{
			TSharedPtr<FJsonObject> Op1 = MakeShared<FJsonObject>();
			Op1->SetStringField(TEXT("op"), TEXT("add_expression"));
			Op1->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.MaterialExpressionConstant3Vector"));
			Op1->SetNumberField(TEXT("editor_x"), -300);
			Op1->SetNumberField(TEXT("editor_y"), 0);
			Ops.Add(MakeShared<FJsonValueObject>(Op1));
		}
		Patch->SetArrayField(TEXT("ops"), Ops);
		Patch->SetBoolField(TEXT("recompile"), false);
		const FUnrealAiToolInvocationResult RP = UnrealAiDispatch_MaterialGraphPatch(Patch);
		TestTrue(TEXT("material_graph_patch.bOk"), RP.bOk);
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RP.ContentForModel);
		TestTrue(TEXT("patch JSON parse"), FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid());
		const TArray<TSharedPtr<FJsonValue>>* OR = nullptr;
		TestTrue(TEXT("op_results array"), Root->TryGetArrayField(TEXT("op_results"), OR) && OR && (*OR).Num() > 0);
		const TSharedPtr<FJsonObject>* O0 = nullptr;
		TestTrue(TEXT("op0 object"), (*OR)[0]->TryGetObject(O0) && O0->IsValid());
		TestTrue(TEXT("op0 ok"), (*O0)->GetBoolField(TEXT("ok")));
		TestTrue(TEXT("expression_guid present"), (*O0)->TryGetStringField(TEXT("expression_guid"), NewGuidStr) && !NewGuidStr.IsEmpty());
	}

	{
		TSharedPtr<FJsonObject> Patch2 = MakeShared<FJsonObject>();
		Patch2->SetStringField(TEXT("material_path"), MatPath);
		TArray<TSharedPtr<FJsonValue>> Ops2;
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("op"), TEXT("set_constant3vector"));
			O->SetStringField(TEXT("expression_guid"), NewGuidStr);
			O->SetNumberField(TEXT("r"), 0.2);
			O->SetNumberField(TEXT("g"), 0.8);
			O->SetNumberField(TEXT("b"), 0.1);
			Ops2.Add(MakeShared<FJsonValueObject>(O));
		}
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("op"), TEXT("connect_material_input"));
			O->SetStringField(TEXT("from_expression_guid"), NewGuidStr);
			O->SetStringField(TEXT("material_input"), TEXT("BaseColor"));
			O->SetStringField(TEXT("from_output"), TEXT(""));
			Ops2.Add(MakeShared<FJsonValueObject>(O));
		}
		Patch2->SetArrayField(TEXT("ops"), Ops2);
		Patch2->SetBoolField(TEXT("recompile"), true);
		const FUnrealAiToolInvocationResult R2 = UnrealAiDispatch_MaterialGraphPatch(Patch2);
		TestTrue(TEXT("patch color+basecolor.bOk"), R2.bOk);
	}

	{
		TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>();
		V->SetStringField(TEXT("material_path"), MatPath);
		const FUnrealAiToolInvocationResult RV = UnrealAiDispatch_MaterialGraphValidate(V);
		TestTrue(TEXT("material_graph_validate.bOk"), RV.bOk);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
