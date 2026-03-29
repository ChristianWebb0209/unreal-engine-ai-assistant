#include "Widgets/SToolEditorNotePanel.h"

#include "Tools/Presentation/UnrealAiToolEditorNoteBuilders.h"
#include "Tools/Presentation/UnrealAiToolEditorPresentation.h"

#include "Widgets/UnrealAiChatMarkdown.h"
#include "Widgets/UnrealAiImagePreview.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"

#include "Style/UnrealAiEditorStyle.h"
#include "Styling/AppStyle.h"

void SToolEditorNotePanel::Construct(const FArguments& InArgs)
{
	EditorPresentation = InArgs._EditorPresentation;

	FLinearColor NotePanelShell = FUnrealAiEditorStyle::LinearColorToolCallCardInset();
	NotePanelShell.A = 0.45f;

	if (!EditorPresentation.IsValid())
	{
		ChildSlot
			[
				SNullWidget::NullWidget
			];
		return;
	}

	TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);

	if (!EditorPresentation->ImageFilePath.IsEmpty())
	{
		if (const FSlateBrush* ImgBrush = UnrealAiGetOrCreateScreenshotThumbnailBrush(EditorPresentation->ImageFilePath))
		{
			Root->AddSlot()
				.AutoHeight()
				.Padding(0.f, 4.f, 0.f, 6.f)
				[
					SNew(SImage)
						.Image(ImgBrush)
				];
		}
	}

	if (!EditorPresentation->MarkdownBody.IsEmpty())
	{
		Root->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 6.f)
			[
				UnrealAiBuildMarkdownToolNoteBody(EditorPresentation->MarkdownBody)
			];
	}

	if (EditorPresentation->AssetLinks.Num() > 0)
	{
		TSharedRef<SWrapBox> LinksWrap = SNew(SWrapBox);
		for (const FUnrealAiToolAssetLink& L : EditorPresentation->AssetLinks)
		{
			const FString Label = L.Label.IsEmpty() ? L.ObjectPath : L.Label;
			const FString ObjectPath = L.ObjectPath;
			LinksWrap->AddSlot()
				.Padding(FMargin(0.f, 2.f, 10.f, 2.f))
				[
					UnrealAiMakeChatHyperlink(Label, ObjectPath)
				];
		}

		Root->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 2.f)
			[
				SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
					.BorderBackgroundColor(FUnrealAiEditorStyle::LinearColorToolCallCodeWell())
					.Padding(FMargin(8.f, 6.f))
					[
						LinksWrap
					]
			];
	}

	ChildSlot
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
				.BorderBackgroundColor(NotePanelShell)
				.Padding(FMargin(6.f))
				[
					Root
				]
		];
}

