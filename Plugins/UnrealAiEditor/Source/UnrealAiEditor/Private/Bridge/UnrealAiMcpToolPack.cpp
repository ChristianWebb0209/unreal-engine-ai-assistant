#include "Bridge/UnrealAiMcpToolPack.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tools/UnrealAiToolCatalog.h"

bool FUnrealAiMcpToolPack::LoadFromPlugin()
{
	bLoaded = false;
	PackVersion.Reset();
	CatalogVersionPin.Reset();
	AllowedToolIdsOrdered.Empty();
	AllowedToolIds.Empty();

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealAiEditor"));
	if (!Plugin.IsValid())
	{
		return false;
	}

	const FString Path = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/mcp/mcp-tool-pack.v1.json"));
	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *Path))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	Root->TryGetStringField(TEXT("pack_version"), PackVersion);
	Root->TryGetStringField(TEXT("catalog_version_pin"), CatalogVersionPin);

	const TArray<TSharedPtr<FJsonValue>>* ToolsArray = nullptr;
	if (!Root->TryGetArrayField(TEXT("tools"), ToolsArray) || !ToolsArray)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& V : *ToolsArray)
	{
		const TSharedPtr<FJsonObject> Obj = V.IsValid() ? V->AsObject() : nullptr;
		if (!Obj.IsValid())
		{
			continue;
		}
		FString ToolId;
		if (Obj->TryGetStringField(TEXT("tool_id"), ToolId) && !ToolId.IsEmpty())
		{
			if (!AllowedToolIds.Contains(ToolId))
			{
				AllowedToolIdsOrdered.Add(ToolId);
				AllowedToolIds.Add(ToolId);
			}
		}
	}

	bLoaded = AllowedToolIdsOrdered.Num() > 0;
	return bLoaded;
}

bool FUnrealAiMcpToolPack::IsAllowed(const FString& ToolId) const
{
	return bLoaded && AllowedToolIds.Contains(ToolId);
}

bool FUnrealAiMcpToolPack::BuildToolListJson(
	const FUnrealAiToolCatalog& Catalog,
	TSharedPtr<FJsonObject>& OutRoot) const
{
	OutRoot = MakeShared<FJsonObject>();
	if (!bLoaded || !Catalog.IsLoaded())
	{
		OutRoot->SetBoolField(TEXT("ok"), false);
		OutRoot->SetStringField(TEXT("error"), TEXT("Tool pack or catalog not loaded"));
		return false;
	}

	OutRoot->SetBoolField(TEXT("ok"), true);
	OutRoot->SetStringField(TEXT("pack_version"), PackVersion);
	OutRoot->SetStringField(TEXT("catalog_version_pin"), CatalogVersionPin);

	TArray<TSharedPtr<FJsonValue>> ToolsJson;
	for (const FString& ToolId : AllowedToolIdsOrdered)
	{
		const TSharedPtr<FJsonObject> Def = Catalog.FindToolDefinition(ToolId);
		if (!Def.IsValid())
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("tool_id"), ToolId);

		FString Summary;
		if (Def->TryGetStringField(TEXT("summary"), Summary))
		{
			Entry->SetStringField(TEXT("summary"), Summary);
		}

		const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
		if (Def->TryGetObjectField(TEXT("parameters"), ParamsObj) && ParamsObj && ParamsObj->IsValid())
		{
			Entry->SetObjectField(TEXT("parameters"), *ParamsObj);
		}
		else
		{
			TSharedPtr<FJsonObject> EmptyParams = MakeShared<FJsonObject>();
			EmptyParams->SetStringField(TEXT("type"), TEXT("object"));
			Entry->SetObjectField(TEXT("parameters"), EmptyParams);
		}

		FString Permission;
		if (Def->TryGetStringField(TEXT("permission"), Permission))
		{
			Entry->SetStringField(TEXT("permission"), Permission);
		}
		FString SideEffects;
		if (Def->TryGetStringField(TEXT("side_effects"), SideEffects))
		{
			Entry->SetStringField(TEXT("side_effects"), SideEffects);
		}
		FString Status;
		if (Def->TryGetStringField(TEXT("status"), Status))
		{
			Entry->SetStringField(TEXT("status"), Status);
		}

		ToolsJson.Add(MakeShared<FJsonValueObject>(Entry));
	}

	OutRoot->SetArrayField(TEXT("tools"), ToolsJson);
	OutRoot->SetNumberField(TEXT("tool_count"), ToolsJson.Num());
	return true;
}
