#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Tools/Presentation/UnrealAiToolEditorPresentation.h"
#include "Harness/IToolExecutionHost.h"

/** Shared JSON helpers for tool results (single place for serialization). */
namespace UnrealAiToolJson
{
	/** Serialize JSON object to compact string (shared by all tool modules). */
	FString SerializeObject(const TSharedPtr<FJsonObject>& Obj);
	FUnrealAiToolInvocationResult Ok(const TSharedPtr<FJsonObject>& Payload);
	FUnrealAiToolInvocationResult OkWithEditorPresentation(const TSharedPtr<FJsonObject>& Payload, const TSharedPtr<FUnrealAiToolEditorPresentation>& EditorPresentation);
	FUnrealAiToolInvocationResult Error(const FString& Message);
}
