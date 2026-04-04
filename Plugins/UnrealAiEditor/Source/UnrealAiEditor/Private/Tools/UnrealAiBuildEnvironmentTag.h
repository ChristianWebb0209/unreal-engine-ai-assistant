#pragma once

#include "CoreMinimal.h"
#include "UnrealAiEnvironmentBuilderTargetKind.h"

namespace UnrealAiBuildEnvironmentTag
{
	bool TryConsume(const FString& Content, FString& OutInnerSpec, FString& OutVisibleWithoutTags);

	void ParseAndStripHandoffMetadata(FString& InOutInner, EUnrealAiEnvironmentBuilderTargetKind& OutKind);

	void StripProtocolMarkersForUi(FString& InOutText);
}

namespace UnrealAiEnvironmentBuilderResultTag
{
	bool TryConsume(const FString& Content, FString& OutInnerPayload, FString& OutVisibleWithoutTags);
}
