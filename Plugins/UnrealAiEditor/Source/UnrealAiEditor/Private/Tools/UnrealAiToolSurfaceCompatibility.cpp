#include "Tools/UnrealAiToolSurfaceCompatibility.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealAiToolSurfaceCompatibility
{
	const FString GAgentSurfaceToken_All = TEXT("all");
	const FString GAgentSurfaceToken_MainAgent = TEXT("main_agent");
	const FString GAgentSurfaceToken_BlueprintBuilder = TEXT("blueprint_builder");
	const FString GAgentSurfaceToken_EnvironmentBuilder = TEXT("environment_builder");

	void ParseAgentSurfaces(const FJsonObject& ToolDef, TSet<FString>& OutTokens, bool& bOutAll)
	{
		OutTokens.Reset();
		bOutAll = true;

		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!ToolDef.TryGetArrayField(TEXT("agent_surfaces"), Arr) || !Arr || Arr->Num() == 0)
		{
			return;
		}

		bOutAll = false;
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			if (!V.IsValid())
			{
				continue;
			}
			FString S;
			if (!V->TryGetString(S))
			{
				continue;
			}
			S.TrimStartAndEndInline();
			if (S.IsEmpty())
			{
				continue;
			}
			S.ToLowerInline();
			if (S == GAgentSurfaceToken_All)
			{
				bOutAll = true;
				OutTokens.Reset();
				return;
			}
			OutTokens.Add(S);
		}

		if (OutTokens.Num() == 0)
		{
			bOutAll = true;
		}
	}

	bool ToolAllowedOnSurface(const TSet<FString>& Tokens, bool bAll, EUnrealAiToolSurfaceKind Kind)
	{
		if (bAll)
		{
			return true;
		}
		switch (Kind)
		{
		case EUnrealAiToolSurfaceKind::MainAgent:
			return Tokens.Contains(GAgentSurfaceToken_MainAgent);
		case EUnrealAiToolSurfaceKind::BlueprintBuilder:
			return Tokens.Contains(GAgentSurfaceToken_BlueprintBuilder);
		case EUnrealAiToolSurfaceKind::EnvironmentBuilder:
			return Tokens.Contains(GAgentSurfaceToken_EnvironmentBuilder);
		default:
			return false;
		}
	}
}
