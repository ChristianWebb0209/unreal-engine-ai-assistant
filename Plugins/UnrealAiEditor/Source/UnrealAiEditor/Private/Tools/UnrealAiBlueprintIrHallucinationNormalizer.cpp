#include "Tools/UnrealAiBlueprintIrHallucinationNormalizer.h"

#include "Dom/JsonObject.h"

namespace UnrealAiBlueprintIrHallucinationNormalizer
{
	static void MarkRewrite(
		const FString& Token,
		const FString& Note,
		TArray<FString>& OutNotes,
		TArray<FString>& OutDeprecatedFields)
	{
		OutDeprecatedFields.Add(Token);
		OutNotes.Add(Note);
	}

	bool NormalizeNode(
		const TSharedPtr<FJsonObject>& Node,
		TArray<FString>& OutNotes,
		TArray<FString>& OutDeprecatedFields)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		FString Op;
		if (!Node->TryGetStringField(TEXT("op"), Op) || Op.IsEmpty())
		{
			return false;
		}

		const FString OpLower = Op.ToLower();
		bool bApplied = false;

		// Common gameplay hallucination: non-IR pseudo-op for launching the player.
		if (OpLower == TEXT("launch_character"))
		{
			// Fallback to custom_event because launch semantics require a Character
			// target and are often emitted without valid actor typing context.
			Node->SetStringField(TEXT("op"), TEXT("custom_event"));
			FString ExistingName;
			if (!Node->TryGetStringField(TEXT("name"), ExistingName) || ExistingName.IsEmpty())
			{
				FString NodeId;
				if (Node->TryGetStringField(TEXT("node_id"), NodeId) && !NodeId.IsEmpty())
				{
					Node->SetStringField(TEXT("name"), NodeId);
				}
				else
				{
					Node->SetStringField(TEXT("name"), TEXT("LaunchCharacterIntent"));
				}
			}
			MarkRewrite(
				TEXT("op:launch_character"),
				TEXT("Mapped hallucinated op launch_character -> custom_event fallback."),
				OutNotes,
				OutDeprecatedFields);
			bApplied = true;
		}
		// Common gameplay hallucination: pseudo-op for sound playback.
		else if (OpLower == TEXT("play_sound"))
		{
			Node->SetStringField(TEXT("op"), TEXT("call_function"));
			if (!Node->HasField(TEXT("class_path")))
			{
				Node->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.GameplayStatics"));
			}
			if (!Node->HasField(TEXT("function_name")))
			{
				Node->SetStringField(TEXT("function_name"), TEXT("PlaySoundAtLocation"));
			}
			MarkRewrite(
				TEXT("op:play_sound"),
				TEXT("Mapped hallucinated op play_sound -> call_function(/Script/Engine.GameplayStatics::PlaySoundAtLocation)."),
				OutNotes,
				OutDeprecatedFields);
			bApplied = true;
		}
		// Frequent alias for custom_event produced by some models.
		else if (OpLower == TEXT("event_custom"))
		{
			Node->SetStringField(TEXT("op"), TEXT("custom_event"));
			MarkRewrite(
				TEXT("op:event_custom"),
				TEXT("Mapped op event_custom -> custom_event."),
				OutNotes,
				OutDeprecatedFields);
			bApplied = true;
		}
		// Common overlap shorthand hallucination.
		else if (OpLower == TEXT("event_overlap"))
		{
			Node->SetStringField(TEXT("op"), TEXT("event_actor_begin_overlap"));
			MarkRewrite(
				TEXT("op:event_overlap"),
				TEXT("Mapped op event_overlap -> event_actor_begin_overlap."),
				OutNotes,
				OutDeprecatedFields);
			bApplied = true;
		}
		// Common pseudo-op: "set_boolean" should be set_variable.
		else if (OpLower == TEXT("set_boolean"))
		{
			Node->SetStringField(TEXT("op"), TEXT("set_variable"));
			MarkRewrite(
				TEXT("op:set_boolean"),
				TEXT("Mapped op set_boolean -> set_variable."),
				OutNotes,
				OutDeprecatedFields);
			bApplied = true;
		}
		// Common pseudo-op: "set_visibility" is represented as function call.
		else if (OpLower == TEXT("set_visibility"))
		{
			Node->SetStringField(TEXT("op"), TEXT("call_function"));
			if (!Node->HasField(TEXT("class_path")))
			{
				Node->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.Actor"));
			}
			if (!Node->HasField(TEXT("function_name")))
			{
				Node->SetStringField(TEXT("function_name"), TEXT("SetActorHiddenInGame"));
			}
			MarkRewrite(
				TEXT("op:set_visibility"),
				TEXT("Mapped op set_visibility -> call_function(/Script/Engine.Actor::SetActorHiddenInGame)."),
				OutNotes,
				OutDeprecatedFields);
			bApplied = true;
		}
		// Common pseudo-op variant often emitted as if it were a native IR op.
		else if (OpLower == TEXT("play_sound_at_location"))
		{
			Node->SetStringField(TEXT("op"), TEXT("call_function"));
			if (!Node->HasField(TEXT("class_path")))
			{
				Node->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.GameplayStatics"));
			}
			if (!Node->HasField(TEXT("function_name")))
			{
				Node->SetStringField(TEXT("function_name"), TEXT("PlaySoundAtLocation"));
			}
			MarkRewrite(
				TEXT("op:play_sound_at_location"),
				TEXT("Mapped pseudo-op play_sound_at_location -> call_function(/Script/Engine.GameplayStatics::PlaySoundAtLocation)."),
				OutNotes,
				OutDeprecatedFields);
			bApplied = true;
		}
		// Common structural hallucinations for component construction in IR.
		// There is no first-class component authoring op in current IR. Convert to
		// custom_event so apply can proceed and avoid unsupported-op retry loops.
		else if (OpLower == TEXT("add_component")
			|| OpLower == TEXT("box_collision")
			|| OpLower == TEXT("static_mesh")
			|| OpLower == TEXT("skeletal_mesh")
			|| OpLower == TEXT("audio_component"))
		{
			Node->SetStringField(TEXT("op"), TEXT("custom_event"));
			FString ExistingName;
			if (!Node->TryGetStringField(TEXT("name"), ExistingName) || ExistingName.IsEmpty())
			{
				FString NodeId;
				if (Node->TryGetStringField(TEXT("node_id"), NodeId) && !NodeId.IsEmpty())
				{
					Node->SetStringField(TEXT("name"), NodeId);
				}
				else
				{
					Node->SetStringField(TEXT("name"), TEXT("RecoveredComponentIntent"));
				}
			}
			MarkRewrite(
				FString::Printf(TEXT("op:%s"), *OpLower),
				TEXT("Mapped component-authoring pseudo-op to custom_event fallback (IR has no component construction op)."),
				OutNotes,
				OutDeprecatedFields);
			bApplied = true;
		}
		// End-overlap event is often hallucinated; fallback to custom_event.
		else if (OpLower == TEXT("event_end_overlap") || OpLower == TEXT("event_actor_end_overlap"))
		{
			Node->SetStringField(TEXT("op"), TEXT("custom_event"));
			FString ExistingName;
			if (!Node->TryGetStringField(TEXT("name"), ExistingName) || ExistingName.IsEmpty())
			{
				Node->SetStringField(TEXT("name"), TEXT("OnEndOverlap"));
			}
			MarkRewrite(
				TEXT("op:event_end_overlap"),
				TEXT("Mapped unsupported end-overlap event op to custom_event fallback."),
				OutNotes,
				OutDeprecatedFields);
			bApplied = true;
		}
		else if (OpLower == TEXT("call_function"))
		{
			// Frequently emitted as an instance call without a component target in IR.
			// Prefer a static gameplay helper when we detect this specific shape.
			FString ClassPath;
			FString FunctionName;
			Node->TryGetStringField(TEXT("class_path"), ClassPath);
			Node->TryGetStringField(TEXT("function_name"), FunctionName);
			if (ClassPath.Equals(TEXT("/Script/Engine.AudioComponent"), ESearchCase::IgnoreCase)
				&& FunctionName.Equals(TEXT("Play"), ESearchCase::IgnoreCase))
			{
				Node->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.GameplayStatics"));
				Node->SetStringField(TEXT("function_name"), TEXT("PlaySoundAtLocation"));
				MarkRewrite(
					TEXT("call_function.AudioComponent.Play"),
					TEXT("Mapped call_function AudioComponent::Play -> GameplayStatics::PlaySoundAtLocation fallback."),
					OutNotes,
					OutDeprecatedFields);
				bApplied = true;
			}
			else if (ClassPath.StartsWith(TEXT("/Game/")) && FunctionName.Equals(TEXT("Play"), ESearchCase::IgnoreCase))
			{
				Node->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.GameplayStatics"));
				Node->SetStringField(TEXT("function_name"), TEXT("PlaySoundAtLocation"));
				MarkRewrite(
					TEXT("call_function.GameAsset.Play"),
					TEXT("Mapped /Game asset class_path + Play to GameplayStatics::PlaySoundAtLocation fallback."),
					OutNotes,
					OutDeprecatedFields);
				bApplied = true;
			}
		}

		return bApplied;
	}
}

