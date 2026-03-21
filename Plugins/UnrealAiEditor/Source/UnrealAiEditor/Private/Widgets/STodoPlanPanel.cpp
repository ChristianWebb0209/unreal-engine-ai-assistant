#include "Widgets/STodoPlanPanel.h"

#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/AgentContextTypes.h"
#include "Context/IAgentContextService.h"
#include "Context/UnrealAiProjectId.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "Style/UnrealAiEditorStyle.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

void STodoPlanPanel::ParseSteps(const FString& InPlanJson, TArray<FString>& OutTitles)
{
	OutTitles.Reset();
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(InPlanJson);
	if (!FJsonSerializer::Deserialize(R, Root) || !Root.IsValid())
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
	if (!Root->TryGetArrayField(TEXT("steps"), Steps) || !Steps)
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& V : *Steps)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (!V.IsValid() || !V->TryGetObject(O) || !O->IsValid())
		{
			continue;
		}
		FString T;
		if ((*O)->TryGetStringField(TEXT("title"), T) && !T.IsEmpty())
		{
			OutTitles.Add(MoveTemp(T));
		}
	}
}

void STodoPlanPanel::Construct(const FArguments& InArgs)
{
	const FString Title = InArgs._Title.IsEmpty() ? FString(TEXT("Plan")) : InArgs._Title;
	PlanJson = InArgs._PlanJson;
	BackendRegistry = InArgs._BackendRegistry;
	Session = InArgs._Session;

	TArray<FString> Steps;
	ParseSteps(PlanJson, Steps);

	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	const FString ThreadId = Session.IsValid()
		? Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens)
		: FString();
	TArray<bool> DoneFlags;
	if (BackendRegistry.IsValid() && Session.IsValid())
	{
		if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
		{
			Ctx->LoadOrCreate(ProjectId, ThreadId);
			if (const FAgentContextState* St = Ctx->GetState(ProjectId, ThreadId))
			{
				if (!St->ActiveTodoPlanJson.IsEmpty() && St->ActiveTodoPlanJson == PlanJson)
				{
					DoneFlags = St->TodoStepsDone;
				}
			}
		}
	}

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(4.f)
		[
			SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("Plan: %s"), *Title)))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		];
	if (Steps.Num() == 0)
	{
		Box->AddSlot().AutoHeight().Padding(FMargin(4.f, 0.f))
			[
				SNew(STextBlock)
					.AutoWrapText(true)
					.Text(FText::FromString(PlanJson.Left(400)))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.74f, 0.72f, 0.68f, 1.f)))
			];
	}
	else
	{
		for (int32 i = 0; i < Steps.Num(); ++i)
		{
			const FString Line = FString::Printf(TEXT("%d. %s"), i + 1, *Steps[i]);
			const bool bDone = DoneFlags.IsValidIndex(i) && DoneFlags[i];
			Box->AddSlot().AutoHeight().Padding(FMargin(8.f, 2.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0.f, 0.f, 8.f, 0.f))
					[
						SNew(SCheckBox)
							.IsChecked(bDone ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
							.IsEnabled(false)
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						SNew(STextBlock)
							.AutoWrapText(true)
							.Text(FText::FromString(Line))
					]
				];
		}
	}

	ChildSlot
		[
			SNew(SBorder)
				.BorderImage(FUnrealAiEditorStyle::GetBrush(TEXT("UnrealAiEditor.TodoPlanPanel")))
				.Padding(FMargin(8.f))
				[
					Box
				]
		];
}

#undef LOCTEXT_NAMESPACE
