#pragma once

#include "CoreMinimal.h"

enum class EUnrealAiToolVisualCategory : uint8
{
	Read,
	Search,
	Write,
	Meta,
	Other,
};

/** Heuristic mapping from tool id to UI category (icons/colors). */
EUnrealAiToolVisualCategory UnrealAiClassifyToolVisuals(const FString& ToolName);

FLinearColor UnrealAiToolCategoryTint(EUnrealAiToolVisualCategory Cat);

FName UnrealAiToolCategoryIconName(EUnrealAiToolVisualCategory Cat);

/** Truncate long JSON for tool cards. */
FString UnrealAiTruncateForUi(const FString& S, int32 MaxLen = 280);
