#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tools/UnrealAiToolDispatch.h"
#include "Tools/UnrealAiToolJson.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"

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

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
