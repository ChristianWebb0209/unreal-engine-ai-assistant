#include "Widgets/SChatComposer.h"

#include "UnrealAiEditorModule.h"
#include "Style/UnrealAiEditorStyle.h"
#include "Async/Async.h"
#include "Widgets/SChatMessageList.h"
#include "Widgets/UnrealAiChatTranscript.h"
#include "Widgets/UnrealAiChatUiHelpers.h"
#include "Widgets/FUnrealAiChatRunSink.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Harness/FUnrealAiPlanExecutor.h"
#include "Harness/UnrealAiPlanDag.h"
#include "Harness/IAgentRunSink.h"
#include "Harness/IUnrealAiAgentHarness.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Context/AgentContextTypes.h"
#include "Context/IAgentContextService.h"
#include "Context/UnrealAiContextMentionParser.h"
#include "Context/UnrealAiContextOverview.h"
#include "Context/UnrealAiContextDragDrop.h"
#include "Context/UnrealAiEditorContextQueries.h"
#include "Context/UnrealAiProjectId.h"
#include "Composer/UnrealAiComposerMentionIndex.h"
#include "Composer/UnrealAiComposerPromptResolver.h"
#include "Interfaces/IPluginManager.h"
#include "PluginDescriptor.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Input/Events.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Misc/EngineVersion.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SComboButton.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Containers/Ticker.h"
#include "Widgets/UnrealAiImagePreview.h"
#include "Tools/UnrealAiToolViewportHelpers.h"
#include "EditorViewportClient.h"
#include "Framework/Docking/TabManager.h"
#include "UnrealAiEditorTabIds.h"

#if WITH_EDITOR
static FIntRect UnrealAiComputeCappedViewportRect(FViewport* VP, int32 MaxDim = 1280)
{
	if (!VP)
	{
		return FIntRect();
	}
	const FIntPoint S = VP->GetSizeXY();
	const int32 W = S.X;
	const int32 H = S.Y;
	if (W <= 0 || H <= 0)
	{
		return FIntRect();
	}
	if (FMath::Max(W, H) <= MaxDim)
	{
		return FIntRect(0, 0, W, H);
	}
	const float Scale = static_cast<float>(MaxDim) / static_cast<float>(FMath::Max(W, H));
	const int32 NW = FMath::Max(1, FMath::RoundToInt(static_cast<float>(W) * Scale));
	const int32 NH = FMath::Max(1, FMath::RoundToInt(static_cast<float>(H) * Scale));
	const int32 X0 = (W - NW) / 2;
	const int32 Y0 = (H - NH) / 2;
	return FIntRect(X0, Y0, X0 + NW, Y0 + NH);
}
#endif

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ClassIconFinder.h"
#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPath.h"
#endif

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

namespace UnrealAiChatComposerModelUi
{
static const FString GAddNewModelToken(TEXT("__UNREAL_AI_ADD_NEW_MODEL__"));
}

namespace UnrealAiAttachmentUi
{
#if WITH_EDITOR
	static const FSlateBrush* BrushForChip(const FContextAttachment& A)
	{
		if (A.Type == EContextAttachmentType::ContentFolder)
		{
			return FAppStyle::GetBrush(TEXT("ContentBrowser.AssetTreeFolderClosed"));
		}
		if (A.IconClassPath == TEXT("UnrealAi.ViewportScreenshot"))
		{
			return FAppStyle::GetBrush(TEXT("Icons.Camera"));
		}
		if (!A.IconClassPath.IsEmpty())
		{
			if (UClass* Cls = FSoftClassPath(A.IconClassPath).TryLoadClass<UObject>())
			{
				return FClassIconFinder::FindThumbnailForClass(Cls);
			}
		}
		if (A.Type == EContextAttachmentType::AssetPath && !A.Payload.IsEmpty())
		{
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			const FAssetData AD = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(A.Payload));
			if (AD.IsValid())
			{
				bool bIsClassType = false;
				const UClass* IconCls = FClassIconFinder::GetIconClassForAssetData(AD, &bIsClassType);
				if (IconCls)
				{
					return FClassIconFinder::FindThumbnailForClass(IconCls);
				}
			}
		}
		if (A.Type == EContextAttachmentType::ActorReference && !A.Payload.IsEmpty())
		{
			if (AActor* Act = FindObject<AActor>(nullptr, *A.Payload))
			{
				return FClassIconFinder::FindIconForActor(Act);
			}
		}
		if (A.Type == EContextAttachmentType::FilePath)
		{
			if (A.IconClassPath == TEXT("UnrealAi.ViewportScreenshot") || A.Label.Contains(TEXT("Viewport screenshot")))
			{
				return FAppStyle::GetBrush(TEXT("Icons.Camera"));
			}
			return FAppStyle::GetBrush(TEXT("Icons.Document"));
		}
		return FAppStyle::GetBrush(TEXT("Icons.Document"));
	}
#else
	static const FSlateBrush* BrushForChip(const FContextAttachment& A)
	{
		(void)A;
		return FAppStyle::GetBrush(TEXT("Icons.Document"));
	}
#endif
} // namespace UnrealAiAttachmentUi

namespace UnrealAiModeUi
{
	static FLinearColor Accent(EUnrealAiAgentMode M)
	{
		// Mode accents: cool Ask, violet Agent, amber Plan.
		switch (M)
		{
		case EUnrealAiAgentMode::Ask:
			return FLinearColor(0.45f, 0.55f, 0.72f, 1.f);
		case EUnrealAiAgentMode::Agent:
			return FLinearColor(0.58f, 0.36f, 0.95f, 1.f);
		case EUnrealAiAgentMode::Plan:
			return FLinearColor(0.86f, 0.56f, 0.20f, 1.f);
		default:
			return FLinearColor(0.45f, 0.55f, 0.72f, 1.f);
		}
	}

	static const FSlateBrush* ModeIconBrush(EUnrealAiAgentMode M)
	{
		switch (M)
		{
		case EUnrealAiAgentMode::Ask:
			return FAppStyle::GetBrush(TEXT("Icons.Help"));
		case EUnrealAiAgentMode::Agent:
			return FUnrealAiEditorStyle::GetAgentChatTabIconBrush();
		case EUnrealAiAgentMode::Plan:
			return FAppStyle::GetBrush(TEXT("Icons.Blueprint"));
		default:
			return FAppStyle::GetBrush(TEXT("Icons.Help"));
		}
	}
}

#if WITH_EDITOR
namespace UnrealAiMentionRowUi
{
	static const FSlateBrush* BrushForCandidate(const FMentionCandidate& C)
	{
		if (C.IsAsset() && C.AssetData.IsValid())
		{
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			const FAssetData AD = C.AssetData.IsValid()
				? C.AssetData
				: ARM.Get().GetAssetByObjectPath(FSoftObjectPath(C.PrimaryKey));
			if (AD.IsValid())
			{
				bool bIsClassType = false;
				const UClass* IconCls = FClassIconFinder::GetIconClassForAssetData(AD, &bIsClassType);
				if (IconCls)
				{
					return FClassIconFinder::FindThumbnailForClass(IconCls);
				}
			}
		}
		if (C.IsActor())
		{
			if (AActor* A = FindObject<AActor>(nullptr, *C.PrimaryKey))
			{
				return FClassIconFinder::FindIconForActor(A);
			}
		}
		return FAppStyle::GetBrush(TEXT("Icons.Document"));
	}
} // namespace UnrealAiMentionRowUi

