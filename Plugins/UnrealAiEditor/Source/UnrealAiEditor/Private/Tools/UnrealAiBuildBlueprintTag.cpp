#include "Tools/UnrealAiBuildBlueprintTag.h"

bool UnrealAiBuildBlueprintTag::TryConsume(const FString& Content, FString& OutInnerSpec, FString& OutVisibleWithoutTags)
{
	OutInnerSpec.Reset();
	OutVisibleWithoutTags = Content;

	static const FString Begin = TEXT("<unreal_ai_build_blueprint>");
	static const FString End = TEXT("</unreal_ai_build_blueprint>");

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
