#include "Misc/UnrealAiRecentUiTracker.h"

#include "Backend/IUnrealAiPersistence.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Widgets/Docking/SDockTab.h"

IUnrealAiPersistence* FUnrealAiRecentUiTracker::Persistence = nullptr;
FString FUnrealAiRecentUiTracker::ProjectId;
FString FUnrealAiRecentUiTracker::ActiveThreadId;
FTSTicker::FDelegateHandle FUnrealAiRecentUiTracker::TickerHandle;
float FUnrealAiRecentUiTracker::TickAccumSec = 0.f;
TArray<FRecentUiEntry> FUnrealAiRecentUiTracker::ProjectGlobalHistory;
TMap<FString, TArray<FRecentUiEntry>> FUnrealAiRecentUiTracker::ThreadOverlayByThreadId;

namespace UnrealAiRecentUiTrackerPriv
{
	static constexpr int32 MaxGlobalEntries = 300;
	static constexpr int32 MaxPerThreadEntries = 120;
	static constexpr float TickIntervalSec = 0.20f;
	static constexpr int32 MaxLabelChars = 96;
}

void FUnrealAiRecentUiTracker::Startup(IUnrealAiPersistence* InPersistence, const FString& InProjectId)
{
	Shutdown();
	Persistence = InPersistence;
	ProjectId = InProjectId;
	LoadGlobalHistory();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&FUnrealAiRecentUiTracker::OnTick));
}

void FUnrealAiRecentUiTracker::Shutdown()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	SaveGlobalHistory();
	Persistence = nullptr;
	ProjectId.Reset();
	ActiveThreadId.Reset();
	TickAccumSec = 0.f;
	ProjectGlobalHistory.Reset();
	ThreadOverlayByThreadId.Reset();
}

void FUnrealAiRecentUiTracker::SetActiveThreadId(const FString& InThreadId)
{
	ActiveThreadId = InThreadId;
}

void FUnrealAiRecentUiTracker::MarkCurrentFocusNow()
{
	TrackFocusedWidget(ERecentUiSource::SlateFocus);
}

void FUnrealAiRecentUiTracker::RecordAgentToolNavigationToAssetPath(const FString& ObjectPath)
{
	if (ObjectPath.IsEmpty())
	{
		return;
	}
	FString Label = ObjectPath;
	int32 Dot = INDEX_NONE;
	if (ObjectPath.FindChar(TEXT('.'), Dot))
	{
		Label = ObjectPath.Left(Dot);
	}
	FRecentUiEntry Candidate;
	Candidate.UiKind = ERecentUiKind::AssetEditor;
	Candidate.Source = ERecentUiSource::AgentToolNavigation;
	Candidate.DisplayName = TrimmedLabel(Label);
	Candidate.LastSeenUtc = FDateTime::UtcNow();
	Candidate.bCurrentlyActive = true;
	Candidate.bThreadLocalPreferred = !ActiveThreadId.IsEmpty();
	Candidate.StableId = BuildStableId(ERecentUiKind::AssetEditor, FString::Printf(TEXT("agent_nav|%s"), *ObjectPath));

	for (FRecentUiEntry& E : ProjectGlobalHistory)
	{
		E.bCurrentlyActive = false;
	}
	Upsert(ProjectGlobalHistory, Candidate, UnrealAiRecentUiTrackerPriv::MaxGlobalEntries);

	if (!ActiveThreadId.IsEmpty())
	{
		TArray<FRecentUiEntry>& Overlay = ThreadOverlayByThreadId.FindOrAdd(ActiveThreadId);
		for (FRecentUiEntry& E : Overlay)
		{
			E.bCurrentlyActive = false;
		}
		Upsert(Overlay, Candidate, UnrealAiRecentUiTrackerPriv::MaxPerThreadEntries);
	}
}

void FUnrealAiRecentUiTracker::GetProjectGlobalHistory(TArray<FRecentUiEntry>& OutEntries)
{
	OutEntries = ProjectGlobalHistory;
}

void FUnrealAiRecentUiTracker::GetThreadOverlay(const FString& ThreadId, TArray<FRecentUiEntry>& OutEntries)
{
	OutEntries.Reset();
	if (const TArray<FRecentUiEntry>* Found = ThreadOverlayByThreadId.Find(ThreadId))
	{
		OutEntries = *Found;
	}
}

