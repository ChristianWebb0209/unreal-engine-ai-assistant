#pragma once

#include "CoreMinimal.h"
#include "Context/AgentContextTypes.h"
#include "Dom/JsonObject.h"
#include "Templates/Function.h"

struct FUnrealAiModelCapabilities;

/** When `UNREAL_AI_TOOL_PACK=core`, only tools with catalog `context_selector.always_include_in_core_pack` plus `AdditionalToolIds` are sent (still subject to mode + native-tools caps). */
struct FUnrealAiToolPackOptions
{
	bool bRestrictToCorePack = false;
	TArray<FString> AdditionalToolIds;
};

/** Loads Resources/UnrealAiToolCatalog.json from the UnrealAiEditor plugin (single file, meta + tools[]). */
class FUnrealAiToolCatalog
{
public:
	bool LoadFromPlugin();

	bool IsLoaded() const { return bLoaded; }

	/** Full document root (meta + tools). */
	TSharedPtr<FJsonObject> GetRoot() const { return Root; }

	/** Find tool definition object by tool_id, or null. */
	TSharedPtr<FJsonObject> FindToolDefinition(const FString& ToolId) const;

	int32 GetToolCount() const { return ToolById.Num(); }

	/** Invoke Fn for each tool in deterministic order (sorted by tool_id). */
	void ForEachTool(TFunctionRef<void(const FString& ToolId, const TSharedPtr<FJsonObject>& Definition)> Fn) const;

	/**
	 * JSON array for HTTP `tools` (chat-completions function-calling shape used by many providers).
	 * Filtered by agent mode + model capabilities. `PackOptions == nullptr` keeps full mode-filtered catalog.
	 */
	void BuildLlmToolsJsonArrayForMode(
		EUnrealAiAgentMode Mode,
		const FUnrealAiModelCapabilities& Caps,
		const FUnrealAiToolPackOptions* PackOptions,
		FString& OutJsonArray) const;

	/**
	 * Single wrapper tool `unreal_ai_dispatch` so the HTTP `tools` array stays tiny; pair with BuildCompactToolIndexAppendix in the system message.
	 */
	void BuildUnrealAiDispatchToolsJson(
		EUnrealAiAgentMode Mode,
		const FUnrealAiModelCapabilities& Caps,
		const FUnrealAiToolPackOptions* PackOptions,
		FString& OutJsonArray) const;

	/** Markdown list of enabled tools (id + summary) for the same filter as BuildLlmToolsJsonArrayForMode. */
	void BuildCompactToolIndexAppendix(
		EUnrealAiAgentMode Mode,
		const FUnrealAiModelCapabilities& Caps,
		const FUnrealAiToolPackOptions* PackOptions,
		FString& OutMarkdown) const;

	/**
	 * Same filtering as BuildCompactToolIndexAppendix; invokes Fn once per enabled tool in deterministic tool_id order.
	 */
	void ForEachEnabledToolForMode(
		EUnrealAiAgentMode Mode,
		const FUnrealAiModelCapabilities& Caps,
		const FUnrealAiToolPackOptions* PackOptions,
		TFunctionRef<void(const FString& ToolId, const TSharedPtr<FJsonObject>& Definition)> Fn) const;

	/**
	 * Tiered markdown: first ExpandedCount tools include a compact JSON parameters excerpt; remaining tools are one line each.
	 * OrderedToolIds empty = same set as ForEachEnabled (sorted ids). GuardrailIds are evicted last when over MaxTotalChars.
	 */
	void BuildCompactToolIndexAppendixTiered(
		EUnrealAiAgentMode Mode,
		const FUnrealAiModelCapabilities& Caps,
		const FUnrealAiToolPackOptions* PackOptions,
		const TArray<FString>& OrderedToolIds,
		const TSet<FString>& GuardrailToolIds,
		int32 ExpandedCount,
		int32 MaxTotalChars,
		FString& OutMarkdown) const;

	/** Minified parameters object JSON for repair prompts; false if unknown tool. */
	bool TryGetToolParametersJsonString(const FString& ToolId, FString& OutParametersJson) const;

private:
	bool bLoaded = false;
	TSharedPtr<FJsonObject> Root;
	TMap<FString, TSharedPtr<FJsonObject>> ToolById;
};
