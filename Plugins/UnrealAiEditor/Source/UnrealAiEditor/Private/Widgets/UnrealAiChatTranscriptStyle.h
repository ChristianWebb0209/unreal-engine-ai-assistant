#pragma once

#include "CoreMinimal.h"

#include "Internationalization/Text.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "Templates/SharedPointer.h"

struct FUnrealAiChatBlock;
class SWidget;
struct FSlateBrush;

/**
 * Agent Chat transcript chrome — how we layer color, icons, and hierarchy.
 *
 * Principles (keep widgets dumb; change tokens here + FUnrealAiEditorStyle):
 * - Numeric colors live only in FUnrealAiEditorStyle (LinearColorChatTranscriptAccent*). Widgets must not
 *   invent FLinearColor for roles; they ask this module or the style for a semantic token.
 * - Each block kind maps to one accent (left bar + label + icon tint). Info / success / error use distinct
 *   accents so scanning the timeline is fast without re-reading body copy.
 * - Icons are engine FAppStyle brushes or the plugin Agent tab vector (assistant); never hardcode content
 *   paths in feature widgets — resolve through GetRoleIconBrush. Names must match StarshipCoreStyle /
 *   Editor StarshipStyle (e.g. there is no Icons.Document or Icons.Clock — use Icons.Comment, Icons.Recent).
 * - Chrome modes: Full = accent bar + role label row + body; AccentBarOnly = bar + body (dense cards);
 *   None = raw body (rare).
 * - Step timing captions (harness) stay muted; a small clock icon reinforces “metadata”, not content.
 */
enum class EUnrealAiChatTranscriptChromeMode : uint8
{
	None,
	AccentBarOnly,
	Full,
};

namespace UnrealAiChatTranscriptStyle
{
	/** Width of the vertical accent stripe (px). */
	constexpr float RoleAccentBarWidth = 3.f;
	/** Role / step icon box (px); matches editor list density. */
	constexpr float RoleIconSize = 14.f;

	const FTextBlockStyle& TranscriptReadOnlyBodyTextStyle();

	TSharedRef<SWidget> MakeStepTimingCaptionRow(const FString& Caption);

	const FSlateBrush* GetRoleIconBrush(const FUnrealAiChatBlock& Block);

	FLinearColor GetRoleAccentLinear(const FUnrealAiChatBlock& Block);

	FText GetRoleLabelText(const FUnrealAiChatBlock& Block);

	TSharedRef<SWidget> WrapTranscriptBlockBody(
		const FUnrealAiChatBlock& Block,
		const TSharedRef<SWidget>& Body,
		EUnrealAiChatTranscriptChromeMode Mode);
} // namespace UnrealAiChatTranscriptStyle