namespace UnrealAiComposerMentionInsert
{
	static FString LastPathSegment(const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return Path;
		}
		int32 Slash = INDEX_NONE;
		if (Path.FindLastChar(TEXT('/'), Slash))
		{
			return Path.Mid(Slash + 1);
		}
		return Path;
	}

	static FString SanitizeInlineMentionToken(const FString& In)
	{
		FString Out;
		Out.Reserve(FMath::Max(8, In.Len()));
		for (int32 i = 0; i < In.Len(); ++i)
		{
			const TCHAR C = In[i];
			if (FChar::IsAlnum(C) || C == TEXT('_') || C == TEXT('.') || C == TEXT('/'))
			{
				Out.AppendChar(C);
			}
			else if (FChar::IsWhitespace(C) || C == TEXT('-'))
			{
				Out.AppendChar(TEXT('_'));
			}
		}
		if (Out.IsEmpty())
		{
			Out = TEXT("item");
		}
		return Out;
	}

	/** Chip + inline @ token: short, regex-safe; full payload stays on attachment. */
	static FString ChipLabelForCandidate(const FMentionCandidate& C)
	{
		const FString Base = C.DisplayName.IsEmpty() ? LastPathSegment(C.PrimaryKey) : C.DisplayName;
		constexpr int32 MaxLen = 36;
		if (Base.Len() <= MaxLen)
		{
			return Base;
		}
		const FString Last = LastPathSegment(C.PrimaryKey);
		if (!Last.IsEmpty() && Last.Len() <= MaxLen)
		{
			return Last;
		}
		return Base.Left(MaxLen - 1) + TEXT("...");
	}

	static FString InlineTokenForCandidate(const FMentionCandidate& C)
	{
		FString Src = C.DisplayName.IsEmpty() ? LastPathSegment(C.PrimaryKey) : C.DisplayName;
		if (Src.IsEmpty())
		{
			Src = LastPathSegment(C.PrimaryKey);
		}
		return SanitizeInlineMentionToken(Src);
	}

	/** Same rules as @ tokens inserted by OnPickMentionCandidate (not chip label truncation). */
	static FString InlineTokenFromContextAttachment(const FContextAttachment& A)
	{
		switch (A.Type)
		{
		case EContextAttachmentType::AssetPath:
		{
			if (A.Payload.IsEmpty())
			{
				return FString();
			}
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			const FAssetData AD = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(A.Payload));
			const FString Src = AD.IsValid() ? AD.AssetName.ToString() : FPaths::GetBaseFilename(A.Payload);
			return SanitizeInlineMentionToken(Src);
		}
		case EContextAttachmentType::ActorReference:
		{
			FString Src = A.Label;
			if (Src.IsEmpty())
			{
				Src = LastPathSegment(A.Payload);
			}
			return SanitizeInlineMentionToken(Src);
		}
		case EContextAttachmentType::FilePath:
		{
			if (A.Payload.IsEmpty())
			{
				return FString();
			}
			return SanitizeInlineMentionToken(FPaths::GetCleanFilename(A.Payload));
		}
		case EContextAttachmentType::ContentFolder:
		{
			if (A.Payload.IsEmpty())
			{
				return FString();
			}
			return SanitizeInlineMentionToken(LastPathSegment(A.Payload));
		}
		case EContextAttachmentType::FreeText:
		case EContextAttachmentType::BlueprintNodeRef:
		{
			const FString Src = A.Label.IsEmpty() ? A.Payload : A.Label;
			if (Src.IsEmpty())
			{
				return FString();
			}
			return SanitizeInlineMentionToken(Src);
		}
		default:
			return FString();
		}
	}

	/** Remove @<fragment> (and one trailing space if present) when fragment sanitizes to Tok. */
	static void RemoveAtMentionsForToken(FString& Text, const FString& Tok)
	{
		if (Tok.IsEmpty())
		{
			return;
		}
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			if (Text[i] != TEXT('@'))
			{
				continue;
			}
			int32 j = i + 1;
			while (j < Text.Len() && !FChar::IsWhitespace(Text[j]))
			{
				++j;
			}
			const FString Candidate = Text.Mid(i + 1, j - (i + 1));
			if (SanitizeInlineMentionToken(Candidate).Equals(Tok, ESearchCase::CaseSensitive))
			{
				int32 End = j;
				if (End < Text.Len() && Text[End] == TEXT(' '))
				{
					++End;
				}
				Text.RemoveAt(i, End - i);
				i = FMath::Max(i - 1, 0);
			}
		}
	}
} // namespace UnrealAiComposerMentionInsert

