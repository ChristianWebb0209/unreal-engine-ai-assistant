#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "Tools/Presentation/UnrealAiToolEditorPresentation.h"

/** Renders a rich editor note for a tool call: optional image, markdown body, and clickable asset links. */
class SToolEditorNotePanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SToolEditorNotePanel) {}
	SLATE_ARGUMENT(TSharedPtr<FUnrealAiToolEditorPresentation>, EditorPresentation)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedPtr<FUnrealAiToolEditorPresentation> EditorPresentation;
};

