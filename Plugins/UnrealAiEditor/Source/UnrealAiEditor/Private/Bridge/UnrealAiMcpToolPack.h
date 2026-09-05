#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FUnrealAiToolCatalog;

/** Loads Resources/mcp/mcp-tool-pack.v1.json and gates MCP bridge invokes. */
class FUnrealAiMcpToolPack
{
public:
	bool LoadFromPlugin();

	bool IsLoaded() const { return bLoaded; }

	bool IsAllowed(const FString& ToolId) const;

	FString GetPackVersion() const { return PackVersion; }

	FString GetCatalogVersionPin() const { return CatalogVersionPin; }

	int32 GetToolCount() const { return AllowedToolIdsOrdered.Num(); }

	const TArray<FString>& GetAllowedToolIdsOrdered() const { return AllowedToolIdsOrdered; }

	/** Build GET /v1/tools payload from allowlist + merged catalog definitions. */
	bool BuildToolListJson(const FUnrealAiToolCatalog& Catalog, TSharedPtr<FJsonObject>& OutRoot) const;

private:
	bool bLoaded = false;
	FString PackVersion;
	FString CatalogVersionPin;
	TArray<FString> AllowedToolIdsOrdered;
	TSet<FString> AllowedToolIds;
};
