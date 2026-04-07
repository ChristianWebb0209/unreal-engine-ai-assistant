#include "Widgets/UnrealAiChatMarkdown.h"

#include "Tools/Presentation/UnrealAiEditorNavigation.h"
#include "Style/UnrealAiEditorStyle.h"

#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Misc/Char.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Input/CursorReply.h"
#include "Input/Events.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/ISlateEditableTextWidget.h"
#include "Framework/Text/SlateTextRun.h"
#include "Framework/Text/TextLayout.h"

/** Updates shared prose wrap width from allotted geometry so every frame's layout sees the current column width (narrow and wide). */
class SChatMarkdownProseWidthShim final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChatMarkdownProseWidthShim) {}
		/** Must be set; use TSharedPtr so default FArguments() does not invoke forbidden TSharedRef(). */
		SLATE_ARGUMENT(TSharedPtr<float>, ProseWrapWidthPx)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ProseWrapWidthPx = InArgs._ProseWrapWidthPx;
		check(ProseWrapWidthPx.IsValid());
		ChildSlot.HAlign(HAlign_Fill)
		[
			InArgs._Content.Widget
		];
		SetCanTick(true);
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
		const float W = AllottedGeometry.GetLocalSize().X;
		if (W <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		const float Clamped = FMath::Max(8.f, W);
		if (!FMath::IsNearlyEqual(*ProseWrapWidthPx, Clamped, 0.25f))
		{
			*ProseWrapWidthPx = Clamped;
			Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Prepass);
		}
	}

