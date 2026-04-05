#include "Tools/UnrealAiBuildBlueprintTag.h"
#include "UnrealAiBlueprintBuilderTargetKind.h"

namespace UnrealAiBuildBlueprintTagPriv
{
	static bool IsAllowedMisnamedHandoffWrapperTag(const FString& TagLower)
	{
		if (TagLower.Len() < 1 || TagLower.Len() > 48)
		{
			return false;
		}
		if (!FChar::IsLower(TagLower[0]))
		{
			return false;
		}
		if (TagLower.StartsWith(TEXT("unreal_ai_")))
		{
			return false;
		}
		for (int32 i = 0; i < TagLower.Len(); ++i)
		{
			const TCHAR C = TagLower[i];
			if (!FChar::IsLower(C) && !FChar::IsDigit(C) && C != TEXT('_'))
			{
				return false;
			}
		}
		return true;
	}

	static bool InnerLooksLikeBuilderHandoffSpec(const FString& Inner)
	{
		const FString T = Inner.TrimStartAndEnd();
		if (T.StartsWith(TEXT("---")))
		{
			return true;
		}
		return T.Contains(TEXT("target_kind:"), ESearchCase::IgnoreCase);
	}

	/**
	 * Models sometimes copy the inner YAML/spec but wrap it in a mistaken custom tag instead of <unreal_ai_build_blueprint>.
	 * If the inner payload matches a real handoff body, treat it like <unreal_ai_build_blueprint>.
	 */
	static bool TryConsumeMisnamedWrapper(const FString& Content, FString& OutInnerSpec, FString& OutVisibleWithoutTags)
	{
		const int32 OpenAngle = Content.Find(TEXT("<"), ESearchCase::CaseSensitive);
		if (OpenAngle == INDEX_NONE)
		{
			return false;
		}
		const int32 AfterOpen = OpenAngle + 1;
		const int32 CloseAngle = Content.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, AfterOpen);
		if (CloseAngle == INDEX_NONE || CloseAngle <= AfterOpen)
		{
			return false;
		}
		const FString TagName = Content.Mid(AfterOpen, CloseAngle - AfterOpen).TrimStartAndEnd();
		if (TagName.IsEmpty())
		{
			return false;
		}
		if (TagName.Equals(TEXT("unreal_ai_build_blueprint"), ESearchCase::IgnoreCase))
		{
			return false;
		}
		if (!IsAllowedMisnamedHandoffWrapperTag(TagName.ToLower()))
		{
			return false;
		}
		const FString CloseTag = FString::Printf(TEXT("</%s>"), *TagName);
		const int32 InnerStart = CloseAngle + 1;
		const int32 CloseIdx = Content.Find(CloseTag, ESearchCase::IgnoreCase, ESearchDir::FromStart, InnerStart);
		if (CloseIdx == INDEX_NONE)
		{
			return false;
		}
		const FString Inner = Content.Mid(InnerStart, CloseIdx - InnerStart).TrimStartAndEnd();
		if (!InnerLooksLikeBuilderHandoffSpec(Inner))
		{
			return false;
		}
		OutInnerSpec = Inner;
		const FString Before = Content.Left(OpenAngle);
		const FString After = Content.Mid(CloseIdx + CloseTag.Len());
		OutVisibleWithoutTags = (Before + After).TrimStartAndEnd();
		UE_LOG(
			LogTemp,
			Display,
			TEXT("UnrealAiBuildBlueprintTag: recovered Blueprint Builder handoff from misnamed wrapper <%s>...</>"),
			*TagName);
		return true;
	}
}

