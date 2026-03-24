#include "Tools/Presentation/UnrealAiEditorNavigation.h"

#include "Editor.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformProcess.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	static FString NormalizePotentialObjectPath(FString S)
	{
		S.TrimStartAndEndInline();
		if (S.StartsWith(TEXT("Game/")))
		{
			S = FString(TEXT("/")) + S;
		}
		return S;
	}

	static bool IsProbablyWebOrMailto(const FString& S)
	{
		return S.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
			|| S.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase)
			|| S.StartsWith(TEXT("mailto:"), ESearchCase::IgnoreCase);
	}

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
		return ObjectPath.StartsWith(TEXT("/Game/")) || ObjectPath.StartsWith(TEXT("/Script/"))
			|| ObjectPath.StartsWith(TEXT("/Engine/"));
	}
}

void UnrealAiEditorNavigation::OpenAssetEditorPreferDocked(UObject* Asset)
{
	if (!Asset || !GEditor)
	{
		return;
	}
	UAssetEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!Sub)
	{
		return;
	}
	// Do not use EToolkitMode::WorldCentric here: WorkflowCentricApplication asserts
	// TabManager.IsValid() even when FLevelEditorModule::GetLevelEditorTabManager() is non-null
	// (e.g. Mass/editor stacks, startup timing). Standalone asset editors are reliable for tool calls.
	Sub->OpenEditorForAsset(Asset);
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

bool UnrealAiEditorNavigation::NavigateFromChatMarkdownTarget(const FString& TargetIn)
{
	FString Target = NormalizePotentialObjectPath(TargetIn);
	if (Target.IsEmpty())
	{
		return false;
	}

	if (IsProbablyWebOrMailto(Target))
	{
		FPlatformProcess::LaunchURL(*Target, nullptr, nullptr);
		return true;
	}

	if (NavigateToAssetObjectPath(Target))
	{
		return true;
	}

	if (!GEditor)
	{
		return false;
	}

	// Subobject / actor paths (e.g. /Game/Maps/Foo.Foo:PersistentLevel.StaticMeshActor_0)
	if (Target.Contains(TEXT(":")))
	{
		if (AActor* Act = Cast<AActor>(StaticFindObject(AActor::StaticClass(), nullptr, *Target, EFindObjectFlags::None)))
		{
			GEditor->SelectNone(false, true);
			GEditor->SelectActor(Act, true, true);
			return true;
		}
		if (UObject* SubObj = StaticFindObject(UObject::StaticClass(), nullptr, *Target, EFindObjectFlags::None))
		{
			OpenAssetEditorPreferDocked(SubObj);
			return true;
		}
	}

	return false;
}

