#include "Widgets/UnrealAiChatMarkdown.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SHyperlink.h"

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
		return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11);
	}

	static FSlateFontInfo HeadingFont(int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), Size);
	}

	static TSharedRef<STextBlock> MakeBodyText(const FString& S)
	{
		return SNew(STextBlock)
			.Font(BodyFont())
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.93f, 0.95f, 1.f)))
			.Text(FText::FromString(UnrealAiStripInlineMarkdown(S)));
	}

	static TSharedRef<STextBlock> MakeHeading(const FString& S, int32 Size)
	{
		return SNew(STextBlock)
			.Font(HeadingFont(Size))
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor(FLinearColor(0.98f, 0.98f, 1.f, 1.f)))
			.ShadowOffset(FVector2D(0.f, 1.f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.35f))
			.Text(FText::FromString(UnrealAiStripInlineMarkdown(S)));
	}

	static TSharedRef<SWidget> MakeTodoRow(bool bDone, const FString& ItemText)
	{
		const TCHAR* Box = bDone ? TEXT("\x2611") : TEXT("\x2610");
		const FLinearColor Accent = bDone ? FLinearColor(0.4f, 0.82f, 0.52f, 1.f)
										  : FLinearColor(0.65f, 0.7f, 0.78f, 1.f);
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
						.Font(HeadingFont(12))
						.ColorAndOpacity(FSlateColor(Accent))
						.Text(FText::FromString(FString(Box)))
				]
				+ SHorizontalBox::Slot()
					  .FillWidth(1.f)
					  .VAlign(VAlign_Center)
				[
					SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11))
						.AutoWrapText(true)
						.ColorAndOpacity(FSlateColor(FLinearColor(0.94f, 0.95f, 0.97f, 1.f)))
						.Text(FText::FromString(UnrealAiStripInlineMarkdown(ItemText)))
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
					.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.58f, 0.65f, 1.f)))
					.Text(FText::FromString(TEXT("\x2022")))
			]
			+ SHorizontalBox::Slot()
				  .FillWidth(1.f)
			[
				MakeBodyText(ItemText)
			];
	}
} // namespace UnrealAiChatMarkdown

FString UnrealAiStripInlineMarkdown(const FString& Line)
{
	FString O = Line;
	O.ReplaceInline(TEXT("**"), TEXT(""));
	O.ReplaceInline(TEXT("__"), TEXT(""));
	return O;
}

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
			V->AddSlot().AutoHeight().Padding(0.f, 1.f)[MakeBodyText(Para)];
			continue;
		}

		switch (L.Kind)
		{
		case ELineKind::H1:
			V->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)[MakeHeading(L.Text, 14)];
			break;
		case ELineKind::H2:
			V->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 4.f)[MakeHeading(L.Text, 13)];
			break;
		case ELineKind::H3:
			V->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 2.f)[MakeHeading(L.Text, 11)];
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
			V->AddSlot().AutoHeight().Padding(0.f, 1.f)[MakeBodyText(L.Text)];
			break;
		}
		++i;
	}

	return V;
}

namespace
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
		FString ObjectPath;
	};

	static void SplitInlineLinks(const FString& InText, TArray<FInlineSegment>& OutSegments)
	{
		OutSegments.Reset();
		FString S = InText;
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

			const FString Label = S.Mid(Lb + 1, Rb - (Lb + 1)).TrimStartAndEnd();
			const FString Target = S.Mid(Lp + 1, Rp - (Lp + 1)).TrimStartAndEnd();

			FInlineSegment Seg;
			Seg.Kind = ESegmentKind::Link;
			Seg.Text = Label;
			Seg.ObjectPath = Target;
			if (Seg.Text.IsEmpty() || Seg.ObjectPath.IsEmpty())
			{
				Seg.Kind = ESegmentKind::Text;
				Seg.Text = S.Mid(Lb, Rp - Lb + 1);
				Seg.ObjectPath.Reset();
			}
			OutSegments.Add(MoveTemp(Seg));
			Pos = Rp + 1;
		}
	}

	static TSharedRef<SWidget> MakeLinkAwareBodyText(
		const FString& LineText,
		const TFunction<void(const FString& ObjectPath)>& OnLinkClicked,
		const FSlateFontInfo& Font,
		const FSlateColor& Color)
	{
		using namespace UnrealAiChatMarkdown;

		TArray<FInlineSegment> Segs;
		SplitInlineLinks(LineText, Segs);

		TSharedRef<SWrapBox> Wrap = SNew(SWrapBox);
		for (const FInlineSegment& Seg : Segs)
		{
			if (Seg.Kind == ESegmentKind::Link)
			{
				if (!Seg.ObjectPath.IsEmpty() && Seg.ObjectPath.StartsWith(TEXT("/")))
				{
					Wrap->AddSlot()
						.AutoWidth()
						[
							SNew(SHyperlink)
								.Text(FText::FromString(UnrealAiStripInlineMarkdown(Seg.Text)))
								.OnNavigate_Lambda([ObjectPath = Seg.ObjectPath, LinkCb = OnLinkClicked]()
								{
									LinkCb(ObjectPath);
								})
						];
					continue;
				}
			}

			Wrap->AddSlot()
				.AutoWidth()
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
}

TSharedRef<SWidget> UnrealAiBuildMarkdownToolNoteBody(
	const FString& Markdown,
	const TFunctionRef<void(const FString& ObjectPath)>& OnLinkClicked)
{
	using namespace UnrealAiChatMarkdown;
	const TFunction<void(const FString& ObjectPath)> LinkCb = OnLinkClicked;
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

		auto MakeBody = [&](const FString& S, const FSlateFontInfo& Font, const FSlateColor& Color)
		{
			return MakeLinkAwareBodyText(S, LinkCb, Font, Color);
		};

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
			V->AddSlot().AutoHeight().Padding(0.f, 1.f)[MakeBody(Para, BodyFont(), FSlateColor(FLinearColor(0.92f, 0.93f, 0.95f, 1.f)))];
			continue;
		}

		switch (L.Kind)
		{
		case ELineKind::H1:
			V->AddSlot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
				[
					MakeBody(L.Text, HeadingFont(14), FSlateColor(FLinearColor(0.98f, 0.98f, 1.f, 1.f)))
				];
			break;
		case ELineKind::H2:
			V->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 4.f)
				[
					MakeBody(L.Text, HeadingFont(13), FSlateColor(FLinearColor(0.98f, 0.98f, 1.f, 1.f)))
				];
			break;
		case ELineKind::H3:
			V->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 2.f)
				[
					MakeBody(L.Text, HeadingFont(11), FSlateColor(FLinearColor(0.98f, 0.98f, 1.f, 1.f)))
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
						MakeBody(L.Text, BodyFont(), FSlateColor(FLinearColor(0.92f, 0.93f, 0.95f, 1.f)))
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
			V->AddSlot().AutoHeight().Padding(0.f, 1.f)[MakeBody(L.Text, BodyFont(), FSlateColor(FLinearColor(0.92f, 0.93f, 0.95f, 1.f)))];
			break;
		}

		++i;
	}
	return V;
}
