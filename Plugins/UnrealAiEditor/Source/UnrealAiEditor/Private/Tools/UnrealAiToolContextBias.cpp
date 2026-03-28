#include "Tools/UnrealAiToolContextBias.h"

#include "Context/AgentContextTypes.h"
#include "Dom/JsonObject.h"

void UnrealAiToolContextBias::CollectToolDomainTags(const TSharedPtr<FJsonObject>& ToolDef, TArray<FString>& OutTags)
{
	OutTags.Reset();
	if (!ToolDef.IsValid())
	{
		return;
	}
	const TSharedPtr<FJsonObject>* Ts = nullptr;
	if (ToolDef->TryGetObjectField(TEXT("tool_surface"), Ts) && Ts && (*Ts).IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* Dt = nullptr;
		if ((*Ts)->TryGetArrayField(TEXT("domain_tags"), Dt) && Dt)
		{
			for (const TSharedPtr<FJsonValue>& V : *Dt)
			{
				if (V.IsValid() && V->Type == EJson::String)
				{
					OutTags.AddUnique(V->AsString());
				}
			}
		}
	}
	if (OutTags.Num() > 0)
	{
		return;
	}
	FString Cat;
	ToolDef->TryGetStringField(TEXT("category"), Cat);
	if (Cat == TEXT("blueprints"))
	{
		OutTags.Add(TEXT("BlueprintGraph"));
	}
	else if (Cat == TEXT("materials_rendering"))
	{
		OutTags.Add(TEXT("MaterialEditor"));
	}
	else if (Cat == TEXT("pie_play"))
	{
		OutTags.Add(TEXT("PIE"));
	}
	else if (Cat == TEXT("world_actors") || Cat == TEXT("selection_framing") || Cat == TEXT("viewport_camera"))
	{
		OutTags.Add(TEXT("Viewport"));
	}
	else if (Cat == TEXT("assets_content"))
	{
		OutTags.Add(TEXT("AssetEditor"));
	}
}

void UnrealAiToolContextBias::GatherActiveDomainTags(const FAgentContextState* State, TArray<FString>& OutTags)
{
	OutTags.Reset();
	if (!State)
	{
		return;
	}
	if (State->EditorSnapshot.IsSet())
	{
		const FEditorContextSnapshot& Snap = State->EditorSnapshot.GetValue();
		for (const FRecentUiEntry& E : Snap.RecentUiEntries)
		{
			if (E.bCurrentlyActive || E.SeenCount > 0)
			{
				switch (E.UiKind)
				{
				case ERecentUiKind::BlueprintGraph:
					OutTags.AddUnique(TEXT("BlueprintGraph"));
					break;
				case ERecentUiKind::SceneViewport:
					OutTags.AddUnique(TEXT("Viewport"));
					break;
				case ERecentUiKind::AssetEditor:
					OutTags.AddUnique(TEXT("AssetEditor"));
					break;
				default:
					break;
				}
			}
		}
		if (Snap.ActiveAssetPath.Contains(TEXT("Blueprint"), ESearchCase::IgnoreCase)
			|| Snap.ActiveAssetPath.Contains(TEXT(".uasset"), ESearchCase::IgnoreCase))
		{
			OutTags.AddUnique(TEXT("BlueprintGraph"));
		}
	}
}

float UnrealAiToolContextBias::ScoreMultiplierForTool(const TArray<FString>& ToolDomainTags, const TArray<FString>& ActiveTags)
{
	if (ToolDomainTags.Num() == 0 || ActiveTags.Num() == 0)
	{
		return 1.f;
	}
	for (const FString& T : ToolDomainTags)
	{
		for (const FString& A : ActiveTags)
		{
			if (T.Equals(A, ESearchCase::IgnoreCase))
			{
				return 1.15f;
			}
		}
	}
	return 1.f;
}
