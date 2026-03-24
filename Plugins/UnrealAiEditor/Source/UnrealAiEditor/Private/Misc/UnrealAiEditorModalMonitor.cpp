#include "Misc/UnrealAiEditorModalMonitor.h"

#include "Backend/UnrealAiBackendRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Harness/IAgentRunSink.h"
#include "Harness/IUnrealAiAgentHarness.h"
#include "Widgets/SWindow.h"

TWeakPtr<FUnrealAiBackendRegistry> FUnrealAiEditorModalMonitor::BackendRegistry;
TWeakPtr<IAgentRunSink> FUnrealAiEditorModalMonitor::ActiveSink;
FTSTicker::FDelegateHandle FUnrealAiEditorModalMonitor::TickerHandle;
TSet<uint64> FUnrealAiEditorModalMonitor::SeenModalKeys;
FString FUnrealAiEditorModalMonitor::PendingToolFootnote;
float FUnrealAiEditorModalMonitor::TickAccumSec = 0.f;

void FUnrealAiEditorModalMonitor::Startup(const TSharedPtr<FUnrealAiBackendRegistry>& Registry)
{
	Shutdown();
	BackendRegistry = Registry;
	if (Registry.IsValid())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateStatic(&FUnrealAiEditorModalMonitor::OnTick));
	}
}

void FUnrealAiEditorModalMonitor::Shutdown()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	BackendRegistry.Reset();
	ActiveSink.Reset();
	SeenModalKeys.Reset();
	PendingToolFootnote.Reset();
	TickAccumSec = 0.f;
}

void FUnrealAiEditorModalMonitor::NotifyAgentTurnStarted(const TSharedPtr<IAgentRunSink>& Sink)
{
	ActiveSink = Sink;
	SeenModalKeys.Reset();
	PendingToolFootnote.Reset();
}

void FUnrealAiEditorModalMonitor::NotifyAgentTurnEndedForSink(const TSharedPtr<IAgentRunSink>& FinishedSink)
{
	if (!FinishedSink.IsValid())
	{
		return;
	}
	const TSharedPtr<IAgentRunSink> Cur = ActiveSink.Pin();
	if (Cur == FinishedSink)
	{
		ActiveSink.Reset();
		SeenModalKeys.Reset();
		PendingToolFootnote.Reset();
	}
}

FString FUnrealAiEditorModalMonitor::ConsumePendingToolDialogFootnote()
{
	FString Out = PendingToolFootnote;
	PendingToolFootnote.Reset();
	return Out;
}

bool FUnrealAiEditorModalMonitor::TitleSuggestsBlockingEditorDialog(const FString& Title)
{
	if (Title.IsEmpty())
	{
		return false;
	}
	const FString T = Title.ToLower();
	static const TCHAR* Keys[] = {
		TEXT("overwrite"),
		TEXT("replace"),
		TEXT("already exists"),
		TEXT("exists"),
		TEXT("confirm"),
		TEXT("question"),
		TEXT("warning"),
		TEXT("error"),
		TEXT("save"),
		TEXT("delete"),
		TEXT("cancel"),
		TEXT("unable"),
		TEXT("failed"),
		TEXT("missing"),
		TEXT("duplicate"),
		TEXT("check out"),
		TEXT("checkout"),
		TEXT("read-only"),
		TEXT("readonly"),
		TEXT("perforce"),
		TEXT("revision"),
		TEXT("asset"),
	};
	for (const TCHAR* K : Keys)
	{
		if (T.Contains(K))
		{
			return true;
		}
	}
	return false;
}

bool FUnrealAiEditorModalMonitor::OnTick(float DeltaTime)
{
	TickAccumSec += DeltaTime;
	if (TickAccumSec < 0.05f)
	{
		return true;
	}
	TickAccumSec = 0.f;

	const TSharedPtr<FUnrealAiBackendRegistry> Reg = BackendRegistry.Pin();
	if (!Reg.IsValid())
	{
		return true;
	}
	IUnrealAiAgentHarness* Harness = Reg->GetAgentHarness();
	if (!Harness || !Harness->IsTurnInProgress())
	{
		return true;
	}

	FSlateApplication& SlateApp = FSlateApplication::Get();
	const TArray<TSharedRef<SWindow>> Top = SlateApp.GetInteractiveTopLevelWindows();
	for (const TSharedRef<SWindow>& W : Top)
	{
		if (!W->IsVisible())
		{
			continue;
		}
		// Modal / blocking dialogs are not regular main windows (editor frame, standalone tools).
		if (W->IsRegularWindow())
		{
			continue;
		}

		const FString Title = W->GetTitle().ToString();
		if (!TitleSuggestsBlockingEditorDialog(Title))
		{
			continue;
		}

		const SWindow* const WindowPtr = &(*W);
		const uint64 Key = static_cast<uint64>(reinterpret_cast<uintptr_t>(WindowPtr));
		if (SeenModalKeys.Contains(Key))
		{
			continue;
		}
		SeenModalKeys.Add(Key);

		const FString Summary = Title;
		if (PendingToolFootnote.IsEmpty())
		{
			PendingToolFootnote = Summary;
		}
		else
		{
			PendingToolFootnote += TEXT("\n");
			PendingToolFootnote += Summary;
		}

		if (const TSharedPtr<IAgentRunSink> Sink = ActiveSink.Pin())
		{
			Sink->OnEditorBlockingDialogDuringTools(Summary);
		}
	}

	return true;
}
