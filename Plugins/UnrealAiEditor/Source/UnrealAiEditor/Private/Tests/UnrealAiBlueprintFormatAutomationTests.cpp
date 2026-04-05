#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "BlueprintFormat/BlueprintGraphFormatOptions.h"
#include "BlueprintFormat/BlueprintGraphFormatService.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphNode_Comment.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "K2Node_Event.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Guid.h"
#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UnrealAiEditorSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiBlueprintMakeFormatOptionsFromSettingsTest,
	"UnrealAiEditor.BlueprintFormat.MakeFormatOptionsFromSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiBlueprintMakeFormatOptionsFromSettingsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UUnrealAiEditorSettings* St = GetMutableDefault<UUnrealAiEditorSettings>();
	const bool SavedKnots = St->bBlueprintFormatUseWireKnots;
	const bool SavedPreserve = St->bBlueprintFormatPreserveExistingPositions;

	St->bBlueprintFormatUseWireKnots = true;
	St->bBlueprintFormatPreserveExistingPositions = true;
	const FUnrealBlueprintGraphFormatOptions WithKnots = UnrealAiBlueprintTools_MakeFormatOptionsFromSettings(St);
	TestEqual(TEXT("default SpacingX"), WithKnots.SpacingX, 400);
	TestEqual(TEXT("default SpacingY"), WithKnots.SpacingY, 200);
	TestEqual(TEXT("default BranchVerticalGap"), WithKnots.BranchVerticalGap, 48);
	TestTrue(TEXT("knots on"), WithKnots.WireKnotAggression != EUnrealBlueprintWireKnotAggression::Off);
	TestTrue(TEXT("preserve on"), WithKnots.bPreserveExistingPositions);

	St->bBlueprintFormatUseWireKnots = false;
	const FUnrealBlueprintGraphFormatOptions KnotsOff = UnrealAiBlueprintTools_MakeFormatOptionsFromSettings(St);
	TestEqual(TEXT("knots off SpacingX unchanged"), KnotsOff.SpacingX, 400);
	TestEqual(TEXT("knots off aggression"), KnotsOff.WireKnotAggression, EUnrealBlueprintWireKnotAggression::Off);
	TestEqual(TEXT("knots off max wire"), KnotsOff.MaxWireLengthBeforeReroute, 0);

	St->bBlueprintFormatUseWireKnots = SavedKnots;
	St->bBlueprintFormatPreserveExistingPositions = SavedPreserve;

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealAiBlueprintLayoutIdempotenceTest,
	"UnrealAiEditor.BlueprintFormat.LayoutIdempotence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealAiBlueprintLayoutIdempotenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString PkgName = FString::Printf(
		TEXT("/Temp/UAI_LayoutIdem_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits));
	UPackage* Pkg = CreatePackage(*PkgName);
	TestNotNull(TEXT("package"), Pkg);
	if (!Pkg)
	{
		return false;
	}

	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(),
		Pkg,
		FName(TEXT("LayoutIdemBP")),
		BPTYPE_Normal,
		NAME_None);
	TestNotNull(TEXT("blueprint"), BP);
	if (!BP)
	{
		return false;
	}

	UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(BP);
	if (!Graph && BP->UbergraphPages.Num() > 0)
	{
		Graph = BP->UbergraphPages[0];
	}
	TestNotNull(TEXT("event_graph"), Graph);
	if (!Graph)
	{
		return false;
	}

	bool bHasK2Event = false;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (Cast<UK2Node_Event>(N))
		{
			bHasK2Event = true;
			break;
		}
	}
	if (!bHasK2Event)
	{
		UK2Node_Event* Ev = NewObject<UK2Node_Event>(Graph);
		Ev->EventReference.SetExternalMember(FName(TEXT("ReceiveBeginPlay")), AActor::StaticClass());
		Ev->bOverrideFunction = true;
		Graph->AddNode(Ev, true, false);
		Ev->NodePosX = 0;
		Ev->NodePosY = 0;
		Ev->CreateNewGuid();
		Ev->PostPlacedNewNode();
		Ev->AllocateDefaultPins();
	}

	FUnrealBlueprintGraphFormatOptions Opt;
	Opt.SpacingX = 400;
	Opt.SpacingY = 200;
	Opt.BranchVerticalGap = 48;
	Opt.bReflowCommentsByGeometry = false;
	Opt.CommentsMode = EUnrealAiBlueprintCommentsMode::Off;
	Opt.MaxWireLengthBeforeReroute = 0;
	Opt.MaxCrossingsPerSegment = 0;
	Opt.WireKnotAggression = EUnrealBlueprintWireKnotAggression::Off;

	auto SnapshotNonComment = [&]() -> TMap<FGuid, FIntPoint> {
		TMap<FGuid, FIntPoint> M;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || Cast<UEdGraphNode_Comment>(Node))
			{
				continue;
			}
			M.Add(Node->NodeGuid, FIntPoint(Node->NodePosX, Node->NodePosY));
		}
		return M;
	};

	(void)FUnrealBlueprintGraphFormatService::LayoutEntireGraph(Graph, Opt);
	const TMap<FGuid, FIntPoint> First = SnapshotNonComment();
	(void)FUnrealBlueprintGraphFormatService::LayoutEntireGraph(Graph, Opt);
	const TMap<FGuid, FIntPoint> Second = SnapshotNonComment();

	TestEqual(TEXT("node_count"), First.Num(), Second.Num());
	for (const TPair<FGuid, FIntPoint>& Pair : First)
	{
		const FIntPoint* Q = Second.Find(Pair.Key);
		TestNotNull(TEXT("stable_guid"), Q);
		if (Q)
		{
			TestEqual(TEXT("pos_x"), Pair.Value.X, Q->X);
			TestEqual(TEXT("pos_y"), Pair.Value.Y, Q->Y);
		}
	}

	return !HasAnyErrors();
}

#endif
