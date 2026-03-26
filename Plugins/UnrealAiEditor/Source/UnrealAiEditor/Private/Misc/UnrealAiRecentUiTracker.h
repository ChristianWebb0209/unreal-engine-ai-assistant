#pragma once

#include "Containers/Ticker.h"
#include "Context/AgentContextTypes.h"

class IUnrealAiPersistence;

class FUnrealAiRecentUiTracker
{
public:
	static void Startup(IUnrealAiPersistence* InPersistence, const FString& InProjectId);
	static void Shutdown();

	static void SetActiveThreadId(const FString& InThreadId);
	static void MarkCurrentFocusNow();

	static void GetProjectGlobalHistory(TArray<FRecentUiEntry>& OutEntries);
	static void GetThreadOverlay(const FString& ThreadId, TArray<FRecentUiEntry>& OutEntries);

private:
	static bool OnTick(float DeltaTime);
	static void TrackFocusedWidget(ERecentUiSource Source);
	static ERecentUiKind ClassifyFromTypeName(const FString& TypeName, const FString& Label);
	static FString BuildStableId(ERecentUiKind Kind, const FString& RawId);
	static void Upsert(TArray<FRecentUiEntry>& Target, const FRecentUiEntry& Candidate, int32 MaxEntries);
	static void SaveGlobalHistory();
	static void LoadGlobalHistory();
	static FString TrimmedLabel(const FString& InLabel);

private:
	static IUnrealAiPersistence* Persistence;
	static FString ProjectId;
	static FString ActiveThreadId;
	static FTSTicker::FDelegateHandle TickerHandle;
	static float TickAccumSec;
	static TArray<FRecentUiEntry> ProjectGlobalHistory;
	static TMap<FString, TArray<FRecentUiEntry>> ThreadOverlayByThreadId;
};
