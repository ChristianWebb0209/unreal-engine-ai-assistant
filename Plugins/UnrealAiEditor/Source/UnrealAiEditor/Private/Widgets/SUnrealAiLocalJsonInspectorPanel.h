#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SMultiLineEditableText;

/**
 * Read-only inspector for local JSON/text files.
 * - Loads with a size cap (prevents huge dumps from freezing the editor)
 * - Pretty-prints JSON when the content parses; otherwise shows raw text
 * - Provides copy-to-clipboard of the currently shown inspector text
 */
class SUnrealAiLocalJsonInspectorPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUnrealAiLocalJsonInspectorPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Sets inspector contents directly (string is treated as JSON if parsable). */
	void SetInspectorText(const FString& Text);

	/** Loads a local file with a size cap, then pretty-prints if it parses as JSON. */
	void InspectFilePath(const FString& FilePath);

	/** Copies current inspector text to the clipboard. */
	void CopyCurrentToClipboard();

private:
	FString PrettyOrRawJson(const FString& Raw) const;
	FString LoadFileCapped(const FString& Path, bool& bOutTruncated) const;

	TSharedPtr<SMultiLineEditableText> InspectorText;
	FString CurrentInspectorRaw;
};

