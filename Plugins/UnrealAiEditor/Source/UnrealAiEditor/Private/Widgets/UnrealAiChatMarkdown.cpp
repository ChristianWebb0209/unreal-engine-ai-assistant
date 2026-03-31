#include "Widgets/UnrealAiChatMarkdown.h"

#include "Tools/Presentation/UnrealAiEditorNavigation.h"
#include "Style/UnrealAiEditorStyle.h"

#include "Misc/Char.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "ClassIconFinder.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#endif

FString UnrealAiStripInlineMarkdown(const FString& Line)
{
	FString O = Line;
	O.ReplaceInline(TEXT("**"), TEXT(""));
	O.ReplaceInline(TEXT("__"), TEXT(""));
	O.ReplaceInline(TEXT("`"), TEXT(""));
	return O;
}

namespace UnrealAiChatMarkdownInline
{
	enum class ESegmentKind : uint8
	{
		Text,
		Link,
		Code,
		Bold,
	};

	struct FInlineSegment
	{
		ESegmentKind Kind = ESegmentKind::Text;
		FString Text;
		FString Target;
	};

	static void SplitBareHttpUrlsFromText(const FString& Text, TArray<FInlineSegment>& OutAppend)
	{
		if (Text.IsEmpty())
		{
			return;
		}
		int32 Pos = 0;
		const int32 Len = Text.Len();
		while (Pos < Len)
		{
			const int32 Http = Text.Find(TEXT("http://"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Pos);
			const int32 Https = Text.Find(TEXT("https://"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Pos);
			int32 UrlStart = INDEX_NONE;
			if (Http >= 0 && Https >= 0)
			{
				UrlStart = FMath::Min(Http, Https);
			}
			else if (Http >= 0)
			{
				UrlStart = Http;
			}
			else if (Https >= 0)
			{
				UrlStart = Https;
			}

			if (UrlStart == INDEX_NONE)
			{
				const FString Tail = Text.Mid(Pos);
				if (!Tail.IsEmpty())
				{
					FInlineSegment Seg;
					Seg.Kind = ESegmentKind::Text;
					Seg.Text = Tail;
					OutAppend.Add(MoveTemp(Seg));
				}
				break;
			}
			if (UrlStart > Pos)
			{
				const FString Pre = Text.Mid(Pos, UrlStart - Pos);
				if (!Pre.IsEmpty())
				{
					FInlineSegment Seg;
					Seg.Kind = ESegmentKind::Text;
					Seg.Text = Pre;
					OutAppend.Add(MoveTemp(Seg));
				}
			}

			int32 UrlEnd = UrlStart;
			while (UrlEnd < Len)
			{
				const TCHAR C = Text[UrlEnd];
				if (FChar::IsWhitespace(C) || C == TEXT(')') || C == TEXT(']') || C == TEXT('>')
					|| C == TEXT('"') || C == TEXT('\''))
				{
					break;
				}
				++UrlEnd;
			}
			while (UrlEnd > UrlStart
				&& (Text[UrlEnd - 1] == TEXT('.') || Text[UrlEnd - 1] == TEXT(',') || Text[UrlEnd - 1] == TEXT(';')
					|| Text[UrlEnd - 1] == TEXT('!')))
			{
				--UrlEnd;
			}
			const FString Url = Text.Mid(UrlStart, UrlEnd - UrlStart);
			if (!Url.IsEmpty())
			{
				FInlineSegment Seg;
				Seg.Kind = ESegmentKind::Link;
				Seg.Text = Url;
				Seg.Target = Url;
				OutAppend.Add(MoveTemp(Seg));
			}
			Pos = FMath::Max(UrlEnd, UrlStart + 1);
		}
	}

	static void ExpandTextSegmentsBareUrls(TArray<FInlineSegment>& Segs)
	{
		TArray<FInlineSegment> Expanded;
		for (FInlineSegment& Seg : Segs)
		{
			if (Seg.Kind != ESegmentKind::Text)
			{
				Expanded.Add(MoveTemp(Seg));
				continue;
			}
			SplitBareHttpUrlsFromText(Seg.Text, Expanded);
		}
		Segs = MoveTemp(Expanded);
	}

	static bool IsLikelyUnrealTargetPath(const FString& InText)
	{
		FString T = InText;
		T.TrimStartAndEndInline();
		if (T.StartsWith(TEXT("\"")) && T.EndsWith(TEXT("\"")) && T.Len() > 1)
		{
			T = T.Mid(1, T.Len() - 2);
			T.TrimStartAndEndInline();
		}
		return T.StartsWith(TEXT("/Game/"))
			|| T.StartsWith(TEXT("Game/"))
			|| T.StartsWith(TEXT("/Script/"))
			|| T.StartsWith(TEXT("/Engine/"))
			|| T.Contains(TEXT(":PersistentLevel."))
			|| T.StartsWith(TEXT("PersistentLevel."));
	}

	static void SplitInlineCodeSpansFromText(const FString& Text, TArray<FInlineSegment>& OutAppend)
	{
		if (Text.IsEmpty())
		{
			return;
		}
		const int32 Len = Text.Len();
		int32 Pos = 0;
		while (Pos < Len)
		{
			const int32 Tick = Text.Find(TEXT("`"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			if (Tick == INDEX_NONE)
			{
				const FString Tail = Text.Mid(Pos);
				if (!Tail.IsEmpty())
				{
					FInlineSegment Seg;
					Seg.Kind = ESegmentKind::Text;
					Seg.Text = Tail;
					OutAppend.Add(MoveTemp(Seg));
				}
				break;
			}
			if (Tick > Pos)
			{
				const FString Pre = Text.Mid(Pos, Tick - Pos);
				if (!Pre.IsEmpty())
				{
					FInlineSegment Seg;
					Seg.Kind = ESegmentKind::Text;
					Seg.Text = Pre;
					OutAppend.Add(MoveTemp(Seg));
				}
			}

			int32 FenceLen = 1;
			if (Tick + 1 < Len && Text[Tick + 1] == TEXT('`'))
			{
				FenceLen = 2;
			}
			const FString Fence = FenceLen == 2 ? FString(TEXT("``")) : FString(TEXT("`"));
			const int32 CodeStart = Tick + FenceLen;
			const int32 EndTick = Text.Find(Fence, ESearchCase::CaseSensitive, ESearchDir::FromStart, CodeStart);
			if (EndTick == INDEX_NONE)
			{
				// Unclosed code span: keep as plain text.
				const FString Tail = Text.Mid(Tick);
				FInlineSegment Seg;
				Seg.Kind = ESegmentKind::Text;
				Seg.Text = Tail;
				OutAppend.Add(MoveTemp(Seg));
				break;
			}

			const FString CodeText = Text.Mid(CodeStart, EndTick - CodeStart).TrimStartAndEnd();
			FInlineSegment CodeSeg;
			CodeSeg.Kind = ESegmentKind::Code;
			CodeSeg.Text = CodeText;
			if (IsLikelyUnrealTargetPath(CodeText))
			{
				CodeSeg.Target = CodeText;
			}
			OutAppend.Add(MoveTemp(CodeSeg));
			Pos = EndTick + FenceLen;
		}
	}

	static void ExpandTextSegmentsInlineCode(TArray<FInlineSegment>& Segs)
	{
		TArray<FInlineSegment> Expanded;
		for (FInlineSegment& Seg : Segs)
		{
			if (Seg.Kind != ESegmentKind::Text)
			{
				Expanded.Add(MoveTemp(Seg));
				continue;
			}
			SplitInlineCodeSpansFromText(Seg.Text, Expanded);
		}
		Segs = MoveTemp(Expanded);
	}

	static void SplitBoldSpansFromText(const FString& Text, TArray<FInlineSegment>& OutAppend)
	{
		if (Text.IsEmpty())
		{
			return;
		}
		int32 Pos = 0;
		const int32 Len = Text.Len();
		while (Pos < Len)
		{
			const int32 Open = Text.Find(TEXT("**"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			if (Open == INDEX_NONE)
			{
				const FString Tail = Text.Mid(Pos);
				if (!Tail.IsEmpty())
				{
					FInlineSegment Seg;
					Seg.Kind = ESegmentKind::Text;
					Seg.Text = Tail;
					OutAppend.Add(MoveTemp(Seg));
				}
				break;
			}
			if (Open > Pos)
			{
				const FString Pre = Text.Mid(Pos, Open - Pos);
				if (!Pre.IsEmpty())
				{
					FInlineSegment Seg;
					Seg.Kind = ESegmentKind::Text;
					Seg.Text = Pre;
					OutAppend.Add(MoveTemp(Seg));
				}
			}
			const int32 Close = Text.Find(TEXT("**"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Open + 2);
			if (Close == INDEX_NONE)
			{
				const FString Tail = Text.Mid(Open);
				FInlineSegment Seg;
				Seg.Kind = ESegmentKind::Text;
				Seg.Text = Tail;
				OutAppend.Add(MoveTemp(Seg));
				break;
			}
			FInlineSegment BS;
			BS.Kind = ESegmentKind::Bold;
			BS.Text = Text.Mid(Open + 2, Close - (Open + 2));
			OutAppend.Add(MoveTemp(BS));
			Pos = Close + 2;
		}
	}

	static void ExpandTextSegmentsBold(TArray<FInlineSegment>& Segs)
	{
		TArray<FInlineSegment> Expanded;
		for (FInlineSegment& Seg : Segs)
		{
			if (Seg.Kind != ESegmentKind::Text)
			{
				Expanded.Add(MoveTemp(Seg));
				continue;
			}
			SplitBoldSpansFromText(Seg.Text, Expanded);
		}
		Segs = MoveTemp(Expanded);
	}

	static void SplitInlineLinks(const FString& InText, TArray<FInlineSegment>& OutSegments)
	{
		OutSegments.Reset();
		const FString& S = InText;
		int32 Pos = 0;
		while (Pos < S.Len())
		{
			const int32 Lb = S.Find(TEXT("["), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			if (Lb == INDEX_NONE)
			{
				const FString Rest = S.Mid(Pos);
				if (!Rest.IsEmpty())
				{
					FInlineSegment Seg;
					Seg.Kind = ESegmentKind::Text;
					Seg.Text = Rest;
					OutSegments.Add(MoveTemp(Seg));
				}
				break;
			}
			if (Lb > Pos)
			{
				const FString Pre = S.Mid(Pos, Lb - Pos);
				if (!Pre.IsEmpty())
				{
					FInlineSegment Seg;
					Seg.Kind = ESegmentKind::Text;
					Seg.Text = Pre;
					OutSegments.Add(MoveTemp(Seg));
				}
			}

			const int32 Rb = S.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Lb + 1);
			if (Rb == INDEX_NONE)
			{
				const FString Tail = S.Mid(Lb);
				FInlineSegment Seg;
				Seg.Kind = ESegmentKind::Text;
				Seg.Text = Tail;
				OutSegments.Add(MoveTemp(Seg));
				break;
			}

			const int32 Lp = S.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromStart, Rb + 1);
			if (Lp == INDEX_NONE)
			{
				const FString Tail = S.Mid(Lb);
				FInlineSegment Seg;
				Seg.Kind = ESegmentKind::Text;
				Seg.Text = Tail;
				OutSegments.Add(MoveTemp(Seg));
				break;
			}

			const int32 Rp = S.Find(TEXT(")"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Lp + 1);
			if (Rp == INDEX_NONE)
			{
				const FString Tail = S.Mid(Lb);
				FInlineSegment Seg;
				Seg.Kind = ESegmentKind::Text;
				Seg.Text = Tail;
				OutSegments.Add(MoveTemp(Seg));
				break;
			}

			FString Label = S.Mid(Lb + 1, Rb - (Lb + 1)).TrimStartAndEnd();
			const FString Target = S.Mid(Lp + 1, Rp - (Lp + 1)).TrimStartAndEnd();

			FInlineSegment Seg;
			Seg.Kind = ESegmentKind::Link;
			Seg.Text = Label;
			Seg.Target = Target;
			if (Seg.Target.IsEmpty())
			{
				Seg.Kind = ESegmentKind::Text;
				Seg.Text = S.Mid(Lb, Rp - Lb + 1);
				Seg.Target.Reset();
			}
			else if (Seg.Text.IsEmpty())
			{
				Seg.Text = Seg.Target;
			}
			OutSegments.Add(MoveTemp(Seg));
			Pos = Rp + 1;
		}
		ExpandTextSegmentsInlineCode(OutSegments);
		ExpandTextSegmentsBold(OutSegments);
		ExpandTextSegmentsBareUrls(OutSegments);
	}

	static const FSlateBrush* BrushForChatLinkTarget(const FString& TargetIn)
	{
		FString T = TargetIn;
		T.TrimStartAndEndInline();
		if (T.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
			|| T.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
		{
			return FAppStyle::GetBrush(TEXT("Icons.Import"));
		}
		if (T.StartsWith(TEXT("mailto:"), ESearchCase::IgnoreCase))
		{
			return FAppStyle::GetBrush(TEXT("Icons.Plus"));
		}
#if WITH_EDITOR
		if (!T.StartsWith(TEXT("/")) && T.StartsWith(TEXT("Game/")))
		{
			T = FString(TEXT("/")) + T;
		}
		if (T.Contains(TEXT(":")) && GEditor)
		{
			if (AActor* Act = Cast<AActor>(StaticFindObject(AActor::StaticClass(), nullptr, *T, EFindObjectFlags::None)))
			{
				return FClassIconFinder::FindIconForActor(Act);
			}
		}
		if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
		{
			FAssetRegistryModule& ARM = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			const FAssetData AD = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(T));
			if (AD.IsValid())
			{
				bool bIsClassType = false;
				if (const UClass* IconCls = FClassIconFinder::GetIconClassForAssetData(AD, &bIsClassType))
				{
					return FClassIconFinder::FindThumbnailForClass(IconCls);
				}
			}
		}
		if (UObject* Obj = LoadObject<UObject>(nullptr, *T))
		{
			return FClassIconFinder::FindThumbnailForClass(Obj->GetClass());
		}
#endif
		return FAppStyle::GetBrush(TEXT("Icons.Document"));
	}

	TSharedRef<SWidget> MakeChatHyperlinkRow(const FString& Label, const FString& Target)
	{
		const FSlateBrush* IconBrush = BrushForChatLinkTarget(Target);
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		if (IconBrush)
		{
			Row->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SBox)
						.WidthOverride(14.f)
						.HeightOverride(14.f)
						[
							SNew(SImage)
								.Image(IconBrush)
						]
				];
		}
		Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SHyperlink)
					.Style(FAppStyle::Get(), TEXT("CommonHyperlink"))
					.Text(FText::FromString(UnrealAiStripInlineMarkdown(Label)))
					.OnNavigate_Lambda([Target]()
					{
						UnrealAiEditorNavigation::NavigateFromChatMarkdownTarget(Target);
					})
			];
		return Row;
	}

	TSharedRef<SWidget> MakeLinkAwareBodyText(const FString& LineText, const FSlateFontInfo& Font, const FSlateColor& Color)
	{
		TArray<FInlineSegment> Segs;
		SplitInlineLinks(LineText, Segs);

		TSharedRef<SWrapBox> Wrap = SNew(SWrapBox)
			.UseAllottedWidth(true);
		for (const FInlineSegment& Seg : Segs)
		{
			if (Seg.Kind == ESegmentKind::Link && !Seg.Target.IsEmpty())
			{
				const FString Display = Seg.Text.IsEmpty() ? Seg.Target : Seg.Text;
				Wrap->AddSlot().Padding(FMargin(0.f, 0.f, 2.f, 0.f))[MakeChatHyperlinkRow(Display, Seg.Target)];
				continue;
			}
			if (Seg.Kind == ESegmentKind::Code)
			{
				if (!Seg.Target.IsEmpty())
				{
					Wrap->AddSlot().Padding(FMargin(0.f, 0.f, 2.f, 0.f))
					[
						MakeChatHyperlinkRow(Seg.Text, Seg.Target)
					];
				}
				else
				{
					Wrap->AddSlot()
					[
						SNew(STextBlock)
							.Font(FUnrealAiEditorStyle::FontMono9())
							.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextPrimary())
							.AutoWrapText(true)
							.Text(FText::FromString(Seg.Text))
					];
				}
				continue;
			}
			if (Seg.Kind == ESegmentKind::Bold)
			{
				Wrap->AddSlot().Padding(FMargin(0.f, 0.f, 2.f, 0.f))
				[
					SNew(STextBlock)
						.Font(FUnrealAiEditorStyle::FontBold(11))
						.ColorAndOpacity(Color)
						.AutoWrapText(true)
						.Text(FText::FromString(UnrealAiStripInlineMarkdown(Seg.Text)))
				];
				continue;
			}

			Wrap->AddSlot()
				[
					SNew(STextBlock)
						.Font(Font)
						.ColorAndOpacity(Color)
						.AutoWrapText(true)
						.Text(FText::FromString(UnrealAiStripInlineMarkdown(Seg.Text)))
				];
		}
		return Wrap;
	}
} // namespace UnrealAiChatMarkdownInline

TSharedRef<SWidget> UnrealAiMakeChatHyperlink(const FString& Label, const FString& Target)
{
	const FString L = Label.IsEmpty() ? Target : Label;
	return UnrealAiChatMarkdownInline::MakeChatHyperlinkRow(L, Target);
}

namespace UnrealAiChatMarkdown
{
	enum class ELineKind : uint8
	{
		Blank,
		H1,
		H2,
		H3,
		Bullet,
		OrderedBullet,
		TodoOpen,
		TodoDone,
		Plain,
	};

	struct FLine
	{
		ELineKind Kind = ELineKind::Plain;
		FString Text;
		int32 OrderedIndex = 0;
	};

	static bool TryStripLeadingOrderedListIndex(FString& Line, int32& OutIndex)
	{
		OutIndex = 0;
		const int32 Len = Line.Len();
		int32 NumEnd = 0;
		while (NumEnd < Len && FChar::IsDigit(Line[NumEnd]))
		{
			++NumEnd;
		}
		if (NumEnd == 0)
		{
			return false;
		}
		if (NumEnd >= Len || Line[NumEnd] != TEXT('.'))
		{
			return false;
		}
		int32 AfterDot = NumEnd + 1;
		while (AfterDot < Len && FChar::IsWhitespace(Line[AfterDot]))
		{
			++AfterDot;
		}
		OutIndex = FCString::Atoi(*Line.Left(NumEnd));
		Line = Line.Mid(AfterDot);
		return true;
	}

	static bool ClassifyGitHubTodoLine(const FString& Trimmed, FLine& Out)
	{
		FString W = Trimmed;
		int32 Ord = 0;
		if (TryStripLeadingOrderedListIndex(W, Ord))
		{
			if (W.StartsWith(TEXT("[ ]"), ESearchCase::CaseSensitive))
			{
				Out.Kind = ELineKind::TodoOpen;
				Out.Text = W.Mid(3).TrimStart();
				return true;
			}
			if (W.StartsWith(TEXT("[x]"), ESearchCase::IgnoreCase))
			{
				Out.Kind = ELineKind::TodoDone;
				Out.Text = W.Mid(3).TrimStart();
				return true;
			}
		}

		auto TryPrefix = [&](const TCHAR* Pfx, int32 Len, bool bDone) -> bool
		{
			if (Trimmed.StartsWith(Pfx, ESearchCase::CaseSensitive))
			{
				Out.Kind = bDone ? ELineKind::TodoDone : ELineKind::TodoOpen;
				Out.Text = Trimmed.Mid(Len).TrimStart();
				return true;
			}
			return false;
		};

		if (TryPrefix(TEXT("- [ ] "), 6, false))
		{
			return true;
		}
		if (TryPrefix(TEXT("- [x] "), 6, true))
		{
			return true;
		}
		if (TryPrefix(TEXT("- [X] "), 6, true))
		{
			return true;
		}
		if (TryPrefix(TEXT("* [ ] "), 6, false))
		{
			return true;
		}
		if (TryPrefix(TEXT("* [x] "), 6, true))
		{
			return true;
		}
		if (TryPrefix(TEXT("* [X] "), 6, true))
		{
			return true;
		}
		if (TryPrefix(TEXT("+ [ ] "), 6, false))
		{
			return true;
		}
		if (TryPrefix(TEXT("+ [x] "), 6, true))
		{
			return true;
		}
		if (TryPrefix(TEXT("+ [X] "), 6, true))
		{
			return true;
		}

		if (Trimmed == TEXT("- [ ]") || Trimmed == TEXT("* [ ]") || Trimmed == TEXT("+ [ ]"))
		{
			Out.Kind = ELineKind::TodoOpen;
			Out.Text.Reset();
			return true;
		}
		if (Trimmed == TEXT("- [x]") || Trimmed == TEXT("- [X]") || Trimmed == TEXT("* [x]") || Trimmed == TEXT("* [X]")
			|| Trimmed == TEXT("+ [x]") || Trimmed == TEXT("+ [X]"))
		{
			Out.Kind = ELineKind::TodoDone;
			Out.Text.Reset();
			return true;
		}
		return false;
	}

	static FString TrimmedLine(const FString& Line)
	{
		return Line.TrimStartAndEnd();
	}

	static void ClassifyLine(const FString& Trimmed, FLine& Out)
	{
		if (Trimmed.IsEmpty())
		{
			Out.Kind = ELineKind::Blank;
			return;
		}
		if (Trimmed.StartsWith(TEXT("# ")))
		{
			Out.Kind = ELineKind::H1;
			Out.Text = Trimmed.Mid(2);
			return;
		}
		if (Trimmed.StartsWith(TEXT("## ")))
		{
			Out.Kind = ELineKind::H2;
			Out.Text = Trimmed.Mid(3);
			return;
		}
		if (Trimmed.StartsWith(TEXT("### ")))
		{
			Out.Kind = ELineKind::H3;
			Out.Text = Trimmed.Mid(4);
			return;
		}
		if (ClassifyGitHubTodoLine(Trimmed, Out))
		{
			return;
		}
		{
			FString OrdRest = Trimmed;
			int32 Ord = 0;
			if (TryStripLeadingOrderedListIndex(OrdRest, Ord))
			{
				Out.Kind = ELineKind::OrderedBullet;
				Out.OrderedIndex = Ord;
				Out.Text = OrdRest;
				return;
			}
		}
		if (Trimmed.StartsWith(TEXT("- ")))
		{
			Out.Kind = ELineKind::Bullet;
			Out.Text = Trimmed.Mid(2);
			return;
		}
		if (Trimmed.StartsWith(TEXT("* ")))
		{
			Out.Kind = ELineKind::Bullet;
			Out.Text = Trimmed.Mid(2);
			return;
		}
		if (Trimmed.StartsWith(TEXT("+ ")))
		{
			Out.Kind = ELineKind::Bullet;
			Out.Text = Trimmed.Mid(2);
			return;
		}
		Out.Kind = ELineKind::Plain;
		Out.Text = Trimmed;
	}

	static TArray<FLine> SplitLines(const FString& Markdown)
	{
		TArray<FString> RawLines;
		Markdown.ParseIntoArrayLines(RawLines, false);
		TArray<FLine> Out;
		for (const FString& L : RawLines)
		{
			FLine Fl;
			ClassifyLine(TrimmedLine(L), Fl);
			Out.Add(Fl);
		}
		return Out;
	}

	static FSlateFontInfo BodyFont()
	{
		return FUnrealAiEditorStyle::FontBodyRegular11();
	}

	static FSlateFontInfo HeadingFont(int32 Size)
	{
		return FUnrealAiEditorStyle::FontBold(Size);
	}

	static TSharedRef<SWidget> MakeTodoRow(bool bDone, const FString& ItemText)
	{
		const TCHAR* Box = bDone ? TEXT("\x2611") : TEXT("\x2610");
		const FLinearColor Accent = bDone ? FUnrealAiEditorStyle::LinearColorMarkdownTodoDoneCheck()
										  : FUnrealAiEditorStyle::ColorTextMetaHint().GetSpecifiedColor();
		return SNew(SBorder)
			.BorderBackgroundColor(FLinearColor(0.07f, 0.08f, 0.1f, 0.94f))
			.Padding(FMargin(12.f, 7.f, 12.f, 7.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
					  .AutoWidth()
					  .VAlign(VAlign_Top)
					  .Padding(0.f, 0.f, 10.f, 0.f)
				[
					SNew(STextBlock)
						.Font(FUnrealAiEditorStyle::FontSectionHeading())
						.ColorAndOpacity(FSlateColor(Accent))
						.Text(FText::FromString(FString(Box)))
				]
				+ SHorizontalBox::Slot()
					  .FillWidth(1.f)
					  .VAlign(VAlign_Center)
				[
					UnrealAiChatMarkdownInline::MakeLinkAwareBodyText(
						ItemText,
						FUnrealAiEditorStyle::FontBodyRegular11(),
						FUnrealAiEditorStyle::ColorMarkdownBody())
				]
			];
	}

	static TSharedRef<SWidget> MakeBulletRow(const FString& ItemText)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				  .AutoWidth()
				  .VAlign(VAlign_Top)
				  .Padding(0.f, 1.f, 6.f, 0.f)
			[
				SNew(STextBlock)
					.Font(BodyFont())
					.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
					.Text(FText::FromString(TEXT("\x2022")))
			]
			+ SHorizontalBox::Slot()
				  .FillWidth(1.f)
			[
				UnrealAiChatMarkdownInline::MakeLinkAwareBodyText(
					ItemText,
					BodyFont(),
					FUnrealAiEditorStyle::ColorMarkdownBody())
			];
	}

	static TSharedRef<SWidget> MakeOrderedRow(const int32 OrderedN, const FString& ItemText)
	{
		const int32 N = FMath::Max(1, OrderedN);
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
				  .AutoWidth()
				  .VAlign(VAlign_Top)
				  .Padding(0.f, 1.f, 8.f, 0.f)
			[
				SNew(STextBlock)
					.Font(BodyFont())
					.ColorAndOpacity(FUnrealAiEditorStyle::ColorTextMuted())
					.Text(FText::FromString(FString::Printf(TEXT("%d."), N)))
			]
			+ SHorizontalBox::Slot()
				  .FillWidth(1.f)
			[
				UnrealAiChatMarkdownInline::MakeLinkAwareBodyText(
					ItemText,
					BodyFont(),
					FUnrealAiEditorStyle::ColorMarkdownBody())
			];
	}

	struct FMarkdownChunk
	{
		bool bCode = false;
		FString Text;
	};

	static void SplitMarkdownFencedChunks(const FString& Markdown, TArray<FMarkdownChunk>& Out)
	{
		Out.Reset();
		TArray<FString> Lines;
		Markdown.ParseIntoArrayLines(Lines, false);
		bool bInFence = false;
		FString Acc;
		auto FlushProse = [&]()
		{
			FString T = Acc;
			T.TrimStartAndEndInline();
			if (!T.IsEmpty())
			{
				FMarkdownChunk C;
				C.bCode = false;
				C.Text = MoveTemp(T);
				Out.Add(MoveTemp(C));
			}
			Acc.Reset();
		};
		auto FlushCode = [&]()
		{
			FString T = Acc;
			T.TrimStartAndEndInline();
			FMarkdownChunk C;
			C.bCode = true;
			C.Text = MoveTemp(T);
			Out.Add(MoveTemp(C));
			Acc.Reset();
		};
		for (const FString& Line : Lines)
		{
			if (Line.TrimStart().StartsWith(TEXT("```")))
			{
				if (!bInFence)
				{
					FlushProse();
					bInFence = true;
				}
				else
				{
					FlushCode();
					bInFence = false;
				}
				continue;
			}
			if (!Acc.IsEmpty())
			{
				Acc += LINE_TERMINATOR;
			}
			Acc += Line;
		}
		if (bInFence)
		{
			FlushCode();
		}
		else
		{
			FlushProse();
		}
	}

	static void AppendStructuredMarkdownFromProse(
		const TSharedRef<SVerticalBox>& V,
		const FString& ProseMarkdown,
		const bool bToolNoteMergePlainLinesWithSpace)
	{
		if (ProseMarkdown.TrimStartAndEnd().IsEmpty())
		{
			return;
		}
		const TArray<FLine> Lines = SplitLines(ProseMarkdown);
		int32 i = 0;
		while (i < Lines.Num())
		{
			const FLine& L = Lines[i];
			if (L.Kind == ELineKind::Blank)
			{
				++i;
				continue;
			}
			if (L.Kind == ELineKind::Plain)
			{
				FString Para;
				while (i < Lines.Num() && Lines[i].Kind == ELineKind::Plain)
				{
					if (!Para.IsEmpty())
					{
						Para += bToolNoteMergePlainLinesWithSpace ? TEXT(' ') : TEXT('\n');
					}
					Para += Lines[i].Text;
					++i;
				}
				V->AddSlot().AutoHeight().Padding(0.f, 1.f)
					[
						UnrealAiChatMarkdownInline::MakeLinkAwareBodyText(
							Para,
							BodyFont(),
							FUnrealAiEditorStyle::ColorMarkdownBody())
					];
				continue;
			}

			switch (L.Kind)
			{
			case ELineKind::H1:
				V->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
					[
						UnrealAiChatMarkdownInline::MakeLinkAwareBodyText(
							L.Text,
							HeadingFont(14),
							FUnrealAiEditorStyle::ColorMarkdownHeading())
					];
				break;
			case ELineKind::H2:
				V->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 4.f)
					[
						UnrealAiChatMarkdownInline::MakeLinkAwareBodyText(
							L.Text,
							HeadingFont(13),
							FUnrealAiEditorStyle::ColorMarkdownHeading())
					];
				break;
			case ELineKind::H3:
				V->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 2.f)
					[
						UnrealAiChatMarkdownInline::MakeLinkAwareBodyText(
							L.Text,
							HeadingFont(11),
							FUnrealAiEditorStyle::ColorMarkdownHeading())
					];
				break;
			case ELineKind::Bullet:
				V->AddSlot().AutoHeight().Padding(4.f, 1.f, 0.f, 0.f)[MakeBulletRow(L.Text)];
				break;
			case ELineKind::OrderedBullet:
				V->AddSlot().AutoHeight().Padding(4.f, 1.f, 0.f, 0.f)
					[MakeOrderedRow(L.OrderedIndex, L.Text)];
				break;
			case ELineKind::TodoOpen:
				V->AddSlot().AutoHeight().Padding(2.f, 3.f, 2.f, 0.f)[MakeTodoRow(false, L.Text)];
				break;
			case ELineKind::TodoDone:
				V->AddSlot().AutoHeight().Padding(2.f, 3.f, 2.f, 0.f)[MakeTodoRow(true, L.Text)];
				break;
			default:
				V->AddSlot().AutoHeight().Padding(0.f, 1.f)
					[
						UnrealAiChatMarkdownInline::MakeLinkAwareBodyText(
							L.Text,
							BodyFont(),
							FUnrealAiEditorStyle::ColorMarkdownBody())
					];
				break;
			}
			++i;
		}
	}
} // namespace UnrealAiChatMarkdown

