#include "Tools/UnrealAiToolCatalog.h"

#include "Containers/Set.h"
#include "Dom/JsonValue.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealAiToolCatalogPriv
{
	static bool PassesModeAndPack(
		const TSharedPtr<FJsonObject>& Obj,
		EUnrealAiAgentMode Mode,
		const FUnrealAiToolPackOptions* PackOptions,
		const TSet<FString>& ExtraIdSet,
		FString& OutTid)
	{
		OutTid.Reset();
		if (!Obj.IsValid())
		{
			return false;
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
					(*Modes)->TryGetBoolField(TEXT("fast"), bInclude);
				}
				break;
			case EUnrealAiAgentMode::Plan:
				(*Modes)->TryGetBoolField(TEXT("plan"), bInclude);
				if (!bInclude)
				{
					(*Modes)->TryGetBoolField(TEXT("agent"), bInclude);
				}
				break;
			default:
				break;
			}
		}
		if (!bInclude)
		{
			return false;
		}
		Obj->TryGetStringField(TEXT("tool_id"), OutTid);
		if (OutTid.IsEmpty())
		{
			return false;
		}
		if (PackOptions && PackOptions->bRestrictToCorePack)
		{
			bool bCorePack = false;
			const TSharedPtr<FJsonObject>* ContextSel = nullptr;
			if (Obj->TryGetObjectField(TEXT("context_selector"), ContextSel) && ContextSel && (*ContextSel).IsValid())
			{
				(*ContextSel)->TryGetBoolField(TEXT("always_include_in_core_pack"), bCorePack);
			}
			const bool bExtraListed = ExtraIdSet.Contains(OutTid);
			if (!bCorePack && !bExtraListed)
			{
				return false;
			}
		}
		return true;
	}
} // namespace UnrealAiToolCatalogPriv

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

void FUnrealAiToolCatalog::ForEachEnabledToolForMode(
	EUnrealAiAgentMode Mode,
	const FUnrealAiModelCapabilities& Caps,
	const FUnrealAiToolPackOptions* PackOptions,
	TFunctionRef<void(const FString& ToolId, const TSharedPtr<FJsonObject>& Definition)> Fn) const
{
	if (!Caps.bSupportsNativeTools || !bLoaded)
	{
		return;
	}
	TSet<FString> ExtraIdSet;
	if (PackOptions && PackOptions->bRestrictToCorePack)
	{
		for (const FString& Id : PackOptions->AdditionalToolIds)
		{
			if (!Id.IsEmpty())
			{
				ExtraIdSet.Add(Id);
			}
		}
	}
	TArray<FString> Keys;
	ToolById.GetKeys(Keys);
	Keys.Sort();
	for (const FString& Key : Keys)
	{
		const TSharedPtr<FJsonObject>* ObjPtr = ToolById.Find(Key);
		if (!ObjPtr || !ObjPtr->IsValid())
		{
			continue;
		}
		FString Tid;
		if (!UnrealAiToolCatalogPriv::PassesModeAndPack(*ObjPtr, Mode, PackOptions, ExtraIdSet, Tid))
		{
			continue;
		}
		Fn(Tid, *ObjPtr);
	}
}

void FUnrealAiToolCatalog::BuildLlmToolsJsonArrayForMode(
	EUnrealAiAgentMode Mode,
	const FUnrealAiModelCapabilities& Caps,
	const FUnrealAiToolPackOptions* PackOptions,
	FString& OutJsonArray) const
{
	OutJsonArray.Reset();
	if (!Caps.bSupportsNativeTools || !bLoaded)
	{
		return;
	}
	TArray<TSharedPtr<FJsonValue>> ToolsOut;
	ForEachEnabledToolForMode(
		Mode,
		Caps,
		PackOptions,
		[&](const FString& Tid, const TSharedPtr<FJsonObject>& Obj)
	{
		FString Summary;
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
	});
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	if (!FJsonSerializer::Serialize(ToolsOut, W))
	{
		return;
	}
	OutJsonArray = Out;
}