namespace UnrealAiComposerMentionParse
{
	static bool ParseMentionAutocomplete(const FString& FullText, int32& OutAtPos, FString& OutToken)
	{
		const int32 At = FullText.Find(TEXT("@"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (At == INDEX_NONE)
		{
			return false;
		}
		FString Rest = FullText.Mid(At + 1);
		int32 Space = INDEX_NONE;
		if (Rest.FindChar(TEXT(' '), Space) || Rest.FindChar(TEXT('\n'), Space))
		{
			Rest = Rest.Left(Space);
		}
		const FString Token = Rest.TrimStartAndEnd();
		OutAtPos = At;
		OutToken = Token;
		return true;
	}

	/** @ through end of current token (space/newline), for replacing the active fragment. */
	static bool GetActiveMentionReplaceRange(const FString& FullText, int32& OutAt, int32& OutEndExclusive)
	{
		OutAt = INDEX_NONE;
		OutEndExclusive = INDEX_NONE;
		const int32 At = FullText.Find(TEXT("@"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (At == INDEX_NONE)
		{
			return false;
		}
		FString Rest = FullText.Mid(At + 1);
		int32 Space = INDEX_NONE;
		if (Rest.FindChar(TEXT(' '), Space) || Rest.FindChar(TEXT('\n'), Space))
		{
			Rest = Rest.Left(Space);
		}
		OutAt = At;
		OutEndExclusive = At + 1 + Rest.Len();
		return true;
	}
} // namespace UnrealAiComposerMentionParse
#endif

void SChatComposer::Construct(const FArguments& InArgs)
{
	static const FMargin PadChipsSlot(4.f, 2.f);
	static const FMargin PadMentionSlot(4.f, 0.f);
	static const FMargin PadFooterSlot(6.f, 4.f);
	BackendRegistry = InArgs._BackendRegistry;
	MessageList = InArgs._MessageList;
	Session = InArgs._Session;

	ModelOptions.Reset();
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

	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(PadChipsSlot)
			[
				SAssignNew(ChipsRow, SWrapBox)
					.UseAllottedSize(true)
					.InnerSlotPadding(FVector2D(6.f, 4.f))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(PadMentionSlot)
			[
				SAssignNew(MentionBorder, SBorder)
					.Visibility(EVisibility::Collapsed)
					.BorderBackgroundColor(FLinearColor(0.16f, 0.16f, 0.18f, 0.95f))
					.Padding(FMargin(6.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
						[
							SNew(STextBlock)
								.Text(LOCTEXT("MentionPanelTitle", "Mentions — assets & scene"))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.58f, 0.62f, 1.f)))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SBox)
								.MaxDesiredHeight(240.f)
								[
									SAssignNew(MentionScrollBox, SScrollBox)
									+ SScrollBox::Slot()
									[
										SAssignNew(MentionListBox, SVerticalBox)
									]
								]
						]
					]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(PadMentionSlot)
			[
				SAssignNew(SlashCommandBorder, SBorder)
					.Visibility(EVisibility::Collapsed)
					.BorderBackgroundColor(FLinearColor(0.14f, 0.18f, 0.16f, 0.95f))
					.Padding(FMargin(6.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 4.f))
						[
							SNew(STextBlock)
								.Text(LOCTEXT("SlashPanelTitle", "Commands"))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.58f, 0.62f, 1.f)))
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SBox)
								.MaxDesiredHeight(240.f)
								[
									SAssignNew(SlashScrollBox, SScrollBox)
									+ SScrollBox::Slot()
									[
										SAssignNew(SlashCommandListBox, SVerticalBox)
									]
								]
						]
					]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Top)
				[
					SAssignNew(InputBoxWrap, SBox)
						.MinDesiredHeight(InputMinHeight)
						.MaxDesiredHeight(InputMaxHeight)
						[
							SAssignNew(InputBox, SMultiLineEditableTextBox)
								.AllowMultiLine(true)
								.AutoWrapText(true)
								.HintText(LOCTEXT(
									"ComposerHint",
									"Enter to send · Shift+Enter newline · @ or / for context & commands."))
								.OnKeyDownHandler(this, &SChatComposer::OnInputKeyDown)
								.OnTextChanged(this, &SChatComposer::OnPromptTextChanged)
						]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f).VAlign(VAlign_Top)
				[
					SNew(SBox)
						.MinDesiredWidth(100.f)
						.MinDesiredHeight(34.f)
						[
							SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "RoundButton")
								.ContentPadding(FMargin(12.f, 8.f))
								.OnClicked(this, &SChatComposer::OnSendOrStopClicked)
								.ToolTipText(this, &SChatComposer::GetSendStopTooltip)
								.ForegroundColor(FSlateColor(FLinearColor(0.94f, 0.96f, 1.f, 1.f)))
								.IsEnabled_Lambda(
									[this]()
									{
										return BackendRegistry.IsValid();
									})
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
									[
										SNew(SBox)
											.WidthOverride(22.f)
											.HeightOverride(22.f)
											[
												SNew(SImage)
													.Image_Lambda([this]() { return GetSendStopBrush(); })
													.ColorAndOpacity(FSlateColor(FLinearColor(0.94f, 0.96f, 1.f, 1.f)))
											]
									]
									+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
									[
										SNew(STextBlock)
											.Text_Lambda([this]() { return GetSendStopLabel(); })
											.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
											.ColorAndOpacity(FSlateColor(FLinearColor(0.94f, 0.96f, 1.f, 1.f)))
									]
								]
						]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
				[
					SNew(SComboButton)
						.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>(TEXT("ComboButton")))
						.ContentPadding(FMargin(8.f, 4.f))
						.HasDownArrow(false)
						.ForegroundColor(FSlateColor::UseForeground())
						.OnGetMenuContent(this, &SChatComposer::BuildModeMenuContent)
						.ToolTipText(LOCTEXT("ModeTip", "Mode: Ask (read-only), Agent, or Plan"))
						.ButtonContent()
						[
							SNew(SBox)
								.MinDesiredWidth(168.f)
								[
									SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
										  .AutoWidth()
										  .VAlign(VAlign_Center)
										  .Padding(FMargin(0.f, 0.f, 8.f, 0.f))
									[
										SNew(SBox)
											.WidthOverride(4.f)
											.HeightOverride(22.f)
											[
												SNew(SBorder)
													.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
													.BorderBackgroundColor_Lambda([this]()
													{
														return GetModeAccent();
													})
													[
														SNew(SSpacer)
													]
											]
									]
									+ SHorizontalBox::Slot()
										  .AutoWidth()
										  .VAlign(VAlign_Center)
										  .Padding(FMargin(0.f, 0.f, 6.f, 0.f))
									[
										SNew(SImage)
											.Image_Lambda([this]() { return GetModeIconBrush(); })
											.ColorAndOpacity(FSlateColor::UseForeground())
									]
									+ SHorizontalBox::Slot()
										  .FillWidth(1.f)
										  .VAlign(VAlign_Center)
									[
										SNew(STextBlock)
											.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
											.ColorAndOpacity(FSlateColor::UseForeground())
											.Text(this, &SChatComposer::GetModeLabelShort)
									]
									+ SHorizontalBox::Slot()
										  .AutoWidth()
										  .VAlign(VAlign_Center)
									[
										SNew(SImage)
											.Image(FAppStyle::GetBrush(TEXT("Icons.ChevronDown")))
											.ColorAndOpacity(FSlateColor::UseForeground())
									]
								]
						]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
				[
					SAssignNew(ModelCombo, SComboBox<TSharedPtr<FString>>)
						.OptionsSource(&ModelOptions)
						.OnGenerateWidget_Lambda(
							[](TSharedPtr<FString> InOpt)
							{
								if (InOpt.IsValid()
									&& *InOpt == UnrealAiChatComposerModelUi::GAddNewModelToken)
								{
									return SNew(STextBlock).Text(LOCTEXT("AddNewModel", "+ Add new model"));
								}
								return SNew(STextBlock).Text(
									FText::FromString(InOpt.IsValid() ? **InOpt : FString()));
							})
						.InitiallySelectedItem(Initial)
						.OnSelectionChanged(this, &SChatComposer::OnModelSelectionChanged)
						.IsEnabled_Lambda([this]() { return IsModelComboEnabled(); })
						.Content()
						[
							SNew(STextBlock)
								.Text(this, &SChatComposer::GetSelectedModelText)
						]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(2.f)
				[
					SNew(SButton)
						.Text(LOCTEXT("AttachScreenshot", "Attach screenshot"))
						.ToolTipText(LOCTEXT(
							"AttachScreenshotTip",
							"Capture the active level viewport to PNG (max ~1280px on the long side for speed) and attach it to context."))
						.OnClicked(this, &SChatComposer::OnAttachScreenshotClicked)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(PadFooterSlot)
			[
				SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.52f, 0.55f, 1.f)))
					.Text(this, &SChatComposer::GetFooterText)
			]
		];

	ContextAttachmentsChangedHandle = FUnrealAiEditorModule::OnContextAttachmentsChanged().AddSP(
		this,
		&SChatComposer::SyncAttachmentChipsUi);

	RefreshAttachmentChips();

	if (Session.IsValid())
	{
		Session->OnPlanDraftBuild.AddSP(this, &SChatComposer::HandlePlanDraftBuild);
	}

	RegisterActiveTimer(
		0.5f,
		FWidgetActiveTimerDelegate::CreateSP(this, &SChatComposer::OnComposerRefreshTick));
}

SChatComposer::~SChatComposer()
{
	if (Session.IsValid())
	{
		Session->OnPlanDraftBuild.RemoveAll(this);
	}
	if (ContextAttachmentsChangedHandle.IsValid())
	{
		FUnrealAiEditorModule::OnContextAttachmentsChanged().Remove(ContextAttachmentsChangedHandle);
		ContextAttachmentsChangedHandle.Reset();
	}
	UnbindViewportScreenshotProcessed();
	PendingViewportScreenshotPath.Empty();
}

void SChatComposer::OnModelSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
{
	(void)SelectInfo;
	if (!Session.IsValid() || !NewSelection.IsValid())
	{
		return;
	}

	if (*NewSelection == UnrealAiChatComposerModelUi::GAddNewModelToken)
	{
		FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::SettingsTab);
		RestoreModelComboSelection();
		return;
	}

	Session->ModelProfileId = *NewSelection;
}

TSharedPtr<FString> SChatComposer::FindModelOptionMatchingSession() const
{
	if (Session.IsValid() && !Session->ModelProfileId.IsEmpty())
	{
		for (const TSharedPtr<FString>& M : ModelOptions)
		{
			if (M.IsValid()
				&& *M != UnrealAiChatComposerModelUi::GAddNewModelToken
				&& *M == Session->ModelProfileId)
			{
				return M;
			}
		}
	}

	for (const TSharedPtr<FString>& M : ModelOptions)
	{
		if (M.IsValid() && *M != UnrealAiChatComposerModelUi::GAddNewModelToken)
		{
			return M;
		}
	}

	return nullptr;
}

void SChatComposer::RestoreModelComboSelection()
{
	if (!ModelCombo.IsValid())
	{
		return;
	}

	ModelCombo->SetSelectedItem(FindModelOptionMatchingSession());
}

FText SChatComposer::GetSelectedModelText() const
{
	if (Session.IsValid() && !Session->ModelProfileId.IsEmpty())
	{
		return FText::FromString(Session->ModelProfileId);
	}

	return LOCTEXT("NoModel", "(no model)");
}

bool SChatComposer::IsModelComboEnabled() const
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

