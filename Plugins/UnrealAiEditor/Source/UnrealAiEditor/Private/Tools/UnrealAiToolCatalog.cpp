#include "Tools/UnrealAiToolCatalog.h"

#include "Dom/JsonValue.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

bool FUnrealAiToolCatalog::LoadFromPlugin()
{
	bLoaded = false;
	Root.Reset();
	ToolById.Empty();

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealAiEditor"));
	if (!Plugin.IsValid())
	{
		return false;
	}

	const FString Path = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("UnrealAiToolCatalog.json"));
	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *Path))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Parsed;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ToolsArray = nullptr;
	if (!Parsed->TryGetArrayField(TEXT("tools"), ToolsArray) || !ToolsArray)
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
		FString Tid;
		if (!Obj->TryGetStringField(TEXT("tool_id"), Tid) || Tid.IsEmpty())
		{
			continue;
		}
		ToolById.Add(Tid, Obj);
	}

	Root = Parsed;
	bLoaded = true;
	return true;
}

TSharedPtr<FJsonObject> FUnrealAiToolCatalog::FindToolDefinition(const FString& ToolId) const
{
	if (const TSharedPtr<FJsonObject>* Found = ToolById.Find(ToolId))
	{
		return *Found;
	}
	return nullptr;
}

void FUnrealAiToolCatalog::ForEachTool(TFunctionRef<void(const FString& ToolId, const TSharedPtr<FJsonObject>& Definition)> Fn) const
{
	TArray<FString> Keys;
	ToolById.GetKeys(Keys);
	Keys.Sort();
	for (const FString& Key : Keys)
	{
		const TSharedPtr<FJsonObject>* Def = ToolById.Find(Key);
		if (Def && Def->IsValid())
		{
			Fn(Key, *Def);
		}
	}
}

void FUnrealAiToolCatalog::BuildOpenAiToolsJsonForMode(
	EUnrealAiAgentMode Mode,
	const FUnrealAiModelCapabilities& Caps,
	FString& OutJsonArray) const
{
	OutJsonArray.Reset();
	if (!Caps.bSupportsNativeTools || !bLoaded)
	{
		return;
	}
	TArray<TSharedPtr<FJsonValue>> ToolsOut;
	for (const TPair<FString, TSharedPtr<FJsonObject>>& Pair : ToolById)
	{
		const TSharedPtr<FJsonObject>& Obj = Pair.Value;
		if (!Obj.IsValid())
		{
			continue;
		}
		bool bInclude = false;
		const TSharedPtr<FJsonObject>* Modes = nullptr;
		if (Obj->TryGetObjectField(TEXT("modes"), Modes) && Modes && (*Modes).IsValid())
		{
			switch (Mode)
			{
			case EUnrealAiAgentMode::Ask:
				(*Modes)->TryGetBoolField(TEXT("ask"), bInclude);
				break;
			case EUnrealAiAgentMode::Agent:
				(*Modes)->TryGetBoolField(TEXT("agent"), bInclude);
				if (!bInclude)
				{
					// Back-compat with catalogs that only define fast during migration.
					(*Modes)->TryGetBoolField(TEXT("fast"), bInclude);
				}
				break;
			case EUnrealAiAgentMode::Orchestrate:
				(*Modes)->TryGetBoolField(TEXT("orchestrate"), bInclude);
				if (!bInclude)
				{
					// Default to agent tool surface unless orchestrate is explicitly split.
					(*Modes)->TryGetBoolField(TEXT("agent"), bInclude);
				}
				break;
			default:
				break;
			}
		}
		if (!bInclude)
		{
			continue;
		}
		FString Tid;
		FString Summary;
		Obj->TryGetStringField(TEXT("tool_id"), Tid);
		Obj->TryGetStringField(TEXT("summary"), Summary);
		const TSharedPtr<FJsonObject>* Params = nullptr;
		TSharedPtr<FJsonObject> ParamsToUse;
		if (Obj->TryGetObjectField(TEXT("parameters"), Params) && Params->IsValid())
		{
			ParamsToUse = *Params;
		}
		else
		{
			ParamsToUse = MakeShared<FJsonObject>();
			ParamsToUse->SetStringField(TEXT("type"), TEXT("object"));
		}
		TSharedPtr<FJsonObject> Func = MakeShared<FJsonObject>();
		Func->SetStringField(TEXT("name"), Tid);
		Func->SetStringField(TEXT("description"), Summary);
		Func->SetObjectField(TEXT("parameters"), ParamsToUse);
		TSharedPtr<FJsonObject> Wrap = MakeShared<FJsonObject>();
		Wrap->SetStringField(TEXT("type"), TEXT("function"));
		Wrap->SetObjectField(TEXT("function"), Func);
		ToolsOut.Add(MakeShared<FJsonValueObject>(Wrap.ToSharedRef()));
	}
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	if (!FJsonSerializer::Serialize(ToolsOut, W))
	{
		return;
	}
	OutJsonArray = Out;
}
