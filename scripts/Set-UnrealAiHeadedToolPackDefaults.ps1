#requires -Version 5.1
# Dot-source after Import-RepoDotenv so child UnrealEditor inherits smaller tool payloads.
# Override in .env: UNREAL_AI_TOOL_PACK=full restores full catalog; UNREAL_AI_TOOL_PACK_EXTRA=... merges IDs with core.
# UNREAL_AI_TOOL_SURFACE=native restores one OpenAI function per catalog tool (large JSON).

if ([string]::IsNullOrWhiteSpace($env:UNREAL_AI_TOOL_SURFACE)) {
    $env:UNREAL_AI_TOOL_SURFACE = 'dispatch'
}

if ([string]::IsNullOrWhiteSpace($env:UNREAL_AI_TOOL_PACK)) {
    $env:UNREAL_AI_TOOL_PACK = 'core'
}
if ([string]::IsNullOrWhiteSpace($env:UNREAL_AI_TOOL_PACK_EXTRA)) {
    $env:UNREAL_AI_TOOL_PACK_EXTRA =
        'asset_create,asset_registry_query,blueprint_compile,blueprint_apply_ir,blueprint_get_graph_summary,blueprint_open_graph_tab,blueprint_add_variable,audio_component_preview,project_file_read_text,actor_spawn_from_class,console_command'
}

# When running headed live/editor-driven scenarios, allow destructive tools to auto-confirm
# so the agent doesn't need an extra LLM round just to set `confirm:true`.
# Can be overridden by setting UNREAL_AI_AUTO_RUN_DESTRUCTIVE=0/false in the environment.
if ([string]::IsNullOrWhiteSpace($env:UNREAL_AI_AUTO_RUN_DESTRUCTIVE)) {
    $env:UNREAL_AI_AUTO_RUN_DESTRUCTIVE = '1'
}
