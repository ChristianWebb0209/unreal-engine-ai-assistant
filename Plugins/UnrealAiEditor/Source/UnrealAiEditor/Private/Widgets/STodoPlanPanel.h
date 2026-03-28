#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FUnrealAiBackendRegistry;
struct FUnrealAiChatUiSession;

/** Renders structured todo plan JSON (unreal_ai.todo_plan) or Plan-mode DAG JSON (unreal_ai.plan_dag / nodes). */
class STodoPlanPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STodoPlanPanel) {}
	SLATE_ARGUMENT(FString, Title)
	SLATE_ARGUMENT(FString, PlanJson)
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiBackendRegistry>, BackendRegistry)
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiChatUiSession>, Session)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static void ParseSteps(const FString& PlanJson, TArray<FString>& OutTitles);

private:
	FString PlanJson;
	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry;
	TSharedPtr<FUnrealAiChatUiSession> Session;
};
