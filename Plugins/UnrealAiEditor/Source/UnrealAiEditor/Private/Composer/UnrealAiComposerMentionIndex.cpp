#include "Composer/UnrealAiComposerMentionIndex.h"

#include "Context/UnrealAiProjectId.h"

#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SoftObjectPath.h"

#if WITH_EDITOR
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#endif

namespace UnrealAiMentionIndexPriv
{
	static constexpr int32 SchemaVersion = 1;
	static constexpr int32 MaxActorsScan = 500;

	static FString CacheFilePath()
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAiEditor"), TEXT("MentionAssetIndex.json")));
	}

	static bool JsonGetString(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FString& Out)
	{
		return O.IsValid() && O->TryGetStringField(Key, Out) && !Out.IsEmpty();
	}
} // namespace UnrealAiMentionIndexPriv

FUnrealAiComposerMentionIndex::FUnrealAiComposerMentionIndex() = default;

FUnrealAiComposerMentionIndex& FUnrealAiComposerMentionIndex::Get()
{
	static FUnrealAiComposerMentionIndex Inst;
	return Inst;
}

void FUnrealAiComposerMentionIndex::InvalidateAssetIndex()
{
	FScopeLock Lock(&AssetLock);
	AssetCandidates.Reset();
	bAssetIndexReady = false;
	bAssetIndexBuilding = false;
}

void FUnrealAiComposerMentionIndex::TryLoadAssetCacheFromDisk()
{
	const FString Path = UnrealAiMentionIndexPriv::CacheFilePath();
	if (!FPaths::FileExists(Path))
	{
		return;
	}
	FString JsonStr;
	if (!FFileHelper::LoadFileToString(JsonStr, *Path))
	{
		return;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}
	int32 Ver = 0;
	if (!Root->TryGetNumberField(TEXT("v"), Ver) || Ver != UnrealAiMentionIndexPriv::SchemaVersion)
	{
		return;
	}
	FString CachedProjectId;
	if (!Root->TryGetStringField(TEXT("projectId"), CachedProjectId)
		|| CachedProjectId != UnrealAiProjectId::GetCurrentProjectId())
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!Root->TryGetArrayField(TEXT("items"), Items) || !Items || Items->Num() == 0)
	{
		return;
	}
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AR = ARM.Get();

	TArray<FMentionCandidate> Loaded;
	Loaded.Reserve(Items->Num());
	for (const TSharedPtr<FJsonValue>& V : *Items)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj->IsValid())
		{
			continue;
		}
		FString P, Display, ClassStr;
		if (!UnrealAiMentionIndexPriv::JsonGetString(*Obj, TEXT("path"), P))
		{
			continue;
		}
		UnrealAiMentionIndexPriv::JsonGetString(*Obj, TEXT("display"), Display);
		UnrealAiMentionIndexPriv::JsonGetString(*Obj, TEXT("class"), ClassStr);

		const FSoftObjectPath SOP(P);
		const FAssetData AD = AR.GetAssetByObjectPath(SOP);
		FMentionCandidate C;
		C.Kind = EUnrealAiMentionKind::Asset;
		C.PrimaryKey = P;
		C.DisplayName = Display.IsEmpty() && AD.IsValid() ? AD.AssetName.ToString() : Display;
		C.SortClass = ClassStr.IsEmpty() && AD.IsValid() ? AD.AssetClassPath.ToString() : ClassStr;
		C.AssetData = AD;
		if (!C.DisplayName.IsEmpty() && !C.PrimaryKey.IsEmpty())
		{
			Loaded.Add(MoveTemp(C));
		}
	}
	if (Loaded.Num() == 0)
	{
		return;
	}
	FScopeLock Lock(&AssetLock);
	AssetCandidates = MoveTemp(Loaded);
	bAssetIndexReady = true;
	bAssetIndexBuilding = false;
}

void FUnrealAiComposerMentionIndex::SaveAssetCacheToDisk() const
{
	TArray<FMentionCandidate> Copy;
	{
		FScopeLock Lock(&AssetLock);
		Copy = AssetCandidates;
	}
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("v"), UnrealAiMentionIndexPriv::SchemaVersion);
	Root->SetStringField(TEXT("projectId"), UnrealAiProjectId::GetCurrentProjectId());
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FMentionCandidate& C : Copy)
	{
		if (!C.IsAsset())
		{
			continue;
		}
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("path"), C.PrimaryKey);
		O->SetStringField(TEXT("display"), C.DisplayName);
		O->SetStringField(TEXT("class"), C.SortClass);
		Items.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("items"), Items);
	FString OutStr;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutStr);
	if (FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		const FString Dir = FPaths::GetPath(UnrealAiMentionIndexPriv::CacheFilePath());
		IFileManager::Get().MakeDirectory(*Dir, true);
		FFileHelper::SaveStringToFile(OutStr, *UnrealAiMentionIndexPriv::CacheFilePath());
	}
}

