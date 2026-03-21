#pragma once

#include "Context/AgentContextTypes.h"

class IAgentContextService;

namespace UnrealAiEditorContextQueries
{
	inline constexpr int32 MaxContentBrowserSelectedAssets = 24;
	inline constexpr int32 MaxOpenEditorAssets = 12;

	void PopulateContentBrowserAndSelection(FEditorContextSnapshot& Snap);
	void PopulateOpenEditorAssets(FEditorContextSnapshot& Snap);

	/** Adds up to MaxContentBrowserSelectedAssets selected assets as attachments (active context session). */
	void AddContentBrowserSelectionAsAttachments(IAgentContextService* Ctx);
}