void FUnrealAiToolCatalog::BuildUnrealAiDispatchToolsJson(
	EUnrealAiAgentMode Mode,
	const FUnrealAiModelCapabilities& Caps,
	const FUnrealAiToolPackOptions* PackOptions,
	FString& OutJsonArray) const
{
	OutJsonArray.Reset();
	if (!Caps.bSupportsNativeTools || !bLoaded)
	{
		OutJsonArray = TEXT("[]");
		return;
	}
	TSharedPtr<FJsonObject> ToolIdProp = MakeShared<FJsonObject>();
	ToolIdProp->SetStringField(TEXT("type"), TEXT("string"));
	ToolIdProp->SetStringField(
		TEXT("description"),
		TEXT("Catalog tool_id listed in the Unreal tool index section of the system/developer message."));
	TSharedPtr<FJsonObject> ArgsProp = MakeShared<FJsonObject>();
	ArgsProp->SetStringField(TEXT("type"), TEXT("object"));
	ArgsProp->SetStringField(
		TEXT("description"),
		TEXT("Arguments object for that tool; must satisfy the tool JSON schema (often in plugin Resources/UnrealAiToolCatalog.json)."));
	ArgsProp->SetBoolField(TEXT("additionalProperties"), true);
	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
	Props->SetObjectField(TEXT("tool_id"), ToolIdProp);
	Props->SetObjectField(TEXT("arguments"), ArgsProp);
	TArray<TSharedPtr<FJsonValue>> Req;
	Req.Add(MakeShared<FJsonValueString>(TEXT("tool_id")));
	Req.Add(MakeShared<FJsonValueString>(TEXT("arguments")));
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("type"), TEXT("object"));
	Params->SetObjectField(TEXT("properties"), Props);
	Params->SetArrayField(TEXT("required"), Req);
	TSharedPtr<FJsonObject> Func = MakeShared<FJsonObject>();
	Func->SetStringField(TEXT("name"), TEXT("unreal_ai_dispatch"));
	Func->SetStringField(
		TEXT("description"),
		TEXT("Invoke one Unreal Editor tool. Use tool_id + arguments from the compact tool index in the system message."));
	Func->SetObjectField(TEXT("parameters"), Params);
	TSharedPtr<FJsonObject> Wrap = MakeShared<FJsonObject>();
	Wrap->SetStringField(TEXT("type"), TEXT("function"));
	Wrap->SetObjectField(TEXT("function"), Func);
	TArray<TSharedPtr<FJsonValue>> ToolsOut;
	ToolsOut.Add(MakeShared<FJsonValueObject>(Wrap.ToSharedRef()));
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	if (!FJsonSerializer::Serialize(ToolsOut, W))
	{
		OutJsonArray = TEXT("[]");
		return;
	}
	OutJsonArray = Out;
}

void FUnrealAiToolCatalog::BuildCompactToolIndexAppendix(
	EUnrealAiAgentMode Mode,
	const FUnrealAiModelCapabilities& Caps,
	const FUnrealAiToolPackOptions* PackOptions,
	FString& OutMarkdown) const
{
	TArray<FString> EmptyOrder;
	const TSet<FString> EmptyGuard;
	BuildCompactToolIndexAppendixTiered(Mode, Caps, PackOptions, EmptyOrder, EmptyGuard, 0, 2000000000, OutMarkdown);
}

bool FUnrealAiToolCatalog::TryGetToolParametersJsonString(const FString& ToolId, FString& OutParametersJson) const
{
	OutParametersJson.Reset();
	const TSharedPtr<FJsonObject> Def = FindToolDefinition(ToolId);
	if (!Def.IsValid())
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* Params = nullptr;
	if (!Def->TryGetObjectField(TEXT("parameters"), Params) || !Params || !(*Params).IsValid())
	{
		return false;
	}
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutParametersJson);
	if (!FJsonSerializer::Serialize((*Params).ToSharedRef(), W))
	{
		return false;
	}
	return true;
}

