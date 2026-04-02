#include "Tools/UnrealAiProjectIniSettingsAllowlist.h"

bool UnrealAiIsAllowlistedProjectEngineIniWrite(const FString& Section, const FString& Name)
{
	(void)Section;
	(void)Name;
	// Approve keys explicitly with Canon == Section + ":" + Name when a safe allowlist entry is needed.
	return false;
}
