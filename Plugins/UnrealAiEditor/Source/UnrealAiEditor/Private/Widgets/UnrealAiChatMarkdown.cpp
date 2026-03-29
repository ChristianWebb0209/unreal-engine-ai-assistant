#include "Widgets/UnrealAiChatMarkdown.h"

#include "Tools/Presentation/UnrealAiEditorNavigation.h"
#include "Style/UnrealAiEditorStyle.h"

#include "Misc/Char.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SHyperlink.h"
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
	return O;
}

namespace UnrealAiChatMarkdownInline
{
	enum class ESegmentKind : uint8
	{
		Text,
		Link,
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

		TSharedRef<SWrapBox> Wrap = SNew(SWrapBox);
		for (const FInlineSegment& Seg : Segs)
		{
			if (Seg.Kind == ESegmentKind::Link && !Seg.Target.IsEmpty())
			{
				const FString Display = Seg.Text.IsEmpty() ? Seg.Target : Seg.Text;
				Wrap->AddSlot().Padding(FMargin(0.f, 0.f, 2.f, 0.f))[MakeChatHyperlinkRow(Display, Seg.Target)];
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
		TodoOpen,
		TodoDone,
		Plain,
	};

	struct FLine
	{
		ELineKind Kind = ELineKind::Plain;
		FString Text;
	};

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
		if (Trimmed.StartsWith(TEXT("- [ ] ")))
		{
			Out.Kind = ELineKind::TodoOpen;
			Out.Text = Trimmed.Mid(6);
			return;
		}
		if (Trimmed == TEXT("- [ ]"))
		{
			Out.Kind = ELineKind::TodoOpen;
			Out.Text = FString();
			return;
		}
		if (Trimmed.StartsWith(TEXT("- [x] ")) || Trimmed.StartsWith(TEXT("- [X] ")))
		{
			Out.Kind = ELineKind::TodoDone;
			Out.Text = Trimmed.Mid(6);
			return;
		}
		if (Trimmed.StartsWith(TEXT("- [x]")) || Trimmed.StartsWith(TEXT("- [X]")))
		{
			Out.Kind = ELineKind::TodoDone;
			Out.Text = Trimmed.Mid(5).TrimStart();
			return;
		}
		if (Trimmed.StartsWith(TEXT("- ")) && !Trimmed.StartsWith(TEXT("- [")))
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
} // namespace UnrealAiChatMarkdown

static FString StripMarkdownCodeFences(FString S)
{
	S.TrimStartAndEndInline();
	if (!S.StartsWith(TEXT("```")))
	{
		return S;
	}
	int32 BodyStart = INDEX_NONE;
	for (int32 c = 3; c < S.Len(); ++c)
	{
		if (S[c] == TEXT('\n'))
		{
			BodyStart = c + 1;
			break;
		}
	}
	if (BodyStart == INDEX_NONE)
	{
		return FString();
	}
	S = S.Mid(BodyStart);
	const int32 EndFence = S.Find(TEXT("```"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (EndFence != INDEX_NONE)
	{
		S.LeftInline(EndFence);
	}
	S.TrimStartAndEndInline();
	return S;
}

TSharedRef<SWidget> UnrealAiBuildMarkdownChatBody(const FString& Markdown)
{
	using namespace UnrealAiChatMarkdown;
	const FString Cleaned = StripMarkdownCodeFences(Markdown);
	if (Cleaned.IsEmpty())
	{
		return SNew(SBox);
	}

	const TArray<FLine> Lines = SplitLines(Cleaned);
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
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
					Para += TEXT('\n');
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

	return V;
}

TSharedRef<SWidget> UnrealAiBuildMarkdownToolNoteBody(const FString& Markdown)
{
	using namespace UnrealAiChatMarkdown;
	const FString Cleaned = StripMarkdownCodeFences(Markdown);
	if (Cleaned.IsEmpty())
	{
		return SNew(SBox);
	}

	auto MakeBody = [&](const FString& S, const FSlateFontInfo& Font, const FSlateColor& Color)
	{
		return UnrealAiChatMarkdownInline::MakeLinkAwareBodyText(S, Font, Color);
	};

	const TArray<FLine> Lines = SplitLines(Cleaned);
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
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
					Para += TEXT(" ");
				}
				Para += Lines[i].Text;
				++i;
			}
			V->AddSlot().AutoHeight().Padding(0.f, 1.f)[MakeBody(Para, BodyFont(), FUnrealAiEditorStyle::ColorMarkdownBody())];
			continue;
		}

		switch (L.Kind)
		{
		case ELineKind::H1:
			V->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
				[
					MakeBody(L.Text, HeadingFont(14), FUnrealAiEditorStyle::ColorMarkdownHeading())
				];
			break;
		case ELineKind::H2:
			V->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 4.f)
				[
					MakeBody(L.Text, HeadingFont(13), FUnrealAiEditorStyle::ColorMarkdownHeading())
				];
			break;
		case ELineKind::H3:
			V->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 2.f)
				[
					MakeBody(L.Text, HeadingFont(11), FUnrealAiEditorStyle::ColorMarkdownHeading())
				];
			break;
		case ELineKind::Bullet:
			V->AddSlot().AutoHeight().Padding(4.f, 1.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 1.f, 6.f, 0.f)
					[
						SNew(STextBlock)
							.Font(BodyFont())
							.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.58f, 0.65f, 1.f)))
							.Text(FText::FromString(TEXT("\x2022")))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f)
					[
						MakeBody(L.Text, BodyFont(), FUnrealAiEditorStyle::ColorMarkdownBody())
					]
				];
			break;
		case ELineKind::TodoOpen:
			V->AddSlot().AutoHeight().Padding(2.f, 3.f, 2.f, 0.f)[MakeTodoRow(false, L.Text)];
			break;
		case ELineKind::TodoDone:
			V->AddSlot().AutoHeight().Padding(2.f, 3.f, 2.f, 0.f)[MakeTodoRow(true, L.Text)];
			break;
		default:
			V->AddSlot().AutoHeight().Padding(0.f, 1.f)[MakeBody(L.Text, BodyFont(), FUnrealAiEditorStyle::ColorMarkdownBody())];
			break;
		}

		++i;
	}
	return V;
}