void SChatComposer::ResetComposerInput()
{
	if (InputBox.IsValid())
	{
		InputBox->SetText(FText::GetEmpty());
	}
	if (InputBoxWrap.IsValid())
	{
		InputBoxWrap->Invalidate(EInvalidateWidgetReason::Layout);
	}
	if (MentionBorder.IsValid())
	{
		MentionBorder->SetVisibility(EVisibility::Collapsed);
	}
}

EActiveTimerReturnType SChatComposer::OnComposerRefreshTick(double InCurrentTime, float InDeltaTime)
{
	(void)InCurrentTime;
	(void)InDeltaTime;
#if WITH_EDITOR
	if (InputBox.IsValid() && MentionBorder.IsValid()
		&& MentionBorder->GetVisibility() == EVisibility::Visible)
	{
		FUnrealAiComposerMentionIndex& Idx = FUnrealAiComposerMentionIndex::Get();
		if (Idx.IsBuildingAssets() && !Idx.AreAssetsReady())
		{
			UpdateMentionSuggestions(InputBox->GetText().ToString());
		}
	}
#endif
	const bool bBusy = IsHarnessTurnInProgress();
	if (bBusy != bLastHarnessTurnBusy)
	{
		bLastHarnessTurnBusy = bBusy;
		Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
	return EActiveTimerReturnType::Continue;
}

bool SChatComposer::IsHarnessTurnInProgress() const
{
	if (!BackendRegistry.IsValid())
	{
		return false;
	}
	if (ActivePlanExecutor.IsValid() && ActivePlanExecutor->IsRunning())
	{
		return true;
	}
	if (IUnrealAiAgentHarness* H = BackendRegistry->GetAgentHarness())
	{
		return H->IsTurnInProgress();
	}
	return false;
}

const FSlateBrush* SChatComposer::GetSendStopBrush() const
{
	if (IsHarnessTurnInProgress())
	{
		return FAppStyle::GetBrush(TEXT("Icons.Toolbar.Stop"));
	}
	return FAppStyle::GetBrush(TEXT("Icons.Toolbar.Export"));
}

FText SChatComposer::GetSendStopLabel() const
{
	if (IsHarnessTurnInProgress())
	{
		return LOCTEXT("StopLabelComposer", "Stop");
	}
	return LOCTEXT("SendLabelComposer", "Send");
}

FText SChatComposer::GetSendStopTooltip() const
{
	if (IsHarnessTurnInProgress())
	{
		return LOCTEXT("StopTipComposer", "Stop the in-flight run");
	}
	return LOCTEXT("SendTipComposer", "Send message (Enter)");
}

FReply SChatComposer::OnSendOrStopClicked()
{
	if (!BackendRegistry.IsValid())
	{
		return FReply::Handled();
	}
	if (IUnrealAiAgentHarness* H = BackendRegistry->GetAgentHarness())
	{
		if (ActivePlanExecutor.IsValid() && ActivePlanExecutor->IsRunning())
		{
			ActivePlanExecutor->Cancel();
			return FReply::Handled();
		}
		if (H->IsTurnInProgress())
		{
			H->CancelTurn();
			return FReply::Handled();
		}
	}
	return OnSendClicked();
}

void SChatComposer::SyncAttachmentChipsUi()
{
	RefreshAttachmentChips();
}

FReply SChatComposer::OnInputKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	(void)MyGeometry;
	if (InKeyEvent.IsShiftDown() && InKeyEvent.GetKey() == EKeys::Enter)
	{
		return FReply::Unhandled();
	}

	const FKey Key = InKeyEvent.GetKey();

#if WITH_EDITOR
	const bool bMentionMenuVisible = MentionBorder.IsValid()
		&& MentionBorder->GetVisibility() == EVisibility::Visible;
	const bool bSlashMenuVisible = SlashCommandBorder.IsValid()
		&& SlashCommandBorder->GetVisibility() == EVisibility::Visible;

	if (Key == EKeys::Escape)
	{
		if (bMentionMenuVisible)
		{
			if (MentionBorder.IsValid())
			{
				MentionBorder->SetVisibility(EVisibility::Collapsed);
			}
			return FReply::Handled();
		}
		if (bSlashMenuVisible)
		{
			if (SlashCommandBorder.IsValid())
			{
				SlashCommandBorder->SetVisibility(EVisibility::Collapsed);
			}
			return FReply::Handled();
		}
	}

	if (IsMentionMenuOpenWithChoices())
	{
		const int32 N = MentionFilteredList.Num();
		if (Key == EKeys::Up)
		{
			MentionSelectedIndex = (MentionSelectedIndex - 1 + N) % N;
			RebuildMentionPanelUi();
			return FReply::Handled();
		}
		if (Key == EKeys::Down)
		{
			MentionSelectedIndex = (MentionSelectedIndex + 1) % N;
			RebuildMentionPanelUi();
			return FReply::Handled();
		}
		if (Key == EKeys::Enter || Key == EKeys::Tab)
		{
			return OnPickMentionCandidate(MentionFilteredList[MentionSelectedIndex]);
		}
	}

	if (IsSlashMenuOpenWithChoices())
	{
		const int32 N = SlashFilteredList.Num();
		if (Key == EKeys::Up)
		{
			SlashSelectedIndex = (SlashSelectedIndex - 1 + N) % N;
			RebuildSlashPanelUi();
			return FReply::Handled();
		}
		if (Key == EKeys::Down)
		{
			SlashSelectedIndex = (SlashSelectedIndex + 1) % N;
			RebuildSlashPanelUi();
			return FReply::Handled();
		}
		if (Key == EKeys::Enter || Key == EKeys::Tab)
		{
			return OnPickSlashCommand(SlashFilteredList[SlashSelectedIndex].Command);
		}
	}

	if (Key == EKeys::Enter && !InKeyEvent.IsShiftDown())
	{
		if (bMentionMenuVisible && IsMentionPanelBlockingEnterWhileBuilding())
		{
			return FReply::Handled();
		}
		return OnSendOrStopClicked();
	}
#else
	if (Key == EKeys::Enter && !InKeyEvent.IsShiftDown())
	{
		return OnSendOrStopClicked();
	}
#endif

	return FReply::Unhandled();
}

void SChatComposer::UnbindViewportScreenshotProcessed()
{
	if (ViewportScreenshotProcessedHandle.IsValid())
	{
		FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ViewportScreenshotProcessedHandle);
		ViewportScreenshotProcessedHandle.Reset();
	}
}

