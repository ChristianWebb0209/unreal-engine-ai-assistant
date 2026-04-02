#include "Tools/UnrealAiToolDispatch_SettingsProperties.h"

#include "UnrealAiEditorModule.h"
#include "UnrealAiEditorSettings.h"
#include "Tools/UnrealAiToolActorLookup.h"
#include "Tools/UnrealAiToolJson.h"

#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "UnrealEdGlobals.h"
#include "Tools/UnrealAiToolDispatch_Viewport.h"
#include "Tools/UnrealAiToolDispatch_Actors.h"
#include "Tools/UnrealAiToolDispatch_EditorUi.h"
#include "Tools/UnrealAiProjectIniSettingsAllowlist.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAiSettingsProps
{
	static bool TryGetValueField(const TSharedPtr<FJsonObject>& Args, TSharedPtr<FJsonValue>& OutValue)
	{
		if (!Args.IsValid() || !Args->HasField(TEXT("value")))
		{
			return false;
		}
		OutValue = Args->TryGetField(TEXT("value"));
		return OutValue.IsValid();
	}

	static FUnrealAiToolInvocationResult BuildUnknownEntityPropertyError(const FString& EntityType, const FString& Property)
	{
		return UnrealAiToolJson::Error(FString::Printf(
			TEXT("Unsupported entity property '%s' for entity_type '%s'."),
			*Property,
			*EntityType));
	}
}

