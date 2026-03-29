#include "Planning/UnrealAiComplexityAssessor.h"

namespace UnrealAiComplexityAssessorPriv
{
	static int32 CountBulletLines(const FString& S)
	{
		int32 N = 0;
		TArray<FString> Lines;
		S.ParseIntoArrayLines(Lines, false);
		for (const FString& L : Lines)
		{
			const FString T = L.TrimStartAndEnd();
			if (T.StartsWith(TEXT("-")) || T.StartsWith(TEXT("*")) || T.StartsWith(TEXT("+")))
			{
				++N;
				continue;
			}
			if (T.Len() > 2 && FChar::IsDigit(T[0]) && T.Contains(TEXT(".")))
			{
				++N;
			}
		}
		return N;
	}
}

FString FUnrealAiComplexityAssessment::FormatBlock() const
{
	FString Sig;
	for (int32 i = 0; i < Signals.Num(); ++i)
	{
		if (i > 0)
		{
			Sig += TEXT("; ");
		}
		Sig += Signals[i];
	}
	return FString::Printf(
		TEXT("[Complexity]\nscore: %.2f (%s)\nsignals: %s\npolicy: if high OR clearly multi-goal, prefer Plan chat mode (unreal_ai.plan_dag) or stop with a concise handoff before destructive tools\nrecommendPlanGate: %s"),
		ScoreNormalized,
		*Label,
		Sig.IsEmpty() ? TEXT("(none)") : *Sig,
		bRecommendPlanGate ? TEXT("true") : TEXT("false"));
}

FUnrealAiComplexityAssessment UnrealAiComplexityAssessor::Assess(const FUnrealAiComplexityAssessorInputs& In)
{
	FUnrealAiComplexityAssessment R;
	const int32 Len = In.UserMessage.Len();
	const int32 Bullets = UnrealAiComplexityAssessorPriv::CountBulletLines(In.UserMessage);

	float S = 0.f;
	// Message length (large prompts tend to be multi-goal).
	S += FMath::Clamp(static_cast<float>(Len) / 4500.f, 0.f, 1.f) * 0.28f;
	S += FMath::Clamp(static_cast<float>(In.AttachmentCount) * 0.09f, 0.f, 0.35f);
	S += FMath::Clamp(static_cast<float>(In.ToolResultCount) * 0.08f, 0.f, 0.4f);
	S += FMath::Clamp(static_cast<float>(In.ContextBlockCharCount) / 65000.f, 0.f, 1.f) * 0.18f;
	S += FMath::Clamp(static_cast<float>(Bullets) * 0.04f, 0.f, 0.2f);
	S += FMath::Clamp(static_cast<float>(In.ContentBrowserSelectionCount) * 0.05f, 0.f, 0.15f);

	switch (In.Mode)
	{
	case EUnrealAiAgentMode::Ask:
		break;
	case EUnrealAiAgentMode::Agent:
		S += 0.14f;
		break;
	case EUnrealAiAgentMode::Plan:
		S += 0.20f;
		break;
	}

	R.ScoreNormalized = FMath::Clamp(S, 0.f, 1.f);

	if (R.ScoreNormalized < 0.34f)
	{
		R.Label = TEXT("low");
	}
	else if (R.ScoreNormalized < 0.62f)
	{
		R.Label = TEXT("medium");
	}
	else
	{
		R.Label = TEXT("high");
	}

	if (Len > 900)
	{
		R.Signals.Add(TEXT("long_message"));
	}
	if (Bullets >= 3)
	{
		R.Signals.Add(TEXT("bullets"));
	}
	if (In.AttachmentCount > 0)
	{
		R.Signals.Add(TEXT("attachments"));
	}
	if (In.ToolResultCount > 0)
	{
		R.Signals.Add(TEXT("tool_memory"));
	}
	if (In.ContentBrowserSelectionCount > 2)
	{
		R.Signals.Add(TEXT("many_asset_selections"));
	}
	switch (In.Mode)
	{
	case EUnrealAiAgentMode::Agent:
		R.Signals.Add(TEXT("agent_mode"));
		break;
	case EUnrealAiAgentMode::Plan:
		R.Signals.Add(TEXT("plan_mode"));
		break;
	case EUnrealAiAgentMode::Ask:
		R.Signals.Add(TEXT("ask_mode"));
		break;
	}

	// Gate: tune without weekly prompt churn.
	const bool bHardMulti = Len > 1600 || Bullets >= 5 || In.ToolResultCount >= 4;
	R.bRecommendPlanGate = (R.ScoreNormalized >= 0.44f) || bHardMulti;

	return R;
}