void SChatComposer::OnViewportScreenshotProcessed()
{
	if (PendingViewportScreenshotPath.IsEmpty())
	{
		UnbindViewportScreenshotProcessed();
		return;
	}
	if (!IFileManager::Get().FileExists(*PendingViewportScreenshotPath))
	{
		PendingViewportScreenshotPath.Empty();
		UnbindViewportScreenshotProcessed();
		return;
	}
	TArray<FContextAttachment> Atts;
	Atts.Add(UnrealAiEditorContextQueries::AttachmentFromViewportScreenshotFile(PendingViewportScreenshotPath));
	UnrealAiContextDragDrop::AddAttachmentsToActiveChat(BackendRegistry, Session, Atts);
	FUnrealAiEditorModule::NotifyContextAttachmentsChanged();
	PendingViewportScreenshotPath.Empty();
	UnbindViewportScreenshotProcessed();
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SChatComposer::OnPromptTextChanged(const FText& NewText)
{
	// AutoWrapText: relayout so wrapped line count updates (can be a frame late per Slate).
	if (InputBoxWrap.IsValid())
	{
		InputBoxWrap->Invalidate(EInvalidateWidgetReason::Layout);
	}
	Invalidate(EInvalidateWidgetReason::Layout);
	const FString Full = NewText.ToString();

#if WITH_EDITOR
	int32 SlashPos = INDEX_NONE;
	int32 AtPos = INDEX_NONE;
	FString SlashTok;
	FString AtTok;
	const bool bSlash = UnrealAiComposerPromptResolver::TryParseSlashAutocomplete(Full, SlashPos, SlashTok);
	const bool bAt = UnrealAiComposerMentionParse::ParseMentionAutocomplete(Full, AtPos, AtTok);
	if (bSlash && bAt)
	{
		if (SlashPos > AtPos)
		{
			if (MentionBorder.IsValid())
			{
				MentionBorder->SetVisibility(EVisibility::Collapsed);
			}
			UpdateSlashCommandSuggestions(Full);
		}
		else
		{
			if (SlashCommandBorder.IsValid())
			{
				SlashCommandBorder->SetVisibility(EVisibility::Collapsed);
			}
			UpdateMentionSuggestions(Full);
		}
	}
	else if (bSlash)
	{
		if (MentionBorder.IsValid())
		{
			MentionBorder->SetVisibility(EVisibility::Collapsed);
		}
		UpdateSlashCommandSuggestions(Full);
	}
	else if (bAt)
	{
		if (SlashCommandBorder.IsValid())
		{
			SlashCommandBorder->SetVisibility(EVisibility::Collapsed);
		}
		UpdateMentionSuggestions(Full);
	}
	else
	{
		if (MentionBorder.IsValid())
		{
			MentionBorder->SetVisibility(EVisibility::Collapsed);
		}
		if (SlashCommandBorder.IsValid())
		{
			SlashCommandBorder->SetVisibility(EVisibility::Collapsed);
		}
	}
#else
	{
		int32 SlashPosNE = INDEX_NONE;
		FString SlashTokNE;
		if (SlashCommandBorder.IsValid())
		{
			SlashCommandBorder->SetVisibility(EVisibility::Collapsed);
		}
		if (UnrealAiComposerPromptResolver::TryParseSlashAutocomplete(Full, SlashPosNE, SlashTokNE))
		{
			UpdateSlashCommandSuggestions(Full);
		}
	}
#endif

}

void SChatComposer::UpdateMentionSuggestions(const FString& FullText)
{
#if WITH_EDITOR
	int32 AtPos = INDEX_NONE;
	FString Token;
	if (!UnrealAiComposerMentionParse::ParseMentionAutocomplete(FullText, AtPos, Token))
	{
		if (MentionBorder.IsValid())
		{
			MentionBorder->SetVisibility(EVisibility::Collapsed);
		}
		return;
	}

	FUnrealAiComposerMentionIndex::Get().EnsureAssetIndexBuilding();
	FUnrealAiComposerMentionIndex::Get().FilterCandidates(Token, MentionFilteredList, 40);
	MentionSelectedIndex = 0;
	if (MentionFilteredList.Num() > 0)
	{
		MentionSelectedIndex = FMath::Clamp(MentionSelectedIndex, 0, MentionFilteredList.Num() - 1);
	}

	if (!MentionListBox.IsValid() || !MentionBorder.IsValid())
	{
		return;
	}
	RebuildMentionPanelUi();
	MentionBorder->SetVisibility(EVisibility::Visible);
#else
	(void)FullText;
#endif
}

void SChatComposer::RebuildMentionPanelUi()
{
#if WITH_EDITOR
	if (!MentionListBox.IsValid())
	{
		return;
	}
	MentionListBox->ClearChildren();

	FUnrealAiComposerMentionIndex& Idx = FUnrealAiComposerMentionIndex::Get();
	const bool bLoading = Idx.IsBuildingAssets() && !Idx.AreAssetsReady() && MentionFilteredList.Num() == 0;

	auto AddStatusRow = [this](const FText& Msg)
	{
		MentionListBox->AddSlot().AutoHeight().Padding(FMargin(0.f, 1.f))
			[
				SNew(SBorder)
					.BorderBackgroundColor(FLinearColor(0.14f, 0.15f, 0.17f, 1.f))
					.Padding(FMargin(6.f, 4.f))
					[
						SNew(STextBlock)
							.Text(Msg)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.58f, 0.62f, 1.f)))
					]
			];
	};

	if (bLoading)
	{
		AddStatusRow(LOCTEXT("MentionBuildingIndex", "Building index…"));
		return;
	}
	if (MentionFilteredList.Num() == 0)
	{
		AddStatusRow(LOCTEXT("MentionNoMatches", "No matches"));
		return;
	}

	for (int32 i = 0; i < MentionFilteredList.Num(); ++i)
	{
		const FMentionCandidate C = MentionFilteredList[i];
		const bool bSel = (i == MentionSelectedIndex);
		const TCHAR* KindStr = C.IsActor() ? TEXT("Actor") : TEXT("Asset");
		const FString SecLine = FString::Printf(TEXT("%s · %s"), KindStr, *C.SortClass);

		const FLinearColor AccentSel(0.35f, 0.62f, 0.98f, 1.f);
		const FLinearColor AccentUnsel(0.11f, 0.12f, 0.14f, 1.f);
		const FLinearColor RowBgSel(0.20f, 0.28f, 0.40f, 1.f);
		const FLinearColor RowBgUnsel(0.12f, 0.13f, 0.15f, 1.f);
		const FLinearColor RingSel(0.45f, 0.70f, 1.f, 0.55f);

		MentionListBox->AddSlot().AutoHeight().Padding(FMargin(0.f, 2.f))
			[
				SNew(SBorder)
					.BorderBackgroundColor(bSel ? RingSel : FLinearColor::Transparent)
					.Padding(FMargin(bSel ? 1.f : 0.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill)
						[
							SNew(SBox)
								.WidthOverride(4.f)
								[
									SNew(SBorder)
										.BorderBackgroundColor(bSel ? AccentSel : AccentUnsel)
										[
											SNew(SSpacer)
										]
								]
						]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(SButton)
								.ButtonStyle(FCoreStyle::Get(), "NoBorder")
								.Cursor(EMouseCursor::Hand)
								.IsFocusable(false)
								.ForegroundColor(FSlateColor::UseForeground())
								.OnClicked_Lambda(
									[this, C]()
									{
										return OnPickMentionCandidate(C);
									})
								[
									SNew(SBorder)
										.BorderBackgroundColor(bSel ? RowBgSel : RowBgUnsel)
										.Padding(FMargin(8.f, 5.f))
										.ToolTipText(FText::FromString(C.PrimaryKey))
										[
											SNew(SHorizontalBox)
											+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
											[
												SNew(SBox)
													.WidthOverride(18.f)
													.HeightOverride(18.f)
													[
														SNew(SImage)
															.Image_Lambda([C]() { return UnrealAiMentionRowUi::BrushForCandidate(C); })
													]
											]
											+ SHorizontalBox::Slot().FillWidth(1.f)
											[
												SNew(SVerticalBox)
												+ SVerticalBox::Slot().AutoHeight()
												[
													SNew(STextBlock)
														.Text(FText::FromString(C.DisplayName))
														.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
														.ColorAndOpacity(
															bSel ? FSlateColor(FLinearColor(0.98f, 0.99f, 1.f, 1.f))
																 : FSlateColor::UseForeground())
												]
												+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 2.f, 0.f, 0.f))
												[
													SNew(STextBlock)
														.Text(FText::FromString(SecLine))
														.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
														.ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.70f, 0.78f, 1.f)))
												]
											]
										]
								]
						]
					]
			];
	}

	ScheduleComboScrollIntoView(false);
#endif
}

bool SChatComposer::IsMentionMenuOpenWithChoices() const
{
#if WITH_EDITOR
	return MentionBorder.IsValid() && MentionBorder->GetVisibility() == EVisibility::Visible
		&& MentionFilteredList.Num() > 0;
#else
	return false;
#endif
}

bool SChatComposer::IsSlashMenuOpenWithChoices() const
{
	return SlashCommandBorder.IsValid() && SlashCommandBorder->GetVisibility() == EVisibility::Visible
		&& SlashFilteredList.Num() > 0;
}

