#include "Widgets/SChatComposer.h"

#include "Widgets/SChatMessageList.h"
#include "Widgets/FUnrealAiChatRunSink.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Harness/IAgentRunSink.h"
#include "Harness/IUnrealAiAgentHarness.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Context/IAgentContextService.h"
#include "Context/UnrealAiContextMentionParser.h"
#include "Context/UnrealAiEditorContextQueries.h"
#include "Context/UnrealAiProjectId.h"
#include "Interfaces/IPluginManager.h"
#include "PluginDescriptor.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SHorizontalBox.h"
#include "Widgets/Layout/SVerticalBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Misc/EngineVersion.h"
#include "Styling/CoreStyle.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#endif

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void SChatComposer::Construct(const FArguments& InArgs)
{
	BackendRegistry = InArgs._BackendRegistry;
	MessageList = InArgs._MessageList;
	Session = InArgs._Session;
	OnNewChatParent = InArgs._OnNewChat;
	if (Session.IsValid())
	{
		Session->ThreadId = FGuid::NewGuid();
	}

	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
			[
				SNew(STextBlock)
					.AutoWrapText(true)
					.Text(LOCTEXT(
						"ComposerHint",
						"Enter sends, Shift+Enter newline. Type @ for asset suggestions; Attach adds Content Browser selection."))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 2.f)
			[
				SAssignNew(ChipsRow, SVerticalBox)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f, 0.f)
			[
				SAssignNew(MentionPanel, SBorder)
					.Visibility(EVisibility::Collapsed)
					.BorderBackgroundColor(FLinearColor(0.16f, 0.16f, 0.18f, 0.95f))
					.Padding(FMargin(6.f))
					[
						SNew(SVerticalBox)
					]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
			[
				SAssignNew(InputBox, SMultiLineEditableTextBox)
					.AllowMultiLine(true)
					.OnKeyDownHandler(this, &SChatComposer::OnInputKeyDown)
					.OnTextChanged(this, &SChatComposer::OnPromptTextChanged)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
				[
					SNew(SButton)
						.Text_Lambda([this]() { return GetModeButtonText(); })
						.OnClicked(this, &SChatComposer::OnCycleModeClicked)
						.ToolTipText(LOCTEXT("ModeTip", "Cycle Ask / Fast / Agent (tool packs)"))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
				[
					SNew(SButton)
						.Text(LOCTEXT("Attach", "Attach selection"))
						.OnClicked(this, &SChatComposer::OnAttachClicked)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
				[
					SNew(SButton)
						.Text(LOCTEXT("NewChat", "New chat"))
						.OnClicked(this, &SChatComposer::OnNewChatClicked)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
				[
					SNew(SButton)
						.Text(LOCTEXT("Send", "Send"))
						.OnClicked(this, &SChatComposer::OnSendClicked)
						.IsEnabled_Lambda(
							[this]()
							{
								if (!BackendRegistry.IsValid())
								{
									return false;
								}
								if (IUnrealAiAgentHarness* H = BackendRegistry->GetAgentHarness())
								{
									return !H->IsTurnInProgress();
								}
								return true;
							})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(6.f, 4.f)
			[
				SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.52f, 0.55f, 1.f)))
					.Text(this, &SChatComposer::GetFooterText)
			]
		];

	RefreshAttachmentChips();
}

void SChatComposer::SyncAttachmentChipsUi()
{
	RefreshAttachmentChips();
}

FReply SChatComposer::OnInputKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Enter && !InKeyEvent.IsShiftDown())
	{
		OnSendClicked();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SChatComposer::OnPromptTextChanged(const FText& NewText)
{
	UpdateMentionSuggestions(NewText.ToString());
	RefreshAttachmentChips();
}

void SChatComposer::UpdateMentionSuggestions(const FString& FullText)
{
#if WITH_EDITOR
	int32 At = FullText.Find(TEXT("@"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (At == INDEX_NONE)
	{
		if (MentionBorder.IsValid())
		{
			MentionBorder->SetVisibility(EVisibility::Collapsed);
		}
		return;
	}
	FString Rest = FullText.Mid(At + 1);
	int32 Space = INDEX_NONE;
	if (Rest.FindChar(TEXT(' '), Space) || Rest.FindChar(TEXT('\n'), Space))
	{
		Rest = Rest.Left(Space);
	}
	const FString Token = Rest.TrimStartAndEnd();
	if (Token.Len() < 1)
	{
		if (MentionBorder.IsValid())
		{
			MentionBorder->SetVisibility(EVisibility::Collapsed);
		}
		return;
	}

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AR = ARM.Get();
	FARFilter Filter;
	Filter.PackagePaths.Add(FName(TEXT("/Game")));
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	Filter.AssetName = FName(*Token);
	TArray<FAssetData> Found;
	AR.GetAssets(Filter, Found);
	if (Found.Num() > 15)
	{
		Found.SetNum(15);
	}

	if (!MentionListBox.IsValid() || !MentionBorder.IsValid())
	{
		return;
	}
	MentionListBox->ClearChildren();
	if (Found.Num() == 0)
	{
		MentionBorder->SetVisibility(EVisibility::Collapsed);
		return;
	}
	for (const FAssetData& AD : Found)
	{
		const FString Path = AD.GetObjectPathString();
		MentionListBox->AddSlot().AutoHeight().Padding(0.f, 1.f)
			[
				SNew(SButton)
					.ButtonStyle(FCoreStyle::Get(), "NoBorder")
					.ForegroundColor(FSlateColor::UseForeground())
					.OnClicked_Lambda(
						[this, Path]()
						{
							return OnPickMention(Path);
						})
					[
						SNew(STextBlock)
							.AutoWrapText(true)
							.Text(FText::FromString(Path))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					]
			];
	}
	MentionBorder->SetVisibility(EVisibility::Visible);
#else
	(void)FullText;
#endif
}

FReply SChatComposer::OnPickMention(const FString& AssetPath)
{
	if (!InputBox.IsValid())
	{
		return FReply::Handled();
	}
	FString S = InputBox->GetText().ToString();
	const int32 At = S.Find(TEXT("@"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (At != INDEX_NONE)
	{
		S = S.Left(At);
		S += FString::Printf(TEXT("@%s "), *AssetPath);
		InputBox->SetText(FText::FromString(S));
	}
	if (MentionBorder.IsValid())
	{
		MentionBorder->SetVisibility(EVisibility::Collapsed);
	}
	return FReply::Handled();
}

void SChatComposer::RefreshAttachmentChips()
{
	if (!ChipsRow.IsValid() || !BackendRegistry.IsValid() || !Session.IsValid())
	{
		return;
	}
	ChipsRow->ClearChildren();
	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	const FString Tid = Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
	IAgentContextService* Ctx = BackendRegistry->GetContextService();
	if (!Ctx)
	{
		return;
	}
	Ctx->LoadOrCreate(ProjectId, Tid);
	const FAgentContextState* St = Ctx->GetState(ProjectId, Tid);
	if (!St)
	{
		return;
	}
	for (int32 i = 0; i < St->Attachments.Num(); ++i)
	{
		const FContextAttachment& A = St->Attachments[i];
		const FString Label = A.Label.IsEmpty() ? A.Payload : A.Label;
		const int32 Idx = i;
		ChipsRow->AddSlot().AutoHeight().Padding(0.f, 2.f)
			[
				SNew(SBorder)
					.BorderBackgroundColor(FLinearColor(0.2f, 0.24f, 0.3f, 0.85f))
					.Padding(FMargin(6.f, 2.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(STextBlock)
								.AutoWrapText(true)
								.Text(FText::FromString(Label))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SButton)
								.Text(LOCTEXT("RmAttach", "×"))
								.OnClicked_Lambda(
									[this, Idx]()
									{
										return OnRemoveAttachment(Idx);
									})
						]
					]
			];
	}
}

FReply SChatComposer::OnRemoveAttachment(int32 Index)
{
	if (!BackendRegistry.IsValid() || !Session.IsValid())
	{
		return FReply::Handled();
	}
	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	const FString Tid = Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
	if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
	{
		Ctx->LoadOrCreate(ProjectId, Tid);
		Ctx->RemoveAttachment(Index);
	}
	RefreshAttachmentChips();
	return FReply::Handled();
}

FText SChatComposer::GetFooterText() const
{
	FString PluginVer = TEXT("dev");
	if (TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(TEXT("UnrealAiEditor")); P.IsValid())
	{
		PluginVer = P->GetDescriptor().VersionName;
	}
	const FString EngineLabel = FEngineVersion::Current().ToString();
	return FText::FromString(
		FString::Printf(TEXT("Unreal AI Editor %s · %s"), *PluginVer, *EngineLabel));
}

FReply SChatComposer::OnSendClicked()
{
	if (!BackendRegistry.IsValid() || !MessageList.IsValid() || !InputBox.IsValid() || !Session.IsValid())
	{
		return FReply::Handled();
	}

	IUnrealAiAgentHarness* Harness = BackendRegistry->GetAgentHarness();
	if (!Harness)
	{
		return FReply::Handled();
	}

	const FString Prompt = InputBox->GetText().ToString();
	InputBox->SetText(FText::GetEmpty());

	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	const FString ThreadIdStr = Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);

	if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
	{
		Ctx->LoadOrCreate(ProjectId, ThreadIdStr);
		UnrealAiContextMentionParser::ApplyMentionsFromPrompt(Ctx, Prompt);
		Ctx->RefreshEditorSnapshotFromEngine();
	}

	MessageList->AddUserMessage(Prompt);

	FUnrealAiAgentTurnRequest Req;
	Req.ProjectId = ProjectId;
	Req.ThreadId = ThreadIdStr;
	Req.Mode = AgentMode;
	Req.UserText = Prompt;
	Req.ModelProfileId = Session->ModelProfileId;
	Req.bRecordAssistantAsStubToolResult = true;

	const TSharedPtr<IAgentRunSink> Sink = MakeShared<FUnrealAiChatRunSink>(MessageList->GetTranscript());
	Harness->RunTurn(Req, Sink);

	RefreshAttachmentChips();
	return FReply::Handled();
}

FReply SChatComposer::OnCycleModeClicked()
{
	switch (AgentMode)
	{
	case EUnrealAiAgentMode::Ask:
		AgentMode = EUnrealAiAgentMode::Fast;
		break;
	case EUnrealAiAgentMode::Fast:
		AgentMode = EUnrealAiAgentMode::Agent;
		break;
	case EUnrealAiAgentMode::Agent:
	default:
		AgentMode = EUnrealAiAgentMode::Ask;
		break;
	}
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	return FReply::Handled();
}

FText SChatComposer::GetModeButtonText() const
{
	switch (AgentMode)
	{
	case EUnrealAiAgentMode::Ask:
		return LOCTEXT("ModeAskBtn", "Mode: Ask");
	case EUnrealAiAgentMode::Fast:
		return LOCTEXT("ModeFastBtn", "Mode: Fast");
	case EUnrealAiAgentMode::Agent:
	default:
		return LOCTEXT("ModeAgentBtn", "Mode: Agent");
	}
}

FReply SChatComposer::OnAttachClicked()
{
	if (!BackendRegistry.IsValid() || !Session.IsValid())
	{
		return FReply::Handled();
	}
	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	const FString ThreadIdStr = Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
	if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
	{
		Ctx->LoadOrCreate(ProjectId, ThreadIdStr);
		UnrealAiEditorContextQueries::AddContentBrowserSelectionAsAttachments(Ctx);
	}
	RefreshAttachmentChips();
	return FReply::Handled();
}

FReply SChatComposer::OnNewChatClicked()
{
	if (OnNewChatParent.IsBound())
	{
		OnNewChatParent.Execute();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