TSharedRef<SWidget> UnrealAiBuildMarkdownChatBody(const FString& Markdown)
{
	using namespace UnrealAiChatMarkdown;
	FString Md = Markdown;
	Md.TrimStartAndEndInline();
	if (Md.IsEmpty())
	{
		return SNew(SBox);
	}

	TArray<FMarkdownChunk> Chunks;
	SplitMarkdownFencedChunks(Md, Chunks);
	if (Chunks.Num() == 0)
	{
		return SNew(SBox);
	}

	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	for (const FMarkdownChunk& Ch : Chunks)
	{
		if (Ch.bCode)
		{
			if (!Ch.Text.IsEmpty())
			{
				V->AddSlot()
					.AutoHeight()
					.Padding(0.f, 4.f, 0.f, 4.f)
					[
						SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
							.BorderBackgroundColor(FLinearColor(0.06f, 0.07f, 0.1f, 0.92f))
							.Padding(FMargin(8.f, 6.f))
							[
								SNew(SMultiLineEditableTextBox)
									.IsReadOnly(true)
									.AutoWrapText(true)
									.Font(FUnrealAiEditorStyle::FontMono8())
									.Text(FText::FromString(Ch.Text))
							]
					];
			}
		}
		else
		{
			AppendStructuredMarkdownFromProse(V, Ch.Text, false);
		}
	}
	return V;
}