bool SChatComposer::IsMentionPanelBlockingEnterWhileBuilding() const
{
#if WITH_EDITOR
	if (!MentionBorder.IsValid() || MentionBorder->GetVisibility() != EVisibility::Visible)
	{
		return false;
	}
	const FUnrealAiComposerMentionIndex& Idx = FUnrealAiComposerMentionIndex::Get();
	return Idx.IsBuildingAssets() && !Idx.AreAssetsReady() && MentionFilteredList.Num() == 0;
#else
	return false;
#endif
}

void SChatComposer::UpdateSlashCommandSuggestions(const FString& FullText)
{
	int32 SlashPos = INDEX_NONE;
	FString TokenAfter;
	if (!UnrealAiComposerPromptResolver::TryParseSlashAutocomplete(FullText, SlashPos, TokenAfter))
	{
		if (SlashCommandBorder.IsValid())
		{
			SlashCommandBorder->SetVisibility(EVisibility::Collapsed);
		}
		SlashFilteredList.Reset();
		return;
	}

	TArray<FUnrealAiSlashCommand> All;
	UnrealAiComposerPromptResolver::GetSlashCommands(All);
	const FString Prefix = FString(TEXT("/")) + TokenAfter;
	SlashFilteredList.Reset();
	SlashFilteredList.Reserve(All.Num());
	for (const FUnrealAiSlashCommand& C : All)
	{
		if (TokenAfter.IsEmpty() || C.Command.StartsWith(Prefix))
		{
			SlashFilteredList.Add(C);
		}
	}
	SlashSelectedIndex = 0;
	if (SlashFilteredList.Num() > 0)
	{
		SlashSelectedIndex = FMath::Clamp(SlashSelectedIndex, 0, SlashFilteredList.Num() - 1);
	}

	if (!SlashCommandListBox.IsValid() || !SlashCommandBorder.IsValid())
	{
		return;
	}
	if (SlashFilteredList.Num() == 0)
	{
		SlashCommandBorder->SetVisibility(EVisibility::Collapsed);
		return;
	}
	RebuildSlashPanelUi();
	SlashCommandBorder->SetVisibility(EVisibility::Visible);
}

void SChatComposer::RebuildSlashPanelUi()
{
	if (!SlashCommandListBox.IsValid())
	{
		return;
	}
	SlashCommandListBox->ClearChildren();

	for (int32 i = 0; i < SlashFilteredList.Num(); ++i)
	{
		const FUnrealAiSlashCommand& C = SlashFilteredList[i];
		const bool bSel = (i == SlashSelectedIndex);
		const FString CmdStr = C.Command;

		const FLinearColor AccentSel(0.35f, 0.72f, 0.55f, 1.f);
		const FLinearColor AccentUnsel(0.11f, 0.13f, 0.12f, 1.f);
		const FLinearColor RowBgSel(0.16f, 0.30f, 0.24f, 1.f);
		const FLinearColor RowBgUnsel(0.11f, 0.14f, 0.13f, 1.f);
		const FLinearColor RingSel(0.45f, 0.95f, 0.75f, 0.45f);

		SlashCommandListBox->AddSlot().AutoHeight().Padding(FMargin(0.f, 2.f))
			[
				SNew(SBorder)
					.BorderBackgroundColor(bSel ? RingSel : FLinearColor::Transparent)
					.Padding(FMargin(bSel ? 1.f : 0.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Fill)
						[
							SNew(SBox)
								.WidthOverride(4.f)
								[
									SNew(SBorder)
										.BorderBackgroundColor(bSel ? AccentSel : AccentUnsel)
										[
											SNew(SSpacer)
										]
								]
						]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(SButton)
								.ButtonStyle(FCoreStyle::Get(), "NoBorder")
								.Cursor(EMouseCursor::Hand)
								.IsFocusable(false)
								.ForegroundColor(FSlateColor::UseForeground())
								.OnClicked_Lambda(
									[this, CmdStr]()
									{
										return OnPickSlashCommand(CmdStr);
									})
								[
									SNew(SBorder)
										.BorderBackgroundColor(bSel ? RowBgSel : RowBgUnsel)
										.Padding(FMargin(8.f, 5.f))
										[
											SNew(SHorizontalBox)
											+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
											[
												SNew(SBox)
													.WidthOverride(18.f)
													.HeightOverride(18.f)
													[
														SNew(SImage)
															.Image(FAppStyle::GetBrush(TEXT("Icons.Console")))
															.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.92f, 0.82f, 1.f)))
													]
											]
											+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 10.f, 0.f))
											[
												SNew(STextBlock)
													.Text(FText::FromString(C.Command))
													.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
													.ColorAndOpacity(
														bSel ? FSlateColor(FLinearColor(0.98f, 1.f, 0.96f, 1.f))
															 : FSlateColor::UseForeground())
											]
											+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
											[
												SNew(STextBlock)
													.AutoWrapText(true)
													.Text(C.Description)
													.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
													.ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.72f, 0.68f, 1.f)))
											]
										]
								]
						]
					]
			];
	}

	ScheduleComboScrollIntoView(true);
}

void SChatComposer::ScheduleComboScrollIntoView(bool bSlashPanel)
{
	TSharedPtr<SScrollBox> Scroll = bSlashPanel ? SlashScrollBox : MentionScrollBox;
	TSharedPtr<SVerticalBox> List = bSlashPanel ? SlashCommandListBox : MentionListBox;
	const int32 Sel = bSlashPanel ? SlashSelectedIndex : MentionSelectedIndex;
	const int32 N = bSlashPanel ? SlashFilteredList.Num() : MentionFilteredList.Num();
	if (!Scroll.IsValid() || !List.IsValid() || N <= 0 || Sel < 0 || Sel >= N)
	{
		return;
	}
	FChildren* Ch = List->GetChildren();
	if (!Ch || Ch->Num() <= Sel)
	{
		return;
	}
	const TSharedRef<SWidget> RowW = Ch->GetChildAt(Sel);
	PendingComboScrollBox = Scroll;
	PendingComboScrollRow = RowW;
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SChatComposer::FlushPendingComboScroll));
}

EActiveTimerReturnType SChatComposer::FlushPendingComboScroll(double, float)
{
	if (TSharedPtr<SScrollBox> S = PendingComboScrollBox.Pin())
	{
		if (TSharedPtr<SWidget> W = PendingComboScrollRow.Pin())
		{
			S->ScrollDescendantIntoView(W.ToSharedRef(), false, EDescendantScrollDestination::IntoView);
		}
	}
	PendingComboScrollBox.Reset();
	PendingComboScrollRow.Reset();
	return EActiveTimerReturnType::Stop;
}

FReply SChatComposer::OnPickMention(const FString& AssetPath)
{
#if WITH_EDITOR
	FMentionCandidate C;
	C.Kind = EUnrealAiMentionKind::Asset;
	C.PrimaryKey = AssetPath;
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	C.AssetData = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(AssetPath));
	C.DisplayName = C.AssetData.IsValid() ? C.AssetData.AssetName.ToString() : FPaths::GetBaseFilename(AssetPath);
	C.SortClass = C.AssetData.IsValid() ? C.AssetData.AssetClassPath.ToString() : TEXT("Asset");
	return OnPickMentionCandidate(C);
#else
	return FReply::Handled();
#endif
}