bool FUnrealAiRecentUiTracker::OnTick(float DeltaTime)
{
	TickAccumSec += DeltaTime;
	if (TickAccumSec < UnrealAiRecentUiTrackerPriv::TickIntervalSec)
	{
		return true;
	}
	TickAccumSec = 0.f;
	TrackFocusedWidget(ERecentUiSource::PollFallback);
	return true;
}

ERecentUiKind FUnrealAiRecentUiTracker::ClassifyFromTypeName(const FString& TypeName, const FString& Label)
{
	const FString Hay = (TypeName + TEXT(" ") + Label).ToLower();
	if (Hay.Contains(TEXT("viewport")))
	{
		return ERecentUiKind::SceneViewport;
	}
	if (Hay.Contains(TEXT("detail")) || Hay.Contains(TEXT("inspector")))
	{
		return ERecentUiKind::DetailsPanel;
	}
	if (Hay.Contains(TEXT("contentbrowser")) || Hay.Contains(TEXT("content browser")))
	{
		return ERecentUiKind::ContentBrowser;
	}
	if (Hay.Contains(TEXT("outliner")))
	{
		return ERecentUiKind::SceneOutliner;
	}
	if (Hay.Contains(TEXT("output log")))
	{
		return ERecentUiKind::OutputLog;
	}
	if (Hay.Contains(TEXT("blueprint")) || Hay.Contains(TEXT("graph")))
	{
		return ERecentUiKind::BlueprintGraph;
	}
	if (Hay.Contains(TEXT("tab")))
	{
		return ERecentUiKind::NomadTab;
	}
	return ERecentUiKind::ToolPanel;
}

FString FUnrealAiRecentUiTracker::BuildStableId(const ERecentUiKind Kind, const FString& RawId)
{
	return FString::Printf(TEXT("%d|%s"), static_cast<int32>(Kind), *RawId);
}

FString FUnrealAiRecentUiTracker::TrimmedLabel(const FString& InLabel)
{
	FString L = InLabel.IsEmpty() ? TEXT("(unnamed)") : InLabel;
	L.ReplaceInline(TEXT("\r"), TEXT(" "));
	L.ReplaceInline(TEXT("\n"), TEXT(" "));
	if (L.Len() > UnrealAiRecentUiTrackerPriv::MaxLabelChars)
	{
		L = L.Left(UnrealAiRecentUiTrackerPriv::MaxLabelChars - 3) + TEXT("...");
	}
	return L;
}

void FUnrealAiRecentUiTracker::Upsert(TArray<FRecentUiEntry>& Target, const FRecentUiEntry& Candidate, const int32 MaxEntries)
{
	int32 FoundIdx = INDEX_NONE;
	for (int32 i = 0; i < Target.Num(); ++i)
	{
		if (Target[i].StableId == Candidate.StableId)
		{
			FoundIdx = i;
			break;
		}
	}
	if (FoundIdx != INDEX_NONE)
	{
		FRecentUiEntry& E = Target[FoundIdx];
		E.DisplayName = Candidate.DisplayName;
		E.LastSeenUtc = Candidate.LastSeenUtc;
		E.SeenCount = FMath::Max(E.SeenCount + 1, Candidate.SeenCount);
		E.UiKind = Candidate.UiKind;
		E.Source = Candidate.Source;
		E.bCurrentlyActive = Candidate.bCurrentlyActive;
		E.bThreadLocalPreferred = Candidate.bThreadLocalPreferred;
	}
	else
	{
		Target.Add(Candidate);
	}
	Target.Sort([](const FRecentUiEntry& A, const FRecentUiEntry& B) { return A.LastSeenUtc > B.LastSeenUtc; });
	if (MaxEntries > 0 && Target.Num() > MaxEntries)
	{
		Target.SetNum(MaxEntries, EAllowShrinking::No);
	}
}

