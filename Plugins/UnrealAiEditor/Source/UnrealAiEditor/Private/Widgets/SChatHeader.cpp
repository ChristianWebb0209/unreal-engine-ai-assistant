#include "Widgets/SChatHeader.h"

#include "Backend/UnrealAiBackendRegistry.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void SChatHeader::Construct(const FArguments& InArgs)
{
	OnOpenSettings = InArgs._OnOpenSettings;
	OnNewChatDelegate = InArgs._OnNewChat;

	ChildSlot
		[SNew(SBorder)
				.BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.12f, 1.f))
				.Padding(FMargin(8.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)
					+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(4.f, 0.f))
					[
						SNew(SButton)
							.Text(LOCTEXT("SettingsBtn", "Settings"))
							.OnClicked_Lambda(
								[this]()
								{
									if (OnOpenSettings.IsBound())
									{
										OnOpenSettings.Execute();
									}
									return FReply::Handled();
								})
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(4.f, 0.f))
					[
						SNew(SButton)
							.Text(LOCTEXT("NewChatBtn", "New Chat"))
							.OnClicked(this, &SChatHeader::OnNewChatPressed)
					]
				]];
}

FReply SChatHeader::OnNewChatPressed()
{
	if (OnNewChatDelegate.IsBound())
	{
		OnNewChatDelegate.Execute();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
