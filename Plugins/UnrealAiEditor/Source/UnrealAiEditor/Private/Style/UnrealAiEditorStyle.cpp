#include "Style/UnrealAiEditorStyle.h"

#include "UnrealAiEditorSettings.h"
#include "Brushes/SlateBoxBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/SlateStyleRegistry.h"
#if WITH_EDITOR
#include "Framework/Application/SlateApplication.h"
#endif

TSharedPtr<FSlateStyleSet> FUnrealAiEditorStyle::StyleSet = nullptr;

void FUnrealAiEditorStyle::Initialize()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	StyleSet = MakeShareable(new FSlateStyleSet(GetStyleSetName()));
	StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	FSlateStyleSet* Style = StyleSet.Get();

	Style->Set("UnrealAiEditor.PanelBackground", new FSlateColorBrush(FLinearColor(0.118f, 0.118f, 0.118f, 1.f)));
	Style->Set("UnrealAiEditor.Elevated", new FSlateColorBrush(FLinearColor(0.145f, 0.145f, 0.145f, 1.f)));
	Style->Set("UnrealAiEditor.BorderSubtle", new FSlateColorBrush(FLinearColor(0.235f, 0.235f, 0.235f, 1.f)));
	Style->Set("UnrealAiEditor.ToolPending", new FSlateColorBrush(FLinearColor(0.23f, 0.18f, 0.0f, 0.35f)));
	Style->Set("UnrealAiEditor.ToolSuccess", new FSlateColorBrush(FLinearColor(0.06f, 0.24f, 0.12f, 0.45f)));
	Style->Set("UnrealAiEditor.ToolError", new FSlateColorBrush(FLinearColor(0.24f, 0.08f, 0.08f, 0.45f)));
	// Todo plan card: muted forest (distinct from user taupe and assistant gray).
	Style->Set("UnrealAiEditor.TodoPlanPanel", new FSlateColorBrush(FLinearColor(0.11f, 0.16f, 0.13f, 1.f)));
	Style->Set("UnrealAiEditor.AssistantLane", new FSlateColorBrush(FLinearColor(0.14f, 0.16f, 0.2f, 0.55f)));
	Style->Set("UnrealAiEditor.DebugNav", new FSlateColorBrush(FLinearColor(0.10f, 0.13f, 0.12f, 1.f)));
	Style->Set("UnrealAiEditor.DebugInspect", new FSlateColorBrush(FLinearColor(0.09f, 0.11f, 0.14f, 1.f)));
	ApplyChatBubbleColorsFromSettings();

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FUnrealAiEditorStyle::ApplyChatBubbleColorsFromSettings()
{
	if (!StyleSet.IsValid())
	{
		return;
	}
	const UUnrealAiEditorSettings* Set = GetDefault<UUnrealAiEditorSettings>();
	FSlateStyleSet* Style = StyleSet.Get();
	Style->Set("UnrealAiEditor.UserBubble", new FSlateRoundedBoxBrush(Set->UserChatBubbleColor, 8.f));
	Style->Set("UnrealAiEditor.AssistantBubble", new FSlateColorBrush(Set->AgentChatBubbleColor));
#if WITH_EDITOR
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().InvalidateAllWidgets(true);
	}
#endif
}

void FUnrealAiEditorStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		StyleSet.Reset();
	}
}

const ISlateStyle& FUnrealAiEditorStyle::Get()
{
	return *StyleSet.Get();
}

FName FUnrealAiEditorStyle::GetStyleSetName()
{
	static FName Name(TEXT("UnrealAiEditorStyle"));
	return Name;
}

const FSlateBrush* FUnrealAiEditorStyle::GetBrush(FName PropertyName)
{
	return StyleSet.IsValid() ? StyleSet->GetBrush(PropertyName) : nullptr;
}

FSlateColor FUnrealAiEditorStyle::ColorBackgroundCanvas()
{
	return FSlateColor(FLinearColor(0.118f, 0.118f, 0.118f, 1.f));
}

FSlateColor FUnrealAiEditorStyle::ColorTextPrimary()
{
	return FSlateColor(FLinearColor(0.902f, 0.902f, 0.902f, 1.f));
}

FSlateColor FUnrealAiEditorStyle::ColorAccent()
{
	return FSlateColor(FLinearColor(0.231f, 0.510f, 0.965f, 1.f));
}

FSlateColor FUnrealAiEditorStyle::ColorDebugMuted()
{
	return FSlateColor(FLinearColor(0.55f, 0.58f, 0.62f, 1.f));
}

FSlateColor FUnrealAiEditorStyle::ColorDebugNavFolder()
{
	return FSlateColor(FLinearColor(0.45f, 0.72f, 0.55f, 1.f));
}
