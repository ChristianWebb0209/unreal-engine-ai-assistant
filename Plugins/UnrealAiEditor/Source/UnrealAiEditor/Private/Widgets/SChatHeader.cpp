#include "Widgets/SChatHeader.h"

#include "Backend/UnrealAiBackendRegistry.h"
#include "Style/UnrealAiEditorStyle.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void SChatHeader::Construct(const FArguments& InArgs)
{
	OnOpenSettings = InArgs._OnOpenSettings;
	OnNewChatDelegate = InArgs._OnNewChat;
	BackendRegistry = InArgs._BackendRegistry;
	Session = InArgs._Session;
	if (Session.IsValid())
	{
		Session->OnChatNameChanged.AddSP(this, &SChatHeader::OnChatNameChanged);
	}

	ChildSlot
		[SNew(SBorder)
				.BorderBackgroundColor(FUnrealAiEditorStyle::LinearColorChatHeaderStrip())
				.Padding(FMargin(8.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(STextBlock)
						.Text(this, &SChatHeader::GetChatTitleText)
						.Font(FUnrealAiEditorStyle::FontComposerBadge())
					]
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

SChatHeader::~SChatHeader()
{
	if (Session.IsValid())
	{
		Session->OnChatNameChanged.RemoveAll(this);
	}
}

void SChatHeader::OnChatNameChanged()
{
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

FText SChatHeader::GetChatTitleText() const
{
	const FString Name = (Session.IsValid()) ? Session->ChatName : FString();
	if (!Name.IsEmpty())
	{
		return FText::FromString(Name);
	}
	return LOCTEXT("ChatTitleFallback", "Agent Chat");
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
