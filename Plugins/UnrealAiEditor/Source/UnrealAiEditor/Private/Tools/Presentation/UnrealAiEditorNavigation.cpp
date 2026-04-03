#include "Tools/Presentation/UnrealAiEditorNavigation.h"

#include "ContentBrowserModule.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformProcess.h"
#include "IContentBrowserSingleton.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	static FString NormalizeAssetObjectPath(FString S)
	{
		S.TrimStartAndEndInline();
		if (S.IsEmpty())
		{
			return S;
		}
		if (S.StartsWith(TEXT("\"")) && S.EndsWith(TEXT("\"")) && S.Len() > 1)
		{
			S = S.Mid(1, S.Len() - 2);
			S.TrimStartAndEndInline();
		}
		if (S.Contains(TEXT(":")))
		{
			return S;
		}
		const int32 Slash = S.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (Slash == INDEX_NONE || Slash + 1 >= S.Len())
		{
			return S;
		}
		const FString Leaf = S.Mid(Slash + 1);
		// Convert package path form "/Game/Foo/Bar" to object path form "/Game/Foo/Bar.Bar".
		if (!Leaf.Contains(TEXT(".")))
		{
			S = FString::Printf(TEXT("%s.%s"), *S, *Leaf);
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

	// Surface navigation in the Content Drawer and open the asset editor.
	{
		TArray<UObject*> ToSync;
		ToSync.Add(Obj);
		GEditor->SyncBrowserToObjects(ToSync);
	}
	Sub->OpenEditorForAsset(Obj);
	return true;
}

bool UnrealAiEditorNavigation::NavigateFromChatMarkdownTarget(const FString& TargetIn)
{
	FString Raw = TargetIn;
	Raw.TrimStartAndEndInline();
	if (Raw.StartsWith(TEXT("\"")) && Raw.EndsWith(TEXT("\"")) && Raw.Len() > 1)
	{
		Raw = Raw.Mid(1, Raw.Len() - 2);
		Raw.TrimStartAndEndInline();
	}
	if (Raw.IsEmpty())
	{
		return false;
	}

	if (IsProbablyWebOrMailto(Raw))
	{
		FPlatformProcess::LaunchURL(*Raw, nullptr, nullptr);
		return true;
	}

	if (!GEditor)
	{
		return false;
	}

	FString T = Raw;
	if (T.StartsWith(TEXT("Game/")))
	{
		T = FString(TEXT("/")) + T;
	}

	// Map / subobject paths (e.g. /Game/Maps/Foo.Foo:PersistentLevel.StaticMeshActor_0) — before /Game folder heuristics.
	if (!IsProbablyWebOrMailto(Raw) && (T.Contains(TEXT(":")) || T.StartsWith(TEXT("PersistentLevel."))))
	{
		if (AActor* Act = Cast<AActor>(StaticFindObject(AActor::StaticClass(), nullptr, *T, EFindObjectFlags::None)))
		{
			GEditor->SelectNone(false, true);
			GEditor->SelectActor(Act, true, true);
			return true;
		}
		if (UObject* SubObj = StaticFindObject(UObject::StaticClass(), nullptr, *T, EFindObjectFlags::None))
		{
			OpenAssetEditorPreferDocked(SubObj);
			return true;
		}
		return false;
	}

	const int32 LastSlash = T.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	const FString Tail = (LastSlash != INDEX_NONE) ? T.Mid(LastSlash + 1) : T;
	const bool bTailHasDot = Tail.Contains(TEXT("."));

	if (T.StartsWith(TEXT("/Game/")) || T.StartsWith(TEXT("/Engine/")))
	{
		if (bTailHasDot)
		{
			const FString Normalized = NormalizeAssetObjectPath(T);
			// Full object path: open the asset editor (sync alone felt like a "broken" link).
			if (NavigateToAssetObjectPath(Normalized))
			{
				return true;
			}
		}
		if (FModuleManager::Get().IsModuleLoaded(TEXT("ContentBrowser")))
		{
			FContentBrowserModule& CBM = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
			CBM.Get().SyncBrowserToFolders({T});
			return true;
		}
		return false;
	}

	if (T.StartsWith(TEXT("/Script/")))
	{
		if (UObject* Obj = LoadObject<UObject>(nullptr, *T))
		{
			TArray<UObject*> ToSync;
			ToSync.Add(Obj);
			GEditor->SyncBrowserToObjects(ToSync);
			return true;
		}
		return false;
	}

	// Rare paths: fall back to sync + open (tool-style).
	FString Fallback = NormalizeAssetObjectPath(T);
	return NavigateToAssetObjectPath(Fallback);
}

void UnrealAiEditorNavigation::NavigateFromChatMarkdownTargetFromChatLink(const FString& Target)
{
	FString Quick = Target;
	Quick.TrimStartAndEndInline();
	const bool bUrl = Quick.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
		|| Quick.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase)
		|| Quick.StartsWith(TEXT("mailto:"), ESearchCase::IgnoreCase);
	if (bUrl)
	{
		NavigateFromChatMarkdownTarget(Target);
		return;
	}

	const FString Copy = Target;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Copy](float) -> bool
	{
		UnrealAiEditorNavigation::NavigateFromChatMarkdownTarget(Copy);
		return false;
	}));
}

