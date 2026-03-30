#include "Widgets/SUnrealAiLocalJsonInspectorPanel.h"

#include "Style/UnrealAiEditorStyle.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

namespace
{
	static constexpr int64 GMaxDebugFileBytes = 512 * 1024;
}

void SUnrealAiLocalJsonInspectorPanel::Construct(const FArguments& InArgs)
{
	(void)InArgs;
	CurrentInspectorRaw.Reset();

	ChildSlot
		[
			SNew(SBorder)
			.Padding(0.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(InspectorText, SMultiLineEditableText)
					.IsReadOnly(true)
					.AutoWrapText(true)
						.Font(FUnrealAiEditorStyle::FontMono9())
				]
			]
		];
}

void SUnrealAiLocalJsonInspectorPanel::SetInspectorText(const FString& Text)
{
	CurrentInspectorRaw = Text;
	if (!InspectorText.IsValid())
	{
		return;
	}
	InspectorText->SetText(FText::FromString(PrettyOrRawJson(Text)));
}

void SUnrealAiLocalJsonInspectorPanel::InspectFilePath(const FString& FilePath)
{
	bool bTrunc = false;
	const FString Raw = LoadFileCapped(FilePath, bTrunc);
	FString Body = Raw;
	if (bTrunc)
	{
		Body += TEXT("\n\n--- truncated (size cap) ---");
	}

	// Preserve the original debug tab behavior: only pretty-print when it looks like a JSON file.
	const bool bIsJson = FilePath.ToLower().EndsWith(TEXT(".json"));
	if (bIsJson)
	{
		SetInspectorText(Body);
		return;
	}

	CurrentInspectorRaw = Body;
	if (InspectorText.IsValid())
	{
		InspectorText->SetText(FText::FromString(Body));
	}
}

void SUnrealAiLocalJsonInspectorPanel::CopyCurrentToClipboard()
{
	if (!InspectorText.IsValid())
	{
		return;
	}
	const FString S = InspectorText->GetText().ToString();
	if (!S.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*S);
	}
}

FString SUnrealAiLocalJsonInspectorPanel::PrettyOrRawJson(const FString& Raw) const
{
	TSharedPtr<FJsonObject> Obj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out, 2);
		if (FJsonSerializer::Serialize(Obj.ToSharedRef(), W))
		{
			return Out;
		}
	}

	// Some JSON files may be a raw array or primitive; try generic JSON value formatting.
	TSharedPtr<FJsonValue> Val;
	const TSharedRef<TJsonReader<>> Reader2 = TJsonReaderFactory<>::Create(Raw);
	if (FJsonSerializer::Deserialize(Reader2, Val) && Val.IsValid())
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> W =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out, 2);
		if (FJsonSerializer::Serialize(Val.ToSharedRef(), TEXT(""), W))
		{
			return Out;
		}
	}
	return Raw;
}

FString SUnrealAiLocalJsonInspectorPanel::LoadFileCapped(const FString& Path, bool& bOutTruncated) const
{
	bOutTruncated = false;
	if (!FPaths::FileExists(Path))
	{
		return FString::Printf(TEXT("(file not found: %s)"), *Path);
	}
	const int64 Sz = IFileManager::Get().FileSize(*Path);
	if (Sz < 0)
	{
		return TEXT("(could not read file size)");
	}
	if (Sz > GMaxDebugFileBytes)
	{
		bOutTruncated = true;
		FArchive* Reader = IFileManager::Get().CreateFileReader(*Path);
		if (!Reader)
		{
			return TEXT("(open failed)");
		}
		TArray<uint8> Buf;
		Buf.SetNumUninitialized(GMaxDebugFileBytes);
		Reader->Serialize(Buf.GetData(), GMaxDebugFileBytes);
		delete Reader;

		const FUTF8ToTCHAR Utf8(reinterpret_cast<const ANSICHAR*>(Buf.GetData()), Buf.Num());
		return FString(Utf8.Length(), Utf8.Get());
	}

	FString S;
	if (FFileHelper::LoadFileToString(S, *Path))
	{
		return S;
	}
	return TEXT("(read failed)");
}

#undef LOCTEXT_NAMESPACE