FUnrealAiToolInvocationResult UnrealAiDispatch_EntityGetProperty(const TSharedPtr<FJsonObject>& Args)
{
	FString EntityType;
	FString EntityRef;
	FString Property;
	if (!Args->TryGetStringField(TEXT("entity_type"), EntityType) || EntityType.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("entity_type is required"));
	}
	if (!Args->TryGetStringField(TEXT("entity_ref"), EntityRef) || EntityRef.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("entity_ref is required"));
	}
	if (!Args->TryGetStringField(TEXT("property"), Property) || Property.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("property is required"));
	}

	const FString EntityTypeL = EntityType.ToLower();
	const FString PropertyL = Property.ToLower();
	if (EntityTypeL == TEXT("actor"))
	{
		if (PropertyL == TEXT("hidden_in_editor") || PropertyL == TEXT("visibility") || PropertyL == TEXT("visible"))
		{
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("actor_path"), EntityRef);
			const FUnrealAiToolInvocationResult R = UnrealAiDispatch_ActorGetVisibility(A);
			if (!R.bOk)
			{
				return R;
			}
			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetBoolField(TEXT("ok"), true);
			Out->SetStringField(TEXT("entity_type"), TEXT("actor"));
			Out->SetStringField(TEXT("entity_ref"), EntityRef);
			Out->SetStringField(TEXT("property"), Property);
			TSharedPtr<FJsonObject> Parsed;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
			if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
			{
				bool bHidden = false;
				Parsed->TryGetBoolField(TEXT("hidden"), bHidden);
				if (PropertyL == TEXT("visible"))
				{
					Out->SetBoolField(TEXT("value"), !bHidden);
				}
				else if (PropertyL == TEXT("visibility"))
				{
					Out->SetStringField(TEXT("value"), bHidden ? TEXT("hidden") : TEXT("visible"));
				}
				else
				{
					Out->SetBoolField(TEXT("value"), bHidden);
				}
			}
			return UnrealAiToolJson::Ok(Out);
		}
		return UnrealAiSettingsProps::BuildUnknownEntityPropertyError(EntityType, Property);
	}
	return UnrealAiToolJson::Error(TEXT("entity_type must be 'actor'"));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_EntitySetProperty(const TSharedPtr<FJsonObject>& Args)
{
	FString EntityType;
	FString EntityRef;
	FString Property;
	if (!Args->TryGetStringField(TEXT("entity_type"), EntityType) || EntityType.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("entity_type is required"));
	}
	if (!Args->TryGetStringField(TEXT("entity_ref"), EntityRef) || EntityRef.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("entity_ref is required"));
	}
	if (!Args->TryGetStringField(TEXT("property"), Property) || Property.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("property is required"));
	}

	TSharedPtr<FJsonValue> Value;
	if (!UnrealAiSettingsProps::TryGetValueField(Args, Value))
	{
		return UnrealAiToolJson::Error(TEXT("value is required"));
	}

	const FString EntityTypeL = EntityType.ToLower();
	const FString PropertyL = Property.ToLower();
	if (EntityTypeL == TEXT("actor"))
	{
		if (PropertyL == TEXT("hidden_in_editor") || PropertyL == TEXT("visible"))
		{
			bool bIn = false;
			if (!Value->TryGetBool(bIn))
			{
				return UnrealAiToolJson::Error(TEXT("value must be boolean for actor visibility properties"));
			}
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("actor_path"), EntityRef);
			A->SetBoolField(TEXT("hidden"), PropertyL == TEXT("visible") ? !bIn : bIn);
			const FUnrealAiToolInvocationResult R = UnrealAiDispatch_ActorSetVisibility(A);
			if (!R.bOk)
			{
				return R;
			}
			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetBoolField(TEXT("ok"), true);
			Out->SetStringField(TEXT("entity_type"), TEXT("actor"));
			Out->SetStringField(TEXT("entity_ref"), EntityRef);
			Out->SetStringField(TEXT("property"), Property);
			Out->SetField(TEXT("value"), MakeShared<FJsonValueBoolean>(bIn));
			return UnrealAiToolJson::Ok(Out);
		}
		return UnrealAiSettingsProps::BuildUnknownEntityPropertyError(EntityType, Property);
	}
	return UnrealAiToolJson::Error(TEXT("entity_type must be 'actor'"));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_SettingsGet(const TSharedPtr<FJsonObject>& Args)
{
	FString Scope;
	FString Key;
	Args->TryGetStringField(TEXT("scope"), Scope);
	Args->TryGetStringField(TEXT("key"), Key);
	if (Scope.IsEmpty() || Key.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("scope and key are required"));
	}
	const FString ScopeL = Scope.ToLower();
	const FString KeyL = Key.ToLower();

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("scope"), Scope);
	O->SetStringField(TEXT("key"), Key);

	if (ScopeL == TEXT("viewport") && KeyL == TEXT("view_mode"))
	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatch_ViewportGetViewMode(MakeShared<FJsonObject>());
		if (!R.bOk)
		{
			return R;
		}
		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
		{
			FString Mode;
			Parsed->TryGetStringField(TEXT("view_mode"), Mode);
			O->SetStringField(TEXT("value"), Mode);
		}
		return UnrealAiToolJson::Ok(O);
	}
	if (ScopeL == TEXT("editor") && KeyL == TEXT("mode"))
	{
		const FUnrealAiToolInvocationResult R = UnrealAiDispatch_EditorGetMode(MakeShared<FJsonObject>());
		if (!R.bOk)
		{
			return R;
		}
		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ContentForModel);
		if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
		{
			FString Mode;
			Parsed->TryGetStringField(TEXT("mode_id"), Mode);
			O->SetStringField(TEXT("value"), Mode);
		}
		return UnrealAiToolJson::Ok(O);
	}
	if (ScopeL == TEXT("editor") && KeyL == TEXT("editor_focus"))
	{
		O->SetBoolField(TEXT("value"), FUnrealAiEditorModule::IsEditorFocusEnabled());
		return UnrealAiToolJson::Ok(O);
	}
	if (ScopeL == TEXT("editor") && KeyL == TEXT("pie_tools_enabled"))
	{
		O->SetBoolField(TEXT("value"), FUnrealAiEditorModule::IsPieToolsEnabled());
		return UnrealAiToolJson::Ok(O);
	}
	if (ScopeL == TEXT("editor") && KeyL == TEXT("auto_confirm_destructive"))
	{
		O->SetBoolField(TEXT("value"), FUnrealAiEditorModule::IsAutoConfirmDestructiveEnabled());
		return UnrealAiToolJson::Ok(O);
	}
	if (ScopeL == TEXT("editor") && KeyL == TEXT("stream_llm_chat"))
	{
		const UUnrealAiEditorSettings* S = GetDefault<UUnrealAiEditorSettings>();
		O->SetBoolField(TEXT("value"), S ? S->bStreamLlmChat : true);
		return UnrealAiToolJson::Ok(O);
	}
	if (ScopeL == TEXT("project") && KeyL.StartsWith(TEXT("/script/")))
	{
		FString KeyIn = Key;
		int32 ColonIdx = INDEX_NONE;
		if (!KeyIn.FindChar(TEXT(':'), ColonIdx) || ColonIdx <= 0 || ColonIdx >= KeyIn.Len() - 1)
		{
			return UnrealAiToolJson::Error(TEXT("project scope expects key format '/Script/Section:ConfigKey'"));
		}
		const FString Section = KeyIn.Left(ColonIdx);
		const FString Name = KeyIn.Mid(ColonIdx + 1);
		FString Value;
		if (!GConfig->GetString(*Section, *Name, Value, GEngineIni))
		{
			return UnrealAiToolJson::Error(TEXT("project setting key not found in engine ini"));
		}
		O->SetStringField(TEXT("value"), Value);
		return UnrealAiToolJson::Ok(O);
	}
	return UnrealAiToolJson::Error(TEXT("Unsupported scope/key pair for settings_get"));
}