FReply SChatComposer::OnPickMentionCandidate(const FMentionCandidate& Candidate)
{
#if WITH_EDITOR
	if (!InputBox.IsValid())
	{
		return FReply::Handled();
	}
	FString S = InputBox->GetText().ToString();
	int32 AtPos = INDEX_NONE;
	int32 EndEx = INDEX_NONE;
	if (!UnrealAiComposerMentionParse::GetActiveMentionReplaceRange(S, AtPos, EndEx))
	{
		return FReply::Handled();
	}
	const FString InlineTok = UnrealAiComposerMentionInsert::InlineTokenForCandidate(Candidate);
	S = S.Left(AtPos) + FString::Printf(TEXT("@%s "), *InlineTok) + S.Mid(EndEx);
	InputBox->SetText(FText::FromString(S));

	FContextAttachment Att;
	if (Candidate.IsAsset())
	{
		FAssetData AD = Candidate.AssetData;
		if (!AD.IsValid())
		{
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			AD = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(Candidate.PrimaryKey));
		}
		if (AD.IsValid())
		{
			Att = UnrealAiEditorContextQueries::AttachmentFromAssetData(AD);
		}
	}
	else if (Candidate.IsActor())
	{
		if (AActor* A = FindObject<AActor>(nullptr, *Candidate.PrimaryKey))
		{
			Att = UnrealAiEditorContextQueries::AttachmentFromActor(A);
		}
	}

	if (!Att.Payload.IsEmpty())
	{
		Att.Label = UnrealAiComposerMentionInsert::ChipLabelForCandidate(Candidate);
	}

	if (!Att.Payload.IsEmpty() && BackendRegistry.IsValid() && Session.IsValid())
	{
		const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
		const FString Tid = Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
		if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
		{
			Ctx->LoadOrCreate(ProjectId, Tid);
			Ctx->AddAttachment(Att);
			FUnrealAiEditorModule::NotifyContextAttachmentsChanged();
		}
	}

	if (MentionBorder.IsValid())
	{
		MentionBorder->SetVisibility(EVisibility::Collapsed);
	}
	MentionFilteredList.Reset();
	RefreshAttachmentChips();
#endif
	return FReply::Handled();
}

FReply SChatComposer::OnPickSlashCommand(const FString& Command)
{
	if (!InputBox.IsValid())
	{
		return FReply::Handled();
	}
	FString S = InputBox->GetText().ToString();
	int32 SlashPos = INDEX_NONE;
	FString TokenAfter;
	if (!UnrealAiComposerPromptResolver::TryParseSlashAutocomplete(S, SlashPos, TokenAfter))
	{
		return FReply::Handled();
	}
	S = S.Left(SlashPos);
	S += FString::Printf(TEXT("%s "), *Command);
	InputBox->SetText(FText::FromString(S));
	if (SlashCommandBorder.IsValid())
	{
		SlashCommandBorder->SetVisibility(EVisibility::Collapsed);
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
		const bool bViewportThumb =
			!A.Payload.IsEmpty()
			&& (A.IconClassPath == TEXT("UnrealAi.ViewportScreenshot") || A.Label.Contains(TEXT("Viewport screenshot")));
		if (bViewportThumb)
		{
			const FString PathCopy = A.Payload;
			ChipsRow->AddSlot()
				.Padding(FMargin(0.f, 0.f, 6.f, 4.f))
				[
					SNew(SBorder)
						.BorderBackgroundColor(FLinearColor(0.2f, 0.24f, 0.3f, 0.85f))
						.Padding(FMargin(6.f, 3.f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
							[
								SNew(SButton)
									.ButtonStyle(FCoreStyle::Get(), "NoBorder")
									.ContentPadding(FMargin(0.f))
									.ToolTipText(LOCTEXT("ViewScreenshotTip", "Click to view full-size image"))
									.OnClicked_Lambda([PathCopy]()
									{
										UnrealAiOpenImagePreviewWindow(PathCopy);
										return FReply::Handled();
									})
									[
										SNew(SBox)
											.WidthOverride(56.f)
											.HeightOverride(40.f)
											[
												SNew(SImage)
													.Image_Lambda([PathCopy]()
													{
														return UnrealAiGetOrCreateScreenshotThumbnailBrush(PathCopy);
													})
											]
									]
							]
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
									.AutoWrapText(true)
									.Text(FText::FromString(Label))
									.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
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
			continue;
		}
		ChipsRow->AddSlot()
			.Padding(FMargin(0.f, 0.f, 6.f, 4.f))
			[
				SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
					.BorderBackgroundColor(FLinearColor(0.14f, 0.19f, 0.28f, 0.92f))
					.Padding(FMargin(8.f, 5.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
						[
							SNew(SBox)
								.WidthOverride(20.f)
								.HeightOverride(20.f)
								[
									SNew(SImage)
										.Image_Lambda([A]() { return UnrealAiAttachmentUi::BrushForChip(A); })
								]
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock)
								.AutoWrapText(true)
								.Text(FText::FromString(Label))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.94f, 0.97f, 1.f)))
						]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SButton)
								.ButtonStyle(FCoreStyle::Get(), "NoBorder")
								.Cursor(EMouseCursor::Hand)
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
	TOptional<FContextAttachment> Removed;
	if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
	{
		Ctx->LoadOrCreate(ProjectId, Tid);
		if (const FAgentContextState* St = Ctx->GetState(ProjectId, Tid))
		{
			if (St->Attachments.IsValidIndex(Index))
			{
				Removed = St->Attachments[Index];
			}
		}
		Ctx->RemoveAttachment(Index);
	}
#if WITH_EDITOR
	if (Removed.IsSet() && InputBox.IsValid())
	{
		FString S = InputBox->GetText().ToString();
		const FString Tok = UnrealAiComposerMentionInsert::InlineTokenFromContextAttachment(Removed.GetValue());
		UnrealAiComposerMentionInsert::RemoveAtMentionsForToken(S, Tok);
		InputBox->SetText(FText::FromString(S));
	}
#endif
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

	const FString RawPrompt = InputBox->GetText().ToString();
	const FUnrealAiComposerResolveResult Resolved = UnrealAiComposerPromptResolver::ResolveBeforeSend(RawPrompt);

	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	const FString ThreadIdStr = Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);

	if (Resolved.bLocalOnlyNoLlm)
	{
		InputBox->SetText(FText::GetEmpty());

		FAgentContextBuildOptions Opt;
		Opt.Mode = AgentMode;
		if (FUnrealAiModelProfileRegistry* Profiles = BackendRegistry->GetModelProfileRegistry())
		{
			FUnrealAiModelCapabilities Caps;
			Profiles->GetEffectiveCapabilities(Session->ModelProfileId, Caps);
			Opt.bModelSupportsImages = Caps.bSupportsImages;
		}

		FString Overview = TEXT("Context service unavailable.");
		if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
		{
			Overview = UnrealAiFormatContextOverviewForUi(Ctx, ProjectId, ThreadIdStr, Opt);
		}
		MessageList->GetTranscript()->AddInformationalNotice(Overview);
		RefreshAttachmentChips();
		return FReply::Handled();
	}

	IUnrealAiAgentHarness* Harness = BackendRegistry->GetAgentHarness();
	if (!Harness)
	{
		return FReply::Handled();
	}

	const FString Prompt = Resolved.ResolvedText;
	InputBox->SetText(FText::GetEmpty());

	if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
	{
		Ctx->LoadOrCreate(ProjectId, ThreadIdStr);
		UnrealAiContextMentionParser::ApplyMentionsFromPrompt(Ctx, ProjectId, ThreadIdStr, Prompt);
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

	IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence();
	const TSharedPtr<IAgentRunSink> Sink = MakeShared<FUnrealAiChatRunSink>(
		MessageList->GetTranscript(),
		Session,
		Persist,
		ProjectId,
		ThreadIdStr,
		AgentMode);
	if (AgentMode == EUnrealAiAgentMode::Plan)
	{
		FUnrealAiPlanExecutorStartOptions PlanOpts;
		PlanOpts.bPauseAfterPlannerForBuild = true;
		ActivePlanExecutor = FUnrealAiPlanExecutor::Start(
			Harness,
			BackendRegistry->GetContextService(),
			Req,
			Sink,
			PlanOpts);
	}
	else
	{
		Harness->RunTurn(Req, Sink);
	}

	RefreshAttachmentChips();
	return FReply::Handled();
}

void SChatComposer::HandlePlanDraftBuild(const FString& DagJson)
{
	if (!BackendRegistry.IsValid() || !MessageList.IsValid() || !Session.IsValid())
	{
		return;
	}
	IUnrealAiAgentHarness* Harness = BackendRegistry->GetAgentHarness();
	if (!Harness)
	{
		return;
	}
	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	const FString ThreadIdStr = Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
	IUnrealAiPersistence* Persist = BackendRegistry->GetPersistence();

	FString Err;
	if (ActivePlanExecutor.IsValid() && ActivePlanExecutor->IsAwaitingBuild())
	{
		if (!ActivePlanExecutor->ApplyDagJsonForBuild(DagJson, Err))
		{
			MessageList->GetTranscript()->AddInformationalNotice(
				FString::Printf(TEXT("Plan JSON invalid: %s"), *Err));
			return;
		}
		if (Persist)
		{
			Persist->ClearThreadPlanDraft(ProjectId, ThreadIdStr);
		}
		MessageList->GetTranscript()->RemovePlanDraftPendingBlocks();
		ActivePlanExecutor->ResumeNodeExecution();
		return;
	}

	if (ActivePlanExecutor.IsValid() && ActivePlanExecutor->IsRunning())
	{
		MessageList->GetTranscript()->AddInformationalNotice(
			TEXT("A plan run is already in progress; wait for it to finish."));
		return;
	}

	FUnrealAiPlanDag TestDag;
	if (!UnrealAiPlanDag::ParseDagJson(DagJson, TestDag, Err) || !UnrealAiPlanDag::ValidateDag(TestDag, 64, Err))
	{
		MessageList->GetTranscript()->AddInformationalNotice(FString::Printf(TEXT("Plan JSON invalid: %s"), *Err));
		return;
	}
	if (Persist)
	{
		Persist->ClearThreadPlanDraft(ProjectId, ThreadIdStr);
	}
	MessageList->GetTranscript()->RemovePlanDraftPendingBlocks();

	FUnrealAiAgentTurnRequest Req;
	Req.ProjectId = ProjectId;
	Req.ThreadId = ThreadIdStr;
	Req.Mode = EUnrealAiAgentMode::Plan;
	Req.UserText = FString();
	Req.ModelProfileId = Session->ModelProfileId;
	Req.bRecordAssistantAsStubToolResult = true;

	const TSharedPtr<IAgentRunSink> Sink = MakeShared<FUnrealAiChatRunSink>(
		MessageList->GetTranscript(),
		Session,
		Persist,
		ProjectId,
		ThreadIdStr,
		EUnrealAiAgentMode::Plan);

	ActivePlanExecutor = FUnrealAiPlanExecutor::ResumeExecutionFromDag(Harness, BackendRegistry->GetContextService(), Req, Sink, DagJson);
}

void SChatComposer::SetAgentMode(EUnrealAiAgentMode NewMode)
{
	if (AgentMode == NewMode)
	{
		return;
	}
	AgentMode = NewMode;
	Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
}

FText SChatComposer::GetModeLabelShort() const
{
	switch (AgentMode)
	{
	case EUnrealAiAgentMode::Ask:
		return LOCTEXT("ModeAskShort", "Ask");
	case EUnrealAiAgentMode::Agent:
		return LOCTEXT("ModeAgentShort", "Agent");
	case EUnrealAiAgentMode::Plan:
		return LOCTEXT("ModePlanShort", "Plan");
	default:
		return LOCTEXT("ModeAskShort", "Ask");
	}
}

FLinearColor SChatComposer::GetModeAccent() const
{
	return UnrealAiModeUi::Accent(AgentMode);
}

const FSlateBrush* SChatComposer::GetModeIconBrush() const
{
	return UnrealAiModeUi::ModeIconBrush(AgentMode);
}

TSharedRef<SWidget> SChatComposer::MakeModeMenuRow(EUnrealAiAgentMode Mode, FText Title, FText Blurb)
{
	const FLinearColor Accent = UnrealAiModeUi::Accent(Mode);
	const bool bSelected = (AgentMode == Mode);
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "Menu.Button")
		.ForegroundColor(FSlateColor::UseForeground())
		.OnClicked_Lambda(
			[this, Mode]()
			{
				SetAgentMode(Mode);
				FSlateApplication::Get().DismissAllMenus();
				return FReply::Handled();
			})
		[
			SNew(SBorder)
				.BorderBackgroundColor(
					bSelected ? FLinearColor(0.22f, 0.24f, 0.28f, 0.95f)
							  : FLinearColor(0.14f, 0.14f, 0.16f, 0.55f))
				.Padding(FMargin(8.f, 6.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(SBox)
							.WidthOverride(4.f)
							.HeightOverride(32.f)
							[
								SNew(SBorder)
									.BorderBackgroundColor(Accent)
									[
										SNew(SSpacer)
									]
							]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(SImage)
							.Image(UnrealAiModeUi::ModeIconBrush(Mode))
							.ColorAndOpacity(FSlateColor(FLinearColor::White))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
								.ColorAndOpacity(FSlateColor(FLinearColor::White))
								.Text(Title)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
								.ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.64f, 0.68f, 1.f)))
								.Text(Blurb)
						]
					]
				]
		];
}

