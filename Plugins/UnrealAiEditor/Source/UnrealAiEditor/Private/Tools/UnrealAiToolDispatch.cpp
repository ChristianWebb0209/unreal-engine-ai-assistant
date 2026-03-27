#include "Tools/UnrealAiToolDispatch.h"

#include "Tools/UnrealAiToolDispatch_Actors.h"
#include "Tools/UnrealAiToolDispatch_AssetsMaterials.h"
#include "Tools/UnrealAiToolDispatch_Console.h"
#include "Tools/UnrealAiToolDispatch_ContentBrowserEx.h"
#include "Tools/UnrealAiToolDispatch_Context.h"
#include "Tools/UnrealAiToolDispatch_EditorUi.h"
#include "Tools/UnrealAiToolDispatch_Misc.h"
#include "Tools/UnrealAiToolDispatch_Pie.h"
#include "Tools/UnrealAiToolDispatch_ProjectFiles.h"
#include "Tools/UnrealAiToolDispatch_Search.h"
#include "Tools/UnrealAiToolDispatch_Viewport.h"
#include "Tools/UnrealAiToolDispatch_MoreAssets.h"
#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"
#include "Tools/UnrealAiToolDispatch_EditorMore.h"
#include "Tools/UnrealAiToolDispatch_BuildPackaging.h"
#include "Tools/UnrealAiToolDispatch_ExtraFeatures.h"
#include "Tools/UnrealAiToolDispatch_GenericAssets.h"
#include "Tools/UnrealAiToolJson.h"

#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/UnrealAiProjectId.h"
#include "Misc/CoreMisc.h"

namespace UnrealAiToolDispatchInternal
{
	static FUnrealAiToolInvocationResult NotImplemented(const FString& ToolId, const FString& Reason = FString())
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetBoolField(TEXT("ok"), false);
		O->SetStringField(TEXT("status"), TEXT("not_implemented"));
		O->SetStringField(TEXT("tool_id"), ToolId);
		if (!Reason.IsEmpty())
		{
			O->SetStringField(TEXT("reason"), Reason);
		}
		FUnrealAiToolInvocationResult R;
		R.bOk = false;
		R.ErrorMessage = FString::Printf(TEXT("%s: not implemented"), *ToolId);
		R.ContentForModel = UnrealAiToolJson::SerializeObject(O);
		return R;
	}

	static FString ResolveProjectId(const FString& Session)
	{
		return Session.IsEmpty() ? UnrealAiProjectId::GetCurrentProjectId() : Session;
	}

	static FString ResolveThreadId(const FString& Session)
	{
		return Session.IsEmpty() ? TEXT("default") : Session;
	}
}

using namespace UnrealAiToolDispatchInternal;