TSharedRef<SWidget> UnrealAiBuildMarkdownToolNoteBody(const FString& Markdown)
{
	using namespace UnrealAiChatMarkdown;
	FString Md = Markdown;
	Md.TrimStartAndEndInline();
	if (Md.IsEmpty())
	{
		return SNew(SBox);
	}

	TArray<FMarkdownChunk> Chunks;
	SplitMarkdownFencedChunks(Md, Chunks);
	if (Chunks.Num() == 0)
	{
		return SNew(SBox);
	}

	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	for (const FMarkdownChunk& Ch : Chunks)
	{
		if (Ch.bCode)
		{
			if (!Ch.Text.IsEmpty())
			{
				V->AddSlot()
					.AutoHeight()
					.Padding(0.f, 4.f, 0.f, 4.f)
					[
						SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
							.BorderBackgroundColor(FLinearColor(0.06f, 0.07f, 0.1f, 0.92f))
							.Padding(FMargin(8.f, 6.f))
							[
								SNew(SMultiLineEditableTextBox)
									.IsReadOnly(true)
									.AutoWrapText(true)
									.Font(FUnrealAiEditorStyle::FontMono8())
									.Text(FText::FromString(Ch.Text))
							]
					];
			}
		}
		else
		{
			AppendStructuredMarkdownFromProse(V, Ch.Text, true);
		}
	}
	return V;
}
