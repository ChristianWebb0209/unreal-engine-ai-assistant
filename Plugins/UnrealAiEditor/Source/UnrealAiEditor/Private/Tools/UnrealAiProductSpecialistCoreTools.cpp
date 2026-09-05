#include "Tools/UnrealAiProductSpecialistCoreTools.h"

#include "Tools/UnrealAiToolCatalog.h"

namespace UnrealAiProductSpecialistCoreToolsPriv
{
	static void MergeIds(
		const TCHAR* const* Ids,
		int32 Count,
		TFunctionRef<bool(const FString&)> ToolFilter,
		TArray<FString>& InOutOrdered)
	{
		for (int32 i = 0; i < Count; ++i)
		{
			const FString Tid(Ids[i]);
			if (!ToolFilter(Tid))
			{
				continue;
			}
			if (!InOutOrdered.Contains(Tid))
			{
				InOutOrdered.Add(Tid);
			}
		}
	}
}

void UnrealAiProductSpecialistCoreTools::MergeCoreToolsAfterGuardrails(
	const EUnrealAiProductSpecialistId SpecialistId,
	TFunctionRef<bool(const FString&)> ToolFilter,
	TArray<FString>& InOutOrdered)
{
	if (SpecialistId == EUnrealAiProductSpecialistId::None)
	{
		return;
	}

	switch (SpecialistId)
	{
	case EUnrealAiProductSpecialistId::Scene:
	{
		static const TCHAR* const GIds[] = {
			TEXT("scene_fuzzy_search"),
			TEXT("viewport_list_visible_actors"),
			TEXT("actor_find_by_label"),
			TEXT("editor_get_selection"),
			TEXT("editor_set_selection"),
			TEXT("actor_get_material_slots"),
			TEXT("actor_get_transform"),
			TEXT("actor_get_visibility"),
			TEXT("entity_get_property"),
		};
		UnrealAiProductSpecialistCoreToolsPriv::MergeIds(GIds, UE_ARRAY_COUNT(GIds), ToolFilter, InOutOrdered);
		break;
	}
	case EUnrealAiProductSpecialistId::Assets:
	{
		static const TCHAR* const GIds[] = {
			TEXT("asset_index_fuzzy_search"),
			TEXT("asset_registry_query"),
			TEXT("asset_get_metadata"),
			TEXT("asset_graph_query"),
			TEXT("asset_create"),
			TEXT("asset_duplicate"),
			TEXT("asset_rename"),
			TEXT("asset_delete"),
			TEXT("asset_save_packages"),
			TEXT("asset_open_editor"),
			TEXT("content_browser_sync_asset"),
			TEXT("content_browser_navigate_folder"),
			TEXT("asset_export_properties"),
			TEXT("asset_apply_properties"),
		};
		UnrealAiProductSpecialistCoreToolsPriv::MergeIds(GIds, UE_ARRAY_COUNT(GIds), ToolFilter, InOutOrdered);
		break;
	}
	case EUnrealAiProductSpecialistId::Viewport:
	{
		static const TCHAR* const GIds[] = {
			TEXT("viewport_list_visible_actors"),
			TEXT("viewport_camera_control"),
			TEXT("viewport_get_view_mode"),
			TEXT("viewport_set_view_mode"),
			TEXT("viewport_frame"),
			TEXT("viewport_capture"),
			TEXT("editor_get_selection"),
			TEXT("editor_set_selection"),
		};
		UnrealAiProductSpecialistCoreToolsPriv::MergeIds(GIds, UE_ARRAY_COUNT(GIds), ToolFilter, InOutOrdered);
		break;
	}
	case EUnrealAiProductSpecialistId::Diagnostics:
	{
		static const TCHAR* const GIds[] = {
			TEXT("engine_message_log_read"),
			TEXT("editor_state_snapshot_read"),
			TEXT("tool_audit_append"),
		};
		UnrealAiProductSpecialistCoreToolsPriv::MergeIds(GIds, UE_ARRAY_COUNT(GIds), ToolFilter, InOutOrdered);
		break;
	}
	case EUnrealAiProductSpecialistId::Playtest:
	{
		static const TCHAR* const GIds[] = {
			TEXT("pie_start"),
			TEXT("pie_stop"),
			TEXT("pie_status"),
		};
		UnrealAiProductSpecialistCoreToolsPriv::MergeIds(GIds, UE_ARRAY_COUNT(GIds), ToolFilter, InOutOrdered);
		break;
	}
	case EUnrealAiProductSpecialistId::Animation:
	{
		static const TCHAR* const GIds[] = {
			TEXT("level_sequence_create_asset"),
			TEXT("animation_blueprint_get_graph_summary"),
			TEXT("sequencer_open"),
		};
		UnrealAiProductSpecialistCoreToolsPriv::MergeIds(GIds, UE_ARRAY_COUNT(GIds), ToolFilter, InOutOrdered);
		break;
	}
	case EUnrealAiProductSpecialistId::ProjectIntel:
	{
		static const TCHAR* const GIds[] = {
			TEXT("project_file_read_text"),
			TEXT("project_file_move"),
			TEXT("source_search_symbol"),
			TEXT("cpp_project_compile"),
		};
		UnrealAiProductSpecialistCoreToolsPriv::MergeIds(GIds, UE_ARRAY_COUNT(GIds), ToolFilter, InOutOrdered);
		break;
	}
	case EUnrealAiProductSpecialistId::EditorUi:
	{
		static const TCHAR* const GIds[] = {
			TEXT("editor_get_mode"),
			TEXT("editor_set_mode"),
			TEXT("global_tab_focus"),
			TEXT("menu_command_invoke"),
			TEXT("console_command"),
		};
		UnrealAiProductSpecialistCoreToolsPriv::MergeIds(GIds, UE_ARRAY_COUNT(GIds), ToolFilter, InOutOrdered);
		break;
	}
	case EUnrealAiProductSpecialistId::Settings:
	{
		static const TCHAR* const GIds[] = {
			TEXT("setting_query"),
			TEXT("setting_apply"),
		};
		UnrealAiProductSpecialistCoreToolsPriv::MergeIds(GIds, UE_ARRAY_COUNT(GIds), ToolFilter, InOutOrdered);
		break;
	}
	case EUnrealAiProductSpecialistId::Materials:
	{
		static const TCHAR* const GIds[] = {
			TEXT("editor_get_selection"),
			TEXT("actor_get_material_slots"),
			TEXT("material_instance_set_parameter"),
			TEXT("material_instance_get_scalar_parameter"),
			TEXT("material_instance_get_vector_parameter"),
			TEXT("material_get_usage_summary"),
		};
		UnrealAiProductSpecialistCoreToolsPriv::MergeIds(GIds, UE_ARRAY_COUNT(GIds), ToolFilter, InOutOrdered);
		break;
	}
	case EUnrealAiProductSpecialistId::None:
	default:
		break;
	}
}

void UnrealAiProductSpecialistCoreTools::BuildSpecialistTieredAppendixOrder(
	const EUnrealAiProductSpecialistId SpecialistId,
	FUnrealAiToolCatalog& Catalog,
	const EUnrealAiAgentMode Mode,
	const FUnrealAiModelCapabilities& Caps,
	const FUnrealAiToolPackOptions* PackOptions,
	TFunctionRef<bool(const FString&)> ToolFilter,
	TArray<FString>& OutOrdered)
{
	OutOrdered.Reset();
	if (SpecialistId == EUnrealAiProductSpecialistId::None)
	{
		return;
	}
	MergeCoreToolsAfterGuardrails(SpecialistId, ToolFilter, OutOrdered);
	TArray<FString> Extras;
	Catalog.ForEachEnabledToolForMode(
		Mode,
		Caps,
		PackOptions,
		ToolFilter,
		[&](const FString& Tid, const TSharedPtr<FJsonObject>&)
		{
			if (!OutOrdered.Contains(Tid))
			{
				Extras.Add(Tid);
			}
		});
	Extras.Sort();
	OutOrdered.Append(Extras);
}