void UnrealAiBuildBlueprintTag::ParseAndStripHandoffMetadata(FString& InOutInner, EUnrealAiBlueprintBuilderTargetKind& OutKind)
{
	OutKind = EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint;
	FString S = InOutInner;
	S.TrimStartAndEndInline();
	if (S.IsEmpty())
	{
		InOutInner.Reset();
		return;
	}

	// YAML frontmatter: --- ... ---
	if (S.StartsWith(TEXT("---")))
	{
		const int32 Close = S.Find(TEXT("---"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 3);
		if (Close != INDEX_NONE && Close > 3)
		{
			const FString Front = S.Mid(3, Close - 3).TrimStartAndEnd();
			FString Rest = S.Mid(Close + 3).TrimStartAndEnd();
			TArray<FString> Lines;
			Front.ParseIntoArrayLines(Lines);
			for (const FString& Line : Lines)
			{
				FString L = Line.TrimStartAndEnd();
				if (L.StartsWith(TEXT("target_kind:"), ESearchCase::IgnoreCase))
				{
					const FString Val = L.Mid(12).TrimStartAndEnd();
					OutKind = UnrealAiBlueprintBuilderTargetKind::ParseFromString(Val);
					break;
				}
			}
			InOutInner = Rest;
			return;
		}
	}

	// Single-line header: target_kind: domain
	TArray<FString> Lines;
	S.ParseIntoArrayLines(Lines);
	if (Lines.Num() > 0)
	{
		FString L0 = Lines[0].TrimStartAndEnd();
		if (L0.StartsWith(TEXT("target_kind:"), ESearchCase::IgnoreCase))
		{
			const FString Val = L0.Mid(12).TrimStartAndEnd();
			OutKind = UnrealAiBlueprintBuilderTargetKind::ParseFromString(Val);
			Lines.RemoveAt(0, 1, EAllowShrinking::No);
			InOutInner = FString::Join(Lines, TEXT("\n")).TrimStartAndEnd();
			return;
		}
	}

	InOutInner = S;
}

bool UnrealAiBuildBlueprintTag::TryConsume(const FString& Content, FString& OutInnerSpec, FString& OutVisibleWithoutTags)
{
	OutInnerSpec.Reset();
	OutVisibleWithoutTags = Content;

	static const FString Begin = TEXT("<unreal_ai_build_blueprint>");
	static const FString End = TEXT("</unreal_ai_build_blueprint>");

	const int32 B = Content.Find(Begin, ESearchCase::IgnoreCase);
	const int32 E = Content.Find(End, ESearchCase::IgnoreCase);
	if (B != INDEX_NONE && E != INDEX_NONE && E > B)
	{
		const int32 InnerStart = B + Begin.Len();
		OutInnerSpec = Content.Mid(InnerStart, E - InnerStart).TrimStartAndEnd();
		const FString Before = Content.Left(B);
		const FString After = Content.Mid(E + End.Len());
		OutVisibleWithoutTags = (Before + After).TrimStartAndEnd();
		return true;
	}

	if (UnrealAiBuildBlueprintTagPriv::TryConsumeMisnamedWrapper(Content, OutInnerSpec, OutVisibleWithoutTags))
	{
		return true;
	}

	return false;
}

void UnrealAiBuildBlueprintTag::StripProtocolMarkersForUi(FString& InOutText)
{
	if (InOutText.IsEmpty())
	{
		return;
	}
	static const TCHAR* const Markers[] = {
		TEXT("<unreal_ai_build_blueprint>"),
		TEXT("</unreal_ai_build_blueprint>"),
		TEXT("<unreal_ai_blueprint_builder_result>"),
		TEXT("</unreal_ai_blueprint_builder_result>"),
	};
	for (const TCHAR* const M : Markers)
	{
		for (;;)
		{
			const int32 LenBefore = InOutText.Len();
			InOutText.ReplaceInline(M, TEXT(""), ESearchCase::IgnoreCase);
			if (InOutText.Len() == LenBefore)
			{
				break;
			}
		}
	}
	InOutText.TrimStartAndEndInline();
}

bool UnrealAiBlueprintBuilderResultTag::TryConsume(const FString& Content, FString& OutInnerPayload, FString& OutVisibleWithoutTags)
{
	OutInnerPayload.Reset();
	OutVisibleWithoutTags = Content;

	static const FString Begin = TEXT("<unreal_ai_blueprint_builder_result>");
	static const FString End = TEXT("</unreal_ai_blueprint_builder_result>");

	const int32 B = Content.Find(Begin, ESearchCase::IgnoreCase);
	const int32 E = Content.Find(End, ESearchCase::IgnoreCase);
	if (B == INDEX_NONE || E == INDEX_NONE || E <= B)
	{
		return false;
	}

	const int32 InnerStart = B + Begin.Len();
	OutInnerPayload = Content.Mid(InnerStart, E - InnerStart).TrimStartAndEnd();
	const FString Before = Content.Left(B);
	const FString After = Content.Mid(E + End.Len());
	OutVisibleWithoutTags = (Before + After).TrimStartAndEnd();
	return true;
}