TSharedRef<SWidget> SChatComposer::BuildModeMenuContent()
{
	return SNew(SBorder)
		.Padding(4.f)
		.BorderBackgroundColor(FLinearColor(0.12f, 0.12f, 0.13f, 0.98f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
			[
				MakeModeMenuRow(
					EUnrealAiAgentMode::Ask,
					LOCTEXT("ModeAskTitle", "Ask"),
					LOCTEXT("ModeAskBlurb", "Read-only tools; no writes"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
			[
				MakeModeMenuRow(
					EUnrealAiAgentMode::Agent,
					LOCTEXT("ModeAgentTitle", "Agent"),
					LOCTEXT("ModeAgentBlurb", "Single-model tool loop until done or round cap"))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeModeMenuRow(
					EUnrealAiAgentMode::Plan,
					LOCTEXT("ModePlanTitle", "Plan"),
					LOCTEXT("ModePlanBlurb", "DAG planner + Type-B worker execution"))
			]
		];
}

FReply SChatComposer::OnAttachScreenshotClicked()
{
	if (!BackendRegistry.IsValid() || !Session.IsValid() || !GEditor)
	{
		return FReply::Handled();
	}
	if (!PendingViewportScreenshotPath.IsEmpty())
	{
		return FReply::Handled();
	}
	const FString FullPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("UnrealAiEditor"),
		TEXT("ViewportCaptures"),
		FString::Printf(TEXT("Viewport_%s.png"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FullPath), true);

	UnbindViewportScreenshotProcessed();
	const TWeakPtr<SChatComposer> ComposerWeak = StaticCastSharedRef<SChatComposer>(AsShared());
	ViewportScreenshotProcessedHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda(
		[ComposerWeak]()
		{
			if (const TSharedPtr<SChatComposer> P = ComposerWeak.Pin())
			{
				P->OnViewportScreenshotProcessed();
			}
		});
	PendingViewportScreenshotPath = FullPath;

#if WITH_EDITOR
	FViewport* VP = nullptr;
	if (FEditorViewportClient* VC = UnrealAiGetActiveLevelViewportClient())
	{
		VP = VC->Viewport;
	}
	const FIntRect CapRect = UnrealAiComputeCappedViewportRect(VP, 1280);
	const bool bUseRect = CapRect.Width() > 0 && CapRect.Height() > 0;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([FullPath, CapRect, bUseRect](float)
	{
		if (bUseRect)
		{
			FScreenshotRequest::RequestScreenshot(FullPath, false, false, false, CapRect);
		}
		else
		{
			FScreenshotRequest::RequestScreenshot(FullPath, false, false);
		}
		return false;
	}));
#else
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([FullPath](float)
	{
		FScreenshotRequest::RequestScreenshot(FullPath, false, false);
		return false;
	}));
#endif
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
