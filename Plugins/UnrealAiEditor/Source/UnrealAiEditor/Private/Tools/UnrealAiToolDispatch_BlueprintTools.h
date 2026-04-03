#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraphSchema.h"
#include "BlueprintFormat/BlueprintGraphFormatOptions.h"
#include "Harness/IToolExecutionHost.h"

class UBlueprint;
class UEdGraph;
class UUnrealAiEditorSettings;

/**
 * Canonicalize Blueprint object paths: /Game/Folder/Asset -> /Game/Folder/Asset.Asset
 * (Unreal requires PackagePath.ObjectName for LoadObject).
 */
void UnrealAiNormalizeBlueprintObjectPath(FString& BlueprintObjectPath);

/** Load a /Game Blueprint asset (same rules as other blueprint_* tools). */
UBlueprint* UnrealAiBlueprintTools_LoadBlueprintGame(const FString& BlueprintObjectPath);
bool UnrealAiBlueprintTools_IsGameWritableBlueprintPath(const FString& BlueprintObjectPath);
UEdGraph* UnrealAiBlueprintTools_FindGraphByName(UBlueprint* BP, const FString& GraphName);
/** Preferred for `add_variable`: returns false if the token is not recognized (no silent Boolean fallback). */
bool UnrealAiBlueprintTools_TryParsePinTypeFromString(const FString& TypeStr, FEdGraphPinType& OutType);
/** Legacy: unknown tokens map to Boolean (preserves older callers); prefer TryParse for strict validation. */
FEdGraphPinType UnrealAiBlueprintTools_ParsePinTypeFromString(const FString& TypeStr);

/** Build format options from optional layout_mode / wire_knots strings (same tokens as blueprint_apply_ir). */
FUnrealBlueprintGraphFormatOptions UnrealAiBlueprintTools_MakeFormatOptions(
	const FString& LayoutMode,
	const FString& WireKnots,
	const UUnrealAiEditorSettings* Settings);

/** Open the Blueprint editor; optionally focus a graph by name (EventGraph, etc.). */
void UnrealAiFocusBlueprintEditor(const FString& BlueprintObjectPath, const FString& GraphName);

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintCompile(const TSharedPtr<FJsonObject>& Args);
/** Set a reflected property on a named Actor component instance on the Blueprint generated class default (CDO), then compile. */
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintSetComponentDefault(const TSharedPtr<FJsonObject>& Args);
/** Serialize a Blueprint graph to blueprint_apply_ir-compatible JSON (lossy for unknown node types). */
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintExportIr(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintApplyIr(const TSharedPtr<FJsonObject>& Args);
/** Run bundled graph layout on one script graph (default: first ubergraph); supports full_graph vs selection. */
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintFormatGraph(const TSharedPtr<FJsonObject>& Args);
/** Same as blueprint_format_graph with format_scope forced to selection (Blueprint editor must be open). */
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintFormatSelection(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintGetGraphSummary(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintOpenGraphTab(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintAddVariable(const TSharedPtr<FJsonObject>& Args);
FUnrealAiToolInvocationResult UnrealAiDispatch_AnimBlueprintGetGraphSummary(const TSharedPtr<FJsonObject>& Args);

/** Apply arbitrary K2 node create/connect/pin patch ops (UE 5.7 editor APIs). */
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintGraphPatch(const TSharedPtr<FJsonObject>& Args);
/** Introspect pins on a node (patch_id from same session or guid: form). */
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintGraphListPins(const TSharedPtr<FJsonObject>& Args);

/** Pin audit + node map for Blueprint graph (builder / read paths). */
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintGraphIntrospect(const TSharedPtr<FJsonObject>& Args);
/** Export graph nodes as Unreal T3D clipboard text. */
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintExportGraphT3d(const TSharedPtr<FJsonObject>& Args);
/** Resolve __UAI_G_NNNNNN__ placeholders and run CanImportNodesFromText. */
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintT3dPreflightValidate(const TSharedPtr<FJsonObject>& Args);
/** Atomic ImportNodesFromText after placeholder resolution. */
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintGraphImportT3d(const TSharedPtr<FJsonObject>& Args);
/** Post-edit link sanity checks (extensible steps[]). */
FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintVerifyGraph(const TSharedPtr<FJsonObject>& Args);
