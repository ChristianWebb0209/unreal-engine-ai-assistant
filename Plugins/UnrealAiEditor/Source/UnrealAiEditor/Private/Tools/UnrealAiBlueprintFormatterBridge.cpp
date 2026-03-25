#include "Tools/UnrealAiBlueprintFormatterBridge.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"

#include <atomic>

#if WITH_EDITOR
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

static const FName GFormatterPluginName(TEXT("UnrealBlueprintFormatter"));
static const FName GFormatterModuleName(TEXT("UnrealBlueprintFormatter"));

namespace
{
	std::atomic<bool> GNotifiedFormatterMissing{false};
}

FString UnrealAiBlueprintFormatterBridge::FormatterInstallHint()
{
	return FString(
		TEXT("Enable the **Unreal Blueprint Formatter** plugin (Edit > Plugins) and ensure `Plugins/UnrealBlueprintFormatter` exists next to UnrealAiEditor, then restart the editor. "));
}

bool UnrealAiBlueprintFormatterBridge::IsFormatterPluginEnabled()
{
#if WITH_EDITOR
	const TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(GFormatterPluginName.ToString());
	return P.IsValid() && P->IsEnabled();
#else
	return false;
#endif
}

bool UnrealAiBlueprintFormatterBridge::IsFormatterModuleReady()
{
#if WITH_EDITOR
	return IsFormatterPluginEnabled() && FModuleManager::Get().IsModuleLoaded(GFormatterModuleName);
#else
	return false;
#endif
}

bool UnrealAiBlueprintFormatterBridge::EnsureFormatterModuleLoaded(FString* OutHint)
{
#if !WITH_EDITOR
	if (OutHint)
	{
		OutHint->Reset();
	}
	return false;
#else
	if (!IsFormatterPluginEnabled())
	{
		if (OutHint)
		{
			*OutHint = FormatterInstallHint() + TEXT("Plugin not found or disabled.");
		}
		return false;
	}
	if (!FModuleManager::Get().IsModuleLoaded(GFormatterModuleName))
	{
		FModuleManager::Get().LoadModule(GFormatterModuleName);
	}
	if (!FModuleManager::Get().IsModuleLoaded(GFormatterModuleName))
	{
		if (OutHint)
		{
			*OutHint = FormatterInstallHint() + TEXT("Failed to load formatter module.");
		}
		return false;
	}
	if (OutHint)
	{
		OutHint->Reset();
	}
	return true;
#endif
}

void UnrealAiBlueprintFormatterBridge::NotifyFormatterMissingOnce()
{
#if WITH_EDITOR
	if (GNotifiedFormatterMissing.exchange(true))
	{
		return;
	}
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	FNotificationInfo Info(NSLOCTEXT(
		"UnrealAiEditor",
		"BlueprintFormatterMissing",
		"Unreal Blueprint Formatter is missing or disabled. Blueprint auto-layout from AI tools is off. Enable the plugin under Edit > Plugins, add Plugins/UnrealBlueprintFormatter to the project, then restart."));
	Info.ExpireDuration = 10.f;
	Info.bUseLargeFont = false;
	TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
	if (Item.IsValid())
	{
		Item->SetCompletionState(SNotificationItem::CS_Fail);
	}
#endif
}

void UnrealAiBlueprintFormatterBridge::ValidatePluginDependencyOnStartup()
{
#if WITH_EDITOR
	const TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(GFormatterPluginName.ToString());
	if (!P.IsValid())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("UnrealAiEditor: UnrealBlueprintFormatter plugin not found. Copy Plugins/UnrealBlueprintFormatter beside UnrealAiEditor. Graph auto-layout will be skipped."));
		NotifyFormatterMissingOnce();
		return;
	}
	if (!P->IsEnabled())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("UnrealAiEditor: UnrealBlueprintFormatter plugin is disabled. Enable it under Edit > Plugins."));
		NotifyFormatterMissingOnce();
		return;
	}
	if (!EnsureFormatterModuleLoaded(nullptr))
	{
		UE_LOG(LogTemp, Warning, TEXT("UnrealAiEditor: UnrealBlueprintFormatter module failed to load."));
		NotifyFormatterMissingOnce();
	}
#endif
}

FUnrealBlueprintGraphFormatResult UnrealAiBlueprintFormatterBridge::TryLayoutAfterAiIrApply(
	UEdGraph* Graph,
	const TArray<UEdGraphNode*>& MaterializedNodes,
	const TArray<FUnrealBlueprintIrNodeLayoutHint>& Hints,
	bool bWanted)
{
	FUnrealBlueprintGraphFormatResult R;
	if (!bWanted || !Graph)
	{
		return R;
	}
	FString Hint;
	if (!EnsureFormatterModuleLoaded(&Hint))
	{
		if (!Hint.IsEmpty())
		{
			R.Warnings.Add(MoveTemp(Hint));
		}
		return R;
	}
	return FUnrealBlueprintGraphFormatService::LayoutAfterAiIrApply(Graph, MaterializedNodes, Hints);
}

FUnrealBlueprintGraphFormatResult UnrealAiBlueprintFormatterBridge::TryLayoutEntireGraph(UEdGraph* Graph, bool bWanted)
{
	FUnrealBlueprintGraphFormatResult R;
	if (!bWanted || !Graph)
	{
		return R;
	}
	FString Hint;
	if (!EnsureFormatterModuleLoaded(&Hint))
	{
		if (!Hint.IsEmpty())
		{
			R.Warnings.Add(MoveTemp(Hint));
		}
		return R;
	}
	FUnrealBlueprintGraphFormatService::LayoutEntireGraph(Graph);
	R.NodesPositioned = 0;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (N)
		{
			++R.NodesPositioned;
		}
	}
	return R;
}

int32 UnrealAiBlueprintFormatterBridge::TryLayoutAllScriptGraphs(UBlueprint* BP)
{
	if (!BP)
	{
		return 0;
	}
	if (!EnsureFormatterModuleLoaded(nullptr))
	{
		return 0;
	}
	int32 Count = 0;
	auto LayoutIfHasNodes = [&Count](UEdGraph* G)
	{
		if (!G)
		{
			return;
		}
		bool bAny = false;
		for (UEdGraphNode* N : G->Nodes)
		{
			if (N)
			{
				bAny = true;
				break;
			}
		}
		if (!bAny)
		{
			return;
		}
		FUnrealBlueprintGraphFormatService::LayoutEntireGraph(G);
		++Count;
	};
	for (const TObjectPtr<UEdGraph>& G : BP->UbergraphPages)
	{
		LayoutIfHasNodes(G.Get());
	}
	for (const TObjectPtr<UEdGraph>& G : BP->FunctionGraphs)
	{
		LayoutIfHasNodes(G.Get());
	}
	for (const TObjectPtr<UEdGraph>& G : BP->MacroGraphs)
	{
		LayoutIfHasNodes(G.Get());
	}
	if (Count > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	}
	return Count;
}