private:
	TSharedPtr<float> ProseWrapWidthPx;
};

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

	struct FChatLinkSpan
	{
		int32 Begin = 0;
		int32 End = 0;
		FString Target;
	};

	/**
	 * Single read-only multiline editor for chat prose: links/code/bold are Slate text runs in one layout so selection
	 * spans lines and adjacent text (FSlateHyperlinkRun is not used — it embeds SRichTextHyperlink widgets per link).
	 */
	class SChatMarkdownProseText final : public SMultiLineEditableText
	{
	public:
		SLATE_BEGIN_ARGS(SChatMarkdownProseText) {}
			SLATE_ARGUMENT(TArray<FInlineSegment>, Segments)
			SLATE_ARGUMENT(FSlateFontInfo, BodyFont)
			SLATE_ARGUMENT(FSlateColor, BodyColor)
			SLATE_ARGUMENT(TSharedPtr<float>, ProseWrapWidthPx)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			TArray<FInlineSegment> Segments = InArgs._Segments;
			const FSlateFontInfo BodyFont = InArgs._BodyFont;
			const FSlateColor BodyColor = InArgs._BodyColor;
			ProseWrapWidthPxMember = InArgs._ProseWrapWidthPx;
			check(ProseWrapWidthPxMember.IsValid());

			FTextBlockStyle BaseStyle =
				FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("NormalText"));
			BaseStyle.SetFont(BodyFont);
			BaseStyle.SetColorAndOpacity(BodyColor);

			FTextBlockStyle LinkRunStyle =
				FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>(TEXT("NormalUnderlinedText"));
			LinkRunStyle.SetFont(BodyFont);
			LinkRunStyle.SetColorAndOpacity(
				FSlateColor(FLinearColor(0.25f, 0.52f, 0.96f, 1.f)));

			FTextBlockStyle BoldStyle = BaseStyle;
			BoldStyle.SetFont(FUnrealAiEditorStyle::FontBold(
				FMath::Max(1, static_cast<int32>(BodyFont.Size))));

			FTextBlockStyle CodeStyle = BaseStyle;
			CodeStyle.SetFont(FUnrealAiEditorStyle::FontMono9());
			CodeStyle.SetColorAndOpacity(FUnrealAiEditorStyle::ColorTextPrimary());

			FTextBlockStyle MonoLinkRunStyle = LinkRunStyle;
			MonoLinkRunStyle.SetFont(FUnrealAiEditorStyle::FontMono9());

			SMultiLineEditableText::Construct(SMultiLineEditableText::FArguments()
												  .Text(FText::GetEmpty())
												  .TextStyle(&BaseStyle)
												  .IsReadOnly(false)
												  .AutoWrapText(true)
												  .WrapTextAt(TAttribute<float>::CreateLambda([this]()
												  {
													  return FMath::Max(8.f, *ProseWrapWidthPxMember);
												  }))
												  .WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
												  .AllowContextMenu(true)
												  .SelectAllTextWhenFocused(false)
												  .ContextMenuExtender(
													  FMenuExtensionDelegate::CreateSP(
														  this,
														  &SChatMarkdownProseText::ExtendContextMenu)));

			LinkSpans.Reset();
			PopulateFromSegments(
				Segments,
				BaseStyle,
				LinkRunStyle,
				BoldStyle,
				CodeStyle,
				MonoLinkRunStyle);
			SetIsReadOnly(true);
		}

		virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
			{
				bLinkLeftDown = true;
				LinkPressScreenPos = MouseEvent.GetScreenSpacePosition();
			}
			return SMultiLineEditableText::OnMouseButtonDown(MyGeometry, MouseEvent);
		}

		virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			const bool bLeft = MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton;
			FReply R = SMultiLineEditableText::OnMouseButtonUp(MyGeometry, MouseEvent);
			if (bLeft && bLinkLeftDown)
			{
				bLinkLeftDown = false;
				const FVector2D Delta = MouseEvent.GetScreenSpacePosition() - LinkPressScreenPos;
				if (Delta.SizeSquared() <= 64.f && !AnyTextSelected())
				{
					FString Target;
					if (ResolveLinkTargetAtLinearIndex(TextLocationToLinear(GetCursorLocation()), Target))
					{
						UnrealAiEditorNavigation::NavigateFromChatMarkdownTargetFromChatLink(Target);
						return FReply::Handled();
					}
				}
			}
			else if (bLeft)
			{
				bLinkLeftDown = false;
			}
			return R;
		}

		virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
		{
			bLinkLeftDown = false;
			SMultiLineEditableText::OnMouseLeave(MouseEvent);
		}

	private:
		void ExtendContextMenu(FMenuBuilder& MenuBuilder)
		{
			FString Target;
			if (!ResolveLinkTargetForContext(Target))
			{
				return;
			}
			const FString TargetCopy = Target;
			MenuBuilder.AddMenuEntry(
				NSLOCTEXT("UnrealAiEditor", "ChatOpenLink", "Open link"),
				NSLOCTEXT("UnrealAiEditor", "ChatOpenLinkTip", "Go to this URL or project path"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([TargetCopy]()
				{
					UnrealAiEditorNavigation::NavigateFromChatMarkdownTargetFromChatLink(TargetCopy);
				})));
		}

		int32 TextLocationToLinear(const FTextLocation& Loc) const
		{
			const int32 LineIdx = Loc.GetLineIndex();
			if (LineIdx == INDEX_NONE)
			{
				return 0;
			}
			int32 Acc = 0;
			for (int32 i = 0; i < LineIdx; ++i)
			{
				FString Line;
				GetTextLine(i, Line);
				Acc += Line.Len() + 1;
			}
			return Acc + Loc.GetOffset();
		}

		void SelectionToLinearRange(int32& OutLo, int32& OutHi) const
		{
			const FTextSelection Sel = GetSelection();
			const int32 A = TextLocationToLinear(Sel.GetBeginning());
			const int32 B = TextLocationToLinear(Sel.GetEnd());
			OutLo = FMath::Min(A, B);
			OutHi = FMath::Max(A, B);
		}

		bool ResolveLinkTargetAtLinearIndex(const int32 Idx, FString& OutTarget) const
		{
			for (const FChatLinkSpan& S : LinkSpans)
			{
				if (Idx >= S.Begin && Idx < S.End)
				{
					OutTarget = S.Target;
					return true;
				}
			}
			return false;
		}

		bool ResolveLinkTargetForContext(FString& OutTarget) const
		{
			int32 Lo = 0;
			int32 Hi = 0;
			SelectionToLinearRange(Lo, Hi);
			const bool bCollapsed = (Lo == Hi);
			for (const FChatLinkSpan& S : LinkSpans)
			{
				if (bCollapsed)
				{
					if (Lo >= S.Begin && Lo < S.End)
					{
						OutTarget = S.Target;
						return true;
					}
				}
				else if (Hi > S.Begin && Lo < S.End)
				{
					OutTarget = S.Target;
					return true;
				}
			}
			return false;
		}

		void PopulateFromSegments(
			const TArray<FInlineSegment>& Segments,
			const FTextBlockStyle& BaseStyle,
			const FTextBlockStyle& LinkRunStyle,
			const FTextBlockStyle& BoldStyle,
			const FTextBlockStyle& CodeStyle,
			const FTextBlockStyle& MonoLinkRunStyle)
		{
			GoTo(ETextLocation::BeginningOfDocument);
			int32 DocOffset = 0;

			auto AppendSegmentPiece = [&](const FString& Piece, const FTextBlockStyle& Style, const FString* LinkTarget)
			{
				if (Piece.IsEmpty())
				{
					return;
				}
				const int32 SpanBegin = DocOffset;
				const TSharedRef<const FString> PieceText = MakeShareable(new FString(Piece));
				InsertRunAtCursor(FSlateTextRun::Create(FRunInfo(), PieceText, Style));
				DocOffset += Piece.Len();
				if (LinkTarget != nullptr && !LinkTarget->IsEmpty())
				{
					FChatLinkSpan Span;
					Span.Begin = SpanBegin;
					Span.End = DocOffset;
					Span.Target = *LinkTarget;
					LinkSpans.Add(MoveTemp(Span));
				}
			};

			for (const FInlineSegment& Seg : Segments)
			{
				FString Display;
				switch (Seg.Kind)
				{
				case ESegmentKind::Link:
					if (!Seg.Target.IsEmpty())
					{
						Display = UnrealAiStripInlineMarkdown(Seg.Text.IsEmpty() ? Seg.Target : Seg.Text);
					}
					break;
				case ESegmentKind::Text:
					Display = UnrealAiStripInlineMarkdown(Seg.Text);
					break;
				case ESegmentKind::Bold:
					Display = UnrealAiStripInlineMarkdown(Seg.Text);
					break;
				case ESegmentKind::Code:
					Display = Seg.Text;
					break;
				default:
					break;
				}

				TArray<FString> Lines;
				Display.ParseIntoArrayLines(Lines, false);

				for (int32 Li = 0; Li < Lines.Num(); ++Li)
				{
					if (Li > 0)
					{
						InsertTextAtCursor(TEXT("\n"));
						++DocOffset;
					}
					const FString& LineChunk = Lines[Li];

					if (Seg.Kind == ESegmentKind::Link && !Seg.Target.IsEmpty())
					{
						AppendSegmentPiece(LineChunk, LinkRunStyle, &Seg.Target);
					}
					else if (Seg.Kind == ESegmentKind::Code && !Seg.Target.IsEmpty())
					{
						AppendSegmentPiece(LineChunk, MonoLinkRunStyle, &Seg.Target);
					}
					else if (Seg.Kind == ESegmentKind::Code)
					{
						AppendSegmentPiece(LineChunk, CodeStyle, nullptr);
					}
					else if (Seg.Kind == ESegmentKind::Bold)
					{
						AppendSegmentPiece(LineChunk, BoldStyle, nullptr);
					}
					else
					{
						AppendSegmentPiece(LineChunk, BaseStyle, nullptr);
					}
				}
			}
		}

		TArray<FChatLinkSpan> LinkSpans;
		TSharedPtr<float> ProseWrapWidthPxMember;
		bool bLinkLeftDown = false;
		FVector2D LinkPressScreenPos = FVector2D::ZeroVector;
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

	static void SplitBareUnrealObjectPathsFromText(const FString& Text, TArray<FInlineSegment>& OutAppend)
	{
		if (Text.IsEmpty())
		{
			return;
		}
		int32 Pos = 0;
		const int32 Len = Text.Len();
		while (Pos < Len)
		{
			const int32 HitGame = Text.Find(TEXT("/Game/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			const int32 HitEngine = Text.Find(TEXT("/Engine/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			const int32 HitScript = Text.Find(TEXT("/Script/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			int32 Hit = INDEX_NONE;
			if (HitGame >= 0)
			{
				Hit = HitGame;
			}
			if (HitEngine >= 0)
			{
				Hit = Hit == INDEX_NONE ? HitEngine : FMath::Min(Hit, HitEngine);
			}
			if (HitScript >= 0)
			{
				Hit = Hit == INDEX_NONE ? HitScript : FMath::Min(Hit, HitScript);
			}

			if (Hit == INDEX_NONE)
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
			if (Hit > Pos)
			{
				const FString Pre = Text.Mid(Pos, Hit - Pos);
				if (!Pre.IsEmpty())
				{
					FInlineSegment Seg;
					Seg.Kind = ESegmentKind::Text;
					Seg.Text = Pre;
					OutAppend.Add(MoveTemp(Seg));
				}
			}

			int32 PathEnd = Hit + 1;
			while (PathEnd < Len)
			{
				const TCHAR Ch = Text[PathEnd];
				if (FChar::IsWhitespace(Ch) || Ch == TEXT(')') || Ch == TEXT(']') || Ch == TEXT('>')
					|| Ch == TEXT('"') || Ch == TEXT('\'') || Ch == TEXT('`') || Ch == TEXT(','))
				{
					break;
				}
				++PathEnd;
			}
			while (PathEnd > Hit
				&& (Text[PathEnd - 1] == TEXT(';') || Text[PathEnd - 1] == TEXT('!')))
			{
				--PathEnd;
			}
			const FString PathToken = Text.Mid(Hit, PathEnd - Hit).TrimStartAndEnd();
			if (!PathToken.IsEmpty())
			{
				FInlineSegment Seg;
				Seg.Kind = ESegmentKind::Link;
				Seg.Text = PathToken;
				Seg.Target = PathToken;
				OutAppend.Add(MoveTemp(Seg));
			}
			Pos = FMath::Max(PathEnd, Hit + 1);
		}
	}

	static void ExpandTextSegmentsBareUnrealPaths(TArray<FInlineSegment>& Segs)
	{
		TArray<FInlineSegment> Expanded;
		for (FInlineSegment& Seg : Segs)
		{
			if (Seg.Kind != ESegmentKind::Text)
			{
				Expanded.Add(MoveTemp(Seg));
				continue;
			}
			SplitBareUnrealObjectPathsFromText(Seg.Text, Expanded);
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
		ExpandTextSegmentsBareUnrealPaths(OutSegments);
	}

	TSharedRef<SWidget> MakeChatHyperlinkRow(
		const FString& Label,
		const FString& Target,
		const FSlateFontInfo& LinkFont,
		const TSharedRef<float>& ProseWrapWidthPx)
	{
		TArray<FInlineSegment> Segs;
		FInlineSegment L;
		L.Kind = ESegmentKind::Link;
		L.Text = UnrealAiStripInlineMarkdown(Label);
		L.Target = Target;
		Segs.Add(MoveTemp(L));
		return SNew(SBox)
			.HAlign(HAlign_Fill)
			[
				SNew(SChatMarkdownProseText)
					.Segments(MoveTemp(Segs))
					.BodyFont(LinkFont)
					.BodyColor(FUnrealAiEditorStyle::ColorMarkdownBody())
					.ProseWrapWidthPx(ProseWrapWidthPx.ToSharedPtr())
			];
	}

	TSharedRef<SWidget> MakeLinkAwareBodyText(
		const FString& LineText,
		const FSlateFontInfo& Font,
		const FSlateColor& Color,
		const TSharedRef<float>& ProseWrapWidthPx)
	{
		TArray<FInlineSegment> Segs;
		SplitInlineLinks(LineText, Segs);
		return SNew(SBox)
			.HAlign(HAlign_Fill)
			[
				SNew(SChatMarkdownProseText)
					.Segments(MoveTemp(Segs))
					.BodyFont(Font)
					.BodyColor(Color)
					.ProseWrapWidthPx(ProseWrapWidthPx.ToSharedPtr())
			];
	}
} // namespace UnrealAiChatMarkdownInline

TSharedRef<SWidget> UnrealAiMakeChatHyperlink(const FString& Label, const FString& Target)
{
	const FString L = Label.IsEmpty() ? Target : Label;
	const TSharedRef<float> StandaloneWrapPx = MakeShared<float>(16000.f);
	return UnrealAiChatMarkdownInline::MakeChatHyperlinkRow(
		L,
		Target,
		FUnrealAiEditorStyle::FontBodyRegular11(),
		StandaloneWrapPx);
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

	static void AppendProseParagraphBreak(FString& Doc)
	{
		if (!Doc.IsEmpty())
		{
			Doc += LINE_TERMINATOR;
			Doc += LINE_TERMINATOR;
		}
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
		const bool bToolNoteMergePlainLinesWithSpace,
		const TSharedRef<float>& ProseWrapWidthPx)
	{
		if (ProseMarkdown.TrimStartAndEnd().IsEmpty())
		{
			return;
		}
		const TArray<FLine> Lines = SplitLines(ProseMarkdown);
		FString Doc;
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
				AppendProseParagraphBreak(Doc);
				Doc += Para;
				continue;
			}
			if (L.Kind == ELineKind::Bullet)
			{
				FString Block;
				while (i < Lines.Num() && Lines[i].Kind == ELineKind::Bullet)
				{
					if (!Block.IsEmpty())
					{
						Block += LINE_TERMINATOR;
					}
					Block += FString(TEXT("  \x2022 "));
					Block += Lines[i].Text;
					++i;
				}
				AppendProseParagraphBreak(Doc);
				Doc += Block;
				continue;
			}
			if (L.Kind == ELineKind::OrderedBullet)
			{
				FString Block;
				while (i < Lines.Num() && Lines[i].Kind == ELineKind::OrderedBullet)
				{
					if (!Block.IsEmpty())
					{
						Block += LINE_TERMINATOR;
					}
					const int32 N = FMath::Max(1, Lines[i].OrderedIndex);
					Block += FString::Printf(TEXT("  %d. "), N);
					Block += Lines[i].Text;
					++i;
				}
				AppendProseParagraphBreak(Doc);
				Doc += Block;
				continue;
			}

			AppendProseParagraphBreak(Doc);
			switch (L.Kind)
			{
			case ELineKind::H1:
			case ELineKind::H2:
			case ELineKind::H3:
				Doc += TEXT("**");
				Doc += L.Text;
				Doc += TEXT("**");
				break;
			case ELineKind::TodoOpen:
				Doc += FString(TEXT("\x2610 "));
				Doc += L.Text;
				break;
			case ELineKind::TodoDone:
				Doc += FString(TEXT("\x2611 "));
				Doc += L.Text;
				break;
			default:
				Doc += L.Text;
				break;
			}
			++i;
		}

		Doc.TrimStartAndEndInline();
		if (Doc.IsEmpty())
		{
			return;
		}

		// Single multiline widget: links are FSlateTextRun (not embedded SHyperlink rows) and selection spans lists/paragraphs.
		V->AddSlot().AutoHeight().Padding(0.f, 1.f)
			[
				UnrealAiChatMarkdownInline::MakeLinkAwareBodyText(
					Doc,
					BodyFont(),
					FUnrealAiEditorStyle::ColorMarkdownBody(),
					ProseWrapWidthPx)
			];
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

	// Start with a conservative width so first-frame paint remains inside chat bubble
	// before the width shim updates to the real allotted size.
	const TSharedRef<float> ProseWrapWidthPx = MakeShared<float>(360.f);
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	const TSharedRef<float> Wpx = ProseWrapWidthPx;
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
									.WrapTextAt(TAttribute<float>::CreateLambda([Wpx]() { return FMath::Max(8.f, *Wpx); }))
									.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
									.Font(FUnrealAiEditorStyle::FontMono8())
									.Text(FText::FromString(Ch.Text))
							]
					];
			}
		}
		else
		{
			AppendStructuredMarkdownFromProse(V, Ch.Text, false, ProseWrapWidthPx);
		}
	}
	return SNew(SChatMarkdownProseWidthShim)
		.ProseWrapWidthPx(ProseWrapWidthPx.ToSharedPtr())
		[
			SNew(SBox)
				.HAlign(HAlign_Fill)
				[
					V
				]
		];
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

	// Start with a conservative width so first-frame paint remains inside chat bubble
	// before the width shim updates to the real allotted size.
	const TSharedRef<float> ProseWrapWidthPx = MakeShared<float>(360.f);
	TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
	const TSharedRef<float> Wpx = ProseWrapWidthPx;
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
									.WrapTextAt(TAttribute<float>::CreateLambda([Wpx]() { return FMath::Max(8.f, *Wpx); }))
									.WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)
									.Font(FUnrealAiEditorStyle::FontMono8())
									.Text(FText::FromString(Ch.Text))
							]
					];
			}
		}
		else
		{
			AppendStructuredMarkdownFromProse(V, Ch.Text, true, ProseWrapWidthPx);
		}
	}
	return SNew(SChatMarkdownProseWidthShim)
		.ProseWrapWidthPx(ProseWrapWidthPx.ToSharedPtr())
		[
			SNew(SBox)
				.HAlign(HAlign_Fill)
				[
					V
				]
		];
}
