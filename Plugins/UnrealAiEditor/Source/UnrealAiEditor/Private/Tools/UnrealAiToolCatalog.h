#pragma once

#include "CoreMinimal.h"
#include "Context/AgentContextTypes.h"
#include "Dom/JsonObject.h"
#include "Templates/Function.h"

struct FUnrealAiModelCapabilities;

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

	/** OpenAI `tools` JSON array filtered by agent mode + model capabilities. */
	void BuildOpenAiToolsJsonForMode(EUnrealAiAgentMode Mode, const FUnrealAiModelCapabilities& Caps, FString& OutJsonArray) const;

private:
	bool bLoaded = false;
	TSharedPtr<FJsonObject> Root;
	TMap<FString, TSharedPtr<FJsonObject>> ToolById;
};
