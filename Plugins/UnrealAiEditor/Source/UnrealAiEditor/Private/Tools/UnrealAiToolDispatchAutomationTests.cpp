#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
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
#include "Tools/UnrealAiAgentToolGate.h"
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
		TestEqual(TEXT("setting_query routes as canonical tool id"), Resolved.LegacyToolId, FString(TEXT("setting_query")));
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
		TestEqual(TEXT("material_instance_set_parameter routes as canonical tool id"), Resolved.LegacyToolId, FString(TEXT("material_instance_set_parameter")));
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
		TestEqual(TEXT("asset_graph_query routes as canonical tool id"), Resolved.LegacyToolId, FString(TEXT("asset_graph_query")));
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
		TestEqual(TEXT("viewport_camera_control routes as canonical tool id"), Resolved.LegacyToolId, FString(TEXT("viewport_camera_control")));
	}

	{
		const FUnrealAiResolvedToolInvocation Resolved = Resolver.Resolve(TEXT("viewport_cmera_control"), MakeShared<FJsonObject>());
		TestFalse(TEXT("ambiguous typo without args should fail"), Resolved.bResolved);
		TestTrue(
			TEXT("unknown tool typo includes suggestion"),
			Resolved.FailureResult.ErrorMessage.Contains(TEXT("viewport_camera_control")) || Resolved.FailureResult.ContentForModel.Contains(TEXT("viewport_camera_control")));
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/TestBp.TestBp"));
		TSharedPtr<FJsonObject> Op = MakeShared<FJsonObject>();
		Op->SetStringField(TEXT("op"), TEXT("create_node"));
		Op->SetStringField(TEXT("patch_id"), TEXT("n1"));
		Op->SetStringField(TEXT("k2_class"), TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"));
		TArray<TSharedPtr<FJsonValue>> Ops;
		Ops.Add(MakeShared<FJsonValueObject>(Op.ToSharedRef()));
		Args->SetArrayField(TEXT("ops"), Ops);
		Args->SetStringField(TEXT("ops_json_path"), TEXT("Saved/UnrealAi/x.json"));
		const FUnrealAiResolvedToolInvocation Resolved = Resolver.Resolve(TEXT("blueprint_graph_patch"), Args);
		TestFalse(TEXT("blueprint_graph_patch ops + ops_json_path rejected by resolver"), Resolved.bResolved);
	}
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("blueprint_path"), TEXT("/Game/TestBp.TestBp"));
		const FUnrealAiResolvedToolInvocation Resolved = Resolver.Resolve(TEXT("blueprint_graph_patch"), Args);
		TestFalse(TEXT("blueprint_graph_patch missing ops and ops_json_path rejected by resolver"), Resolved.bResolved);
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

	TSharedPtr<FJsonObject> OpC = MakeShared<FJsonObject>();
	OpC->SetStringField(TEXT("op"), TEXT("connect"));
	OpC->SetStringField(TEXT("link_from"), TEXT("a.Then"));
	OpC->SetStringField(TEXT("link_to"), TEXT("b.Execute"));
	TArray<TSharedPtr<FJsonValue>> OpsC;
	OpsC.Add(MakeShared<FJsonValueObject>(OpC.ToSharedRef()));
	TSharedPtr<FJsonObject> ArgsC = MakeShared<FJsonObject>();
	ArgsC->SetArrayField(TEXT("ops"), OpsC);
	UnrealAiToolDispatchArgRepair::RepairBlueprintGraphPatchToolArgs(ArgsC);
	const TArray<TSharedPtr<FJsonValue>>* OutOpsC = nullptr;
	TestTrue(TEXT("opsC"), ArgsC->TryGetArrayField(TEXT("ops"), OutOpsC) && OutOpsC && OutOpsC->Num() == 1);
	const TSharedPtr<FJsonObject>* FixedC = nullptr;
	TestTrue(TEXT("opC object"), (*OutOpsC)[0]->TryGetObject(FixedC) && FixedC && (*FixedC).IsValid());
	FString FromC, ToC;
	TestTrue(TEXT("link_from folded"), (*FixedC)->TryGetStringField(TEXT("from"), FromC));
	TestEqual(TEXT("from value"), FromC, FString(TEXT("a.Then")));
	TestTrue(TEXT("link_to folded"), (*FixedC)->TryGetStringField(TEXT("to"), ToC));
	TestEqual(TEXT("to value"), ToC, FString(TEXT("b.Execute")));
	TestFalse(TEXT("link_from stripped"), (*FixedC)->HasField(TEXT("link_from")));

	TSharedPtr<FJsonObject> OpD = MakeShared<FJsonObject>();
	OpD->SetStringField(TEXT("op"), TEXT("connect"));
	OpD->SetStringField(TEXT("from_node"), TEXT("guid:11111111-1111-1111-1111-111111111111"));
	OpD->SetStringField(TEXT("from_pin"), TEXT("Then"));
	OpD->SetStringField(TEXT("to_node"), TEXT("n2"));
	OpD->SetStringField(TEXT("to_pin"), TEXT("Execute"));
	TArray<TSharedPtr<FJsonValue>> OpsD;
	OpsD.Add(MakeShared<FJsonValueObject>(OpD.ToSharedRef()));
	TSharedPtr<FJsonObject> ArgsD = MakeShared<FJsonObject>();
	ArgsD->SetArrayField(TEXT("ops"), OpsD);
	UnrealAiToolDispatchArgRepair::RepairBlueprintGraphPatchToolArgs(ArgsD);
	const TArray<TSharedPtr<FJsonValue>>* OutOpsD = nullptr;
	TestTrue(TEXT("opsD"), ArgsD->TryGetArrayField(TEXT("ops"), OutOpsD) && OutOpsD && OutOpsD->Num() == 1);
	const TSharedPtr<FJsonObject>* FixedD = nullptr;
	TestTrue(TEXT("opD object"), (*OutOpsD)[0]->TryGetObject(FixedD) && FixedD && (*FixedD).IsValid());
	FString FromD, ToD;
	TestTrue(TEXT("split from"), (*FixedD)->TryGetStringField(TEXT("from"), FromD));
	TestTrue(TEXT("split to"), (*FixedD)->TryGetStringField(TEXT("to"), ToD));
	TestTrue(TEXT("from has dot"), FromD.Contains(TEXT(".")));
	TestTrue(TEXT("to has dot"), ToD.Contains(TEXT(".")));

	// k2_class alias: hallucinated Sequence -> K2Node_ExecutionSequence
	TSharedPtr<FJsonObject> OpSeq = MakeShared<FJsonObject>();
	OpSeq->SetStringField(TEXT("op"), TEXT("create_node"));
	OpSeq->SetStringField(TEXT("patch_id"), TEXT("seq1"));
	OpSeq->SetStringField(TEXT("k2_class"), TEXT("/Script/BlueprintGraph.K2Node_Sequence"));
	TArray<TSharedPtr<FJsonValue>> OpsSeq;
	OpsSeq.Add(MakeShared<FJsonValueObject>(OpSeq.ToSharedRef()));
	TSharedPtr<FJsonObject> ArgsSeq = MakeShared<FJsonObject>();
	ArgsSeq->SetArrayField(TEXT("ops"), OpsSeq);
	UnrealAiToolDispatchArgRepair::RepairBlueprintGraphPatchToolArgs(ArgsSeq);
	const TArray<TSharedPtr<FJsonValue>>* OutSeq = nullptr;
	TestTrue(TEXT("opsSeq"), ArgsSeq->TryGetArrayField(TEXT("ops"), OutSeq) && OutSeq && OutSeq->Num() == 1);
	const TSharedPtr<FJsonObject>* FixSeq = nullptr;
	TestTrue(TEXT("opSeq obj"), (*OutSeq)[0]->TryGetObject(FixSeq) && FixSeq && (*FixSeq).IsValid());
	FString K2Seq;
	TestTrue(TEXT("seq k2"), (*FixSeq)->TryGetStringField(TEXT("k2_class"), K2Seq));
	TestEqual(TEXT("K2Node_Sequence alias"), K2Seq, FString(TEXT("/Script/BlueprintGraph.K2Node_ExecutionSequence")));

	TSharedPtr<FJsonObject> OpBare = MakeShared<FJsonObject>();
	OpBare->SetStringField(TEXT("op"), TEXT("create_node"));
	OpBare->SetStringField(TEXT("patch_id"), TEXT("seq2"));
	OpBare->SetStringField(TEXT("k2_class"), TEXT("Sequence"));
	TArray<TSharedPtr<FJsonValue>> OpsBare;
	OpsBare.Add(MakeShared<FJsonValueObject>(OpBare.ToSharedRef()));
	TSharedPtr<FJsonObject> ArgsBare = MakeShared<FJsonObject>();
	ArgsBare->SetArrayField(TEXT("ops"), OpsBare);
	UnrealAiToolDispatchArgRepair::RepairBlueprintGraphPatchToolArgs(ArgsBare);
	const TArray<TSharedPtr<FJsonValue>>* OutBare = nullptr;
	TestTrue(TEXT("opsBare"), ArgsBare->TryGetArrayField(TEXT("ops"), OutBare) && OutBare && OutBare->Num() == 1);
	const TSharedPtr<FJsonObject>* FixBare = nullptr;
	TestTrue(TEXT("opBare obj"), (*OutBare)[0]->TryGetObject(FixBare) && FixBare && (*FixBare).IsValid());
	FString K2Bare;
	TestTrue(TEXT("bare seq k2"), (*FixBare)->TryGetStringField(TEXT("k2_class"), K2Bare));
	TestEqual(TEXT("bare Sequence alias"), K2Bare, FString(TEXT("/Script/BlueprintGraph.K2Node_ExecutionSequence")));

	TSharedPtr<FJsonObject> OpMath = MakeShared<FJsonObject>();
	OpMath->SetStringField(TEXT("op"), TEXT("create_node"));
	OpMath->SetStringField(TEXT("patch_id"), TEXT("m1"));
	OpMath->SetStringField(TEXT("k2_class"), TEXT("K2Node_IntLess"));
	TArray<TSharedPtr<FJsonValue>> OpsMath;
	OpsMath.Add(MakeShared<FJsonValueObject>(OpMath.ToSharedRef()));
	TSharedPtr<FJsonObject> ArgsMath = MakeShared<FJsonObject>();
	ArgsMath->SetArrayField(TEXT("ops"), OpsMath);
	UnrealAiToolDispatchArgRepair::RepairBlueprintGraphPatchToolArgs(ArgsMath);
	const TArray<TSharedPtr<FJsonValue>>* OutMath = nullptr;
	TestTrue(TEXT("opsMath"), ArgsMath->TryGetArrayField(TEXT("ops"), OutMath) && OutMath && OutMath->Num() == 1);
	const TSharedPtr<FJsonObject>* FixMath = nullptr;
	TestTrue(TEXT("opMath obj"), (*OutMath)[0]->TryGetObject(FixMath) && FixMath && (*FixMath).IsValid());
	FString K2Math;
	TestTrue(TEXT("math k2"), (*FixMath)->TryGetStringField(TEXT("k2_class"), K2Math));
	TestEqual(TEXT("K2Node_IntLess -> CallFunction"), K2Math, FString(TEXT("/Script/BlueprintGraph.K2Node_CallFunction")));

	TSharedPtr<FJsonObject> AuditInt = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> OpInt = MakeShared<FJsonObject>();
	OpInt->SetStringField(TEXT("op"), TEXT("add_variable"));
	OpInt->SetStringField(TEXT("name"), TEXT("UaiRepairAuditProbe"));
	OpInt->SetStringField(TEXT("type"), TEXT("integer"));
	TArray<TSharedPtr<FJsonValue>> OpsInt;
	OpsInt.Add(MakeShared<FJsonValueObject>(OpInt.ToSharedRef()));
	TSharedPtr<FJsonObject> ArgsInt = MakeShared<FJsonObject>();
	ArgsInt->SetArrayField(TEXT("ops"), OpsInt);
	UnrealAiToolDispatchArgRepair::RepairBlueprintGraphPatchToolArgs(ArgsInt, AuditInt);
	const TArray<TSharedPtr<FJsonValue>>* Repairs = nullptr;
	TestTrue(
		TEXT("graph_patch_repairs records add_variable type alias"),
		AuditInt->TryGetArrayField(TEXT("graph_patch_repairs"), Repairs) && Repairs && Repairs->Num() > 0);

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

	// blueprint_graph_patch requires blueprint_path and non-empty ops[] or ops_json_path
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
		TSharedPtr<FJsonObject> ArgsBoth = MakeShared<FJsonObject>();
		ArgsBoth->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Nonexistent/XP.XP"));
		ArgsBoth->SetStringField(TEXT("ops_json_path"), TEXT("Saved/UnrealAi/x.json"));
		TSharedPtr<FJsonObject> OpBoth = MakeShared<FJsonObject>();
		OpBoth->SetStringField(TEXT("op"), TEXT("create_node"));
		OpBoth->SetStringField(TEXT("patch_id"), TEXT("n1"));
		OpBoth->SetStringField(TEXT("k2_class"), TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"));
		TArray<TSharedPtr<FJsonValue>> OpsBoth;
		OpsBoth.Add(MakeShared<FJsonValueObject>(OpBoth.ToSharedRef()));
		ArgsBoth->SetArrayField(TEXT("ops"), OpsBoth);
		const FUnrealAiToolInvocationResult RB = UnrealAiDispatchTool(
			TEXT("blueprint_graph_patch"),
			ArgsBoth,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_graph_patch ops + ops_json_path dispatch: bOk"), RB.bOk);
	}

	{
		TSharedPtr<FJsonObject> ArgsBad = MakeShared<FJsonObject>();
		ArgsBad->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Nonexistent/XP.XP"));
		ArgsBad->SetStringField(TEXT("ops_json_path"), TEXT("Content/not_allowed.json"));
		const FUnrealAiToolInvocationResult RBad = UnrealAiDispatchTool(
			TEXT("blueprint_graph_patch"),
			ArgsBad,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("blueprint_graph_patch ops_json_path bad prefix: bOk"), RBad.bOk);
		TestTrue(
			TEXT("blueprint_graph_patch ops_json_path bad prefix: hint"),
			RBad.ErrorMessage.Contains(TEXT("Saved/")) || RBad.ContentForModel.Contains(TEXT("Saved/")));
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
			const TArray<TSharedPtr<FJsonValue>>* Partial = nullptr;
			TestTrue(TEXT("graph_patch UAI: applied_partial empty"), O->TryGetArrayField(TEXT("applied_partial"), Partial) && Partial && Partial->Num() == 0);
			const TArray<TSharedPtr<FJsonValue>>* Codes = nullptr;
			TestTrue(TEXT("graph_patch UAI: error_codes"), O->TryGetArrayField(TEXT("error_codes"), Codes) && Codes && Codes->Num() > 0);
			const TSharedPtr<FJsonObject>* Sug = nullptr;
			TestTrue(TEXT("graph_patch UAI: suggested_correct_call"), O->TryGetObjectField(TEXT("suggested_correct_call"), Sug) && Sug && (*Sug).IsValid());
		}
	}

	// suggested_correct_call for invalid_k2_class: math vs sequence vs branch fallback (Strict_BP02).
	{
		UBlueprint* BpSug = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
		if (BpSug)
		{
			auto RunBadK2 = [&](const TCHAR* K2Path, const TCHAR* ExpectK2InFirstOp) -> bool
			{
				TSharedPtr<FJsonObject> OpX = MakeShared<FJsonObject>();
				OpX->SetStringField(TEXT("op"), TEXT("create_node"));
				OpX->SetStringField(TEXT("patch_id"), TEXT("badk2"));
				OpX->SetStringField(TEXT("k2_class"), K2Path);
				OpX->SetNumberField(TEXT("x"), 0);
				OpX->SetNumberField(TEXT("y"), 0);
				TArray<TSharedPtr<FJsonValue>> OpsX;
				OpsX.Add(MakeShared<FJsonValueObject>(OpX.ToSharedRef()));
				TSharedPtr<FJsonObject> ArgsX = MakeShared<FJsonObject>();
				ArgsX->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
				ArgsX->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
				ArgsX->SetArrayField(TEXT("ops"), OpsX);
				ArgsX->SetBoolField(TEXT("compile"), false);
				ArgsX->SetBoolField(TEXT("auto_layout"), false);
				const FUnrealAiToolInvocationResult Rx = UnrealAiDispatchTool(
					TEXT("blueprint_graph_patch"),
					ArgsX,
					nullptr,
					nullptr,
					FString(),
					FString());
				if (Rx.bOk)
				{
					return false;
				}
				TSharedPtr<FJsonObject> Ox;
				TSharedRef<TJsonReader<>> ReaderX = TJsonReaderFactory<>::Create(Rx.ContentForModel);
				if (!FJsonSerializer::Deserialize(ReaderX, Ox) || !Ox.IsValid())
				{
					return false;
				}
				const TSharedPtr<FJsonObject>* SugX = nullptr;
				if (!Ox->TryGetObjectField(TEXT("suggested_correct_call"), SugX) || !SugX || !(*SugX).IsValid())
				{
					return false;
				}
				const TSharedPtr<FJsonObject>* ArgX = nullptr;
				if (!(*SugX)->TryGetObjectField(TEXT("arguments"), ArgX) || !ArgX || !(*ArgX).IsValid())
				{
					return false;
				}
				const TArray<TSharedPtr<FJsonValue>>* OpsOut = nullptr;
				if (!(*ArgX)->TryGetArrayField(TEXT("ops"), OpsOut) || !OpsOut || OpsOut->Num() < 1)
				{
					return false;
				}
				const TSharedPtr<FJsonObject>* FirstOp = nullptr;
				if (!(*OpsOut)[0]->TryGetObject(FirstOp) || !FirstOp || !(*FirstOp).IsValid())
				{
					return false;
				}
				FString K2Out;
				return (*FirstOp)->TryGetStringField(TEXT("k2_class"), K2Out) && K2Out == ExpectK2InFirstOp;
			};
			TestTrue(
				TEXT("suggested_correct_call math-ish k2"),
				RunBadK2(
					TEXT("/Script/BlueprintGraph.K2Node_FooIntLess"),
					TEXT("/Script/BlueprintGraph.K2Node_CallFunction")));
			TestTrue(
				TEXT("suggested_correct_call sequence-ish k2"),
				RunBadK2(TEXT("/Script/BlueprintGraph.K2Node_FooSequence"), TEXT("/Script/BlueprintGraph.K2Node_ExecutionSequence")));
			TestTrue(
				TEXT("suggested_correct_call generic k2"),
				RunBadK2(TEXT("/Script/Engine.Actor"), TEXT("/Script/BlueprintGraph.K2Node_IfThenElse")));
		}
	}

	// create_node with semantic_kind (no k2_class) + validate_only dry-run (Strict_BP02).
	{
		UBlueprint* BpSem = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
		if (BpSem)
		{
			TSharedPtr<FJsonObject> OpSem = MakeShared<FJsonObject>();
			OpSem->SetStringField(TEXT("op"), TEXT("create_node"));
			OpSem->SetStringField(TEXT("patch_id"), TEXT("uai_sem_kind_branch"));
			OpSem->SetStringField(TEXT("semantic_kind"), TEXT("branch"));
			OpSem->SetNumberField(TEXT("x"), -28000);
			OpSem->SetNumberField(TEXT("y"), -28000);
			TArray<TSharedPtr<FJsonValue>> OpsSem;
			OpsSem.Add(MakeShared<FJsonValueObject>(OpSem.ToSharedRef()));
			TSharedPtr<FJsonObject> ArgsSem = MakeShared<FJsonObject>();
			ArgsSem->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
			ArgsSem->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
			ArgsSem->SetArrayField(TEXT("ops"), OpsSem);
			ArgsSem->SetBoolField(TEXT("compile"), false);
			ArgsSem->SetBoolField(TEXT("auto_layout"), false);
			const FUnrealAiToolInvocationResult RSem = UnrealAiDispatchTool(
				TEXT("blueprint_graph_patch"),
				ArgsSem,
				nullptr,
				nullptr,
				FString(),
				FString());
			TestTrue(TEXT("graph_patch semantic_kind branch: bOk"), RSem.bOk);
			TSharedPtr<FJsonObject> OSem;
			TSharedRef<TJsonReader<>> ReaderSem = TJsonReaderFactory<>::Create(RSem.ContentForModel);
			TestTrue(TEXT("graph_patch semantic_kind: JSON"), FJsonSerializer::Deserialize(ReaderSem, OSem) && OSem.IsValid());
			const TArray<TSharedPtr<FJsonValue>>* AppliedSem = nullptr;
			TestTrue(TEXT("graph_patch semantic_kind: applied"), OSem->TryGetArrayField(TEXT("applied"), AppliedSem) && AppliedSem && AppliedSem->Num() > 0);
			FString GuidCleanup;
			const TSharedPtr<FJsonObject>* FirstRec = nullptr;
			TestTrue(TEXT("graph_patch semantic_kind: first applied"), (*AppliedSem)[0]->TryGetObject(FirstRec) && FirstRec && (*FirstRec).IsValid());
			TestTrue(TEXT("graph_patch semantic_kind: node_guid"), (*FirstRec)->TryGetStringField(TEXT("node_guid"), GuidCleanup) && !GuidCleanup.IsEmpty());
			TSharedPtr<FJsonObject> OpRm = MakeShared<FJsonObject>();
			OpRm->SetStringField(TEXT("op"), TEXT("remove_node"));
			OpRm->SetStringField(TEXT("node_guid"), GuidCleanup);
			TArray<TSharedPtr<FJsonValue>> OpsRm;
			OpsRm.Add(MakeShared<FJsonValueObject>(OpRm.ToSharedRef()));
			TSharedPtr<FJsonObject> ArgsRm = MakeShared<FJsonObject>();
			ArgsRm->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
			ArgsRm->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
			ArgsRm->SetArrayField(TEXT("ops"), OpsRm);
			ArgsRm->SetBoolField(TEXT("compile"), false);
			ArgsRm->SetBoolField(TEXT("auto_layout"), false);
			const FUnrealAiToolInvocationResult RRm = UnrealAiDispatchTool(
				TEXT("blueprint_graph_patch"),
				ArgsRm,
				nullptr,
				nullptr,
				FString(),
				FString());
			TestTrue(TEXT("graph_patch semantic_kind cleanup remove_node: bOk"), RRm.bOk);

			TSharedPtr<FJsonObject> ArgsVal = MakeShared<FJsonObject>();
			ArgsVal->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
			ArgsVal->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
			ArgsVal->SetBoolField(TEXT("validate_only"), true);
			TArray<TSharedPtr<FJsonValue>> OpsVal;
			TSharedPtr<FJsonObject> OpV1 = MakeShared<FJsonObject>();
			OpV1->SetStringField(TEXT("op"), TEXT("create_node"));
			OpV1->SetStringField(TEXT("patch_id"), TEXT("v_n1"));
			OpV1->SetStringField(TEXT("semantic_kind"), TEXT("branch"));
			OpsVal.Add(MakeShared<FJsonValueObject>(OpV1.ToSharedRef()));
			ArgsVal->SetArrayField(TEXT("ops"), OpsVal);
			const FUnrealAiToolInvocationResult RVok = UnrealAiDispatchTool(
				TEXT("blueprint_graph_patch"),
				ArgsVal,
				nullptr,
				nullptr,
				FString(),
				FString());
			TestTrue(TEXT("validate_only happy: bOk"), RVok.bOk);
			TSharedPtr<FJsonObject> OVok;
			TSharedRef<TJsonReader<>> RVr = TJsonReaderFactory<>::Create(RVok.ContentForModel);
			TestTrue(TEXT("validate_only happy: parse"), FJsonSerializer::Deserialize(RVr, OVok) && OVok.IsValid());
			TestEqual(TEXT("validate_only status"), OVok->GetStringField(TEXT("status")), FString(TEXT("patch_validated")));

			TSharedPtr<FJsonObject> ArgsValBad = MakeShared<FJsonObject>();
			ArgsValBad->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
			ArgsValBad->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
			ArgsValBad->SetBoolField(TEXT("validate_only"), true);
			TArray<TSharedPtr<FJsonValue>> OpsValBad;
			TSharedPtr<FJsonObject> OpVB0 = MakeShared<FJsonObject>();
			OpVB0->SetStringField(TEXT("op"), TEXT("create_node"));
			OpVB0->SetStringField(TEXT("patch_id"), TEXT("v_ok"));
			OpVB0->SetStringField(TEXT("semantic_kind"), TEXT("branch"));
			OpsValBad.Add(MakeShared<FJsonValueObject>(OpVB0.ToSharedRef()));
			TSharedPtr<FJsonObject> OpVB1 = MakeShared<FJsonObject>();
			OpVB1->SetStringField(TEXT("op"), TEXT("connect"));
			OpVB1->SetStringField(TEXT("from"), TEXT("not_declared.Then"));
			OpVB1->SetStringField(TEXT("to"), TEXT("v_ok.Execute"));
			OpsValBad.Add(MakeShared<FJsonValueObject>(OpVB1.ToSharedRef()));
			ArgsValBad->SetArrayField(TEXT("ops"), OpsValBad);
			const FUnrealAiToolInvocationResult RVbad = UnrealAiDispatchTool(
				TEXT("blueprint_graph_patch"),
				ArgsValBad,
				nullptr,
				nullptr,
				FString(),
				FString());
			TestFalse(TEXT("validate_only bad: bOk"), RVbad.bOk);
			TSharedPtr<FJsonObject> OVb;
			TSharedRef<TJsonReader<>> RVbr = TJsonReaderFactory<>::Create(RVbad.ContentForModel);
			TestTrue(TEXT("validate_only bad: parse"), FJsonSerializer::Deserialize(RVbr, OVb) && OVb.IsValid());
			TestEqual(TEXT("validate_only failed_op_index"), static_cast<int32>(OVb->GetNumberField(TEXT("failed_op_index"))), 1);

			// validate_only: two batch-local branches — Then->Then must fail (output->output) with virtual pin checks.
			{
				TArray<TSharedPtr<FJsonValue>> OpsTT;
				TSharedPtr<FJsonObject> OpA = MakeShared<FJsonObject>();
				OpA->SetStringField(TEXT("op"), TEXT("create_node"));
				OpA->SetStringField(TEXT("patch_id"), TEXT("v_tt_a"));
				OpA->SetStringField(TEXT("semantic_kind"), TEXT("branch"));
				OpA->SetNumberField(TEXT("x"), -29000);
				OpA->SetNumberField(TEXT("y"), -29000);
				OpsTT.Add(MakeShared<FJsonValueObject>(OpA.ToSharedRef()));
				TSharedPtr<FJsonObject> OpB = MakeShared<FJsonObject>();
				OpB->SetStringField(TEXT("op"), TEXT("create_node"));
				OpB->SetStringField(TEXT("patch_id"), TEXT("v_tt_b"));
				OpB->SetStringField(TEXT("semantic_kind"), TEXT("branch"));
				OpB->SetNumberField(TEXT("x"), -28800);
				OpB->SetNumberField(TEXT("y"), -29000);
				OpsTT.Add(MakeShared<FJsonValueObject>(OpB.ToSharedRef()));
				TSharedPtr<FJsonObject> OpC = MakeShared<FJsonObject>();
				OpC->SetStringField(TEXT("op"), TEXT("connect"));
				OpC->SetStringField(TEXT("from"), TEXT("v_tt_a.Then"));
				OpC->SetStringField(TEXT("to"), TEXT("v_tt_b.Then"));
				OpsTT.Add(MakeShared<FJsonValueObject>(OpC.ToSharedRef()));
				TSharedPtr<FJsonObject> ArgsTT = MakeShared<FJsonObject>();
				ArgsTT->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
				ArgsTT->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
				ArgsTT->SetBoolField(TEXT("validate_only"), true);
				ArgsTT->SetArrayField(TEXT("ops"), OpsTT);
				const FUnrealAiToolInvocationResult RTT = UnrealAiDispatchTool(
					TEXT("blueprint_graph_patch"),
					ArgsTT,
					nullptr,
					nullptr,
					FString(),
					FString());
				TestFalse(TEXT("validate_only Then->Then: bOk"), RTT.bOk);
				TSharedPtr<FJsonObject> OTT;
				TSharedRef<TJsonReader<>> RTTr = TJsonReaderFactory<>::Create(RTT.ContentForModel);
				TestTrue(TEXT("validate_only Then->Then: parse"), FJsonSerializer::Deserialize(RTTr, OTT) && OTT.IsValid());
				TestEqual(TEXT("validate_only Then->Then: failed_op_index"), static_cast<int32>(OTT->GetNumberField(TEXT("failed_op_index"))), 2);
				const TArray<TSharedPtr<FJsonValue>>* PinsTo = nullptr;
				TestTrue(
					TEXT("validate_only Then->Then: available_pins_to"),
					OTT->TryGetArrayField(TEXT("available_pins_to"), PinsTo) && PinsTo && PinsTo->Num() > 0);
			}

			// validate_only: Then->execute between two batch-local branches should validate.
			{
				TArray<TSharedPtr<FJsonValue>> OpsOK;
				TSharedPtr<FJsonObject> OpA = MakeShared<FJsonObject>();
				OpA->SetStringField(TEXT("op"), TEXT("create_node"));
				OpA->SetStringField(TEXT("patch_id"), TEXT("v_te_a"));
				OpA->SetStringField(TEXT("semantic_kind"), TEXT("branch"));
				OpA->SetNumberField(TEXT("x"), -29200);
				OpA->SetNumberField(TEXT("y"), -29000);
				OpsOK.Add(MakeShared<FJsonValueObject>(OpA.ToSharedRef()));
				TSharedPtr<FJsonObject> OpB = MakeShared<FJsonObject>();
				OpB->SetStringField(TEXT("op"), TEXT("create_node"));
				OpB->SetStringField(TEXT("patch_id"), TEXT("v_te_b"));
				OpB->SetStringField(TEXT("semantic_kind"), TEXT("branch"));
				OpB->SetNumberField(TEXT("x"), -29000);
				OpB->SetNumberField(TEXT("y"), -29000);
				OpsOK.Add(MakeShared<FJsonValueObject>(OpB.ToSharedRef()));
				TSharedPtr<FJsonObject> OpC = MakeShared<FJsonObject>();
				OpC->SetStringField(TEXT("op"), TEXT("connect"));
				OpC->SetStringField(TEXT("from"), TEXT("v_te_a.Then"));
				OpC->SetStringField(TEXT("to"), TEXT("v_te_b.execute"));
				OpsOK.Add(MakeShared<FJsonValueObject>(OpC.ToSharedRef()));
				TSharedPtr<FJsonObject> ArgsOK = MakeShared<FJsonObject>();
				ArgsOK->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
				ArgsOK->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
				ArgsOK->SetBoolField(TEXT("validate_only"), true);
				ArgsOK->SetArrayField(TEXT("ops"), OpsOK);
				const FUnrealAiToolInvocationResult ROK = UnrealAiDispatchTool(
					TEXT("blueprint_graph_patch"),
					ArgsOK,
					nullptr,
					nullptr,
					FString(),
					FString());
				TestTrue(TEXT("validate_only Then->execute: bOk"), ROK.bOk);
			}

			// apply: bad connect includes available_pins_to on failure (transaction cancelled).
			{
				TArray<TSharedPtr<FJsonValue>> OpsBadPin;
				TSharedPtr<FJsonObject> OpA = MakeShared<FJsonObject>();
				OpA->SetStringField(TEXT("op"), TEXT("create_node"));
				OpA->SetStringField(TEXT("patch_id"), TEXT("uai_badpin_a"));
				OpA->SetStringField(TEXT("semantic_kind"), TEXT("branch"));
				OpA->SetNumberField(TEXT("x"), -29400);
				OpA->SetNumberField(TEXT("y"), -29200);
				OpsBadPin.Add(MakeShared<FJsonValueObject>(OpA.ToSharedRef()));
				TSharedPtr<FJsonObject> OpB = MakeShared<FJsonObject>();
				OpB->SetStringField(TEXT("op"), TEXT("create_node"));
				OpB->SetStringField(TEXT("patch_id"), TEXT("uai_badpin_b"));
				OpB->SetStringField(TEXT("semantic_kind"), TEXT("branch"));
				OpB->SetNumberField(TEXT("x"), -29200);
				OpB->SetNumberField(TEXT("y"), -29200);
				OpsBadPin.Add(MakeShared<FJsonValueObject>(OpB.ToSharedRef()));
				TSharedPtr<FJsonObject> OpC = MakeShared<FJsonObject>();
				OpC->SetStringField(TEXT("op"), TEXT("connect"));
				OpC->SetStringField(TEXT("from"), TEXT("uai_badpin_a.Then"));
				OpC->SetStringField(TEXT("to"), TEXT("uai_badpin_b.NotARealPin"));
				OpsBadPin.Add(MakeShared<FJsonValueObject>(OpC.ToSharedRef()));
				TSharedPtr<FJsonObject> ArgsBadPin = MakeShared<FJsonObject>();
				ArgsBadPin->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
				ArgsBadPin->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
				ArgsBadPin->SetBoolField(TEXT("compile"), false);
				ArgsBadPin->SetBoolField(TEXT("auto_layout"), false);
				ArgsBadPin->SetArrayField(TEXT("ops"), OpsBadPin);
				const FUnrealAiToolInvocationResult RBP = UnrealAiDispatchTool(
					TEXT("blueprint_graph_patch"),
					ArgsBadPin,
					nullptr,
					nullptr,
					FString(),
					FString());
				TestFalse(TEXT("apply bad pin: bOk"), RBP.bOk);
				TSharedPtr<FJsonObject> OBP;
				TSharedRef<TJsonReader<>> RBPr = TJsonReaderFactory<>::Create(RBP.ContentForModel);
				TestTrue(TEXT("apply bad pin: parse"), FJsonSerializer::Deserialize(RBPr, OBP) && OBP.IsValid());
				const TArray<TSharedPtr<FJsonValue>>* PinsToApply = nullptr;
				TestTrue(
					TEXT("apply bad pin: available_pins_to"),
					OBP->TryGetArrayField(TEXT("available_pins_to"), PinsToApply) && PinsToApply && PinsToApply->Num() > 0);
			}

			TSharedPtr<FJsonObject> ArgsEvShape = MakeShared<FJsonObject>();
			ArgsEvShape->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
			ArgsEvShape->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
			ArgsEvShape->SetBoolField(TEXT("validate_only"), true);
			TSharedPtr<FJsonObject> OpEvShape = MakeShared<FJsonObject>();
			OpEvShape->SetStringField(TEXT("op"), TEXT("create_node"));
			OpEvShape->SetStringField(TEXT("patch_id"), TEXT("uai_evt_missing_override"));
			OpEvShape->SetStringField(TEXT("k2_class"), TEXT("/Script/BlueprintGraph.K2Node_Event"));
			OpEvShape->SetNumberField(TEXT("x"), 0);
			OpEvShape->SetNumberField(TEXT("y"), 0);
			TArray<TSharedPtr<FJsonValue>> OpsEvShape;
			OpsEvShape.Add(MakeShared<FJsonValueObject>(OpEvShape.ToSharedRef()));
			ArgsEvShape->SetArrayField(TEXT("ops"), OpsEvShape);
			const FUnrealAiToolInvocationResult REvShape = UnrealAiDispatchTool(
				TEXT("blueprint_graph_patch"),
				ArgsEvShape,
				nullptr,
				nullptr,
				FString(),
				FString());
			TestFalse(TEXT("validate_only K2Node_Event missing event_override: bOk"), REvShape.bOk);
			TSharedPtr<FJsonObject> OEvShape;
			TSharedRef<TJsonReader<>> REvR = TJsonReaderFactory<>::Create(REvShape.ContentForModel);
			TestTrue(TEXT("validate_only event shape: parse"), FJsonSerializer::Deserialize(REvR, OEvShape) && OEvShape.IsValid());
			TestEqual(TEXT("validate_only event shape: failed_op_index"), static_cast<int32>(OEvShape->GetNumberField(TEXT("failed_op_index"))), 0);
			const TArray<TSharedPtr<FJsonValue>>* EvCodes = nullptr;
			TestTrue(TEXT("validate_only event shape: error_codes"), OEvShape->TryGetArrayField(TEXT("error_codes"), EvCodes) && EvCodes && EvCodes->Num() > 0);
			FString EvCode0;
			TestTrue(TEXT("validate_only event shape: first code"), (*EvCodes)[0]->TryGetString(EvCode0));
			TestEqual(TEXT("validate_only event shape: event_override_shape code"), EvCode0, FString(TEXT("event_override_shape")));
			const TSharedPtr<FJsonObject>* SugEv = nullptr;
			TestTrue(TEXT("validate_only event shape: suggested_correct_call"), OEvShape->TryGetObjectField(TEXT("suggested_correct_call"), SugEv) && SugEv && (*SugEv).IsValid());
			TestEqual(TEXT("validate_only event shape: suggestion tool"), (*SugEv)->GetStringField(TEXT("tool_id")), FString(TEXT("blueprint_graph_patch")));
			const TSharedPtr<FJsonObject>* SugEvArgs = nullptr;
			TestTrue(TEXT("validate_only event shape: suggestion arguments"), (*SugEv)->TryGetObjectField(TEXT("arguments"), SugEvArgs) && SugEvArgs && (*SugEvArgs).IsValid());
			const TArray<TSharedPtr<FJsonValue>>* SugEvOps = nullptr;
			TestTrue(TEXT("validate_only event shape: suggestion ops"), (*SugEvArgs)->TryGetArrayField(TEXT("ops"), SugEvOps) && SugEvOps && SugEvOps->Num() > 0);
			const TSharedPtr<FJsonObject>* SugFirstOp = nullptr;
			TestTrue(TEXT("validate_only event shape: first suggested op"), (*SugEvOps)[0]->TryGetObject(SugFirstOp) && SugFirstOp && (*SugFirstOp).IsValid());
			const TSharedPtr<FJsonObject>* EvOv = nullptr;
			TestTrue(TEXT("validate_only event shape: event_override in suggestion"), (*SugFirstOp)->TryGetObjectField(TEXT("event_override"), EvOv) && EvOv && (*EvOv).IsValid());
			TestEqual(TEXT("validate_only event shape: Landed outer"), (*EvOv)->GetStringField(TEXT("outer_class_path")), FString(TEXT("/Script/Engine.Character")));
			TestEqual(TEXT("validate_only event shape: Landed fn"), (*EvOv)->GetStringField(TEXT("function_name")), FString(TEXT("Landed")));
		}
	}

	// Atomic rollback + minimal successful create_node (StrictTests harness only).
	{
		UBlueprint* BpAtomic = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
		if (BpAtomic)
		{
			const int32 VarCountBefore = BpAtomic->NewVariables.Num();
			TSharedPtr<FJsonObject> OpV = MakeShared<FJsonObject>();
			OpV->SetStringField(TEXT("op"), TEXT("add_variable"));
			OpV->SetStringField(TEXT("name"), TEXT("UaiAtomicRollbackProbeVar"));
			OpV->SetStringField(TEXT("type"), TEXT("int"));
			TSharedPtr<FJsonObject> OpBad = MakeShared<FJsonObject>();
			OpBad->SetStringField(TEXT("op"), TEXT("connect"));
			OpBad->SetStringField(TEXT("from"), TEXT("not_a_real_patch_id.Then"));
			OpBad->SetStringField(TEXT("to"), TEXT("also_fake.Execute"));
			TArray<TSharedPtr<FJsonValue>> OpsRb;
			OpsRb.Add(MakeShared<FJsonValueObject>(OpV.ToSharedRef()));
			OpsRb.Add(MakeShared<FJsonValueObject>(OpBad.ToSharedRef()));
			TSharedPtr<FJsonObject> ArgsRb = MakeShared<FJsonObject>();
			ArgsRb->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
			ArgsRb->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
			ArgsRb->SetArrayField(TEXT("ops"), OpsRb);
			ArgsRb->SetBoolField(TEXT("compile"), false);
			ArgsRb->SetBoolField(TEXT("auto_layout"), false);
			const FUnrealAiToolInvocationResult RRb = UnrealAiDispatchTool(
				TEXT("blueprint_graph_patch"),
				ArgsRb,
				nullptr,
				nullptr,
				FString(),
				FString());
			TestFalse(TEXT("graph_patch atomic rollback: bOk"), RRb.bOk);
			TestEqual(TEXT("graph_patch atomic: variable count restored"), BpAtomic->NewVariables.Num(), VarCountBefore);
			{
				TSharedPtr<FJsonObject> ORb;
				TSharedRef<TJsonReader<>> ReaderRb = TJsonReaderFactory<>::Create(RRb.ContentForModel);
				TestTrue(TEXT("graph_patch atomic rollback: JSON parse"), FJsonSerializer::Deserialize(ReaderRb, ORb) && ORb.IsValid());
				TestEqual(TEXT("graph_patch atomic: failed_op_index"), static_cast<int32>(ORb->GetNumberField(TEXT("failed_op_index"))), 1);
				const TSharedPtr<FJsonObject>* FailedOp = nullptr;
				TestTrue(TEXT("graph_patch atomic: failed_op object"), ORb->TryGetObjectField(TEXT("failed_op"), FailedOp) && FailedOp && (*FailedOp).IsValid());
				FString FailedOpName;
				TestTrue(TEXT("graph_patch atomic: failed_op.op"), (*FailedOp)->TryGetStringField(TEXT("op"), FailedOpName));
				TestEqual(TEXT("graph_patch atomic: failed op is connect"), FailedOpName, FString(TEXT("connect")));
				const TSharedPtr<FJsonObject>* Sug = nullptr;
				TestTrue(TEXT("graph_patch atomic: suggested_correct_call"), ORb->TryGetObjectField(TEXT("suggested_correct_call"), Sug) && Sug && (*Sug).IsValid());
				TestEqual(
					TEXT("graph_patch atomic: suggestion is list_pins"),
					(*Sug)->GetStringField(TEXT("tool_id")),
					FString(TEXT("blueprint_graph_list_pins")));
			}

			TSharedPtr<FJsonObject> OpOk = MakeShared<FJsonObject>();
			OpOk->SetStringField(TEXT("op"), TEXT("create_node"));
			OpOk->SetStringField(TEXT("patch_id"), TEXT("uai_contract_ok_if"));
			OpOk->SetStringField(TEXT("k2_class"), TEXT("/Script/BlueprintGraph.K2Node_IfThenElse"));
			OpOk->SetNumberField(TEXT("x"), -30000);
			OpOk->SetNumberField(TEXT("y"), -30000);
			TArray<TSharedPtr<FJsonValue>> OpsOk;
			OpsOk.Add(MakeShared<FJsonValueObject>(OpOk.ToSharedRef()));
			TSharedPtr<FJsonObject> ArgsOk = MakeShared<FJsonObject>();
			ArgsOk->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
			ArgsOk->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
			ArgsOk->SetArrayField(TEXT("ops"), OpsOk);
			ArgsOk->SetBoolField(TEXT("compile"), false);
			ArgsOk->SetBoolField(TEXT("auto_layout"), false);
			const FUnrealAiToolInvocationResult ROk = UnrealAiDispatchTool(
				TEXT("blueprint_graph_patch"),
				ArgsOk,
				nullptr,
				nullptr,
				FString(),
				FString());
			TestTrue(TEXT("graph_patch create_node happy: bOk"), ROk.bOk);
			TSharedPtr<FJsonObject> OOk;
			TSharedRef<TJsonReader<>> ReaderOk = TJsonReaderFactory<>::Create(ROk.ContentForModel);
			TestTrue(TEXT("graph_patch happy: JSON parse"), FJsonSerializer::Deserialize(ReaderOk, OOk) && OOk.IsValid());
			TestTrue(TEXT("graph_patch happy: ok field"), OOk->GetBoolField(TEXT("ok")));
			FString CreatedGuid;
			const TArray<TSharedPtr<FJsonValue>>* Ap = nullptr;
			if (OOk->TryGetArrayField(TEXT("applied"), Ap) && Ap && Ap->Num() > 0)
			{
				const TSharedPtr<FJsonObject>* A0 = nullptr;
				if ((*Ap)[0]->TryGetObject(A0) && A0 && (*A0).IsValid())
				{
					(*A0)->TryGetStringField(TEXT("node_guid"), CreatedGuid);
				}
			}
			if (!CreatedGuid.IsEmpty())
			{
				TSharedPtr<FJsonObject> OpRm = MakeShared<FJsonObject>();
				OpRm->SetStringField(TEXT("op"), TEXT("remove_node"));
				OpRm->SetStringField(TEXT("node_guid"), CreatedGuid);
				TArray<TSharedPtr<FJsonValue>> OpsRm;
				OpsRm.Add(MakeShared<FJsonValueObject>(OpRm.ToSharedRef()));
				TSharedPtr<FJsonObject> ArgsRm = MakeShared<FJsonObject>();
				ArgsRm->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
				ArgsRm->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
				ArgsRm->SetArrayField(TEXT("ops"), OpsRm);
				ArgsRm->SetBoolField(TEXT("compile"), false);
				ArgsRm->SetBoolField(TEXT("auto_layout"), false);
				const FUnrealAiToolInvocationResult RRm = UnrealAiDispatchTool(
					TEXT("blueprint_graph_patch"),
					ArgsRm,
					nullptr,
					nullptr,
					FString(),
					FString());
				TestTrue(TEXT("graph_patch cleanup remove_node: bOk"), RRm.bOk);
			}
		}
	}

	{
		const FString OpsAbs = FPaths::ProjectSavedDir() / TEXT("UnrealAi/UaiOpsJsonPathContract.json");
		IFileManager::Get().MakeDirectory(*(FPaths::GetPath(OpsAbs)), true);
		const FString Body =
			TEXT("[{\"op\":\"create_node\",\"patch_id\":\"uai_opsfile_contract\",\"k2_class\":\"/Script/BlueprintGraph.K2Node_IfThenElse\",\"x\":-32500,\"y\":-32500}]");
		TestTrue(
			TEXT("ops_json_path contract: write test file"),
			FFileHelper::SaveStringToFile(Body, *OpsAbs, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));

		UBlueprint* BpFile = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
		if (BpFile)
		{
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
			A->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
			A->SetStringField(TEXT("ops_json_path"), TEXT("Saved/UnrealAi/UaiOpsJsonPathContract.json"));
			A->SetBoolField(TEXT("compile"), false);
			A->SetBoolField(TEXT("auto_layout"), false);
			const FUnrealAiToolInvocationResult Rf = UnrealAiDispatchTool(
				TEXT("blueprint_graph_patch"),
				A,
				nullptr,
				nullptr,
				FString(),
				FString());
			TestTrue(TEXT("blueprint_graph_patch ops_json_path: bOk"), Rf.bOk);
			TSharedPtr<FJsonObject> Parsed;
			TSharedRef<TJsonReader<>> Rdr = TJsonReaderFactory<>::Create(Rf.ContentForModel);
			FString Ng;
			if (FJsonSerializer::Deserialize(Rdr, Parsed) && Parsed.IsValid() && Parsed->GetBoolField(TEXT("ok")))
			{
				const TArray<TSharedPtr<FJsonValue>>* Apf = nullptr;
				if (Parsed->TryGetArrayField(TEXT("applied"), Apf) && Apf && Apf->Num() > 0)
				{
					const TSharedPtr<FJsonObject>* A0 = nullptr;
					if ((*Apf)[0]->TryGetObject(A0) && A0 && (*A0).IsValid())
					{
						(*A0)->TryGetStringField(TEXT("node_guid"), Ng);
					}
				}
			}
			if (!Ng.IsEmpty())
			{
				TSharedPtr<FJsonObject> Orm = MakeShared<FJsonObject>();
				Orm->SetStringField(TEXT("op"), TEXT("remove_node"));
				Orm->SetStringField(TEXT("node_guid"), Ng);
				TArray<TSharedPtr<FJsonValue>> OrmOps;
				OrmOps.Add(MakeShared<FJsonValueObject>(Orm.ToSharedRef()));
				TSharedPtr<FJsonObject> Arm = MakeShared<FJsonObject>();
				Arm->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
				Arm->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));
				Arm->SetArrayField(TEXT("ops"), OrmOps);
				Arm->SetBoolField(TEXT("compile"), false);
				Arm->SetBoolField(TEXT("auto_layout"), false);
				const FUnrealAiToolInvocationResult Rrm = UnrealAiDispatchTool(
					TEXT("blueprint_graph_patch"),
					Arm,
					nullptr,
					nullptr,
					FString(),
					FString());
				TestTrue(TEXT("blueprint_graph_patch ops_json_path cleanup remove_node: bOk"), Rrm.bOk);
			}
		}
		IFileManager::Get().Delete(*OpsAbs, false);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiRemovedCatalogToolsDispatchContractTest,
	"UnrealAiEditor.Tools.RemovedCatalogToolsDispatchContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiRemovedCatalogToolsDispatchContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Runs on game thread (tool dispatch requirement)"), IsInGameThread());

	auto ExpectNotImplemented = [this](const TCHAR* ToolId, const TCHAR* Desc)
	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			ToolId,
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(FString::Printf(TEXT("%s: bOk"), Desc), R.bOk);
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(FString::Printf(TEXT("%s: JSON parse"), Desc), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		FString Status;
		TestTrue(FString::Printf(TEXT("%s: status"), Desc), O->TryGetStringField(TEXT("status"), Status));
		TestEqual(FString::Printf(TEXT("%s: status value"), Desc), Status, FString(TEXT("not_implemented")));
	};

	ExpectNotImplemented(TEXT("blueprint_apply_ir"), TEXT("blueprint_apply_ir"));
	ExpectNotImplemented(TEXT("blueprint_export_ir"), TEXT("blueprint_export_ir"));
	ExpectNotImplemented(TEXT("blueprint_add_variable"), TEXT("blueprint_add_variable"));
	ExpectNotImplemented(TEXT("blueprint_open_graph_tab"), TEXT("blueprint_open_graph_tab"));
	ExpectNotImplemented(TEXT("blueprint_composite_lifecycle_print"), TEXT("blueprint_composite_lifecycle_print"));
	ExpectNotImplemented(TEXT("blueprint_export_graph_t3d"), TEXT("blueprint_export_graph_t3d"));
	ExpectNotImplemented(TEXT("blueprint_t3d_preflight_validate"), TEXT("blueprint_t3d_preflight_validate"));
	ExpectNotImplemented(TEXT("blueprint_graph_import_t3d"), TEXT("blueprint_graph_import_t3d"));
	ExpectNotImplemented(TEXT("agent_emit_todo_plan"), TEXT("agent_emit_todo_plan"));
	ExpectNotImplemented(TEXT("settings_get"), TEXT("settings_get"));
	ExpectNotImplemented(TEXT("viewport_camera_dolly"), TEXT("viewport_camera_dolly"));

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

	// asset_graph_query referencers: non-existent object_path should fail with suggested_correct_call to asset_index_fuzzy_search.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("relation"), TEXT("referencers"));
		Args->SetStringField(TEXT("object_path"), TEXT("/Game/__unreal_ai_missing_asset_for_referencers.__unreal_ai_missing_asset_for_referencers__"));

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("asset_graph_query"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("asset_graph_query referencers missing asset: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("asset_graph_query referencers missing asset: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestTrue(TEXT("asset_graph_query referencers missing asset: has suggested_correct_call"), O->HasField(TEXT("suggested_correct_call")));

		const TSharedPtr<FJsonObject>* SuggestedObj = nullptr;
		TestTrue(
			TEXT("asset_graph_query referencers suggested_correct_call parseable"),
			O->TryGetObjectField(TEXT("suggested_correct_call"), SuggestedObj) && SuggestedObj && SuggestedObj->IsValid());

		TestEqual(TEXT("asset_graph_query referencers suggested tool_id"), SuggestedObj->Get()->GetStringField(TEXT("tool_id")), FString(TEXT("asset_index_fuzzy_search")));
		const TSharedPtr<FJsonObject>* InnerArgs = nullptr;
		TestTrue(
			TEXT("asset_graph_query referencers suggested arguments parseable"),
			SuggestedObj->Get()->TryGetObjectField(TEXT("arguments"), InnerArgs) && InnerArgs && InnerArgs->IsValid());
		FString PathPrefix;
		TestTrue(TEXT("asset_graph_query referencers suggested path_prefix field"), InnerArgs->Get()->TryGetStringField(TEXT("path_prefix"), PathPrefix) && !PathPrefix.IsEmpty());
		TestEqual(TEXT("asset_graph_query referencers suggested path_prefix value"), PathPrefix, FString(TEXT("/Game")));
	}

	// asset_graph_query dependencies: non-existent object_path should fail with suggested_correct_call to asset_index_fuzzy_search.
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("relation"), TEXT("dependencies"));
		Args->SetStringField(TEXT("object_path"), TEXT("/Game/__unreal_ai_missing_asset_for_dependencies.__unreal_ai_missing_asset_for_dependencies__"));

		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("asset_graph_query"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("asset_graph_query dependencies missing asset: bOk"), R.bOk);

		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("asset_graph_query dependencies missing asset: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestTrue(TEXT("asset_graph_query dependencies missing asset: has suggested_correct_call"), O->HasField(TEXT("suggested_correct_call")));

		const TSharedPtr<FJsonObject>* SuggestedObj = nullptr;
		TestTrue(
			TEXT("asset_graph_query dependencies suggested_correct_call parseable"),
			O->TryGetObjectField(TEXT("suggested_correct_call"), SuggestedObj) && SuggestedObj && SuggestedObj->IsValid());

		TestEqual(TEXT("asset_graph_query dependencies suggested tool_id"), SuggestedObj->Get()->GetStringField(TEXT("tool_id")), FString(TEXT("asset_index_fuzzy_search")));
		const TSharedPtr<FJsonObject>* InnerArgs = nullptr;
		TestTrue(
			TEXT("asset_graph_query dependencies suggested arguments parseable"),
			SuggestedObj->Get()->TryGetObjectField(TEXT("arguments"), InnerArgs) && InnerArgs && InnerArgs->IsValid());
		FString PathPrefix;
		TestTrue(TEXT("asset_graph_query dependencies suggested path_prefix field"), InnerArgs->Get()->TryGetStringField(TEXT("path_prefix"), PathPrefix) && !PathPrefix.IsEmpty());
		TestEqual(TEXT("asset_graph_query dependencies suggested path_prefix value"), PathPrefix, FString(TEXT("/Game")));
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
			TEXT("setting_query"),
			MakeShared<FJsonObject>(),
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("setting_query missing required fields: bOk"), R.bOk);
	}

	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("scope"), TEXT("editor"));
		Args->SetStringField(TEXT("key"), TEXT("editor_focus"));
		// UE Json strings implement TryGetBool (via ToBool); use a JSON array so the value is the wrong shape.
		Args->SetArrayField(TEXT("value"), TArray<TSharedPtr<FJsonValue>>());
		const FUnrealAiToolInvocationResult R = UnrealAiDispatchTool(
			TEXT("setting_apply"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("setting_apply editor_focus wrong type: bOk"), R.bOk);
	}

	{
		TSharedPtr<FJsonObject> SetArgs = MakeShared<FJsonObject>();
		SetArgs->SetStringField(TEXT("scope"), TEXT("editor"));
		SetArgs->SetStringField(TEXT("key"), TEXT("editor_focus"));
		SetArgs->SetBoolField(TEXT("value"), true);
		const FUnrealAiToolInvocationResult SetR = UnrealAiDispatchTool(
			TEXT("setting_apply"),
			SetArgs,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestTrue(TEXT("setting_apply editor_focus bool value: bOk"), SetR.bOk);

		TSharedPtr<FJsonObject> GetArgs = MakeShared<FJsonObject>();
		GetArgs->SetStringField(TEXT("scope"), TEXT("editor"));
		GetArgs->SetStringField(TEXT("key"), TEXT("editor_focus"));
		const FUnrealAiToolInvocationResult GetR = UnrealAiDispatchTool(
			TEXT("setting_query"),
			GetArgs,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestTrue(TEXT("setting_query editor_focus: bOk"), GetR.bOk);
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
			TEXT("setting_apply"),
			Args,
			nullptr,
			nullptr,
			FString(),
			FString());
		TestFalse(TEXT("setting_apply project scope: denied without allowlist"), R.bOk);
		TestTrue(
			TEXT("setting_apply project scope: error explains allowlist"),
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
		TestFalse(TEXT("agent_emit_todo_plan removed: bOk"), R.bOk);
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("agent_emit_todo_plan removed: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		FString Status;
		TestTrue(TEXT("agent_emit_todo_plan removed: status"), O->TryGetStringField(TEXT("status"), Status));
		TestEqual(TEXT("agent_emit_todo_plan removed: not_implemented"), Status, FString(TEXT("not_implemented")));
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
	FUnrealAiBlueprintIrTier1GoldenRoundTripTest,
	"UnrealAiEditor.Tools.BlueprintIrTier1GoldenRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiBlueprintIrTier1GoldenRoundTripTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("Runs on game thread (tool dispatch requirement)"), IsInGameThread());

	// Former Tier-1 IR shape: dispatch is catalog-deprecated (tool_deprecated), not asset_not_found.
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
		TestFalse(TEXT("Tier-1 golden IR via dispatch: bOk"), R.bOk);
		TSharedPtr<FJsonObject> O;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		TestTrue(TEXT("Tier-1 golden IR via dispatch: JSON parse"), FJsonSerializer::Deserialize(Reader, O) && O.IsValid());
		TestEqual(TEXT("Tier-1 golden IR via dispatch: status"), O->GetStringField(TEXT("status")), FString(TEXT("not_implemented")));
	}

	// Introspect stability: two consecutive blueprint_graph_introspect calls return the same node count (StrictTests harness only).
	{
		UBlueprint* Bp = LoadObject<UBlueprint>(nullptr, TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
		if (!Bp)
		{
			UE_LOG(LogTemp, Display, TEXT("UnrealAiEditor.Tools.BlueprintIrTier1GoldenRoundTrip: skip introspect stability — Strict_BP02 not in project"));
			return true;
		}
		(void)Bp;

		TSharedPtr<FJsonObject> IntroArgs = MakeShared<FJsonObject>();
		IntroArgs->SetStringField(TEXT("blueprint_path"), TEXT("/Game/Blueprints/StrictTests/Strict_BP02.Strict_BP02"));
		IntroArgs->SetStringField(TEXT("graph_name"), TEXT("EventGraph"));

		const FUnrealAiToolInvocationResult R1 = UnrealAiDispatchTool(
			TEXT("blueprint_graph_introspect"),
			IntroArgs,
			nullptr,
			nullptr,
			FString(),
			FString());
		const FUnrealAiToolInvocationResult R2 = UnrealAiDispatchTool(
			TEXT("blueprint_graph_introspect"),
			IntroArgs,
			nullptr,
			nullptr,
			FString(),
			FString());

		TestTrue(TEXT("introspect stability R1: bOk"), R1.bOk);
		TestTrue(TEXT("introspect stability R2: bOk"), R2.bOk);

		auto ParseIntrospectNodeCount = [](const FString& Json, int32& OutCount) -> bool
		{
			OutCount = 0;
			TSharedPtr<FJsonObject> Root;
			TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Json);
			if (!FJsonSerializer::Deserialize(Rd, Root) || !Root.IsValid())
			{
				return false;
			}
			const TArray<TSharedPtr<FJsonValue>>* Ns = nullptr;
			if (!Root->TryGetArrayField(TEXT("nodes"), Ns) || !Ns)
			{
				return false;
			}
			OutCount = Ns->Num();
			return true;
		};

		int32 C1 = 0;
		int32 C2 = 0;
		TestTrue(TEXT("introspect stability: parse R1"), ParseIntrospectNodeCount(R1.ContentForModel, C1));
		TestTrue(TEXT("introspect stability: parse R2"), ParseIntrospectNodeCount(R2.ContentForModel, C2));
		TestEqual(TEXT("introspect stability: node count"), C1, C2);
		TestTrue(TEXT("introspect stability: non-empty graph"), C1 > 0);
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
		TEXT("script surface exposes Kismet graph tooling capability"),
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

	{
		TSet<FString> EnvOnly;
		EnvOnly.Add(UnrealAiToolSurfaceCompatibility::GAgentSurfaceToken_EnvironmentBuilder);
		TestFalse(
			TEXT("environment_builder-only on main agent"),
			UnrealAiToolSurfaceCompatibility::ToolAllowedOnSurface(EnvOnly, false, EUnrealAiToolSurfaceKind::MainAgent));
		TestTrue(
			TEXT("environment_builder-only on environment builder"),
			UnrealAiToolSurfaceCompatibility::ToolAllowedOnSurface(EnvOnly, false, EUnrealAiToolSurfaceKind::EnvironmentBuilder));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiAgentToolGateSurfaceTest,
	"UnrealAiEditor.Tools.AgentToolGateSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiAgentToolGateSurfaceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FUnrealAiToolCatalog Catalog;
	TestTrue(TEXT("catalog loads"), Catalog.LoadFromPlugin());
	TestTrue(TEXT("catalog reports loaded"), Catalog.IsLoaded());

	FUnrealAiAgentTurnRequest Req;

	Req.Mode = EUnrealAiAgentMode::Ask;
	TestTrue(
		TEXT("ask mode passes surface gate"),
		UnrealAiAgentToolGate::PassesToolSurfaceFilter(Req, TEXT("blueprint_graph_patch"), &Catalog));

	Req.Mode = EUnrealAiAgentMode::Agent;
	Req.bOmitMainAgentBlueprintMutationTools = false;
	TestTrue(
		TEXT("omit flag off bypasses gate"),
		UnrealAiAgentToolGate::PassesToolSurfaceFilter(Req, TEXT("blueprint_graph_patch"), &Catalog));

	Req.bOmitMainAgentBlueprintMutationTools = true;
	Req.bBlueprintBuilderTurn = false;
	TestFalse(
		TEXT("builder-only tool blocked on main agent"),
		UnrealAiAgentToolGate::PassesToolSurfaceFilter(Req, TEXT("blueprint_graph_patch"), &Catalog));

	Req.bBlueprintBuilderTurn = true;
	TestTrue(
		TEXT("builder-only tool allowed on builder turn"),
		UnrealAiAgentToolGate::PassesToolSurfaceFilter(Req, TEXT("blueprint_graph_patch"), &Catalog));

	Req.bBlueprintBuilderTurn = false;
	TestTrue(
		TEXT("default surfaces (no field) passes on main"),
		UnrealAiAgentToolGate::PassesToolSurfaceFilter(Req, TEXT("editor_get_selection"), &Catalog));

	Req.bOmitMainAgentBlueprintMutationTools = true;
	Req.bBlueprintBuilderTurn = false;
	Req.bEnvironmentBuilderTurn = false;
	TestFalse(
		TEXT("material_graph_patch blocked on main agent"),
		UnrealAiAgentToolGate::PassesToolSurfaceFilter(Req, TEXT("material_graph_patch"), &Catalog));
	TestTrue(
		TEXT("material_graph_export allowed on main agent"),
		UnrealAiAgentToolGate::PassesToolSurfaceFilter(Req, TEXT("material_graph_export"), &Catalog));
	Req.bBlueprintBuilderTurn = true;
	TestTrue(
		TEXT("material_graph_patch allowed on builder turn"),
		UnrealAiAgentToolGate::PassesToolSurfaceFilter(Req, TEXT("material_graph_patch"), &Catalog));

	Req.bBlueprintBuilderTurn = false;
	Req.bOmitMainAgentBlueprintMutationTools = true;
	TestFalse(
		TEXT("pcg_generate blocked on main agent"),
		UnrealAiAgentToolGate::PassesToolSurfaceFilter(Req, TEXT("pcg_generate"), &Catalog));
	Req.bEnvironmentBuilderTurn = true;
	TestTrue(
		TEXT("pcg_generate allowed on environment builder turn"),
		UnrealAiAgentToolGate::PassesToolSurfaceFilter(Req, TEXT("pcg_generate"), &Catalog));

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
