#include "Style/UnrealAiEditorStyle.h"

#include "Brushes/SlateBoxBrush.h"
#include "Styling/SlateStyleRegistry.h"

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
	Style->Set("UnrealAiEditor.UserBubble", new FSlateColorBrush(FLinearColor(0.18f, 0.22f, 0.28f, 1.f)));
	Style->Set("UnrealAiEditor.AssistantLane", new FSlateColorBrush(FLinearColor(0.14f, 0.16f, 0.2f, 0.55f)));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
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
