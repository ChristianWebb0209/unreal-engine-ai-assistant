#include "Widgets/UnrealAiChatMarkdown.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

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
