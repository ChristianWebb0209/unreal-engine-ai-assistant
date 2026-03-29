#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/** Compact status pill for a plan DAG node. */
class SPlanNodeStatusBadge final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPlanNodeStatusBadge) {}
	SLATE_ARGUMENT(FString, StatusRaw)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetStatusRaw(const FString& NewStatus);

private:
	static FString NormalizeToken(const FString& Raw);

	FString StatusRaw;
};
