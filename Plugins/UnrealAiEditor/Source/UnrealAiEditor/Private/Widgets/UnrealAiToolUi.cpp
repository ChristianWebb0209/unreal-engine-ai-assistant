#include "Widgets/UnrealAiToolUi.h"

EUnrealAiToolVisualCategory UnrealAiClassifyToolVisuals(const FString& ToolName)
{
	const FString L = ToolName.ToLower();
	if (L.Contains(TEXT("search")) || L.Contains(TEXT("fuzzy")) || L.Contains(TEXT("query")))
	{
		return EUnrealAiToolVisualCategory::Search;
	}
	if (L.Contains(TEXT("read")) || L.Contains(TEXT("get")) || L.Contains(TEXT("snapshot")) || L.Contains(TEXT("list")))
	{
		return EUnrealAiToolVisualCategory::Read;
	}
	if (L.Contains(TEXT("write")) || L.Contains(TEXT("sync")) || L.Contains(TEXT("save")) || L.Contains(TEXT("edit")) || L.Contains(TEXT("create")))
	{
		return EUnrealAiToolVisualCategory::Write;
	}
	if (L.Contains(TEXT("agent_")) || L.Contains(TEXT("emit")) || L.Contains(TEXT("plan")))
	{
		return EUnrealAiToolVisualCategory::Meta;
	}
	return EUnrealAiToolVisualCategory::Other;
}

FLinearColor UnrealAiToolCategoryTint(EUnrealAiToolVisualCategory Cat)
{
	switch (Cat)
	{
	case EUnrealAiToolVisualCategory::Read:
		return FLinearColor(0.35f, 0.65f, 0.95f, 0.9f);
	case EUnrealAiToolVisualCategory::Search:
		return FLinearColor(0.85f, 0.75f, 0.25f, 0.9f);
	case EUnrealAiToolVisualCategory::Write:
		return FLinearColor(0.45f, 0.85f, 0.45f, 0.9f);
	case EUnrealAiToolVisualCategory::Meta:
		return FLinearColor(0.75f, 0.45f, 0.95f, 0.9f);
	default:
		return FLinearColor(0.55f, 0.55f, 0.6f, 0.9f);
	}
}

FName UnrealAiToolCategoryIconName(EUnrealAiToolVisualCategory Cat)
{
	switch (Cat)
	{
	case EUnrealAiToolVisualCategory::Read:
		return FName(TEXT("Icons.Eye"));
	case EUnrealAiToolVisualCategory::Search:
		return FName(TEXT("Icons.Search"));
	case EUnrealAiToolVisualCategory::Write:
		return FName(TEXT("Icons.Edit"));
	case EUnrealAiToolVisualCategory::Meta:
		return FName(TEXT("Icons.Settings"));
	default:
		return FName(TEXT("Icons.Question"));
	}
}

FString UnrealAiTruncateForUi(const FString& S, const int32 MaxLen)
{
	if (S.Len() <= MaxLen)
	{
		return S;
	}
	return S.Left(MaxLen / 2) + FString(TEXT(" … ")) + S.Right(MaxLen / 2);
}