void FUnrealAiToolCatalog::BuildCompactToolIndexAppendixTiered(
	EUnrealAiAgentMode Mode,
	const FUnrealAiModelCapabilities& Caps,
	const FUnrealAiToolPackOptions* PackOptions,
	const TArray<FString>& OrderedToolIds,
	const TSet<FString>& GuardrailToolIds,
	int32 ExpandedCount,
	int32 MaxTotalChars,
	FString& OutMarkdown) const
{
	OutMarkdown.Reset();
	if (!Caps.bSupportsNativeTools || !bLoaded)
	{
		return;
	}

	TArray<FString> Order = OrderedToolIds;
	if (Order.Num() == 0)
	{
		ForEachEnabledToolForMode(
			Mode,
			Caps,
			PackOptions,
			[&](const FString& Tid, const TSharedPtr<FJsonObject>&)
			{
				Order.Add(Tid);
			});
	}

	struct FSeg
	{
		FString Text;
		bool bGuardrail = false;
	};
	TArray<FSeg> Segments;
	auto AppendOneLiner = [&](const FString& Tid, const TSharedPtr<FJsonObject>& Obj)
	{
		FString Summary;
		Obj->TryGetStringField(TEXT("summary"), Summary);
		FSeg S;
		S.Text = FString::Printf(TEXT("- `%s`: %s"), *Tid, *Summary);
		S.bGuardrail = GuardrailToolIds.Contains(Tid);
		Segments.Add(S);
	};
	auto AppendExpanded = [&](const FString& Tid, const TSharedPtr<FJsonObject>& Obj)
	{
		FString Summary;
		Obj->TryGetStringField(TEXT("summary"), Summary);
		FString ParamsJson;
		TryGetToolParametersJsonString(Tid, ParamsJson);
		if (ParamsJson.Len() > 1200)
		{
			ParamsJson.LeftInline(1200);
			ParamsJson += TEXT("...");
		}
		FSeg S;
		S.Text = FString::Printf(
			TEXT("- `%s`: %s\n  Parameters (excerpt): `%s`"),
			*Tid,
			*Summary,
			*ParamsJson);
		S.bGuardrail = GuardrailToolIds.Contains(Tid);
		Segments.Add(S);
	};

	int32 Idx = 0;
	for (const FString& Tid : Order)
	{
		const TSharedPtr<FJsonObject> Obj = FindToolDefinition(Tid);
		if (!Obj.IsValid())
		{
			continue;
		}
		FString DummyTid;
		TSet<FString> ExtraIdSet;
		if (PackOptions && PackOptions->bRestrictToCorePack)
		{
			for (const FString& Id : PackOptions->AdditionalToolIds)
			{
				if (!Id.IsEmpty())
				{
					ExtraIdSet.Add(Id);
				}
			}
		}
		if (!UnrealAiToolCatalogPriv::PassesModeAndPack(Obj, Mode, PackOptions, ExtraIdSet, DummyTid))
		{
			continue;
		}
		if (ExpandedCount > 0 && Idx < ExpandedCount)
		{
			AppendExpanded(Tid, Obj);
		}
		else
		{
			AppendOneLiner(Tid, Obj);
		}
		++Idx;
	}

	auto Join = [&]() -> FString
	{
		TArray<FString> Parts;
		for (const FSeg& S : Segments)
		{
			Parts.Add(S.Text);
		}
		return FString::Join(Parts, LINE_TERMINATOR);
	};

	FString Current = Join();
	while (Current.Len() > MaxTotalChars && Segments.Num() > 0)
	{
		int32 RemoveAt = INDEX_NONE;
		for (int32 I = Segments.Num() - 1; I >= 0; --I)
		{
			if (!Segments[I].bGuardrail)
			{
				RemoveAt = I;
				break;
			}
		}
		if (RemoveAt == INDEX_NONE)
		{
			break;
		}
		Segments.RemoveAt(RemoveAt);
		Current = Join();
	}
	OutMarkdown = Current;
}
