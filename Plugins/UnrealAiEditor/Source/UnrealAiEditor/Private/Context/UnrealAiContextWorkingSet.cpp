#include "Context/UnrealAiContextWorkingSet.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealAiContextWorkingSetPriv
{
	static bool IsLikelyGameAssetPath(const FString& P)
	{
		FString T = P;
		T.TrimStartAndEndInline();
		return T.StartsWith(TEXT("/Game/"));
	}

	static void TryAddStringField(const TSharedPtr<FJsonObject>& Root, const TCHAR* Key, FString& Out)
	{
		FString V;
		if (Root.IsValid() && Root->TryGetStringField(Key, V))
		{
			V.TrimStartAndEndInline();
			if (IsLikelyGameAssetPath(V))
			{
				Out = V;
			}
		}
	}

	static void ExtractPathsFromJsonRoot(const TSharedPtr<FJsonObject>& Root, TArray<FString>& OutPaths)
	{
		if (!Root.IsValid())
		{
			return;
		}
		FString One;
		TryAddStringField(Root, TEXT("blueprint_path"), One);
		if (!One.IsEmpty())
		{
			OutPaths.AddUnique(One);
		}
		One.Reset();
		TryAddStringField(Root, TEXT("object_path"), One);
		if (!One.IsEmpty())
		{
			OutPaths.AddUnique(One);
		}
		One.Reset();
		TryAddStringField(Root, TEXT("asset_path"), One);
		if (!One.IsEmpty())
		{
			OutPaths.AddUnique(One);
		}
		const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
		if (Root->TryGetArrayField(TEXT("assets"), Assets) && Assets)
		{
			for (const TSharedPtr<FJsonValue>& V : *Assets)
			{
				const TSharedPtr<FJsonObject>* O = nullptr;
				if (!V.IsValid() || !V->TryGetObject(O) || !O || !(*O).IsValid())
				{
					continue;
				}
				FString Op;
				(*O)->TryGetStringField(TEXT("object_path"), Op);
				Op.TrimStartAndEndInline();
				if (IsLikelyGameAssetPath(Op))
				{
					OutPaths.AddUnique(Op);
				}
			}
		}
	}
}

void UnrealAiContextWorkingSet::Touch(
	FAgentContextState& State,
	const FString& ObjectPath,
	const FString& AssetClassPath,
	const EThreadAssetTouchSource Source,
	const FString& LastToolName)
{
	FString P = ObjectPath;
	P.TrimStartAndEndInline();
	if (!UnrealAiContextWorkingSetPriv::IsLikelyGameAssetPath(P))
	{
		return;
	}
	for (int32 i = State.ThreadAssetWorkingSet.Num() - 1; i >= 0; --i)
	{
		if (State.ThreadAssetWorkingSet[i].ObjectPath.Equals(P, ESearchCase::CaseSensitive))
		{
			State.ThreadAssetWorkingSet.RemoveAt(i);
			break;
		}
	}
	FThreadAssetWorkingEntry E;
	E.ObjectPath = P;
	E.AssetClassPath = AssetClassPath;
	E.LastTouchedUtc = FDateTime::UtcNow();
	E.TouchSource = Source;
	E.LastToolName = LastToolName;
	State.ThreadAssetWorkingSet.Insert(E, 0);
	while (State.ThreadAssetWorkingSet.Num() > MaxEntries)
	{
		State.ThreadAssetWorkingSet.Pop(EAllowShrinking::No);
	}
}

void UnrealAiContextWorkingSet::TouchFromToolResult(FAgentContextState& State, const FName ToolName, const FString& ResultText)
{
	if (ResultText.IsEmpty() || !ResultText.TrimStart().StartsWith(TEXT("{")))
	{
		return;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResultText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}
	TArray<FString> Paths;
	UnrealAiContextWorkingSetPriv::ExtractPathsFromJsonRoot(Root, Paths);
	const FString ToolStr = ToolName.ToString();
	for (const FString& Path : Paths)
	{
		Touch(State, Path, FString(), EThreadAssetTouchSource::ToolResult, ToolStr);
	}
}

void UnrealAiContextWorkingSet::ReorderOpenEditorAssets(
	FEditorContextSnapshot& Snap,
	const TArray<FThreadAssetWorkingEntry>& WorkingSet)
{
	if (Snap.OpenEditorAssets.Num() <= 1)
	{
		return;
	}
	TSet<FString> Seen;
	TArray<FString> Ordered;
	Ordered.Reserve(Snap.OpenEditorAssets.Num());
	for (const FThreadAssetWorkingEntry& W : WorkingSet)
	{
		if (W.ObjectPath.IsEmpty())
		{
			continue;
		}
		if (Snap.OpenEditorAssets.Contains(W.ObjectPath) && !Seen.Contains(W.ObjectPath))
		{
			Ordered.Add(W.ObjectPath);
			Seen.Add(W.ObjectPath);
		}
	}
	for (const FString& P : Snap.OpenEditorAssets)
	{
		if (!Seen.Contains(P))
		{
			Ordered.Add(P);
			Seen.Add(P);
		}
	}
	Snap.OpenEditorAssets = MoveTemp(Ordered);
}

void UnrealAiContextWorkingSet::TouchFromEditorSnapshot(FAgentContextState& State, const FEditorContextSnapshot& Snap)
{
	if (!Snap.ActiveAssetPath.IsEmpty())
	{
		Touch(State, Snap.ActiveAssetPath, FString(), EThreadAssetTouchSource::ContentBrowserSelection);
	}
	for (const FString& A : Snap.ContentBrowserSelectedAssets)
	{
		Touch(State, A, FString(), EThreadAssetTouchSource::ContentBrowserSelection);
	}
	for (const FString& O : Snap.OpenEditorAssets)
	{
		Touch(State, O, FString(), EThreadAssetTouchSource::OpenEditor);
	}
}

static bool PathLooksLikeWritableBlueprint(const FString& Path)
{
	if (!Path.StartsWith(TEXT("/Game/")))
	{
		return false;
	}
	const FString Lower = Path.ToLower();
	return Lower.Contains(TEXT(".bp")) || Lower.Contains(TEXT("_bp")) || Lower.Contains(TEXT("blueprint"));
}

FString UnrealAiContextWorkingSet::FindBlueprintPathForAutofill(const FAgentContextState& State)
{
	for (const FThreadAssetWorkingEntry& E : State.ThreadAssetWorkingSet)
	{
		if (PathLooksLikeWritableBlueprint(E.ObjectPath))
		{
			return E.ObjectPath;
		}
	}
	if (State.EditorSnapshot.IsSet() && State.EditorSnapshot.GetValue().bValid)
	{
		const FEditorContextSnapshot& S = State.EditorSnapshot.GetValue();
		if (PathLooksLikeWritableBlueprint(S.ActiveAssetPath))
		{
			return S.ActiveAssetPath;
		}
		for (const FString& A : S.ContentBrowserSelectedAssets)
		{
			if (PathLooksLikeWritableBlueprint(A))
			{
				return A;
			}
		}
		for (const FString& O : S.OpenEditorAssets)
		{
			if (PathLooksLikeWritableBlueprint(O))
			{
				return O;
			}
		}
	}
	return FString();
}
