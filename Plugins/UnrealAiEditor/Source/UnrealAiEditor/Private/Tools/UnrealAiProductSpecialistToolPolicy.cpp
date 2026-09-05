#include "Tools/UnrealAiProductSpecialistToolPolicy.h"

#include "Dom/JsonObject.h"

static FString GetCategory(const FJsonObject& ToolDef)
{
	FString Category;
	ToolDef.TryGetStringField(TEXT("category"), Category);
	return Category;
}

static bool SceneSpecialistAllows(const FString& ToolId, const FJsonObject& ToolDef)
{
	const FString Category = GetCategory(ToolDef);
	if (Category == TEXT("world_actors"))
	{
		return true;
	}
	if (ToolId == TEXT("editor_get_selection") || ToolId == TEXT("editor_set_selection"))
	{
		return true;
	}
	if (ToolId == TEXT("viewport_list_visible_actors"))
	{
		return true;
	}
	if (ToolId == TEXT("actor_get_material_slots"))
	{
		return true;
	}
	if (ToolId.StartsWith(TEXT("entity_")))
	{
		return true;
	}
	if (ToolId == TEXT("outliner_folder_move"))
	{
		return true;
	}
	return false;
}

static bool AssetsSpecialistAllows(const FString& ToolId, const FJsonObject& ToolDef)
{
	const FString Category = GetCategory(ToolDef);
	if (Category == TEXT("assets_content"))
	{
		return true;
	}
	if (Category == TEXT("editor_ui_navigation"))
	{
		if (ToolId.StartsWith(TEXT("content_browser_")))
		{
			return true;
		}
		if (ToolId == TEXT("asset_open_editor"))
		{
			return true;
		}
	}
	return false;
}

static bool ViewportSpecialistAllows(const FString& ToolId, const FJsonObject& ToolDef)
{
	const FString Category = GetCategory(ToolDef);
	if (Category == TEXT("viewport_camera") || Category == TEXT("capture_vision"))
	{
		return true;
	}
	if (Category == TEXT("selection_framing"))
	{
		return ToolId == TEXT("viewport_frame") || ToolId == TEXT("editor_get_selection") || ToolId == TEXT("editor_set_selection");
	}
	if (ToolId == TEXT("viewport_capture") || ToolId == TEXT("viewport_camera_control") || ToolId == TEXT("viewport_get_view_mode")
		|| ToolId == TEXT("viewport_set_view_mode") || ToolId == TEXT("viewport_frame") || ToolId == TEXT("viewport_list_visible_actors"))
	{
		return true;
	}
	return false;
}

static bool DiagnosticsSpecialistAllows(const FString& ToolId, const FJsonObject& ToolDef)
{
	(void)ToolId;
	return GetCategory(ToolDef) == TEXT("diagnostics_logs");
}

static bool PlaytestSpecialistAllows(const FString& ToolId, const FJsonObject& ToolDef)
{
	(void)ToolId;
	return GetCategory(ToolDef) == TEXT("pie_play");
}

static bool AnimationSpecialistAllows(const FString& ToolId, const FJsonObject& ToolDef)
{
	(void)ToolId;
	return GetCategory(ToolDef) == TEXT("animation_sequencer");
}

static bool ProjectIntelSpecialistAllows(const FString& ToolId, const FJsonObject& ToolDef)
{
	(void)ToolId;
	return GetCategory(ToolDef) == TEXT("project_files_search");
}

static bool EditorUiSpecialistAllows(const FString& ToolId, const FJsonObject& ToolDef)
{
	const FString Category = GetCategory(ToolDef);
	if (Category == TEXT("console_exec"))
	{
		return ToolId == TEXT("console_command");
	}
	if (Category == TEXT("editor_ui_navigation"))
	{
		if (ToolId.StartsWith(TEXT("content_browser_")) || ToolId == TEXT("asset_open_editor"))
		{
			return false;
		}
		return true;
	}
	return false;
}

static bool SettingsSpecialistAllows(const FString& ToolId, const FJsonObject& ToolDef)
{
	(void)ToolId;
	return GetCategory(ToolDef) == TEXT("settings_properties");
}

static bool MaterialsSpecialistAllows(const FString& ToolId, const FJsonObject& ToolDef)
{
	const FString Category = GetCategory(ToolDef);
	if (Category == TEXT("materials_rendering"))
	{
		if (ToolId.StartsWith(TEXT("material_graph_")))
		{
			return false;
		}
		return true;
	}
	if (ToolId == TEXT("editor_get_selection") || ToolId == TEXT("actor_get_material_slots"))
	{
		return true;
	}
	if (ToolId == TEXT("asset_index_fuzzy_search"))
	{
		return true;
	}
	return false;
}

bool UnrealAiProductSpecialistToolPolicy::PassesSpecialistToolFilter(
	const EUnrealAiProductSpecialistId SpecialistId,
	const FString& ToolId,
	const FJsonObject& ToolDef)
{
	switch (SpecialistId)
	{
	case EUnrealAiProductSpecialistId::Scene:
		return SceneSpecialistAllows(ToolId, ToolDef);
	case EUnrealAiProductSpecialistId::Assets:
		return AssetsSpecialistAllows(ToolId, ToolDef);
	case EUnrealAiProductSpecialistId::Viewport:
		return ViewportSpecialistAllows(ToolId, ToolDef);
	case EUnrealAiProductSpecialistId::Diagnostics:
		return DiagnosticsSpecialistAllows(ToolId, ToolDef);
	case EUnrealAiProductSpecialistId::Playtest:
		return PlaytestSpecialistAllows(ToolId, ToolDef);
	case EUnrealAiProductSpecialistId::Animation:
		return AnimationSpecialistAllows(ToolId, ToolDef);
	case EUnrealAiProductSpecialistId::ProjectIntel:
		return ProjectIntelSpecialistAllows(ToolId, ToolDef);
	case EUnrealAiProductSpecialistId::EditorUi:
		return EditorUiSpecialistAllows(ToolId, ToolDef);
	case EUnrealAiProductSpecialistId::Settings:
		return SettingsSpecialistAllows(ToolId, ToolDef);
	case EUnrealAiProductSpecialistId::Materials:
		return MaterialsSpecialistAllows(ToolId, ToolDef);
	case EUnrealAiProductSpecialistId::None:
	default:
		return false;
	}
}
