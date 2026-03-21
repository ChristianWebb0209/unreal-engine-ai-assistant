#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

class FUnrealAiEditorCommands : public TCommands<FUnrealAiEditorCommands>
{
public:
	FUnrealAiEditorCommands();

	virtual void RegisterCommands() override;

	TSharedPtr<FUICommandInfo> OpenChatTab;
	TSharedPtr<FUICommandInfo> OpenSettingsTab;
	TSharedPtr<FUICommandInfo> OpenQuickStartTab;
	TSharedPtr<FUICommandInfo> OpenHelpTab;
};
