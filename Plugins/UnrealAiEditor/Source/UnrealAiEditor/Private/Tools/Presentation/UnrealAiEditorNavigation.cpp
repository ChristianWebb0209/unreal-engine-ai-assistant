#include "Tools/Presentation/UnrealAiEditorNavigation.h"

#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	static bool IsProbablyUnrealAssetObjectPath(const FString& ObjectPath)
	{
		if (ObjectPath.IsEmpty())
		{
			return false;
		}
		// Simple safety checks: avoid `..` traversal and require game/asset style root.
		if (ObjectPath.Contains(TEXT("..")))
		{
			return false;
		}
		return ObjectPath.StartsWith(TEXT("/Game/")) || ObjectPath.StartsWith(TEXT("/Script/"));
	}
}

bool UnrealAiEditorNavigation::NavigateToAssetObjectPath(const FString& ObjectPath)
{
	if (!IsProbablyUnrealAssetObjectPath(ObjectPath))
	{
		return false;
	}

	UObject* Obj = LoadObject<UObject>(nullptr, *ObjectPath);
	if (!Obj)
	{
		return false;
	}

	if (!GEditor)
	{
		return false;
	}

	UAssetEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!Sub)
	{
		return false;
	}

	Sub->OpenEditorForAsset(Obj);
	return true;
}

