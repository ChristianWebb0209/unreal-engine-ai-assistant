#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

class FUnrealAiEditorStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static const ISlateStyle& Get();
	static FName GetStyleSetName();

	static const FSlateBrush* GetBrush(FName PropertyName);
	static FSlateColor ColorBackgroundCanvas();
	static FSlateColor ColorTextPrimary();
	static FSlateColor ColorAccent();

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
