#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/** Inline error/warning strip for plan JSON validation (mirrors chat Notice styling). */
class SPlanJsonValidationBanner final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPlanJsonValidationBanner) {}
	SLATE_ARGUMENT(FString, Message)
	/** When true, uses error tint; otherwise informational (e.g. parse warning). */
	SLATE_ARGUMENT(bool, bIsError)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetMessage(const FString& NewMessage, bool bInIsError);

private:
	FString Message;
	bool bIsError = true;
};
