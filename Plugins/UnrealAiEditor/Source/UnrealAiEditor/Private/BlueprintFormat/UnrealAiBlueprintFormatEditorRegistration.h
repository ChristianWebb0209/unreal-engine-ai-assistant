#pragma once

void UnrealAiBlueprintFormatEditorRegistrationStartup();
void UnrealAiBlueprintFormatEditorRegistrationShutdown();

/** Invoked from FUnrealAiEditorModule::RegisterMenus (same tool-menu owner scope). */
void UnrealAiBlueprintFormatEditorExtendBlueprintToolbar();