FUnrealAiToolInvocationResult UnrealAiDispatch_SettingsSet(const TSharedPtr<FJsonObject>& Args)
{
	FString Scope;
	FString Key;
	Args->TryGetStringField(TEXT("scope"), Scope);
	Args->TryGetStringField(TEXT("key"), Key);
	if (Scope.IsEmpty() || Key.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("scope and key are required"));
	}
	TSharedPtr<FJsonValue> Value;
	if (!UnrealAiSettingsProps::TryGetValueField(Args, Value))
	{
		return UnrealAiToolJson::Error(TEXT("value is required"));
	}
	const FString ScopeL = Scope.ToLower();
	const FString KeyL = Key.ToLower();

	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("scope"), Scope);
	O->SetStringField(TEXT("key"), Key);

	if (ScopeL == TEXT("viewport") && KeyL == TEXT("view_mode"))
	{
		FString ViewMode;
		if (!Value->TryGetString(ViewMode) || ViewMode.IsEmpty())
		{
			return UnrealAiToolJson::Error(TEXT("value must be string for viewport view_mode"));
		}
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("view_mode"), ViewMode);
		const FUnrealAiToolInvocationResult R = UnrealAiDispatch_ViewportSetViewMode(A);
		if (!R.bOk)
		{
			return R;
		}
		O->SetStringField(TEXT("value"), ViewMode);
		return UnrealAiToolJson::Ok(O);
	}
	if (ScopeL == TEXT("editor") && KeyL == TEXT("mode"))
	{
		FString Mode;
		if (!Value->TryGetString(Mode) || Mode.IsEmpty())
		{
			return UnrealAiToolJson::Error(TEXT("value must be string for editor mode"));
		}
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("mode_id"), Mode);
		const FUnrealAiToolInvocationResult R = UnrealAiDispatch_EditorSetMode(A);
		if (!R.bOk)
		{
			return R;
		}
		O->SetStringField(TEXT("value"), Mode);
		return UnrealAiToolJson::Ok(O);
	}
	if (ScopeL == TEXT("editor") && KeyL == TEXT("editor_focus"))
	{
		bool bEnabled = false;
		if (!Value->TryGetBool(bEnabled))
		{
			return UnrealAiToolJson::Error(TEXT("value must be boolean for editor_focus"));
		}
		FUnrealAiEditorModule::SetEditorFocusEnabled(bEnabled);
		O->SetBoolField(TEXT("value"), bEnabled);
		return UnrealAiToolJson::Ok(O);
	}
	if (ScopeL == TEXT("editor") && KeyL == TEXT("auto_confirm_destructive"))
	{
		bool bEnabled = false;
		if (!Value->TryGetBool(bEnabled))
		{
			return UnrealAiToolJson::Error(TEXT("value must be boolean for auto_confirm_destructive"));
		}
		FUnrealAiEditorModule::SetAutoConfirmDestructiveEnabled(bEnabled);
		O->SetBoolField(TEXT("value"), bEnabled);
		return UnrealAiToolJson::Ok(O);
	}
	if (ScopeL == TEXT("editor") && KeyL == TEXT("stream_llm_chat"))
	{
		bool bEnabled = false;
		if (!Value->TryGetBool(bEnabled))
		{
			return UnrealAiToolJson::Error(TEXT("value must be boolean for stream_llm_chat"));
		}
		if (UUnrealAiEditorSettings* S = GetMutableDefault<UUnrealAiEditorSettings>())
		{
			S->bStreamLlmChat = bEnabled;
			S->SaveConfig();
		}
		O->SetBoolField(TEXT("value"), bEnabled);
		return UnrealAiToolJson::Ok(O);
	}
	if (ScopeL == TEXT("project") && KeyL.StartsWith(TEXT("/script/")))
	{
		FString KeyIn = Key;
		int32 ColonIdx = INDEX_NONE;
		if (!KeyIn.FindChar(TEXT(':'), ColonIdx) || ColonIdx <= 0 || ColonIdx >= KeyIn.Len() - 1)
		{
			return UnrealAiToolJson::Error(TEXT("project scope expects key format '/Script/Section:ConfigKey'"));
		}
		const FString Section = KeyIn.Left(ColonIdx);
		const FString Name = KeyIn.Mid(ColonIdx + 1);
		if (!UnrealAiIsAllowlistedProjectEngineIniWrite(Section, Name))
		{
			return UnrealAiToolJson::Error(
				FString::Printf(
					TEXT("project-scope engine INI key '%s:%s' is not allowlisted for settings_set. "
						 "Prefer drafts under Saved/UnrealAiEditorAgent/ or extend UnrealAiProjectIniSettingsAllowlist.cpp."),
					*Section,
					*Name));
		}
		FString ValueString;
		if (!Value->TryGetString(ValueString))
		{
			bool B = false;
			double N = 0.0;
			if (Value->TryGetBool(B))
			{
				ValueString = B ? TEXT("True") : TEXT("False");
			}
			else if (Value->TryGetNumber(N))
			{
				ValueString = FString::SanitizeFloat(N);
			}
			else
			{
				return UnrealAiToolJson::Error(TEXT("project setting value must be string/number/bool"));
			}
		}
		GConfig->SetString(*Section, *Name, *ValueString, GEngineIni);
		GConfig->Flush(false, GEngineIni);
		O->SetStringField(TEXT("value"), ValueString);
		return UnrealAiToolJson::Ok(O);
	}
	return UnrealAiToolJson::Error(TEXT("Unsupported scope/key pair for settings_set"));
}
