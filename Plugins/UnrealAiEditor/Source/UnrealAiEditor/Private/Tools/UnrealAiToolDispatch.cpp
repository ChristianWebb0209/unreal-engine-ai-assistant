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
		return UnrealAiDispatch_AssetDelete(A);
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
