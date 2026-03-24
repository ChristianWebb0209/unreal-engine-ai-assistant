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
	/** Re-read bubble colors from project settings (called after edits and during style init). */
	static void ApplyChatBubbleColorsFromSettings();
	static FSlateColor ColorBackgroundCanvas();
	static FSlateColor ColorTextPrimary();
	static FSlateColor ColorAccent();
	static FSlateColor ColorDebugMuted();
	static FSlateColor ColorDebugNavFolder();

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
