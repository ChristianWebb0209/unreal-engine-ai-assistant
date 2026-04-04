#include "Tools/UnrealAiBuildEnvironmentTag.h"

#include "UnrealAiEnvironmentBuilderTargetKind.h"

void UnrealAiBuildEnvironmentTag::ParseAndStripHandoffMetadata(FString& InOutInner, EUnrealAiEnvironmentBuilderTargetKind& OutKind)
{
	OutKind = EUnrealAiEnvironmentBuilderTargetKind::PcgScene;
	FString S = InOutInner;
	S.TrimStartAndEndInline();
	if (S.IsEmpty())
	{
		InOutInner.Reset();
		return;
	}

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
					OutKind = UnrealAiEnvironmentBuilderTargetKind::ParseFromString(Val);
					break;
				}
			}
			InOutInner = Rest;
			return;
		}
	}

	TArray<FString> Lines;
	S.ParseIntoArrayLines(Lines);
	if (Lines.Num() > 0)
	{
		FString L0 = Lines[0].TrimStartAndEnd();
		if (L0.StartsWith(TEXT("target_kind:"), ESearchCase::IgnoreCase))
		{
			const FString Val = L0.Mid(12).TrimStartAndEnd();
			OutKind = UnrealAiEnvironmentBuilderTargetKind::ParseFromString(Val);
			Lines.RemoveAt(0, 1, EAllowShrinking::No);
			InOutInner = FString::Join(Lines, TEXT("\n")).TrimStartAndEnd();
			return;
		}
	}

	InOutInner = S;
}

bool UnrealAiBuildEnvironmentTag::TryConsume(const FString& Content, FString& OutInnerSpec, FString& OutVisibleWithoutTags)
{
	OutInnerSpec.Reset();
	OutVisibleWithoutTags = Content;

	static const FString Begin = TEXT("<unreal_ai_build_environment>");
	static const FString End = TEXT("</unreal_ai_build_environment>");

	const int32 B = Content.Find(Begin, ESearchCase::IgnoreCase);
	const int32 E = Content.Find(End, ESearchCase::IgnoreCase);
	if (B == INDEX_NONE || E == INDEX_NONE || E <= B)
	{
		return false;
	}

	const int32 InnerStart = B + Begin.Len();
	OutInnerSpec = Content.Mid(InnerStart, E - InnerStart).TrimStartAndEnd();
	const FString Before = Content.Left(B);
	const FString After = Content.Mid(E + End.Len());
	OutVisibleWithoutTags = (Before + After).TrimStartAndEnd();
	return true;
}

void UnrealAiBuildEnvironmentTag::StripProtocolMarkersForUi(FString& InOutText)
{
	if (InOutText.IsEmpty())
	{
		return;
	}
	static const TCHAR* const Markers[] = {
		TEXT("<unreal_ai_build_environment>"),
		TEXT("</unreal_ai_build_environment>"),
		TEXT("<unreal_ai_environment_builder_result>"),
		TEXT("</unreal_ai_environment_builder_result>"),
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

bool UnrealAiEnvironmentBuilderResultTag::TryConsume(const FString& Content, FString& OutInnerPayload, FString& OutVisibleWithoutTags)
{
	OutInnerPayload.Reset();
	OutVisibleWithoutTags = Content;

	static const FString Begin = TEXT("<unreal_ai_environment_builder_result>");
	static const FString End = TEXT("</unreal_ai_environment_builder_result>");

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
