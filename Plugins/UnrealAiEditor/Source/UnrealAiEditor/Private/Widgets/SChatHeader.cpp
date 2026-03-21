#include "Widgets/SChatHeader.h"

#include "Backend/UnrealAiBackendRegistry.h"
#include "Backend/IUnrealAiChatService.h"
#include "Context/AgentContextTypes.h"
#include "Context/IAgentContextService.h"
#include "Context/UnrealAiProjectId.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Harness/IUnrealAiAgentHarness.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SHorizontalBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void SChatHeader::Construct(const FArguments& InArgs)
{
	BackendRegistry = InArgs._BackendRegistry;
	Session = InArgs._Session;
	OnOpenSettings = InArgs._OnOpenSettings;
	OnNewChatDelegate = InArgs._OnNewChat;

	if (BackendRegistry.IsValid() && Session.IsValid())
	{
		if (FUnrealAiModelProfileRegistry* Reg = BackendRegistry->GetModelProfileRegistry())
		{
			TArray<FString> Ids;
			Reg->EnumerateModelProfileIds(Ids);
			for (const FString& Id : Ids)
			{
				ModelOptions.Add(MakeShared<FString>(Id));
			}
			if (Session->ModelProfileId.IsEmpty() && ModelOptions.Num() > 0)
			{
				Session->ModelProfileId = *ModelOptions[0];
			}
			else if (!Session->ModelProfileId.IsEmpty())
			{
				bool bFound = false;
				for (const TSharedPtr<FString>& M : ModelOptions)
				{
					if (M.IsValid() && *M == Session->ModelProfileId)
					{
						bFound = true;
						break;
					}
				}
				if (!bFound)
				{
					ModelOptions.Add(MakeShared<FString>(Session->ModelProfileId));
				}
			}
		}
	}

	TSharedPtr<FString> Initial = ModelOptions.Num() > 0 ? ModelOptions[0] : nullptr;
	for (const TSharedPtr<FString>& M : ModelOptions)
	{
		if (M.IsValid() && Session.IsValid() && *M == Session->ModelProfileId)
		{
			Initial = M;
			break;
		}
	}

	RegisterActiveTimer(
		0.5f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SChatHeader::OnComplexityTimerTick));

	ChildSlot
		[SNew(SBorder)
				.BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.12f, 1.f))
				.Padding(FMargin(8.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f)
					[
						SNew(STextBlock).Text(LOCTEXT("ModelHdr", "Model"))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f)
					[
						SAssignNew(ModelCombo, SComboBox<TSharedPtr<FString>>)
							.OptionsSource(&ModelOptions)
							.OnGenerateWidget_Lambda(
								[](TSharedPtr<FString> InOpt)
								{
									return SNew(STextBlock).Text(FText::FromString(InOpt.IsValid() ? **InOpt : FString()));
								})
							.InitiallySelectedItem(Initial)
							.OnSelectionChanged(this, &SChatHeader::OnModelSelectionChanged)
							.IsEnabled_Lambda([this]() { return IsModelComboEnabled(); })
							.Content()
							[
								SNew(STextBlock)
									.Text(this, &SChatHeader::GetSelectedModelText)
							]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f)
					[
						SNew(STextBlock)
							.Text(this, &SChatHeader::GetConnectionStatusText)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.75f, 0.85f, 1.f)))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f)
					[
						SNew(STextBlock)
							.Text(this, &SChatHeader::GetComplexityText)
							.ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.55f, 1.f)))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
					[
						SNew(SButton)
							.Text(LOCTEXT("StopBtn", "Stop"))
							.ToolTipText(LOCTEXT("StopTip", "Cancel the in-flight model / tool run"))
							.OnClicked(this, &SChatHeader::OnStopPressed)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
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
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
					[
						SNew(SButton)
							.Text(LOCTEXT("NewChatBtn", "New Chat"))
							.OnClicked(this, &SChatHeader::OnNewChatPressed)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
					[
						SNew(SButton)
							.Text_Lambda([this]() { return GetConnectButtonText(); })
							.OnClicked(this, &SChatHeader::OnConnectPressed)
					]
				]];
}

void SChatHeader::OnModelSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	(void)SelectInfo;
	if (Session.IsValid() && NewSelection.IsValid())
	{
		Session->ModelProfileId = *NewSelection;
	}
}

FText SChatHeader::GetSelectedModelText() const
{
	if (Session.IsValid() && !Session->ModelProfileId.IsEmpty())
	{
		return FText::FromString(Session->ModelProfileId);
	}
	return LOCTEXT("NoModel", "(no model)");
}

bool SChatHeader::IsModelComboEnabled() const
{
	if (!BackendRegistry.IsValid())
	{
		return true;
	}
	if (IUnrealAiAgentHarness* H = BackendRegistry->GetAgentHarness())
	{
		return !H->IsTurnInProgress();
	}
	return true;
}

FText SChatHeader::GetConnectionStatusText() const
{
	if (!BackendRegistry.IsValid())
	{
		return FText::GetEmpty();
	}
	if (FUnrealAiModelProfileRegistry* Reg = BackendRegistry->GetModelProfileRegistry())
	{
		if (Reg->HasAnyConfiguredApiKey())
		{
			return LOCTEXT("ConnHttp", "LLM: HTTP");
		}
	}
	return LOCTEXT("ConnStub", "LLM: stub (no API key)");
}

FText SChatHeader::GetComplexityText() const
{
	if (CachedComplexityLabel.IsEmpty())
	{
		return LOCTEXT("ComplexityNone", "Complexity: —");
	}
	return FText::FromString(FString::Printf(TEXT("Complexity: %s"), *CachedComplexityLabel));
}

EActiveTimerReturnType SChatHeader::OnComplexityTimerTick(double InCurrentTime, float InDeltaTime)
{
	(void)InCurrentTime;
	(void)InDeltaTime;
	if (BackendRegistry.IsValid() && Session.IsValid())
	{
		if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
		{
			const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
			const FString Tid = Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
			Ctx->LoadOrCreate(ProjectId, Tid);
			FAgentContextBuildOptions Opt;
			const FAgentContextBuildResult R = Ctx->BuildContextWindow(Opt);
			CachedComplexityLabel = R.ComplexityLabel;
		}
	}
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	return EActiveTimerReturnType::Continue;
}

FText SChatHeader::GetConnectButtonText() const
{
	return bStubConnected ? LOCTEXT("Disconnect", "Disconnect") : LOCTEXT("Connect", "Connect");
}

FReply SChatHeader::OnStopPressed()
{
	if (BackendRegistry.IsValid())
	{
		if (IUnrealAiAgentHarness* Harness = BackendRegistry->GetAgentHarness())
		{
			Harness->CancelTurn();
		}
	}
	return FReply::Handled();
}

FReply SChatHeader::OnNewChatPressed()
{
	if (OnNewChatDelegate.IsBound())
	{
		OnNewChatDelegate.Execute();
	}
	return FReply::Handled();
}

FReply SChatHeader::OnConnectPressed()
{
	if (!BackendRegistry.IsValid())
	{
		return FReply::Handled();
	}

	IUnrealAiChatService* Chat = BackendRegistry->GetChatService();
	if (!Chat)
	{
		return FReply::Handled();
	}

	if (bStubConnected)
	{
		Chat->Disconnect();
		bStubConnected = false;
	}
	else
	{
		Chat->Connect(
			FUnrealAiChatSimpleDelegate::CreateLambda(
				[this]()
				{
					bStubConnected = true;
				}));
	}

	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