FUnrealAiToolInvocationResult UnrealAiDispatchTool(
	const FString& ToolId,
	const TSharedPtr<FJsonObject>& Args,
	const TSharedPtr<FJsonObject>& CatalogEntry,
	FUnrealAiBackendRegistry* Registry,
	const FString& SessionProjectId,
	const FString& SessionThreadId)
{
	if (!IsInGameThread())
	{
		return UnrealAiToolJson::Error(TEXT("Tools must be invoked on the game thread"));
	}

	const TSharedPtr<FJsonObject>& A = Args.IsValid() ? Args : MakeShared<FJsonObject>();
	const bool bAutoRunDestructive = []() {
		const FString Raw = FPlatformMisc::GetEnvironmentVariable(TEXT("UNREAL_AI_AUTO_RUN_DESTRUCTIVE"));
		FString V = Raw;
		V.TrimStartAndEndInline();
		V = V.ToLower();
		return !V.IsEmpty()
			&& (V.Equals(TEXT("1")) || V.Equals(TEXT("true")) || V.Equals(TEXT("yes")) || V.Equals(TEXT("on")));
	}();

	const FString ProjectId = ResolveProjectId(SessionProjectId);
	const FString ThreadId = ResolveThreadId(SessionThreadId);

	// --- Implemented tools (router) ---
	if (ToolId == TEXT("editor_get_selection"))
	{
		return UnrealAiDispatch_EditorGetSelection(A);
	}
	if (ToolId == TEXT("editor_set_selection"))
	{
		return UnrealAiDispatch_EditorSetSelection(A);
	}
	if (ToolId == TEXT("editor_set_mode"))
	{
		return UnrealAiDispatch_EditorSetMode(A);
	}

	if (ToolId == TEXT("actor_destroy"))
	{
		if (bAutoRunDestructive)
		{
			// Tool contract uses a "loose confirm" string (DELETE/yes/true/1/etc).
			// When auto-run is enabled, fill missing/invalid confirm so the agent
			// doesn't need an extra retry just to set intent.
			FString Confirm;
			const bool bHasConfirm = A->TryGetStringField(TEXT("confirm"), Confirm);
			if (!bHasConfirm || Confirm.IsEmpty())
			{
				A->SetStringField(TEXT("confirm"), TEXT("DELETE"));
			}
			else
			{
				FString C = Confirm;
				C.TrimStartAndEndInline();
				C = C.ToLower();
				const bool bOk = C.Equals(TEXT("delete")) || C.Equals(TEXT("yes")) || C.Equals(TEXT("y")) || C.Equals(TEXT("true"))
					|| C.Equals(TEXT("confirm")) || C.Equals(TEXT("1"));
				if (!bOk)
				{
					A->SetStringField(TEXT("confirm"), TEXT("DELETE"));
				}
			}
		}
		return UnrealAiDispatch_ActorDestroy(A);
	}
	if (ToolId == TEXT("actor_find_by_label"))
	{
		return UnrealAiDispatch_ActorFindByLabel(A);
	}
	if (ToolId == TEXT("actor_get_transform"))
	{
		return UnrealAiDispatch_ActorGetTransform(A);
	}
	if (ToolId == TEXT("actor_set_transform"))
	{
		return UnrealAiDispatch_ActorSetTransform(A);
	}
	if (ToolId == TEXT("actor_set_visibility"))
	{
		return UnrealAiDispatch_ActorSetVisibility(A);
	}
	if (ToolId == TEXT("actor_blueprint_toggle_visibility"))
	{
		return UnrealAiDispatch_ActorBlueprintToggleVisibility(A);
	}
	if (ToolId == TEXT("actor_attach_to"))
	{
		return UnrealAiDispatch_ActorAttachTo(A);
	}
	if (ToolId == TEXT("actor_spawn_from_class"))
	{
		return UnrealAiDispatch_ActorSpawnFromClass(A);
	}

	if (ToolId == TEXT("viewport_camera_get_transform"))
	{
		return UnrealAiDispatch_ViewportCameraGetTransform();
	}
	if (ToolId == TEXT("viewport_camera_set_transform"))
	{
		return UnrealAiDispatch_ViewportCameraSetTransform(A);
	}
	if (ToolId == TEXT("viewport_camera_dolly"))
	{
		return UnrealAiDispatch_ViewportCameraDolly(A);
	}
	if (ToolId == TEXT("viewport_camera_pan"))
	{
		return UnrealAiDispatch_ViewportCameraPan(A);
	}
	if (ToolId == TEXT("viewport_camera_orbit"))
	{
		return UnrealAiDispatch_ViewportCameraOrbit(A);
	}
	if (ToolId == TEXT("viewport_camera_pilot"))
	{
		return UnrealAiDispatch_ViewportCameraPilot(A);
	}
	if (ToolId == TEXT("viewport_frame_actors"))
	{
		return UnrealAiDispatch_ViewportFrameActors(A);
	}
	if (ToolId == TEXT("viewport_frame_selection"))
	{
		return UnrealAiDispatch_ViewportFrameSelection(A);
	}
	if (ToolId == TEXT("viewport_capture_png"))
	{
		return UnrealAiDispatch_ViewportCapturePng(A);
	}
	if (ToolId == TEXT("viewport_capture_delayed"))
	{
		return UnrealAiDispatch_ViewportCaptureDelayed(A);
	}
	if (ToolId == TEXT("viewport_set_view_mode"))
	{
		return UnrealAiDispatch_ViewportSetViewMode(A);
	}

	if (ToolId == TEXT("project_file_read_text"))
	{
		return UnrealAiDispatch_ProjectFileReadText(A);
	}
	if (ToolId == TEXT("project_file_write_text"))
	{
		if (bAutoRunDestructive)
		{
			bool bConfirm = false;
			const bool bHasConfirm = A->TryGetBoolField(TEXT("confirm"), bConfirm);
			if (!bHasConfirm || !bConfirm)
			{
				A->SetBoolField(TEXT("confirm"), true);
			}
		}
		return UnrealAiDispatch_ProjectFileWriteText(A);
	}

	if (ToolId == TEXT("cook_content_for_platform"))
	{
		return UnrealAiDispatch_CookContentForPlatform(A);
	}
	if (ToolId == TEXT("package_project"))
	{
		return UnrealAiDispatch_PackageProject(A);
	}
	if (ToolId == TEXT("shader_compile_wait"))
	{
		return UnrealAiDispatch_ShaderCompileWait(A);
	}

	if (ToolId == TEXT("console_command"))
	{
		return UnrealAiDispatch_ConsoleCommand(A);
	}

	if (ToolId == TEXT("asset_save_packages"))
	{
		return UnrealAiDispatch_AssetSavePackages(A);
	}
	if (ToolId == TEXT("asset_rename"))
	{
		return UnrealAiDispatch_AssetRename(A);
	}
	if (ToolId == TEXT("asset_get_metadata"))
	{
		return UnrealAiDispatch_AssetGetMetadata(A);
	}
	if (ToolId == TEXT("material_get_usage_summary"))
	{
		return UnrealAiDispatch_MaterialGetUsageSummary(A);
	}
	if (ToolId == TEXT("material_instance_set_scalar_parameter"))
	{
		return UnrealAiDispatch_MaterialInstanceSetScalarParameter(A);
	}
	if (ToolId == TEXT("material_instance_set_vector_parameter"))
	{
		return UnrealAiDispatch_MaterialInstanceSetVectorParameter(A);
	}

	if (ToolId == TEXT("asset_delete"))
	{
		// Alias normalization: accept `object_path`/`path` as a single `object_paths[]` entry.
		TSharedPtr<FJsonObject> Norm = MakeShared<FJsonObject>();
		const TArray<TSharedPtr<FJsonValue>>* ObjPaths = nullptr;
		if (A->TryGetArrayField(TEXT("object_paths"), ObjPaths) && ObjPaths && ObjPaths->Num() > 0)
		{
			Norm->SetArrayField(TEXT("object_paths"), *ObjPaths);
		}
		else
		{
			FString OnePath;
			A->TryGetStringField(TEXT("object_path"), OnePath);
			if (OnePath.IsEmpty())
			{
				A->TryGetStringField(TEXT("path"), OnePath);
			}
			if (OnePath.IsEmpty())
			{
				A->TryGetStringField(TEXT("asset_path"), OnePath);
			}

			if (!OnePath.IsEmpty())
			{
				TArray<TSharedPtr<FJsonValue>> Arr;
				Arr.Add(MakeShared<FJsonValueString>(OnePath));
				Norm->SetArrayField(TEXT("object_paths"), Arr);
			}
		}

		// Preserve provided confirm when present; if missing, dispatcher treats it as false and returns a suggested call.
		// When auto-run destructive is enabled, default missing/false confirm to true to avoid an extra retry.
		bool bConfirm = false;
		const bool bHasConfirm = A->TryGetBoolField(TEXT("confirm"), bConfirm);
		if (bAutoRunDestructive)
		{
			Norm->SetBoolField(TEXT("confirm"), true);
		}
		else if (bHasConfirm)
		{
			Norm->SetBoolField(TEXT("confirm"), bConfirm);
		}
		return UnrealAiDispatch_AssetDelete(Norm);
	}
	if (ToolId == TEXT("asset_destroy"))
	{
		// Compatibility alias: normalize legacy asset_destroy shape into asset_delete.
		TSharedPtr<FJsonObject> Norm = MakeShared<FJsonObject>();
		bool bConfirm = false;
		A->TryGetBoolField(TEXT("confirm"), bConfirm);
		Norm->SetBoolField(TEXT("confirm"), bAutoRunDestructive ? true : bConfirm);
		const TArray<TSharedPtr<FJsonValue>>* ObjPaths = nullptr;
		if (A->TryGetArrayField(TEXT("object_paths"), ObjPaths) && ObjPaths && ObjPaths->Num() > 0)
		{
			Norm->SetArrayField(TEXT("object_paths"), *ObjPaths);
		}
		else
		{
			FString OnePath;
			A->TryGetStringField(TEXT("object_path"), OnePath);
			if (OnePath.IsEmpty())
			{
				A->TryGetStringField(TEXT("asset_path"), OnePath);
			}
			if (OnePath.IsEmpty())
			{
				A->TryGetStringField(TEXT("path"), OnePath);
			}
			if (!OnePath.IsEmpty())
			{
				TArray<TSharedPtr<FJsonValue>> Arr;
				Arr.Add(MakeShared<FJsonValueString>(OnePath));
				Norm->SetArrayField(TEXT("object_paths"), Arr);
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* NormPaths = nullptr;
		if (!Norm->TryGetArrayField(TEXT("object_paths"), NormPaths) || !NormPaths || NormPaths->Num() == 0)
		{
			TSharedPtr<FJsonObject> Suggested = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> SuggestedPaths;
			SuggestedPaths.Add(MakeShared<FJsonValueString>(TEXT("/Game/Blueprints/MyBP.MyBP")));
			Suggested->SetArrayField(TEXT("object_paths"), SuggestedPaths);
			Suggested->SetBoolField(TEXT("confirm"), true);
			return UnrealAiToolJson::ErrorWithSuggestedCall(
				TEXT("asset_destroy is a compatibility alias. Use asset_delete with object_paths[] and confirm:true."),
				TEXT("asset_delete"),
				Suggested);
		}
		return UnrealAiDispatch_AssetDelete(Norm);
	}
	if (ToolId == TEXT("asset_duplicate"))
	{
		return UnrealAiDispatch_AssetDuplicate(A);
	}
	if (ToolId == TEXT("asset_import"))
	{
		return UnrealAiDispatch_AssetImport(A);
	}
	if (ToolId == TEXT("asset_open_editor"))
	{
		return UnrealAiDispatch_AssetOpenEditor(A);
	}
	if (ToolId == TEXT("asset_create"))
	{
		return UnrealAiDispatch_AssetCreate(A);
	}
	if (ToolId == TEXT("asset_export_properties"))
	{
		return UnrealAiDispatch_AssetExportProperties(A);
	}
	if (ToolId == TEXT("asset_apply_properties"))
	{
		return UnrealAiDispatch_AssetApplyProperties(A);
	}
	if (ToolId == TEXT("asset_find_referencers"))
	{
		return UnrealAiDispatch_AssetFindReferencers(A);
	}
	if (ToolId == TEXT("asset_get_dependencies"))
	{
		return UnrealAiDispatch_AssetGetDependencies(A);
	}
	if (ToolId == TEXT("level_sequence_create_asset"))
	{
		return UnrealAiDispatch_LevelSequenceCreateAsset(A);
	}

	if (ToolId == TEXT("blueprint_compile"))
	{
		return UnrealAiDispatch_BlueprintCompile(A);
	}
	if (ToolId == TEXT("blueprint_export_ir"))
	{
		return UnrealAiDispatch_BlueprintExportIr(A);
	}
	if (ToolId == TEXT("blueprint_apply_ir"))
	{
		return UnrealAiDispatch_BlueprintApplyIr(A);
	}
	if (ToolId == TEXT("blueprint_format_graph"))
	{
		return UnrealAiDispatch_BlueprintFormatGraph(A);
	}
	if (ToolId == TEXT("blueprint_get_graph_summary"))
	{
		return UnrealAiDispatch_BlueprintGetGraphSummary(A);
	}
	if (ToolId == TEXT("blueprint_open_graph_tab"))
	{
		return UnrealAiDispatch_BlueprintOpenGraphTab(A);
	}
	if (ToolId == TEXT("blueprint_add_variable"))
	{
		return UnrealAiDispatch_BlueprintAddVariable(A);
	}
	if (ToolId == TEXT("animation_blueprint_get_graph_summary"))
	{
		return UnrealAiDispatch_AnimBlueprintGetGraphSummary(A);
	}

	if (ToolId == TEXT("content_browser_navigate_folder"))
	{
		return UnrealAiDispatch_ContentBrowserNavigateFolder(A);
	}

	if (ToolId == TEXT("pie_start"))
	{
		return UnrealAiDispatch_PieStart(A);
	}
	if (ToolId == TEXT("pie_stop"))
	{
		return UnrealAiDispatch_PieStop(A);
	}
	if (ToolId == TEXT("pie_status"))
	{
		return UnrealAiDispatch_PieStatus(A);
	}

	if (ToolId == TEXT("collision_trace_editor_world"))
	{
		return UnrealAiDispatch_CollisionTraceEditorWorld(A);
	}
	if (ToolId == TEXT("physics_impulse_actor"))
	{
		return UnrealAiDispatch_PhysicsImpulseActor(A);
	}

	if (ToolId == TEXT("asset_registry_query"))
	{
		return UnrealAiDispatch_AssetRegistryQuery(A);
	}
	if (ToolId == TEXT("editor_state_snapshot_read"))
	{
		return UnrealAiDispatch_EditorStateSnapshotRead(Registry, ProjectId, ThreadId, A);
	}
	if (ToolId == TEXT("agent_emit_todo_plan"))
	{
		return UnrealAiDispatch_AgentEmitTodoPlan(Registry, ProjectId, ThreadId, A);
	}
	if (ToolId == TEXT("worker_merge_results"))
	{
		return UnrealAiDispatch_WorkerMergeResults(A);
	}
	if (ToolId == TEXT("tool_audit_append"))
	{
		return UnrealAiDispatch_ToolAuditAppend(A);
	}
	if (ToolId == TEXT("content_browser_sync_asset"))
	{
		return UnrealAiDispatch_ContentBrowserSyncAsset(A);
	}
	if (ToolId == TEXT("engine_message_log_read"))
	{
		return UnrealAiDispatch_EngineMessageLogRead(A);
	}

	if (ToolId == TEXT("global_tab_focus"))
	{
		return UnrealAiDispatch_GlobalTabFocus(A);
	}
	if (ToolId == TEXT("menu_command_invoke"))
	{
		return UnrealAiDispatch_MenuCommandInvoke(A);
	}
	if (ToolId == TEXT("outliner_folder_move"))
	{
		return UnrealAiDispatch_OutlinerFolderMove(A);
	}

	if (ToolId == TEXT("audio_component_preview"))
	{
		return UnrealAiDispatch_AudioComponentPreview(A);
	}
	if (ToolId == TEXT("render_target_readback_editor"))
	{
		return UnrealAiDispatch_RenderTargetReadbackEditor(A);
	}
	if (ToolId == TEXT("sequencer_open"))
	{
		return UnrealAiDispatch_SequencerOpen(A);
	}
	if (ToolId == TEXT("metasound_open_editor"))
	{
		return UnrealAiDispatch_MetasoundOpenEditor(A);
	}
	if (ToolId == TEXT("foliage_paint_instances"))
	{
		return UnrealAiDispatch_FoliagePaintInstances(A);
	}
	if (ToolId == TEXT("landscape_import_heightmap"))
	{
		return UnrealAiDispatch_LandscapeImportHeightmap(A);
	}
	if (ToolId == TEXT("pcg_generate"))
	{
		return UnrealAiDispatch_PcgGenerate(A);
	}
	if (ToolId == TEXT("subagent_spawn"))
	{
		return UnrealAiDispatch_SubagentSpawn(A);
	}

	if (ToolId == TEXT("scene_fuzzy_search.query"))
	{
		// Some agents/tools refer to the canonical key as `<tool_id>.query` for parameters.
		// Treat `scene_fuzzy_search.query` as an alias for `scene_fuzzy_search`.
		return UnrealAiDispatch_SceneFuzzySearch(A);
	}

	if (ToolId == TEXT("scene_fuzzy_search"))
	{
		return UnrealAiDispatch_SceneFuzzySearch(A);
	}
	if (ToolId == TEXT("asset_index_fuzzy_search"))
	{
		return UnrealAiDispatch_AssetIndexFuzzySearch(A);
	}
	if (ToolId == TEXT("source_search_symbol"))
	{
		return UnrealAiDispatch_SourceSearchFuzzy(A);
	}

	if (CatalogEntry.IsValid())
	{
		FString Status;
		if (CatalogEntry->TryGetStringField(TEXT("status"), Status) && Status == TEXT("future"))
		{
			return NotImplemented(ToolId, TEXT("Marked future in catalog; implementation pending."));
		}
	}

	return NotImplemented(ToolId, TEXT("No UE5 handler registered yet for this tool_id."));
}
