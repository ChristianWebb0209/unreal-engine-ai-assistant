#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"

#include "Tools/UnrealAiToolDispatch_MoreAssets.h"
#include "Tools/UnrealAiToolJson.h"

#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Misc/ScopedTransaction.h"
#include "StructUtils/TBaseStructure.h"

namespace UnrealAiBlueprintToolsPriv
{
	static UBlueprint* LoadBlueprint(const FString& Path)
	{
		return LoadObject<UBlueprint>(nullptr, *Path);
	}

	static UEdGraph* FindGraphByName(UBlueprint* BP, const FString& GraphName)
	{
		if (!BP || GraphName.IsEmpty())
		{
			return nullptr;
		}
		const FName GN(*GraphName);
		auto TryList = [&GN](const TArray<UEdGraph*>& List) -> UEdGraph*
		{
			for (UEdGraph* G : List)
			{
				if (G && (G->GetFName() == GN || G->GetName().Equals(GraphName, ESearchCase::IgnoreCase)))
				{
					return G;
				}
			}
			return nullptr;
		};
		if (UEdGraph* G = TryList(BP->UbergraphPages))
		{
			return G;
		}
		if (UEdGraph* G = TryList(BP->FunctionGraphs))
		{
			return G;
		}
		if (UEdGraph* G = TryList(BP->MacroGraphs))
		{
			return G;
		}
		return nullptr;
	}

	static FEdGraphPinType ParsePinType(const FString& TypeStr)
	{
		FEdGraphPinType P;
		const FString T = TypeStr.ToLower();
		if (T == TEXT("bool") || T == TEXT("boolean"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		}
		else if (T == TEXT("int") || T == TEXT("integer"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Int;
		}
		else if (T == TEXT("float") || T == TEXT("double"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Real;
			P.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		}
		else if (T == TEXT("string"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_String;
		}
		else if (T == TEXT("vector"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Struct;
			P.PinSubCategoryObject = TBaseStructure<FVector>::Get();
		}
		else if (T == TEXT("rotator"))
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Struct;
			P.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
		}
		else
		{
			P.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		}
		return P;
	}
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintCompile(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path is required"));
	}
	UBlueprint* BP = UnrealAiBlueprintToolsPriv::LoadBlueprint(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load Blueprint"));
	}
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), BP->Status != BS_Error);
	O->SetNumberField(TEXT("blueprint_status"), static_cast<double>(static_cast<int32>(BP->Status)));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintGetGraphSummary(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path is required"));
	}
	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);
	UBlueprint* BP = UnrealAiBlueprintToolsPriv::LoadBlueprint(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load Blueprint"));
	}
	TArray<UEdGraph*> Graphs;
	if (!GraphName.IsEmpty())
	{
		if (UEdGraph* G = UnrealAiBlueprintToolsPriv::FindGraphByName(BP, GraphName))
		{
			Graphs.Add(G);
		}
	}
	else
	{
		Graphs.Append(BP->UbergraphPages);
		Graphs.Append(BP->FunctionGraphs);
		Graphs.Append(BP->MacroGraphs);
	}
	TArray<TSharedPtr<FJsonValue>> GraphArr;
	for (UEdGraph* G : Graphs)
	{
		if (!G)
		{
			continue;
		}
		TSharedPtr<FJsonObject> Go = MakeShared<FJsonObject>();
		Go->SetStringField(TEXT("name"), G->GetName());
		int32 Nodes = 0;
		for (UEdGraphNode* N : G->Nodes)
		{
			if (N)
			{
				++Nodes;
			}
		}
		Go->SetNumberField(TEXT("node_count"), static_cast<double>(Nodes));
		GraphArr.Add(MakeShareable(new FJsonValueObject(Go.ToSharedRef())));
	}
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetArrayField(TEXT("graphs"), GraphArr);
	O->SetNumberField(TEXT("graph_count"), static_cast<double>(GraphArr.Num()));
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintOpenGraphTab(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	FString GraphName;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty()
		|| !Args->TryGetStringField(TEXT("graph_name"), GraphName) || GraphName.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path and graph_name are required"));
	}
	UBlueprint* BP = UnrealAiBlueprintToolsPriv::LoadBlueprint(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load Blueprint"));
	}
	UEdGraph* G = UnrealAiBlueprintToolsPriv::FindGraphByName(BP, GraphName);
	if (!G)
	{
		return UnrealAiToolJson::Error(TEXT("Graph not found"));
	}
	TSharedPtr<FJsonObject> OpenArgs = MakeShared<FJsonObject>();
	OpenArgs->SetStringField(TEXT("object_path"), Path);
	const FUnrealAiToolInvocationResult OpenRes = UnrealAiDispatch_AssetOpenEditor(OpenArgs);
	if (!OpenRes.bOk)
	{
		return OpenRes;
	}
	FKismetEditorUtilities::BringKismetToFocusAttention(G);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	O->SetStringField(TEXT("graph"), GraphName);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_BlueprintAddVariable(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	FString VarName;
	FString TypeStr;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), Path) || Path.IsEmpty()
		|| !Args->TryGetStringField(TEXT("name"), VarName) || VarName.IsEmpty()
		|| !Args->TryGetStringField(TEXT("type"), TypeStr) || TypeStr.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("blueprint_path, name, and type are required"));
	}
	UBlueprint* BP = UnrealAiBlueprintToolsPriv::LoadBlueprint(Path);
	if (!BP)
	{
		return UnrealAiToolJson::Error(TEXT("Could not load Blueprint"));
	}
	const FEdGraphPinType PinType = UnrealAiBlueprintToolsPriv::ParsePinType(TypeStr);
	const FScopedTransaction Txn(NSLOCTEXT("UnrealAiEditor", "TxnBpVar", "Unreal AI: add BP variable"));
	FBlueprintEditorUtils::AddMemberVariable(BP, FName(*VarName), PinType);
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), true);
	return UnrealAiToolJson::Ok(O);
}

FUnrealAiToolInvocationResult UnrealAiDispatch_AnimBlueprintGetGraphSummary(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	if (!Args->TryGetStringField(TEXT("anim_blueprint_path"), Path) || Path.IsEmpty())
	{
		return UnrealAiToolJson::Error(TEXT("anim_blueprint_path is required"));
	}
	if (!LoadObject<UAnimBlueprint>(nullptr, *Path))
	{
		return UnrealAiToolJson::Error(TEXT("Could not load AnimBlueprint"));
	}
	TSharedPtr<FJsonObject> Fake = MakeShared<FJsonObject>();
	Fake->SetStringField(TEXT("blueprint_path"), Path);
	return UnrealAiDispatch_BlueprintGetGraphSummary(Fake);
}
