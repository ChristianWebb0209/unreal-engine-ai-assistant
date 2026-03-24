#include "Widgets/SToolEditorNotePanel.h"

#include "Tools/Presentation/UnrealAiEditorNavigation.h"
#include "Tools/Presentation/UnrealAiToolEditorNoteBuilders.h"
#include "Tools/Presentation/UnrealAiToolEditorPresentation.h"

#include "Widgets/UnrealAiChatMarkdown.h"
#include "Widgets/UnrealAiImagePreview.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/SHyperlink.h"

void SToolEditorNotePanel::Construct(const FArguments& InArgs)
{
	EditorPresentation = InArgs._EditorPresentation;

	if (!EditorPresentation.IsValid())
	{
		ChildSlot
			[
				SNullWidget::NullWidget
			];
		return;
	}

	const auto OnLinkClicked = [](const FString& ObjectPath)
	{
		UnrealAiEditorNavigation::NavigateToAssetObjectPath(ObjectPath);
	};

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
				UnrealAiBuildMarkdownToolNoteBody(EditorPresentation->MarkdownBody, OnLinkClicked)
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
				.AutoWidth()
				.Padding(FMargin(0.f, 2.f, 10.f, 2.f))
				[
					SNew(SHyperlink)
						.Text(FText::FromString(Label))
						.OnNavigate_Lambda([ObjectPath, OnLinkClicked]()
						{
							OnLinkClicked(ObjectPath);
						})
				];
		}

		Root->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 2.f)
			[
				SNew(SBorder)
					.BorderBackgroundColor(FLinearColor(0.07f, 0.08f, 0.1f, 0.9f))
					.Padding(FMargin(8.f, 6.f))
					[
						LinksWrap
					]
			];
	}

	ChildSlot
		[
			SNew(SBorder)
				.BorderBackgroundColor(FLinearColor(0.05f, 0.06f, 0.08f, 0.45f))
				.Padding(FMargin(6.f))
				[
					Root
				]
		];
}