void FUnrealAiComposerMentionIndex::BuildAssetsFromRegistry()
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AR = ARM.Get();
	FARFilter Filter;
	Filter.PackagePaths.Add(FName(TEXT("/Game")));
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Found;
	AR.GetAssets(Filter, Found);

	TArray<FMentionCandidate> Built;
	Built.Reserve(Found.Num());
	for (const FAssetData& AD : Found)
	{
		if (!AD.IsValid())
		{
			continue;
		}
		FMentionCandidate C;
		C.Kind = EUnrealAiMentionKind::Asset;
		C.DisplayName = AD.AssetName.ToString();
		C.SortClass = AD.AssetClassPath.ToString();
		C.PrimaryKey = AD.GetObjectPathString();
		C.AssetData = AD;
		Built.Add(MoveTemp(C));
	}
	{
		FScopeLock Lock(&AssetLock);
		AssetCandidates = MoveTemp(Built);
		bAssetIndexReady = true;
		bAssetIndexBuilding = false;
	}
	SaveAssetCacheToDisk();
}

void FUnrealAiComposerMentionIndex::EnsureAssetIndexBuilding()
{
	if (bAssetIndexReady || bAssetIndexBuilding)
	{
		return;
	}
	TryLoadAssetCacheFromDisk();
	if (bAssetIndexReady)
	{
		return;
	}
	bAssetIndexBuilding = true;
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this](float)
		{
			BuildAssetsFromRegistry();
			return false;
		}),
		0.f);
}

void FUnrealAiComposerMentionIndex::GatherActors(TArray<FMentionCandidate>& Out) const
{
#if WITH_EDITOR
	if (!GEditor)
	{
		return;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return;
	}
	int32 N = 0;
	for (TActorIterator<AActor> It(World); It && N < UnrealAiMentionIndexPriv::MaxActorsScan; ++It, ++N)
	{
		AActor* A = *It;
		if (!IsValid(A))
		{
			continue;
		}
		FMentionCandidate C;
		C.Kind = EUnrealAiMentionKind::Actor;
		C.DisplayName = A->GetActorLabel().IsEmpty() ? A->GetName() : A->GetActorLabel();
		C.SortClass = A->GetClass() ? A->GetClass()->GetName() : TEXT("Actor");
		C.PrimaryKey = A->GetPathName();
		Out.Add(MoveTemp(C));
	}
#endif
}

bool FUnrealAiComposerMentionIndex::MatchesToken(const FMentionCandidate& C, const FString& TokenLower)
{
	if (TokenLower.IsEmpty())
	{
		return true;
	}
	const FString A = (C.DisplayName + TEXT(" ") + C.SortClass + TEXT(" ") + C.PrimaryKey).ToLower();
	return A.Contains(TokenLower);
}

void FUnrealAiComposerMentionIndex::SortCandidates(TArray<FMentionCandidate>& Items)
{
	Items.Sort([](const FMentionCandidate& A, const FMentionCandidate& B)
	{
		if (A.Kind != B.Kind)
		{
			return (uint8)A.Kind < (uint8)B.Kind;
		}
		const int32 CmpClass = A.SortClass.Compare(B.SortClass);
		if (CmpClass != 0)
		{
			return CmpClass < 0;
		}
		return A.DisplayName.Compare(B.DisplayName) < 0;
	});
}

void FUnrealAiComposerMentionIndex::FilterCandidates(
	const FString& Token,
	TArray<FMentionCandidate>& Out,
	const int32 MaxResults) const
{
	const FString TokenLower = Token.ToLower();
	TArray<FMentionCandidate> Merged;
	{
		FScopeLock Lock(&AssetLock);
		Merged = AssetCandidates;
	}
	GatherActors(Merged);

	TArray<FMentionCandidate> Filtered;
	Filtered.Reserve(Merged.Num());
	for (const FMentionCandidate& C : Merged)
	{
		if (MatchesToken(C, TokenLower))
		{
			Filtered.Add(C);
		}
	}
	SortCandidates(Filtered);
	if (Filtered.Num() > MaxResults)
	{
		Filtered.SetNum(MaxResults);
	}
	Out = MoveTemp(Filtered);
}
