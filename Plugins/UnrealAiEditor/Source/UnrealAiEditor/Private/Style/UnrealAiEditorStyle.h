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
	/** Agent Chat tab/menus/mode: Starship AIController_16 vector icon from engine Editor/Slate (always present). */
	static FName AgentChatTabIconName();
	static const FSlateBrush* GetAgentChatTabIconBrush();
	/** Refreshes fixed chat bubble brushes (plugin-defined; not user-configurable). */
	static void ApplyChatBubbleColorsFromSettings();
	static FSlateColor ColorBackgroundCanvas();
	static FSlateColor ColorTextPrimary();
	static FSlateColor ColorAccent();
	static FSlateColor ColorDebugMuted();
	static FSlateColor ColorDebugNavFolder();

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