void FUnrealAiRecentUiTracker::TrackFocusedWidget(const ERecentUiSource Source)
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	FSlateApplication& Slate = FSlateApplication::Get();
	const TSharedPtr<SWidget> Focused = Slate.GetKeyboardFocusedWidget();
	if (!Focused.IsValid())
	{
		return;
	}

	FString TypeName = Focused->GetTypeAsString();
	FString RawId = TypeName;
	FString Label = TypeName;
	TSharedPtr<SWidget> Cur = Focused;
	while (Cur.IsValid())
	{
		if (Cur->GetType() == FName(TEXT("SDockTab")))
		{
			const TSharedPtr<SDockTab> Dock = StaticCastSharedPtr<SDockTab>(Cur);
			if (Dock.IsValid())
			{
				const FText DockLabel = Dock->GetTabLabel();
				Label = DockLabel.IsEmpty() ? Label : DockLabel.ToString();
				RawId = Dock->GetLayoutIdentifier().ToString();
				if (RawId.IsEmpty())
				{
					RawId = Label;
				}
			}
			break;
		}
		Cur = Cur->GetParentWidget();
	}

	const ERecentUiKind Kind = ClassifyFromTypeName(TypeName, Label);
	FRecentUiEntry Candidate;
	Candidate.UiKind = Kind;
	Candidate.Source = Source;
	Candidate.DisplayName = TrimmedLabel(Label);
	Candidate.LastSeenUtc = FDateTime::UtcNow();
	Candidate.bCurrentlyActive = true;
	Candidate.bThreadLocalPreferred = !ActiveThreadId.IsEmpty();
	Candidate.StableId = BuildStableId(Kind, RawId);

	for (FRecentUiEntry& E : ProjectGlobalHistory)
	{
		E.bCurrentlyActive = false;
	}
	Upsert(ProjectGlobalHistory, Candidate, UnrealAiRecentUiTrackerPriv::MaxGlobalEntries);

	if (!ActiveThreadId.IsEmpty())
	{
		TArray<FRecentUiEntry>& Overlay = ThreadOverlayByThreadId.FindOrAdd(ActiveThreadId);
		for (FRecentUiEntry& E : Overlay)
		{
			E.bCurrentlyActive = false;
		}
		Upsert(Overlay, Candidate, UnrealAiRecentUiTrackerPriv::MaxPerThreadEntries);
	}
}

void FUnrealAiRecentUiTracker::SaveGlobalHistory()
{
	if (!Persistence || ProjectId.IsEmpty())
	{
		return;
	}
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), TEXT("unreal_ai.recent_ui_v1"));
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FRecentUiEntry& E : ProjectGlobalHistory)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("stableId"), E.StableId);
		Obj->SetStringField(TEXT("displayName"), E.DisplayName);
		Obj->SetStringField(TEXT("uiKind"), FString::FromInt(static_cast<int32>(E.UiKind)));
		Obj->SetStringField(TEXT("source"), FString::FromInt(static_cast<int32>(E.Source)));
		Obj->SetStringField(TEXT("lastSeenUtc"), E.LastSeenUtc.ToIso8601());
		Obj->SetNumberField(TEXT("seenCount"), E.SeenCount);
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Root->SetArrayField(TEXT("entries"), Arr);
	FString OutJson;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
	if (FJsonSerializer::Serialize(Root.ToSharedRef(), W))
	{
		Persistence->SaveProjectRecentUiJson(ProjectId, OutJson);
	}
}

void FUnrealAiRecentUiTracker::LoadGlobalHistory()
{
	ProjectGlobalHistory.Reset();
	if (!Persistence || ProjectId.IsEmpty())
	{
		return;
	}
	FString Json;
	if (!Persistence->LoadProjectRecentUiJson(ProjectId, Json))
	{
		return;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Root->TryGetArrayField(TEXT("entries"), Arr) || !Arr)
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& V : *Arr)
	{
		const TSharedPtr<FJsonObject> Obj = V.IsValid() ? V->AsObject() : nullptr;
		if (!Obj.IsValid())
		{
			continue;
		}
		FRecentUiEntry E;
		Obj->TryGetStringField(TEXT("stableId"), E.StableId);
		Obj->TryGetStringField(TEXT("displayName"), E.DisplayName);
		int32 Kind = 0;
		int32 Source = 0;
		LexFromString(Kind, *Obj->GetStringField(TEXT("uiKind")));
		LexFromString(Source, *Obj->GetStringField(TEXT("source")));
		E.UiKind = static_cast<ERecentUiKind>(Kind);
		E.Source = static_cast<ERecentUiSource>(Source);
		FString Ts;
		if (Obj->TryGetStringField(TEXT("lastSeenUtc"), Ts))
		{
			FDateTime::ParseIso8601(*Ts, E.LastSeenUtc);
		}
		E.SeenCount = Obj->HasField(TEXT("seenCount")) ? static_cast<int32>(Obj->GetNumberField(TEXT("seenCount"))) : 1;
		if (!E.StableId.IsEmpty())
		{
			ProjectGlobalHistory.Add(E);
		}
	}
}
