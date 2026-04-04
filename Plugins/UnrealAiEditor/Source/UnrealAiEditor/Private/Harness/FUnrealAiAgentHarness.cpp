#include "Harness/FUnrealAiAgentHarness.h"

#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Templates/SharedPointer.h"
#include "Tools/UnrealAiToolCatalog.h"
#include "Harness/FUnrealAiConversationStore.h"
#include "Tools/UnrealAiBuildBlueprintTag.h"
#include "Tools/UnrealAiBlueprintBuilderToolSurface.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Harness/IAgentRunSink.h"
#include "Planning/FUnrealAiPlanExecutor.h"
#include "Planning/UnrealAiPlanPlannerHarness.h"
#include "Harness/ILlmTransport.h"
#include "Harness/UnrealAiTurnLlmRequestBuilder.h"
#include "Harness/IToolExecutionHost.h"
#include "Context/IAgentContextService.h"
#include "Context/AgentContextTypes.h"
#include "Misc/SecureHash.h"
#include "Tools/UnrealAiToolUsageEventLogger.h"
#include "Tools/UnrealAiToolUsagePrior.h"
#include "Tools/UnrealAiBlueprintToolGate.h"
#include "GraphBuilder/UnrealAiGraphEditDomain.h"
#include "Backend/FUnrealAiUsageTracker.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Memory/FUnrealAiMemoryCompactor.h"
#include "Memory/IUnrealAiMemoryService.h"
#include "Harness/UnrealAiLlmInvocationService.h"
#include "Harness/UnrealAiHarnessTpmThrottle.h"
#include "Misc/UnrealAiEditorModalMonitor.h"
#include "Misc/UnrealAiHarnessProgressTelemetry.h"
#include "Misc/UnrealAiRuntimeDefaults.h"
#include "Misc/UnrealAiWaitTimePolicy.h"
#include "Observability/UnrealAiBackgroundOpsLog.h"
#include "Widgets/UnrealAiToolUi.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include <atomic>

namespace UnrealAiAgentHarnessPriv
{
	static const TCHAR* GUnrealAiDispatchToolName = TEXT("unreal_ai_dispatch");

	/** Stop after this many consecutive identical tool failures (same invoke + args). */
	static constexpr int32 GHarnessRepeatedFailureStopCount = 4;
	/** How many retries we allow when streamed tool JSON is truncated and never becomes valid. */
	static constexpr int32 GHarnessStreamToolCallIncompleteMaxRetriesPerRound = 1;
	/** Default token budget per agent turn when profile sets maxAgentTurnTokens to 0 (unset in JSON). */
	static constexpr int32 GHarnessDefaultMaxTurnTokens = 1000000;
	/** Hard backstop on LLM↔tool iterations if token/repeat limits do not apply first. */
	static constexpr int32 GHarnessMaxLlmRoundBackstop = 512;

	static FString HashUtf8Query(const FString& S)
	{
		FTCHARToUTF8 Utf(*S);
		FMD5 Md5;
		Md5.Update(reinterpret_cast<const uint8*>(Utf.Get()), Utf.Length());
		uint8 Digest[16];
		Md5.Final(Digest);
		return BytesToHex(Digest, 16);
	}

	static FString SerializeSuggestedCorrectCallForNudge(const TSharedPtr<FJsonObject>& Obj)
	{
		if (!Obj.IsValid())
		{
			return FString();
		}
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		if (FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer))
		{
			return Out;
		}
		return FString();
	}

	/** Extract compact JSON for harness repeated-validation nudge when resolvers return ErrorWithSuggestedCall. */
	static void TryCaptureSuggestedCorrectCallFromToolContent(const FString& ModelToolContent, FString& OutSerialized)
	{
		OutSerialized.Reset();
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ModelToolContent);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return;
		}
		const TSharedPtr<FJsonObject>* Suggested = nullptr;
		if (!Root->TryGetObjectField(TEXT("suggested_correct_call"), Suggested) || !Suggested || !Suggested->IsValid())
		{
			return;
		}
		OutSerialized = SerializeSuggestedCorrectCallForNudge(*Suggested);
	}

	/** asset_apply_properties failures often differ only in property keys; normalize so repeat-failure stop triggers. */
	static FString NormalizeToolFailureSignatureForRepeatCount(const FString& InvokeName, const FString& InvokeArgs)
	{
		if (InvokeName.Equals(TEXT("asset_apply_properties"), ESearchCase::IgnoreCase))
		{
			TSharedPtr<FJsonObject> O;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InvokeArgs);
			if (FJsonSerializer::Deserialize(Reader, O) && O.IsValid())
			{
				FString P;
				if (O->TryGetStringField(TEXT("object_path"), P) || O->TryGetStringField(TEXT("path"), P))
				{
					P.TrimStartAndEndInline();
					if (!P.IsEmpty())
					{
						return (InvokeName + TEXT("|") + P).ToLower();
					}
				}
			}
		}
		return (InvokeName + TEXT("|") + InvokeArgs.Left(220)).ToLower();
	}

	static bool LooksLikeIncompleteAskAnswer(const FString& Trim)
	{
		if (Trim.IsEmpty())
		{
			return true;
		}
		if (Trim.Equals(TEXT("Yes."), ESearchCase::IgnoreCase) || Trim.Equals(TEXT("No."), ESearchCase::IgnoreCase)
			|| Trim.Equals(TEXT("OK."), ESearchCase::IgnoreCase))
		{
			return false;
		}
		if (Trim.Len() >= 40)
		{
			return false;
		}
		const TCHAR Last = Trim[Trim.Len() - 1];
		if (Last == TEXT('.') || Last == TEXT('?') || Last == TEXT('!'))
		{
			return false;
		}
		if (Trim.Len() < 20)
		{
			return true;
		}
		return !Trim.Contains(TEXT(" "));
	}

	static FString BuildToolSelectorRanksJson(const FUnrealAiToolSurfaceTelemetry& Tel)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("query_shape"), Tel.QueryShape);
		Root->SetStringField(TEXT("query_hash"), Tel.QueryHash);
		if (!Tel.SurfaceProfile.IsEmpty())
		{
			Root->SetStringField(TEXT("surface_profile"), Tel.SurfaceProfile);
		}
		if (Tel.AppendixCharBudgetLimit > 0)
		{
			Root->SetNumberField(TEXT("appendix_char_budget_limit"), static_cast<double>(Tel.AppendixCharBudgetLimit));
		}
		Root->SetNumberField(TEXT("k_effective"), static_cast<double>(Tel.KEffective));
		Root->SetNumberField(TEXT("eligible_count"), static_cast<double>(Tel.EligibleCount));
		TArray<TSharedPtr<FJsonValue>> ToolsArr;
		for (const FUnrealAiToolSurfaceRankedEntry& E : Tel.RankedTools)
		{
			TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("rank"), static_cast<double>(E.Rank));
			O->SetStringField(TEXT("tool_id"), E.ToolId);
			O->SetNumberField(TEXT("combined"), static_cast<double>(E.CombinedScore));
			O->SetNumberField(TEXT("bm25_norm"), static_cast<double>(E.Bm25Norm01));
			O->SetNumberField(TEXT("context_mult"), static_cast<double>(E.ContextMultiplier));
			O->SetNumberField(TEXT("usage_prior"), static_cast<double>(E.UsagePrior01));
			O->SetBoolField(TEXT("prior_blended"), E.bUsagePriorBlended);
			O->SetBoolField(TEXT("guardrail"), E.bGuardrail);
			O->SetStringField(TEXT("selection_reason"), E.SelectionReason);
			ToolsArr.Add(MakeShared<FJsonValueObject>(O.ToSharedRef()));
		}
		Root->SetArrayField(TEXT("tools"), ToolsArr);
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
		{
			return FString();
		}
		return Out;
	}

	static FString BuildToolRepairUserLine(FUnrealAiToolCatalog* InCatalog, const TArray<FUnrealAiToolCallSpec>& Calls)
	{
		if (!InCatalog)
		{
			return FString();
		}
		FString Msg = TEXT(
			"[Harness][tool_call_repair] Use `unreal_ai_dispatch` with valid JSON: {\"tool_id\":\"<catalog id>\",\"arguments\":{...}}. ");
		if (Calls.Num() == 1 && Calls[0].Name == GUnrealAiDispatchToolName)
		{
			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<>> Jr = TJsonReaderFactory<>::Create(Calls[0].ArgumentsJson);
			if (FJsonSerializer::Deserialize(Jr, Root) && Root.IsValid())
			{
				FString InnerId;
				if (Root->TryGetStringField(TEXT("tool_id"), InnerId) && !InnerId.IsEmpty())
				{
					FString P;
					if (InCatalog->TryGetToolParametersJsonString(InnerId, P))
					{
						Msg += FString::Printf(TEXT("Inner tool `%s` parameters schema excerpt: %s"), *InnerId, *P);
						return Msg;
					}
				}
			}
		}
		Msg += TEXT("Arguments must be complete JSON objects.");
		return Msg;
	}

	static bool IsHarnessSyntheticUserMessage(const FString& Content)
	{
		return Content.StartsWith(TEXT("[Harness]"));
	}

	static FString GetLastRealUserMessage(const TArray<FUnrealAiConversationMessage>& Messages)
	{
		for (int32 i = Messages.Num() - 1; i >= 0; --i)
		{
			if (Messages[i].Role != TEXT("user"))
			{
				continue;
			}
			if (IsHarnessSyntheticUserMessage(Messages[i].Content))
			{
				continue;
			}
			return Messages[i].Content;
		}
		return FString();
	}

	static bool UserLikelyRequestsActionTool(const FString& UserText)
	{
		const FString T = UserText.ToLower();
		if (T.IsEmpty())
		{
			return false;
		}
		static const TCHAR* Tokens[] = {
			TEXT("run"), TEXT("start"), TEXT("stop"), TEXT("compile"), TEXT("save"),
			TEXT("open"), TEXT("re-open"), TEXT("reopen"), TEXT("fix"), TEXT("apply"),
			TEXT("change"), TEXT("adjust"), TEXT("tune"), TEXT("create"), TEXT("delete"),
			TEXT("playtest"), TEXT("test"), TEXT("resolve")
		};
		for (const TCHAR* K : Tokens)
		{
			if (T.Contains(K))
			{
				return true;
			}
		}
		return false;
	}

	static bool UserLikelyRequestsMutation(const FString& UserText)
	{
		const FString T = UserText.ToLower();
		if (T.IsEmpty())
		{
			return false;
		}
		auto ContainsWholeWord = [&](const FString& Needle) -> bool
		{
			if (Needle.IsEmpty())
			{
				return false;
			}
			int32 SearchFrom = 0;
			while (SearchFrom < T.Len())
			{
				const int32 Hit = T.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
				if (Hit == INDEX_NONE)
				{
					return false;
				}
				const int32 End = Hit + Needle.Len();
				const bool bLeftOk = Hit == 0 || !FChar::IsAlnum(T[Hit - 1]);
				const bool bRightOk = End >= T.Len() || !FChar::IsAlnum(T[End]);
				if (bLeftOk && bRightOk)
				{
					return true;
				}
				SearchFrom = Hit + 1;
			}
			return false;
		};
		static const TCHAR* Tokens[] = {
			TEXT("fix"), TEXT("apply"), TEXT("change"), TEXT("adjust"), TEXT("tune"),
			TEXT("reduce"), TEXT("increase"), TEXT("set "), TEXT("compile"), TEXT("save"),
			TEXT("resolve"), TEXT("make ")
		};
		for (const TCHAR* K : Tokens)
		{
			const FString Tok = FString(K).TrimStartAndEnd();
			if (!Tok.IsEmpty() && ContainsWholeWord(Tok))
			{
				return true;
			}
		}
		return false;
	}

	static bool AssistantContainsExplicitBlocker(const FString& AssistantText)
	{
		const FString T = AssistantText.ToLower();
		if (T.IsEmpty())
		{
			return false;
		}
		static const TCHAR* Tokens[] = {
			TEXT("blocked"),
			TEXT("blocker"),
			TEXT("cannot"),
			TEXT("can't"),
			TEXT("unable"),
			TEXT("failed"),
			TEXT("error"),
			TEXT("missing"),
			TEXT("not available"),
			TEXT("no access"),
			TEXT("permission"),
			TEXT("manual step"),
			TEXT("need you to"),
			TEXT("requires")
		};
		int32 Hits = 0;
		for (const TCHAR* K : Tokens)
		{
			if (T.Contains(K))
			{
				++Hits;
			}
		}
		return Hits > 0 && T.Len() >= 24;
	}

	static bool IsLikelyReadOnlyToolName(const FString& ToolName)
	{
		const FString T = ToolName.ToLower();
		return T.Contains(TEXT("_search"))
			|| T.Contains(TEXT("_query"))
			|| T.Contains(TEXT("_read"))
			|| T.Contains(TEXT("_get_"))
			|| T.Contains(TEXT("_list_"))
			|| T.Contains(TEXT("snapshot"))
			|| T.Contains(TEXT("_status"))
			|| T == TEXT("blueprint_export_ir");
	}

	static bool IsLikelyRequiredArgsTool(const FString& ToolName)
	{
		return ToolName == TEXT("asset_create")
			|| ToolName == TEXT("asset_rename")
			|| ToolName == TEXT("blueprint_get_graph_summary")
			|| ToolName == TEXT("blueprint_export_ir")
			|| ToolName == TEXT("blueprint_apply_ir")
			|| ToolName == TEXT("project_file_read_text")
			|| ToolName == TEXT("project_file_write_text")
			|| ToolName == TEXT("project_file_move");
	}

	static FString DefaultProjectReadRelativePath()
	{
		const FString ProjectFileAbs = FPaths::GetProjectFilePath();
		if (!ProjectFileAbs.IsEmpty())
		{
			FString RelToProject = ProjectFileAbs;
			if (FPaths::MakePathRelativeTo(RelToProject, *FPaths::ProjectDir()))
			{
				RelToProject.ReplaceInline(TEXT("\\"), TEXT("/"));
				return RelToProject;
			}
		}
		return TEXT("Config/DefaultEngine.ini");
	}

	static FString DefaultAssetCreatePackagePath()
	{
		return TEXT("/Game/Blueprints");
	}

	static void TryExtractBlueprintPathFromToolResult(const FString& ToolName, const FString& ToolContent, FString& OutPath)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ToolContent);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return;
		}
		auto AcceptPath = [&](const FString& P) -> bool
		{
			if (P.IsEmpty() || !P.StartsWith(TEXT("/Game/")))
			{
				return false;
			}
			OutPath = P;
			return true;
		};
		if (ToolName == TEXT("asset_index_fuzzy_search"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Matches = nullptr;
			if (Root->TryGetArrayField(TEXT("matches"), Matches) && Matches)
			{
				for (const TSharedPtr<FJsonValue>& V : *Matches)
				{
					const TSharedPtr<FJsonObject>* O = nullptr;
					if (!V.IsValid() || !V->TryGetObject(O) || !O || !(*O).IsValid())
					{
						continue;
					}
					FString ClassPath;
					(*O)->TryGetStringField(TEXT("class_path"), ClassPath);
					FString ObjPath;
					(*O)->TryGetStringField(TEXT("object_path"), ObjPath);
					if (ClassPath.Contains(TEXT("Blueprint")) && AcceptPath(ObjPath))
					{
						return;
					}
				}
			}
			return;
		}
		if (ToolName == TEXT("asset_registry_query"))
		{
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
					FString ClassName;
					(*O)->TryGetStringField(TEXT("class"), ClassName);
					FString ObjPath;
					(*O)->TryGetStringField(TEXT("object_path"), ObjPath);
					if (ClassName.Contains(TEXT("Blueprint")) && AcceptPath(ObjPath))
					{
						return;
					}
				}
			}
			return;
		}
		FString ExistingPath;
		if ((Root->TryGetStringField(TEXT("blueprint_path"), ExistingPath) || Root->TryGetStringField(TEXT("object_path"), ExistingPath))
			&& AcceptPath(ExistingPath))
		{
			return;
		}
	}

	static FString FindRecentBlueprintPathFromContextState(const FAgentContextState* State)
	{
		if (!State)
		{
			return FString();
		}
		for (int32 i = State->ToolResults.Num() - 1; i >= 0; --i)
		{
			const FToolContextEntry& E = State->ToolResults[i];
			FString Path;
			TryExtractBlueprintPathFromToolResult(E.ToolName, E.TruncatedResult, Path);
			if (!Path.IsEmpty())
			{
				return Path;
			}
		}
		return FString();
	}

	static void TryExtractAssetCreateHintsFromToolResult(
		const FString& ToolName,
		const FString& ToolContent,
		FString& OutPackagePath,
		FString& OutAssetClass)
	{
		auto AcceptPackage = [&](const FString& In) -> bool
		{
			FString P = In;
			P.TrimStartAndEndInline();
			if (P.StartsWith(TEXT("/Game")))
			{
				OutPackagePath = P;
				return true;
			}
			return false;
		};
		auto AcceptClass = [&](const FString& In) -> bool
		{
			FString C = In;
			C.TrimStartAndEndInline();
			if (!C.IsEmpty())
			{
				OutAssetClass = C;
				return true;
			}
			return false;
		};
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ToolContent);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			return;
		}
		if (ToolName == TEXT("asset_create"))
		{
			FString ObjPath;
			if (Root->TryGetStringField(TEXT("object_path"), ObjPath))
			{
				int32 Dot = INDEX_NONE;
				const FString Pkg = ObjPath.FindChar(TEXT('.'), Dot) ? ObjPath.Left(Dot) : ObjPath;
				AcceptPackage(Pkg);
			}
			return;
		}
		if (ToolName == TEXT("asset_index_fuzzy_search"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Matches = nullptr;
			if (!Root->TryGetArrayField(TEXT("matches"), Matches) || !Matches)
			{
				return;
			}
			for (const TSharedPtr<FJsonValue>& V : *Matches)
			{
				const TSharedPtr<FJsonObject>* O = nullptr;
				if (!V.IsValid() || !V->TryGetObject(O) || !O || !(*O).IsValid())
				{
					continue;
				}
				FString ObjPath;
				(*O)->TryGetStringField(TEXT("object_path"), ObjPath);
				int32 Dot = INDEX_NONE;
				const FString Pkg = ObjPath.FindChar(TEXT('.'), Dot) ? ObjPath.Left(Dot) : ObjPath;
				if (AcceptPackage(Pkg))
				{
					break;
				}
			}
		}
		if (ToolName == TEXT("asset_registry_query"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
			if (!Root->TryGetArrayField(TEXT("assets"), Assets) || !Assets)
			{
				return;
			}
			for (const TSharedPtr<FJsonValue>& V : *Assets)
			{
				const TSharedPtr<FJsonObject>* O = nullptr;
				if (!V.IsValid() || !V->TryGetObject(O) || !O || !(*O).IsValid())
				{
					continue;
				}
				FString ObjPath;
				(*O)->TryGetStringField(TEXT("object_path"), ObjPath);
				FString ClassPath;
				(*O)->TryGetStringField(TEXT("class_path"), ClassPath);
				int32 Dot = INDEX_NONE;
				const FString Pkg = ObjPath.FindChar(TEXT('.'), Dot) ? ObjPath.Left(Dot) : ObjPath;
				AcceptPackage(Pkg);
				AcceptClass(ClassPath);
				if (!OutPackagePath.IsEmpty() && !OutAssetClass.IsEmpty())
				{
					break;
				}
			}
		}
	}

	static void FindRecentAssetCreateHintsFromContextState(
		const FAgentContextState* State,
		FString& OutPackagePath,
		FString& OutAssetClass)
	{
		OutPackagePath.Reset();
		OutAssetClass.Reset();
		if (!State)
		{
			return;
		}
		for (int32 i = State->ToolResults.Num() - 1; i >= 0; --i)
		{
			const FToolContextEntry& E = State->ToolResults[i];
			TryExtractAssetCreateHintsFromToolResult(E.ToolName, E.TruncatedResult, OutPackagePath, OutAssetClass);
			if (!OutPackagePath.IsEmpty() && !OutAssetClass.IsEmpty())
			{
				return;
			}
		}
	}

	static void TryAutoFillDispatchArgsFromContext(
		const FString& InvokeName,
		FString& InOutInvokeArgs,
		IAgentContextService* ContextService,
		const FString& ProjectId,
		const FString& ThreadId)
	{
		if (!ContextService)
		{
			return;
		}
		if (InvokeName != TEXT("blueprint_get_graph_summary") && InvokeName != TEXT("blueprint_export_ir")
			&& InvokeName != TEXT("project_file_read_text")
			&& InvokeName != TEXT("asset_create"))
		{
			return;
		}
		TSharedPtr<FJsonObject> ArgsObj;
		const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(InOutInvokeArgs);
		if (!FJsonSerializer::Deserialize(R, ArgsObj) || !ArgsObj.IsValid())
		{
			return;
		}
		if (InvokeName == TEXT("project_file_read_text"))
		{
			FString RelPath;
			if ((!ArgsObj->TryGetStringField(TEXT("relative_path"), RelPath) || RelPath.TrimStartAndEnd().IsEmpty())
				&& (!ArgsObj->TryGetStringField(TEXT("file_path"), RelPath) || RelPath.TrimStartAndEnd().IsEmpty()))
			{
				ArgsObj->SetStringField(TEXT("relative_path"), DefaultProjectReadRelativePath());
			}
		}
		if (InvokeName == TEXT("blueprint_get_graph_summary") || InvokeName == TEXT("blueprint_export_ir"))
		{
			FString BpPath;
			const bool bHasBpPath = ArgsObj->TryGetStringField(TEXT("blueprint_path"), BpPath) && !BpPath.TrimStartAndEnd().IsEmpty();
			if (!bHasBpPath)
			{
				FString AliasPath;
				if (ArgsObj->TryGetStringField(TEXT("object_path"), AliasPath) && !AliasPath.TrimStartAndEnd().IsEmpty())
				{
					ArgsObj->SetStringField(TEXT("blueprint_path"), AliasPath);
				}
				else if (ArgsObj->TryGetStringField(TEXT("asset_path"), AliasPath) && !AliasPath.TrimStartAndEnd().IsEmpty())
				{
					ArgsObj->SetStringField(TEXT("blueprint_path"), AliasPath);
				}
				else if (ArgsObj->TryGetStringField(TEXT("path"), AliasPath) && !AliasPath.TrimStartAndEnd().IsEmpty())
				{
					ArgsObj->SetStringField(TEXT("blueprint_path"), AliasPath);
				}
				else if (ArgsObj->TryGetStringField(TEXT("blueprint"), AliasPath) && !AliasPath.TrimStartAndEnd().IsEmpty())
				{
					ArgsObj->SetStringField(TEXT("blueprint_path"), AliasPath);
				}
				else
				{
					const FAgentContextState* State = ContextService->GetState(ProjectId, ThreadId);
					const FString Resolved = FindRecentBlueprintPathFromContextState(State);
					if (!Resolved.IsEmpty())
					{
						ArgsObj->SetStringField(TEXT("blueprint_path"), Resolved);
					}
				}
			}
		}
		if (InvokeName == TEXT("asset_create"))
		{
			FString PackagePath;
			const bool bHasPackagePath =
				(ArgsObj->TryGetStringField(TEXT("package_path"), PackagePath) || ArgsObj->TryGetStringField(TEXT("path"), PackagePath))
				&& !PackagePath.TrimStartAndEnd().IsEmpty();
			FString AssetClass;
			const bool bHasAssetClass =
				(ArgsObj->TryGetStringField(TEXT("asset_class"), AssetClass) || ArgsObj->TryGetStringField(TEXT("class_path"), AssetClass))
				&& !AssetClass.TrimStartAndEnd().IsEmpty();
			FString AssetName;
			const bool bHasAssetName =
				(ArgsObj->TryGetStringField(TEXT("asset_name"), AssetName) || ArgsObj->TryGetStringField(TEXT("name"), AssetName))
				&& !AssetName.TrimStartAndEnd().IsEmpty();

			FString HintPackagePath;
			FString HintClassPath;
			const FAgentContextState* State = ContextService->GetState(ProjectId, ThreadId);
			FindRecentAssetCreateHintsFromContextState(State, HintPackagePath, HintClassPath);

			if (!bHasPackagePath)
			{
				ArgsObj->SetStringField(
					TEXT("package_path"),
					!HintPackagePath.IsEmpty() ? HintPackagePath : DefaultAssetCreatePackagePath());
			}
			if (!bHasAssetClass)
			{
				ArgsObj->SetStringField(
					TEXT("asset_class"),
					!HintClassPath.IsEmpty() ? HintClassPath : TEXT("/Script/Engine.Blueprint"));
			}
			if (!bHasAssetName)
			{
				ArgsObj->SetStringField(
					TEXT("asset_name"),
					FString::Printf(TEXT("NewAsset_%lld"), static_cast<long long>(FDateTime::UtcNow().ToUnixTimestamp())));
			}
		}
		FString Out;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		if (FJsonSerializer::Serialize(ArgsObj.ToSharedRef(), W))
		{
			InOutInvokeArgs = Out;
		}
	}

	static int32 CountConversationUserTurnsForMemory(const TArray<FUnrealAiConversationMessage>& Messages)
	{
		int32 Count = 0;
		for (const FUnrealAiConversationMessage& M : Messages)
		{
			if (M.Role != TEXT("user"))
			{
				continue;
			}
			// Skip harness synthetic nudges; count only real chat turns.
			if (M.Content.StartsWith(TEXT("[Harness]")))
			{
				continue;
			}
			++Count;
		}
		return Count;
	}

	/** Tools whose JSON payload is mostly an echo of assembled context — skip persisting to avoid nested duplication. */
	static bool ShouldPersistToolResultToContextState(const FString& InvokeName)
	{
		static const TCHAR* SkipToolIds[] = {
			// Returns BuildContextWindow output as `context_block`; snapshot + ranked candidates already cover this.
			TEXT("editor_state_snapshot_read"),
		};
		for (const TCHAR* Id : SkipToolIds)
		{
			if (InvokeName == Id)
			{
				return false;
			}
		}
		return true;
	}

	static bool ShouldPersistFailedCompileToolForContext(const FString& InvokeName)
	{
		return InvokeName == TEXT("blueprint_compile") || InvokeName == TEXT("cpp_project_compile");
	}

	static bool UnwrapDispatchToolCall(const FUnrealAiToolCallSpec& Tc, FString& OutInvokeName, FString& OutInvokeArgsJson, FString& OutError)
	{
		if (Tc.Name != GUnrealAiDispatchToolName)
		{
			OutInvokeName = Tc.Name;
			OutInvokeArgsJson = Tc.ArgumentsJson;
			OutError.Reset();
			return true;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Tc.ArgumentsJson);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("unreal_ai_dispatch: invalid JSON arguments");
			return false;
		}
		if (!Root->TryGetStringField(TEXT("tool_id"), OutInvokeName) || OutInvokeName.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("unreal_ai_dispatch: missing or empty tool_id");
			return false;
		}
		OutInvokeName.TrimStartAndEndInline();
		const TSharedPtr<FJsonObject>* ArgsObjPtr = nullptr;
		if (Root->TryGetObjectField(TEXT("arguments"), ArgsObjPtr) && ArgsObjPtr && (*ArgsObjPtr).IsValid())
		{
			FString Out;
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
			if (FJsonSerializer::Serialize((*ArgsObjPtr).ToSharedRef(), W))
			{
				OutInvokeArgsJson = Out;
			}
			else
			{
				OutInvokeArgsJson = TEXT("{}");
			}
		}
		else
		{
			FString ArgsStr;
			if (Root->TryGetStringField(TEXT("arguments"), ArgsStr))
			{
				OutInvokeArgsJson = ArgsStr;
			}
			else
			{
				OutInvokeArgsJson = TEXT("{}");
			}
		}
		OutError.Reset();
		return true;
	}

	static bool ShouldRetryTransientTransportError(const FString& Msg)
	{
		if (Msg.IsEmpty())
		{
			return false;
		}
		const bool bTransportFail = Msg.Contains(TEXT("HTTP request failed"), ESearchCase::IgnoreCase)
			|| Msg.Contains(TEXT("Failed (Cancelled)"), ESearchCase::IgnoreCase);
		const bool bTransient = Msg.Contains(TEXT("Cancelled"), ESearchCase::IgnoreCase)
			|| Msg.Contains(TEXT("ConnectionError"), ESearchCase::IgnoreCase)
			|| Msg.Contains(TEXT("TimedOut"), ESearchCase::IgnoreCase)
			|| Msg.Contains(TEXT("Timeout"), ESearchCase::IgnoreCase);
		return bTransportFail && bTransient;
	}

	static void MergeToolCallDeltas(TArray<FUnrealAiToolCallSpec>& Acc, const TArray<FUnrealAiToolCallSpec>& Delta)
	{
		for (const FUnrealAiToolCallSpec& D : Delta)
		{
			if (D.StreamMergeIndex >= 0)
			{
				while (Acc.Num() <= D.StreamMergeIndex)
				{
					Acc.AddDefaulted();
				}
				FUnrealAiToolCallSpec& Slot = Acc[D.StreamMergeIndex];
				Slot.StreamMergeIndex = D.StreamMergeIndex;
				if (!D.Id.IsEmpty())
				{
					Slot.Id = D.Id;
				}
				if (!D.Name.IsEmpty())
				{
					Slot.Name = D.Name;
				}
				Slot.ArgumentsJson += D.ArgumentsJson;
			}
			else
			{
				Acc.Add(D);
			}
		}
	}

	/** Padding rows created when the first streamed chunk uses index>0; never timeout those. */
	static bool IsPlaceholderPendingToolSlot(const FUnrealAiToolCallSpec& Tc)
	{
		return Tc.Name.IsEmpty() && Tc.Id.IsEmpty() && Tc.ArgumentsJson.IsEmpty();
	}

	static bool ToolArgumentsJsonOversizedForDeserialize(const FString& S)
	{
		return S.Len() > UnrealAiWaitTime::MaxToolArgumentsJsonDeserializeChars;
	}

	/**
	 * Malformed streamed tool `arguments` (e.g. truncated "\\u", stray "\\" at EOS) can trip a check() inside
	 * UE's TJsonReader (BufferReader ReaderPos + Num <= ReaderSize). Conservative scan: return true if we should
	 * skip Deserialize until more chunks arrive or the slot times out.
	 */
	static bool ToolArgumentsJsonLikelyUnsafeForEngineJsonReader(const FString& S)
	{
		const int32 N = S.Len();
		for (int32 i = 0; i < N; ++i)
		{
			if (S[i] != TEXT('\\'))
			{
				continue;
			}
			if (i + 1 >= N)
			{
				return true;
			}
			const TCHAR C = S[i + 1];
			if (C == TEXT('u') || C == TEXT('U'))
			{
				if (i + 6 > N)
				{
					return true;
				}
				for (int32 h = 2; h < 6; ++h)
				{
					if (!FChar::IsHexDigit(S[i + h]))
					{
						return true;
					}
				}
				i += 5;
				continue;
			}
			if (C == TEXT('"') || C == TEXT('\\') || C == TEXT('/') || C == TEXT('b') || C == TEXT('f')
				|| C == TEXT('n') || C == TEXT('r') || C == TEXT('t'))
			{
				++i;
				continue;
			}
			return true;
		}
		return false;
	}

	struct FAgentTurnRunner : public TSharedFromThis<FAgentTurnRunner>
	{
		FUnrealAiAgentTurnRequest Request;
		TSharedPtr<IAgentRunSink> Sink;
		IUnrealAiPersistence* Persistence = nullptr;
		IAgentContextService* ContextService = nullptr;
		FUnrealAiModelProfileRegistry* Profiles = nullptr;
		FUnrealAiToolCatalog* Catalog = nullptr;
		TSharedPtr<ILlmTransport> Transport;
		IToolExecutionHost* Tools = nullptr;
		FUnrealAiUsageTracker* UsageTracker = nullptr;
		IUnrealAiMemoryService* MemoryService = nullptr;
		TUniquePtr<FUnrealAiConversationStore> Conv;

		FUnrealAiTokenUsage UsageThisRound;
		FUnrealAiTokenUsage AccumulatedUsage;

		FGuid RunId;
		int32 LlmRound = 0;
		double LastLlmSubmitSeconds = 0.0;
		/** From model profile + env; set each DispatchLlm (backstop iterations). */
		int32 EffectiveMaxLlmRounds = GHarnessMaxLlmRoundBackstop;
		/** Last resolved token budget for this turn (for near-budget hints in CompleteToolPath). */
		int32 EffectiveMaxTurnTokensHint = 0;
		/** Model profile maxAgentTurnTokens < 0 disables the per-turn token cap (near-budget hints off). */
		bool bAgentTurnTokenBudgetDisabled = false;
		/** Retries transient HTTP-level cancellations without consuming a round. */
		int32 TransientTransportRetryCountThisRound = 0;
		/** Plan-node agent only: retries HTTP complete-without-Finish (truncated SSE) without consuming a round. */
		int32 StreamNoFinishRetryCountThisRound = 0;

		/** Streamed tool call JSON may arrive fragmented; if args never complete, we recover once per LLM round. */
		bool bStreamToolCallIncompleteTimedOut = false;
		int32 StreamToolCallIncompleteRetryCountThisRound = 0;

		FString AssistantBuffer;
		TArray<FUnrealAiToolCallSpec> PendingToolCalls;
		TArray<int32> CompletedToolCallQueue;
		TSet<int32> EnqueuedToolCallIndices;
		TSet<FString> ExecutedToolCallIds;
		TMap<int32, int32> ToolCallFirstSeenEventCount;
		TMap<int32, double> ToolCallFirstSeenSeconds;
		FString LastToolFailureSignature;
		/** Latest `suggested_correct_call` JSON from a failed tool (for repeated-validation nudge). */
		FString LastSuggestedCorrectCallSerialized;
		int32 RepeatedToolFailureCount = 0;
		TMap<FString, int32> ToolInvokeCountByName;
		TMap<FString, int32> ToolInvokeCountBySignature;
		// Counts repeated non-progress discovery/search results (e.g. empty low-confidence searches).
		// Used to trigger replan-or-stop earlier than max rounds.
		TMap<FString, int32> NonProgressEmptySearchCountByToolName;
		/** Consecutive successful invocations with identical (name+args) signature — spans LLM rounds within one user send. */
		FString LastConsecutiveIdenticalOkSignature;
		int32 ConsecutiveIdenticalOkCount = 0;
		bool bPendingFailRepeatedIdenticalOk = false;
		/** Plan node (`*_plan_*`) only: same inner tool name exceeded per-node limit; CompleteToolPath calls Fail. */
		bool bPendingFailPlanNodeSameToolBudget = false;
		FString PlanNodeSameToolBudgetName;
		int32 PlanNodeSameToolBudgetCount = 0;
		int32 PlanNodeSameToolBudgetLimit = 0;
		/** Headed scenario runner only: editor_state_snapshot_read budget; CompleteToolPath fails if exceeded. */
		bool bPendingFailHarnessSnapshotReadSpam = false;
		bool bHeadedScenarioStrictToolBudgets = false;
		bool bHeadedScenarioSyncRun = false;
		int32 EditorSnapshotReadInvokeCount = 0;
		int32 ReplanCount = 0;
		int32 QueueStepsPending = 0;
		bool bFinishSeen = false;
		int32 ActionNoToolNudgeCount = 0;
		int32 MutationFollowthroughNudgeCount = 0;
		int32 ActionIntentTurnCount = 0;
		int32 ActionTurnsWithToolCallsCount = 0;
		int32 ActionTurnsWithExplicitBlockerCount = 0;
		int32 MutationIntentTurnCount = 0;
		bool bActionIntentCounted = false;
		bool bActionToolOutcomeCounted = false;
		bool bActionBlockerOutcomeCounted = false;
		bool bMutationIntentCounted = false;
		bool bToolExecutionInProgress = false;
		/** When true, a ticker is draining streamed tools before CompleteToolPath may call DispatchLlm. */
		bool bCompleteToolPathReschedulePending = false;
		bool bAssistantToolCallMessageRecorded = false;
		bool bFinishReceived = false;
		FString FinishReason;
		int32 StreamToolEventCount = 0;
		int32 CurrentRoundToolSuccessCount = 0;
		int32 CurrentRoundToolFailCount = 0;
		bool bCurrentRoundRepeatedToolLoop = false;
		bool bCurrentRoundRepeatedEmptySearch = false;
		TArray<FString> CurrentRoundExecutedToolNames;
		std::atomic<bool> bCancelled{false};
		std::atomic<bool> bTerminal{false};
		/** Best-effort query fingerprint for tool usage JSONL (tools-expansion §2.5). */
		FString UsageQueryHash;
		int32 UnwrapFailuresThisRound = 0;
		int32 ToolRepairNudgesThisRun = 0;
		/** Ask mode: one repair nudge for empty or truncated assistant text before accepting success. */
		int32 AskAnswerRepairNudgeCount = 0;

		void HandleEvent(const FUnrealAiLlmStreamEvent& Ev);
		void DispatchLlm(bool bRetrySameRound = false);
		void CompleteToolPath();
		void StartOrContinueStreamedToolExecution();
		void ExecuteSingleToolCall(int32 ToolIndex);
		bool TryParseArgumentsJsonComplete(const FString& ArgsJson) const;
		bool IsToolCallReadyForExecution(const FUnrealAiToolCallSpec& Tc) const;
		void EnqueueNewlyCompleteCalls();
		bool CheckIncompleteToolCallTimeout(bool bForceOnFinish);
		void CompleteAssistantOnly();
		void Fail(const FString& Msg);
		void Succeed();
		void AccumulateRoundUsage();
		void EmitPlanningDecision(const FString& ModeUsed, const TArray<FString>& TriggerReasons);
		void EmitEnforcementEvent(const FString& EventType, const FString& Detail);
		void EmitEnforcementSummary();

		bool ShouldSuppressIdleAbort() const;
		void MaybeFailIfHttpCompleteWithoutFinish();
		bool IsPlanNodeAgentThread() const;
		int32 GetMaxTransientTransportRetriesPerRound() const;
		int32 GetMaxStreamNoFinishRetriesPerRound() const;
		uint32 GetEffectiveStreamNoFinishGraceMs() const;

		static constexpr int32 CharPerTokenApprox = 4;
	};

	void FAgentTurnRunner::EmitPlanningDecision(const FString& ModeUsed, const TArray<FString>& TriggerReasons)
	{
		if (Sink.IsValid())
		{
			Sink->OnPlanningDecision(ModeUsed, TriggerReasons, ReplanCount, QueueStepsPending);
		}
	}

	void FAgentTurnRunner::EmitEnforcementEvent(const FString& EventType, const FString& Detail)
	{
		if (Sink.IsValid())
		{
			Sink->OnEnforcementEvent(EventType, Detail);
		}
	}

	void FAgentTurnRunner::EmitEnforcementSummary()
	{
		if (Sink.IsValid())
		{
			Sink->OnEnforcementSummary(
				ActionIntentTurnCount,
				ActionTurnsWithToolCallsCount,
				ActionTurnsWithExplicitBlockerCount,
				ActionNoToolNudgeCount,
				MutationIntentTurnCount,
				MutationFollowthroughNudgeCount);
		}
	}

	void FAgentTurnRunner::Fail(const FString& Msg)
	{
		bool Expected = false;
		if (!bTerminal.compare_exchange_strong(Expected, true, std::memory_order_relaxed))
		{
			return;
		}
		UE_LOG(
			LogTemp,
			Display,
			TEXT("UnrealAi harness: runner_terminal_set result=failed msg=%s"),
			Msg.IsEmpty() ? TEXT("<none>") : *Msg);
		bCompleteToolPathReschedulePending = false;
		FUnrealAiEditorModalMonitor::NotifyAgentTurnEndedForSink(Sink);
		if (Sink.IsValid())
		{
			EmitEnforcementSummary();
			Sink->OnRunFinished(false, Msg);
		}
	}

	void FAgentTurnRunner::Succeed()
	{
		bool Expected = false;
		if (!bTerminal.compare_exchange_strong(Expected, true, std::memory_order_relaxed))
		{
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("UnrealAi harness: runner_terminal_set result=success"));
		bCompleteToolPathReschedulePending = false;
		if (Conv.IsValid())
		{
			Conv->SaveNow();
		}
		if (UsageTracker && Profiles && !Request.ModelProfileId.IsEmpty()
			&& (AccumulatedUsage.PromptTokens > 0 || AccumulatedUsage.CompletionTokens > 0
				|| AccumulatedUsage.TotalTokens > 0))
		{
			UsageTracker->RecordUsage(Request.ModelProfileId, AccumulatedUsage, *Profiles);
		}
		if (Request.bRecordAssistantAsStubToolResult && ContextService && Conv.IsValid())
		{
			FString LastAssistant;
			for (int32 i = Conv->GetMessages().Num() - 1; i >= 0; --i)
			{
				if (Conv->GetMessages()[i].Role == TEXT("assistant") && Conv->GetMessages()[i].ToolCalls.Num() == 0)
				{
					LastAssistant = Conv->GetMessages()[i].Content;
					break;
				}
			}
			if (!LastAssistant.IsEmpty())
			{
				ContextService->LoadOrCreate(Request.ProjectId, Request.ThreadId);
				FContextRecordPolicy Policy;
				ContextService->RecordToolResult(FName(TEXT("assistant_harness")), LastAssistant, Policy);
				ContextService->SaveNow(Request.ProjectId, Request.ThreadId);
			}
		}
		if (Sink.IsValid())
		{
			EmitEnforcementSummary();
			Sink->OnRunFinished(true, FString());
		}
		if (MemoryService && Conv.IsValid())
		{
			const TArray<FUnrealAiConversationMessage>& Ms = Conv->GetMessages();
			const int32 UserTurns = CountConversationUserTurnsForMemory(Ms);
			const int32 TurnInterval = UnrealAiRuntimeDefaults::MemoryCompactTurnInterval;
			const int32 TokenThreshold = UnrealAiRuntimeDefaults::MemoryCompactTokenThreshold;
			const int32 PromptThreshold = UnrealAiRuntimeDefaults::MemoryCompactPromptThreshold;
			const bool bByTurns = (UserTurns > 0) && ((UserTurns % TurnInterval) == 0);
			const bool bByTokens = AccumulatedUsage.TotalTokens >= TokenThreshold;
			const bool bByPromptChars = Request.UserText.Len() >= PromptThreshold;
			if (bByTurns || bByTokens || bByPromptChars)
			{
				const int32 HistoryMessages = UnrealAiRuntimeDefaults::MemoryCompactHistoryMessages;
				const int32 MaxBodyChars = UnrealAiRuntimeDefaults::MemoryCompactMaxBodyChars;
				const int32 MaxToCreate = UnrealAiRuntimeDefaults::MemoryCompactMaxCreate;
				const float MinConfidence = static_cast<float>(UnrealAiRuntimeDefaults::MemoryCompactMinConfidencePercent) / 100.0f;
				const int32 PruneMaxItems = UnrealAiRuntimeDefaults::MemoryPruneMaxItems;
				const int32 PruneRetentionDays = UnrealAiRuntimeDefaults::MemoryPruneRetentionDays;
				const float PruneMinConfidence = static_cast<float>(UnrealAiRuntimeDefaults::MemoryPruneMinConfidencePercent) / 100.0f;

				FString ConversationForCompactor;
				const int32 Start = FMath::Max(0, Ms.Num() - HistoryMessages);
				for (int32 i = Start; i < Ms.Num(); ++i)
				{
					ConversationForCompactor += Ms[i].Role + TEXT(": ") + Ms[i].Content.Left(400) + TEXT("\n");
				}
				if (ConversationForCompactor.Len() > MaxBodyChars)
				{
					ConversationForCompactor = ConversationForCompactor.Right(MaxBodyChars);
				}

				FUnrealAiMemoryCompactionInput CompactionInput;
				CompactionInput.ProjectId = Request.ProjectId;
				CompactionInput.ThreadId = Request.ThreadId;
				CompactionInput.ConversationJson = ConversationForCompactor;
				CompactionInput.bApiKeyConfigured = Profiles && Profiles->HasAnyConfiguredApiKey();
				CompactionInput.bExpectProviderGeneration = true;
				FUnrealAiMemoryCompactor Compactor(MemoryService);
				const FUnrealAiMemoryCompactionResult Cmp = Compactor.Run(CompactionInput, MaxToCreate, MinConfidence);
				const int32 Pruned = MemoryService->Prune(PruneMaxItems, PruneRetentionDays, PruneMinConfidence);
				UE_LOG(
					LogTemp,
					Display,
					TEXT("UnrealAi memory dispatch: turns=%d byTurns=%d byTokens=%d byPromptChars=%d totalTokens=%d accepted=%d pruned=%d"),
					UserTurns,
					bByTurns ? 1 : 0,
					bByTokens ? 1 : 0,
					bByPromptChars ? 1 : 0,
					AccumulatedUsage.TotalTokens,
					Cmp.Accepted,
					Pruned);
				if (Sink.IsValid())
				{
					const FString Detail = UnrealAiBackgroundOpsLog::BuildDetailJson(
						TEXT("memory_dispatch"),
						TEXT("ran"),
						Request.ProjectId,
						Request.ThreadId,
						0.0,
						[&](const TSharedPtr<FJsonObject>& O)
						{
							O->SetNumberField(TEXT("accepted"), Cmp.Accepted);
							O->SetNumberField(TEXT("pruned"), Pruned);
							O->SetNumberField(TEXT("user_turns"), UserTurns);
							O->SetBoolField(TEXT("trigger_by_turns"), bByTurns);
							O->SetBoolField(TEXT("trigger_by_tokens"), bByTokens);
							O->SetBoolField(TEXT("trigger_by_prompt_chars"), bByPromptChars);
						});
					Sink->OnEnforcementEvent(TEXT("background_op"), Detail);
				}
			}
			else
			{
				UE_LOG(
					LogTemp,
					VeryVerbose,
					TEXT("UnrealAi memory dispatch skipped: turns=%d totalTokens=%d thresholds(turnInterval=%d token=%d promptChars=%d)"),
					UserTurns,
					AccumulatedUsage.TotalTokens,
					TurnInterval,
					TokenThreshold,
					PromptThreshold);
				if (Sink.IsValid())
				{
					const FString Detail = UnrealAiBackgroundOpsLog::BuildDetailJson(
						TEXT("memory_dispatch"),
						TEXT("skipped"),
						Request.ProjectId,
						Request.ThreadId,
						0.0,
						[&](const TSharedPtr<FJsonObject>& O)
						{
							O->SetNumberField(TEXT("user_turns"), UserTurns);
							O->SetNumberField(TEXT("total_tokens"), AccumulatedUsage.TotalTokens);
							O->SetNumberField(TEXT("threshold_turn_interval"), TurnInterval);
						});
					Sink->OnEnforcementEvent(TEXT("background_op"), Detail);
				}
			}
		}
		FUnrealAiEditorModalMonitor::NotifyAgentTurnEndedForSink(Sink);
	}

	void FAgentTurnRunner::AccumulateRoundUsage()
	{
		int32 P = UsageThisRound.PromptTokens;
		int32 C = UsageThisRound.CompletionTokens;
		if (P == 0 && C == 0 && UsageThisRound.TotalTokens > 0)
		{
			P = UsageThisRound.TotalTokens / 2;
			C = UsageThisRound.TotalTokens - P;
		}
		const int32 TpmRecord = UsageThisRound.TotalTokens > 0 ? UsageThisRound.TotalTokens : (P + C);
		if (TpmRecord > 0)
		{
			UnrealAiHarnessTpmThrottle::RecordChatCompletionTokens(TpmRecord);
		}
		AccumulatedUsage.PromptTokens += P;
		AccumulatedUsage.CompletionTokens += C;
		AccumulatedUsage.TotalTokens += UsageThisRound.TotalTokens;
		UsageThisRound = FUnrealAiTokenUsage();
	}

	void FAgentTurnRunner::DispatchLlm(bool bRetrySameRound)
	{
		if (!Transport.IsValid() || !Profiles || !Catalog || !ContextService || !Conv.IsValid() || !Tools)
		{
			Fail(TEXT("Harness not configured"));
			return;
		}
		if (bCancelled.load(std::memory_order_relaxed))
		{
			Fail(TEXT("Cancelled"));
			return;
		}
		FUnrealAiModelCapabilities CapLimits;
		Profiles->GetEffectiveCapabilities(Request.ModelProfileId, CapLimits);
		// Per-turn token budget: <0 = unlimited; 0 = harness default; >0 = hard cap.
		bAgentTurnTokenBudgetDisabled = false;
		int32 EffectiveMaxTurnTokens = CapLimits.MaxAgentTurnTokens;
		if (EffectiveMaxTurnTokens < 0)
		{
			bAgentTurnTokenBudgetDisabled = true;
			EffectiveMaxTurnTokens = 0;
			EffectiveMaxTurnTokensHint = 0;
		}
		else
		{
			if (EffectiveMaxTurnTokens == 0)
			{
				EffectiveMaxTurnTokens = GHarnessDefaultMaxTurnTokens;
			}
			EffectiveMaxTurnTokensHint = EffectiveMaxTurnTokens;
		}
		int32 ParsedMax = CapLimits.MaxAgentLlmRounds > 0 ? CapLimits.MaxAgentLlmRounds : GHarnessMaxLlmRoundBackstop;
		ParsedMax = FMath::Clamp(ParsedMax, 1, GHarnessMaxLlmRoundBackstop);
		if (Request.LlmRoundBudgetFloor > 0)
		{
			ParsedMax = FMath::Max(ParsedMax, Request.LlmRoundBudgetFloor);
		}
		ParsedMax = FMath::Clamp(ParsedMax, 1, GHarnessMaxLlmRoundBackstop);
		EffectiveMaxLlmRounds = ParsedMax;
		if (UnrealAiPlanPlannerHarness::IsPlanPlannerPass(Request.Mode))
		{
			EffectiveMaxLlmRounds = FMath::Min(EffectiveMaxLlmRounds, UnrealAiWaitTime::PlannerMaxLlmRounds);
		}
		else if (Request.Mode == EUnrealAiAgentMode::Agent && Request.ThreadId.Contains(TEXT("_plan_")))
		{
			EffectiveMaxLlmRounds = FMath::Min(EffectiveMaxLlmRounds, UnrealAiWaitTime::PlanNodeMaxLlmRounds);
		}

		if (!bRetrySameRound)
		{
			if (RepeatedToolFailureCount >= GHarnessRepeatedFailureStopCount)
			{
				Fail(FString::Printf(
					TEXT("Stopped after %d consecutive identical tool failures (%s). Fix arguments, use suggested_correct_call, or state a concise blocker."),
					GHarnessRepeatedFailureStopCount,
					LastToolFailureSignature.IsEmpty() ? TEXT("unknown signature") : *LastToolFailureSignature));
				return;
			}
			if (!bAgentTurnTokenBudgetDisabled && EffectiveMaxTurnTokens > 0
				&& AccumulatedUsage.TotalTokens >= EffectiveMaxTurnTokens)
			{
				Fail(FString::Printf(
					TEXT("Agent turn token budget exceeded (%d tokens, limit %d). Raise maxAgentTurnTokens in the model profile or set it to -1 for unlimited."),
					AccumulatedUsage.TotalTokens,
					EffectiveMaxTurnTokens));
				return;
			}
			if (LlmRound >= EffectiveMaxLlmRounds)
			{
				const bool bHasRepeatValidationFailures = RepeatedToolFailureCount >= 2;
				Fail(FString::Printf(
					TEXT("LLM round backstop reached (%d rounds). Primary limits are repeated identical tool failures (%d max) and token budget; increase maxAgentLlmRounds in the model profile only if needed. %s"),
					EffectiveMaxLlmRounds,
					GHarnessRepeatedFailureStopCount,
					bHasRepeatValidationFailures
						? TEXT("Repeated tool validation failures were detected; provide a concise blocked summary with the last failing tool+args. ")
						: TEXT("")));
				return;
			}
			++LlmRound;
			TransientTransportRetryCountThisRound = 0;
			StreamNoFinishRetryCountThisRound = 0;
			bStreamToolCallIncompleteTimedOut = false;
			StreamToolCallIncompleteRetryCountThisRound = 0;
			if (LlmRound > 1 && Sink.IsValid())
			{
				Sink->OnRunContinuation(LlmRound - 1, EffectiveMaxLlmRounds);
			}
		}
		UsageThisRound = FUnrealAiTokenUsage();
		AssistantBuffer.Reset();
		PendingToolCalls.Reset();
		CompletedToolCallQueue.Reset();
		EnqueuedToolCallIndices.Reset();
		ExecutedToolCallIds.Reset();
		ToolCallFirstSeenEventCount.Reset();
		ToolCallFirstSeenSeconds.Reset();
		bFinishSeen = false;
		bFinishReceived = false;
		FinishReason.Reset();
		StreamToolEventCount = 0;
		bToolExecutionInProgress = false;
		bAssistantToolCallMessageRecorded = false;
		CurrentRoundToolSuccessCount = 0;
		CurrentRoundToolFailCount = 0;
		bCurrentRoundRepeatedToolLoop = false;
		bCurrentRoundRepeatedEmptySearch = false;
		CurrentRoundExecutedToolNames.Reset();
		UnwrapFailuresThisRound = 0;
		LastSuggestedCorrectCallSerialized.Reset();

		FUnrealAiLlmRequest LlmReq;
		FString BuildErr;
		TArray<FString> ContextUserMsgs;
		const FString RetrievalTurnKey = FString::Printf(
			TEXT("%s|%s|round_%d"),
			*Request.ThreadId,
			*RunId.ToString(EGuidFormats::DigitsWithHyphens),
			LlmRound);
		const FString& ComplexityQuery = Request.ContextComplexityUserText.IsEmpty() ? Request.UserText : Request.ContextComplexityUserText;
		ContextService->StartRetrievalPrefetch(RetrievalTurnKey, ComplexityQuery);
		if (Sink.IsValid())
		{
			const FString Detail = UnrealAiBackgroundOpsLog::BuildDetailJson(
				TEXT("retrieval_prefetch"),
				TEXT("started"),
				Request.ProjectId,
				Request.ThreadId,
				0.0,
				[&RetrievalTurnKey](const TSharedPtr<FJsonObject>& O)
				{
					O->SetStringField(TEXT("turn_key"), RetrievalTurnKey);
				});
			Sink->OnEnforcementEvent(TEXT("background_op"), Detail);
		}
		FUnrealAiToolSurfaceTelemetry ToolSurfaceTel;
		if (!UnrealAiTurnLlmRequestBuilder::Build(
				Request,
				LlmRound,
				EffectiveMaxLlmRounds,
				RetrievalTurnKey,
				ContextService,
				Profiles,
				Catalog,
				Conv.Get(),
				CharPerTokenApprox,
				LlmReq,
				ContextUserMsgs,
				BuildErr,
				&ToolSurfaceTel))
		{
			Fail(BuildErr);
			return;
		}
		UsageQueryHash = ToolSurfaceTel.QueryHash;
		if (UsageQueryHash.IsEmpty())
		{
			UsageQueryHash = UnrealAiAgentHarnessPriv::HashUtf8Query(ComplexityQuery);
		}
		if (Sink.IsValid() && !ToolSurfaceTel.ToolSurfaceMode.Equals(TEXT("off")))
		{
			FString ExpandedCsv;
			for (const FString& Id : ToolSurfaceTel.ExpandedToolIds)
			{
				if (!ExpandedCsv.IsEmpty())
				{
					ExpandedCsv += TEXT(",");
				}
				ExpandedCsv += Id;
			}
			Sink->OnEnforcementEvent(
				TEXT("tool_surface_metrics"),
				FString::Printf(
					TEXT("mode=%s eligible=%d roster_chars=%d budget_left=%d appendix_budget=%d latency_ms=%d k=%d qshape=%s qhash=%s expanded=%s"),
					*ToolSurfaceTel.ToolSurfaceMode,
					ToolSurfaceTel.EligibleCount,
					ToolSurfaceTel.RosterChars,
					ToolSurfaceTel.BudgetRemaining,
					ToolSurfaceTel.AppendixCharBudgetLimit,
					ToolSurfaceTel.RetrievalLatencyMs,
					ToolSurfaceTel.KEffective,
					*ToolSurfaceTel.QueryShape,
					*ToolSurfaceTel.QueryHash,
					*ExpandedCsv));
			if (UnrealAiRuntimeDefaults::ToolSelectorVerboseLogEnabled && ToolSurfaceTel.RankedTools.Num() > 0)
			{
				const FString RanksJson = UnrealAiAgentHarnessPriv::BuildToolSelectorRanksJson(ToolSurfaceTel);
				if (!RanksJson.IsEmpty())
				{
					Sink->OnEnforcementEvent(TEXT("tool_selector_ranks"), RanksJson);
				}
			}
		}
		if (Sink.IsValid() && ContextUserMsgs.Num() > 0)
		{
			Sink->OnContextUserMessages(ContextUserMsgs);
		}

		// Optional pacing to reduce request burstiness and provider 429 rate limits.
		const int32 MinDelayMs = FMath::Clamp(UnrealAiWaitTime::HarnessRoundMinDelayMs, 0, 5000);
		if (MinDelayMs > 0 && LastLlmSubmitSeconds > 0.0)
		{
			const double NowSec = FPlatformTime::Seconds();
			const double ElapsedMs = (NowSec - LastLlmSubmitSeconds) * 1000.0;
			if (ElapsedMs < static_cast<double>(MinDelayMs))
			{
				const float SleepSec = static_cast<float>((static_cast<double>(MinDelayMs) - ElapsedMs) / 1000.0);
				if (SleepSec > 0.0f)
				{
					UE_LOG(LogTemp, Display, TEXT("UnrealAi harness: pacing LLM dispatch by %.2fs (min_delay_ms=%d)."), SleepSec, MinDelayMs);
					FPlatformProcess::SleepNoStats(SleepSec);
				}
			}
		}

		UnrealAiHarnessTpmThrottle::MaybeWaitBeforeChatRequest(LlmReq, CharPerTokenApprox);

		if (Sink.IsValid())
		{
			Sink->OnLlmRequestPreparedForHttp(Request, RunId, LlmRound, EffectiveMaxLlmRounds, LlmReq);
		}

		const TSharedPtr<FAgentTurnRunner> Self = AsShared();
		UnrealAiHarnessProgressTelemetry::NotifyLlmSubmit();
		UE_LOG(LogTemp, Display,
			TEXT("UnrealAi harness: LLM round %d/%d — submitting HTTP request (stream=%s). "
				 "No viewport progress until tools execute."),
			LlmRound,
			EffectiveMaxLlmRounds,
			LlmReq.bStream ? TEXT("yes") : TEXT("no"));
		const FUnrealAiLlmInvocationService Invoker(Transport);
		Invoker.SubmitStreamChatCompletion(
			LlmReq,
			FUnrealAiLlmStreamCallback::CreateLambda(
				[Self](const FUnrealAiLlmStreamEvent& Ev)
				{
					AsyncTask(ENamedThreads::GameThread,
							  [Self, Ev]()
							  {
								  Self->HandleEvent(Ev);
							  });
				}));
		LastLlmSubmitSeconds = FPlatformTime::Seconds();
	}

	bool FAgentTurnRunner::TryParseArgumentsJsonComplete(const FString& ArgsJson) const
	{
		FString Trimmed = ArgsJson;
		Trimmed.TrimStartAndEndInline();
		if (Trimmed.IsEmpty())
		{
			return false;
		}
		if (ToolArgumentsJsonOversizedForDeserialize(Trimmed))
		{
			return false;
		}
		if (ToolArgumentsJsonLikelyUnsafeForEngineJsonReader(Trimmed))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> ReaderObj = TJsonReaderFactory<>::Create(Trimmed);
		if (FJsonSerializer::Deserialize(ReaderObj, Obj) && Obj.IsValid())
		{
			return true;
		}
		TArray<TSharedPtr<FJsonValue>> Arr;
		const TSharedRef<TJsonReader<>> ReaderArr = TJsonReaderFactory<>::Create(Trimmed);
		return FJsonSerializer::Deserialize(ReaderArr, Arr);
	}

	bool FAgentTurnRunner::IsToolCallReadyForExecution(const FUnrealAiToolCallSpec& Tc) const
	{
		return !Tc.Name.TrimStartAndEnd().IsEmpty() && TryParseArgumentsJsonComplete(Tc.ArgumentsJson);
	}

	bool FAgentTurnRunner::ShouldSuppressIdleAbort() const
	{
		if (bTerminal.load(std::memory_order_relaxed) || bCancelled.load(std::memory_order_relaxed))
		{
			return false;
		}
		if (bToolExecutionInProgress)
		{
			return true;
		}
		// Plan-mode planner uses ToolsJson []; do not let spurious incomplete streamed tool slots block idle abort.
		if (UnrealAiPlanPlannerHarness::IsPlanPlannerPass(Request.Mode))
		{
			return CompletedToolCallQueue.Num() > 0;
		}
		if (CompletedToolCallQueue.Num() > 0)
		{
			return true;
		}
		for (int32 I = 0; I < PendingToolCalls.Num(); ++I)
		{
			const FUnrealAiToolCallSpec& Tc = PendingToolCalls[I];
			if (UnrealAiAgentHarnessPriv::IsPlaceholderPendingToolSlot(Tc))
			{
				continue;
			}
			if (!Tc.Id.IsEmpty() && ExecutedToolCallIds.Contains(Tc.Id))
			{
				continue;
			}
			if (!IsToolCallReadyForExecution(Tc))
			{
				return true;
			}
		}
		return false;
	}

	void FAgentTurnRunner::EnqueueNewlyCompleteCalls()
	{
		for (int32 I = 0; I < PendingToolCalls.Num(); ++I)
		{
			const FUnrealAiToolCallSpec& Tc = PendingToolCalls[I];
			if (!IsToolCallReadyForExecution(Tc))
			{
				continue;
			}
			if (EnqueuedToolCallIndices.Contains(I))
			{
				continue;
			}
			if (!Tc.Id.IsEmpty() && ExecutedToolCallIds.Contains(Tc.Id))
			{
				continue;
			}
			CompletedToolCallQueue.Add(I);
			EnqueuedToolCallIndices.Add(I);
			EmitEnforcementEvent(TEXT("stream_tool_ready"), FString::Printf(TEXT("index=%d id=%s name=%s"), I, *Tc.Id, *Tc.Name));
		}
	}

	bool FAgentTurnRunner::CheckIncompleteToolCallTimeout(const bool bForceOnFinish)
	{
		const int32 MaxEvents = UnrealAiWaitTime::StreamToolIncompleteMaxEvents;
		const int32 MaxMs = UnrealAiWaitTime::StreamToolIncompleteMaxMs;
		const double NowSec = FPlatformTime::Seconds();
		for (int32 I = 0; I < PendingToolCalls.Num(); ++I)
		{
			const FUnrealAiToolCallSpec& Tc = PendingToolCalls[I];
			if (IsToolCallReadyForExecution(Tc))
			{
				continue;
			}
			if (UnrealAiAgentHarnessPriv::IsPlaceholderPendingToolSlot(Tc))
			{
				continue;
			}
			if (!Tc.Id.IsEmpty() && ExecutedToolCallIds.Contains(Tc.Id))
			{
				continue;
			}
			// Key by pending-array index only: matches MergeToolCallDeltas padding (index>0 first chunk leaves empty slot 0).
			const int32 FirstSeenEvent = ToolCallFirstSeenEventCount.FindRef(I);
			const double FirstSeenSec = ToolCallFirstSeenSeconds.FindRef(I);
			const int32 AgeEvents = FMath::Max(0, StreamToolEventCount - FirstSeenEvent);
			const int32 AgeMs = FirstSeenSec > 0.0 ? static_cast<int32>((NowSec - FirstSeenSec) * 1000.0) : 0;
			if (!bForceOnFinish && AgeEvents < MaxEvents && AgeMs < MaxMs)
			{
				continue;
			}
			EmitEnforcementEvent(
				TEXT("stream_tool_call_incomplete_timeout"),
				FString::Printf(TEXT("index=%d id=%s name=%s age_events=%d age_ms=%d"), I, *Tc.Id, *Tc.Name, AgeEvents, AgeMs));
			if (!bForceOnFinish)
			{
				// Don't hard-fail during the live stream: SSE can be fragmented and might still complete after thresholds.
				// We mark once and wait until HTTP finishes, where we can retry safely.
				bStreamToolCallIncompleteTimedOut = true;
				return false;
			}

			// On Finish: recover with a single repair nudge (bounded per LLM round).
			if (StreamToolCallIncompleteRetryCountThisRound < GHarnessStreamToolCallIncompleteMaxRetriesPerRound)
			{
				++StreamToolCallIncompleteRetryCountThisRound;
				bStreamToolCallIncompleteTimedOut = false;

				if (Conv.IsValid())
				{
					FUnrealAiConversationMessage RepairNudge;
					RepairNudge.Role = TEXT("user");
					RepairNudge.Content = TEXT(
						"[Harness][reason=stream_tool_call_incomplete] The previous tool call arguments were truncated. "
						"Retry the same `unreal_ai_dispatch` tool call with complete JSON arguments (do not truncate; ensure `tool_id` and the full `arguments` object are present).");

					if (Catalog)
					{
						const FString RepairSchemaLine = UnrealAiAgentHarnessPriv::BuildToolRepairUserLine(Catalog, PendingToolCalls);
						if (!RepairSchemaLine.IsEmpty())
						{
							RepairNudge.Content += TEXT("\n");
							RepairNudge.Content += RepairSchemaLine;
						}
					}
					Conv->GetMessagesMutable().Add(RepairNudge);
				}

				if (Sink.IsValid())
				{
					EmitEnforcementEvent(TEXT("stream_tool_call_incomplete_retry"), FString::Printf(TEXT("retry=%d/%d"), StreamToolCallIncompleteRetryCountThisRound, GHarnessStreamToolCallIncompleteMaxRetriesPerRound));
				}

				// Retry the same round (retry does not increment LlmRound).
				DispatchLlm(true);
				return true;
			}

			Fail(FString::Printf(
				TEXT("Streamed tool call did not reach complete JSON arguments in time (index=%d, id=%s, name=%s)."),
				I,
				*Tc.Id,
				*Tc.Name));
			return true;
		}
		return false;
	}

	bool FAgentTurnRunner::IsPlanNodeAgentThread() const
	{
		return Request.Mode == EUnrealAiAgentMode::Agent && Request.ThreadId.Contains(TEXT("_plan_"));
	}

	int32 FAgentTurnRunner::GetMaxTransientTransportRetriesPerRound() const
	{
		return IsPlanNodeAgentThread() ? FMath::Max(0, UnrealAiWaitTime::PlanNodeTransientHttpMaxRetries) : 0;
	}

	int32 FAgentTurnRunner::GetMaxStreamNoFinishRetriesPerRound() const
	{
		if (IsPlanNodeAgentThread())
		{
			return FMath::Max(0, UnrealAiWaitTime::PlanNodeStreamNoFinishMaxRetries);
		}
		if (bHeadedScenarioSyncRun)
		{
			return FMath::Clamp(UnrealAiWaitTime::HeadedAgentStreamNoFinishMaxRetries, 0, 1);
		}
		return 0;
	}

	uint32 FAgentTurnRunner::GetEffectiveStreamNoFinishGraceMs() const
	{
		if (IsPlanNodeAgentThread() && UnrealAiWaitTime::PlanNodeStreamNoFinishGraceMs > 0)
		{
			return UnrealAiWaitTime::PlanNodeStreamNoFinishGraceMs;
		}
		if (bHeadedScenarioSyncRun && UnrealAiWaitTime::HeadedAgentStreamNoFinishGraceMs > 0)
		{
			return UnrealAiWaitTime::HeadedAgentStreamNoFinishGraceMs;
		}
		return UnrealAiWaitTime::HarnessStreamNoFinishGraceMs;
	}

	void FAgentTurnRunner::MaybeFailIfHttpCompleteWithoutFinish()
	{
		if (bCancelled.load(std::memory_order_relaxed) || bTerminal.load(std::memory_order_relaxed) || bFinishReceived)
		{
			return;
		}
		if (!Transport.IsValid() || Transport->HasActiveRequest())
		{
			return;
		}
		const uint32 GraceMs = GetEffectiveStreamNoFinishGraceMs();
		if (GraceMs == 0)
		{
			return;
		}
		double StreamIdle = -1.0;
		double HttpIdle = -1.0;
		double LlmSubmitIdle = -1.0;
		double UnusedRawAssistantIdle = -1.0;
		UnrealAiHarnessProgressTelemetry::GetAssistantOrThinkingIdleSeconds(StreamIdle);
		UnrealAiHarnessProgressTelemetry::GetStreamIdleSeconds(UnusedRawAssistantIdle, HttpIdle, LlmSubmitIdle);
		(void)UnusedRawAssistantIdle;
		(void)LlmSubmitIdle;
		const double ThreshSec = static_cast<double>(GraceMs) / 1000.0;
		if (StreamIdle < 0.0 || HttpIdle < 0.0)
		{
			return;
		}
		if (StreamIdle < ThreshSec || HttpIdle < ThreshSec)
		{
			return;
		}
		const int32 MaxNoFinishRetries = GetMaxStreamNoFinishRetriesPerRound();
		if (MaxNoFinishRetries > 0 && !bToolExecutionInProgress)
		{
			bool bIncompleteToolStream = false;
			for (int32 Idx = 0; Idx < PendingToolCalls.Num(); ++Idx)
			{
				const FUnrealAiToolCallSpec& Slot = PendingToolCalls[Idx];
				if (UnrealAiAgentHarnessPriv::IsPlaceholderPendingToolSlot(Slot))
				{
					continue;
				}
				if (!IsToolCallReadyForExecution(Slot))
				{
					bIncompleteToolStream = true;
					break;
				}
			}
			if (!bIncompleteToolStream && StreamNoFinishRetryCountThisRound < MaxNoFinishRetries)
			{
				++StreamNoFinishRetryCountThisRound;
				EmitEnforcementEvent(
					TEXT("harness_stream_no_finish_retry"),
					FString::Printf(
						TEXT("attempt=%d max=%d grace_ms=%u"),
						StreamNoFinishRetryCountThisRound,
						MaxNoFinishRetries,
						GraceMs));
				DispatchLlm(true);
				return;
			}
		}
		Fail(TEXT("HTTP response completed without a terminal stream Finish event (incomplete SSE or provider error)."));
	}

	void FAgentTurnRunner::HandleEvent(const FUnrealAiLlmStreamEvent& Ev)
	{
		if (bCancelled.load(std::memory_order_relaxed) || bTerminal.load(std::memory_order_relaxed))
		{
			return;
		}
		if (!Sink.IsValid())
		{
			return;
		}
		switch (Ev.Type)
		{
		case EUnrealAiLlmStreamEventType::AssistantDelta:
			AssistantBuffer += Ev.DeltaText;
			Sink->OnAssistantDelta(Ev.DeltaText);
			break;
		case EUnrealAiLlmStreamEventType::ThinkingDelta:
			Sink->OnThinkingDelta(Ev.DeltaText);
			break;
		case EUnrealAiLlmStreamEventType::ToolCalls:
			++StreamToolEventCount;
			MergeToolCallDeltas(PendingToolCalls, Ev.ToolCalls);
			for (int32 Idx = 0; Idx < PendingToolCalls.Num(); ++Idx)
			{
				const FUnrealAiToolCallSpec& Slot = PendingToolCalls[Idx];
				if (IsToolCallReadyForExecution(Slot))
				{
					continue;
				}
				if (UnrealAiAgentHarnessPriv::IsPlaceholderPendingToolSlot(Slot))
				{
					continue;
				}
				ToolCallFirstSeenEventCount.FindOrAdd(Idx, StreamToolEventCount);
				ToolCallFirstSeenSeconds.FindOrAdd(Idx, FPlatformTime::Seconds());
			}
			EnqueueNewlyCompleteCalls();
			StartOrContinueStreamedToolExecution();
			if (CheckIncompleteToolCallTimeout(false))
			{
				return;
			}
			break;
		case EUnrealAiLlmStreamEventType::Finish:
			UsageThisRound.PromptTokens = FMath::Max(UsageThisRound.PromptTokens, Ev.Usage.PromptTokens);
			UsageThisRound.CompletionTokens = FMath::Max(UsageThisRound.CompletionTokens, Ev.Usage.CompletionTokens);
			UsageThisRound.TotalTokens = FMath::Max(UsageThisRound.TotalTokens, Ev.Usage.TotalTokens);
			bFinishSeen = true;
			bFinishReceived = true;
			FinishReason = Ev.FinishReason;
			if (Ev.FinishReason == TEXT("tool_calls") && PendingToolCalls.Num() == 0)
			{
				Fail(TEXT("Model requested tools but sent no tool_calls"));
				break;
			}
			// Planner pass: ToolsJson is []; streamed tool_calls are non-actionable — always assistant-only terminal.
			if (UnrealAiPlanPlannerHarness::IsPlanPlannerPass(Request.Mode))
			{
				if (PendingToolCalls.Num() > 0 || Ev.FinishReason == TEXT("tool_calls"))
				{
					EmitEnforcementEvent(
						TEXT("plan_finish_ignore_streamed_tool_calls"),
						FString::Printf(TEXT("finish_reason=%s pending_slots=%d"), *Ev.FinishReason, PendingToolCalls.Num()));
				}
				PendingToolCalls.Reset();
				UE_LOG(
					LogTemp,
					Verbose,
					TEXT("UnrealAi harness: Plan Finish finish_reason=%s -> CompleteAssistantOnly (DAG-only)"),
					*Ev.FinishReason);
				CompleteAssistantOnly();
				break;
			}
			if (Ev.FinishReason != TEXT("tool_calls"))
			{
				PendingToolCalls.Reset();
			}
			// Tool execution + CompleteToolPath deferral live in CompleteToolPath / CompleteAssistantOnly — do not
			// call StartOrContinueStreamedToolExecution here first, or we can run tools then enter CompleteToolPath
			// while bToolExecutionInProgress is still true (ticker yield) and abort the stream via DispatchLlm.
			if (CheckIncompleteToolCallTimeout(true))
			{
				return;
			}
			if (Ev.FinishReason == TEXT("tool_calls") || PendingToolCalls.Num() > 0 || ExecutedToolCallIds.Num() > 0)
			{
				CompleteToolPath();
			}
			else
			{
				CompleteAssistantOnly();
			}
			break;
		case EUnrealAiLlmStreamEventType::Error:
		{
			const FString ErrorMsg = Ev.ErrorMessage.IsEmpty() ? TEXT("LLM error") : Ev.ErrorMessage;
			if (!bCancelled.load(std::memory_order_relaxed)
				&& ShouldRetryTransientTransportError(ErrorMsg)
				&& TransientTransportRetryCountThisRound < GetMaxTransientTransportRetriesPerRound())
			{
				++TransientTransportRetryCountThisRound;
				DispatchLlm(true);
				break;
			}
			Fail(ErrorMsg);
			break;
		}
		default:
			break;
		}
		MaybeFailIfHttpCompleteWithoutFinish();
	}

	void FAgentTurnRunner::StartOrContinueStreamedToolExecution()
	{
		if (bToolExecutionInProgress)
		{
			return;
		}
		bToolExecutionInProgress = true;
		while (CompletedToolCallQueue.Num() > 0)
		{
			const int32 ToolIndex = CompletedToolCallQueue[0];
			CompletedToolCallQueue.RemoveAt(0);
			ExecuteSingleToolCall(ToolIndex);
			if (bTerminal.load(std::memory_order_relaxed))
			{
				break;
			}
			// Headed editor runs: yield between tools so Slate/engine can tick. Without this,
			// chained streamed tool calls (Blueprint Builder graph ops) monopolize one game-thread
			// slice and the whole editor looks frozen. Scenario sync runs keep the old tight loop.
			if (!bHeadedScenarioSyncRun && CompletedToolCallQueue.Num() > 0)
			{
				const TWeakPtr<FAgentTurnRunner> WeakSelf = AsShared();
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
					[WeakSelf](float /*DeltaTime*/) -> bool
					{
						const TSharedPtr<FAgentTurnRunner> Self = WeakSelf.Pin();
						if (!Self.IsValid())
						{
							return false;
						}
						// Release the guard before re-entering; it stayed true while we yielded.
						Self->bToolExecutionInProgress = false;
						Self->StartOrContinueStreamedToolExecution();
						return false;
					}));
				return;
			}
		}
		bToolExecutionInProgress = false;
	}

	void FAgentTurnRunner::ExecuteSingleToolCall(const int32 ToolIndex)
	{
		if (!PendingToolCalls.IsValidIndex(ToolIndex))
		{
			return;
		}
		const FUnrealAiToolCallSpec& Tc = PendingToolCalls[ToolIndex];
		if (!Tc.Id.IsEmpty() && ExecutedToolCallIds.Contains(Tc.Id))
		{
			return;
		}
		if (!bAssistantToolCallMessageRecorded && Conv.IsValid())
		{
			FUnrealAiConversationMessage Am;
			Am.Role = TEXT("assistant");
			Am.Content = AssistantBuffer;
			Am.ToolCalls = PendingToolCalls;
			Conv->GetMessagesMutable().Add(Am);
			bAssistantToolCallMessageRecorded = true;
		}
		FString InvokeName;
		FString InvokeArgs;
		FString UnwrapErr;
		if (!UnwrapDispatchToolCall(Tc, InvokeName, InvokeArgs, UnwrapErr))
		{
			++UnwrapFailuresThisRound;
			if (Sink.IsValid())
			{
				Sink->OnToolCallStarted(Tc.Name, Tc.Id, Tc.ArgumentsJson);
				Sink->OnToolCallFinished(Tc.Name, Tc.Id, false, UnrealAiTruncateForUi(UnwrapErr), nullptr);
			}
			if (Conv.IsValid())
			{
				FUnrealAiConversationMessage Tm;
				Tm.Role = TEXT("tool");
				Tm.ToolCallId = Tc.Id;
				Tm.Content = UnwrapErr;
				Conv->GetMessagesMutable().Add(Tm);
			}
			++CurrentRoundToolFailCount;
			if (!Tc.Id.IsEmpty())
			{
				ExecutedToolCallIds.Add(Tc.Id);
			}
			EmitEnforcementEvent(TEXT("stream_tool_exec_done"), FString::Printf(TEXT("id=%s ok=0"), *Tc.Id));
			if (UnrealAiRuntimeDefaults::ToolUsageLogEnabled)
			{
				UnrealAiToolUsageEventLogger::AppendOperationalEvent(UsageQueryHash, Tc.Name, false, Request.ThreadId);
			}
			UnrealAiToolUsagePrior::NoteSessionOutcome(Tc.Name, false);
			LastConsecutiveIdenticalOkSignature.Reset();
			ConsecutiveIdenticalOkCount = 0;
			return;
		}
		UnrealAiAgentHarnessPriv::TryAutoFillDispatchArgsFromContext(
			InvokeName,
			InvokeArgs,
			ContextService,
			Request.ProjectId,
			Request.ThreadId);
		{
			FString TrimArgs = InvokeArgs;
			TrimArgs.TrimStartAndEndInline();
			if (TrimArgs == TEXT("{}") && UnrealAiAgentHarnessPriv::IsLikelyRequiredArgsTool(InvokeName))
			{
				const FString EmptySig = (InvokeName + TEXT("|{}")).ToLower();
				const int32 Prior = ToolInvokeCountBySignature.FindRef(EmptySig);
				if (Prior >= 1)
				{
					EmitEnforcementEvent(TEXT("required_args_empty_repeat_abort"), EmptySig.Left(240));
					Fail(FString::Printf(
						TEXT("Stopped repeated empty-args invocation for required-arg tool (%s). Provide required fields or use suggested_correct_call."),
						*InvokeName));
					return;
				}
			}
		}
		if (Sink.IsValid())
		{
			Sink->OnToolCallStarted(InvokeName, Tc.Id, InvokeArgs);
		}
		if (bHeadedScenarioStrictToolBudgets && InvokeName == TEXT("editor_state_snapshot_read"))
		{
			++EditorSnapshotReadInvokeCount;
			if (EditorSnapshotReadInvokeCount > UnrealAiWaitTime::HarnessScenarioMaxReadSnapshotToolsPerTurn)
			{
				const FString InvokeSignatureBlocked = (InvokeName + TEXT("|") + InvokeArgs.Left(220)).ToLower();
				const int32 SigCountBlocked = ToolInvokeCountBySignature.FindOrAdd(InvokeSignatureBlocked) + 1;
				ToolInvokeCountBySignature.FindOrAdd(InvokeSignatureBlocked) = SigCountBlocked;
				const int32 PrevNameCountSnap = ToolInvokeCountByName.FindRef(InvokeName);
				ToolInvokeCountByName.FindOrAdd(InvokeName) = PrevNameCountSnap + 1;
				const FString BlockMsg = FString::Printf(
					TEXT("[Harness][reason=harness_read_tool_spam] Blocked: editor_state_snapshot_read exceeded headed scenario limit (%d per turn)."),
					UnrealAiWaitTime::HarnessScenarioMaxReadSnapshotToolsPerTurn);
				if (Sink.IsValid())
				{
					Sink->OnToolCallFinished(InvokeName, Tc.Id, false, UnrealAiTruncateForUi(BlockMsg), nullptr);
				}
				if (Conv.IsValid())
				{
					FUnrealAiConversationMessage Tm;
					Tm.Role = TEXT("tool");
					Tm.ToolCallId = Tc.Id;
					Tm.Content = BlockMsg;
					Conv->GetMessagesMutable().Add(Tm);
				}
				++CurrentRoundToolFailCount;
				CurrentRoundExecutedToolNames.Add(InvokeName);
				if (!Tc.Id.IsEmpty())
				{
					ExecutedToolCallIds.Add(Tc.Id);
				}
				bPendingFailHarnessSnapshotReadSpam = true;
				bCurrentRoundRepeatedToolLoop = true;
				EmitEnforcementEvent(TEXT("stream_tool_exec_done"), FString::Printf(TEXT("id=%s ok=0 blocked=snapshot_read_budget"), *Tc.Id));
				EmitEnforcementEvent(
					TEXT("harness_snapshot_read_blocked"),
					FString::Printf(TEXT("invocation=%d"), EditorSnapshotReadInvokeCount));
				if (UnrealAiRuntimeDefaults::ToolUsageLogEnabled)
				{
					UnrealAiToolUsageEventLogger::AppendOperationalEvent(UsageQueryHash, InvokeName, false, Request.ThreadId);
				}
				UnrealAiToolUsagePrior::NoteSessionOutcome(InvokeName, false);
				LastConsecutiveIdenticalOkSignature.Reset();
				ConsecutiveIdenticalOkCount = 0;
				LastToolFailureSignature = InvokeSignatureBlocked;
				RepeatedToolFailureCount = FMath::Max(RepeatedToolFailureCount, 1);
				return;
			}
		}
		const int32 PrevNameCount = ToolInvokeCountByName.FindRef(InvokeName);
		const int32 PlanSameToolLimit = IsLikelyReadOnlyToolName(InvokeName)
			? UnrealAiWaitTime::PlanNodeMaxInvocationsSameToolReadOnly
			: UnrealAiWaitTime::PlanNodeMaxInvocationsSameTool;
		if (Request.ThreadId.Contains(TEXT("_plan_")) && Request.Mode == EUnrealAiAgentMode::Agent
			&& InvokeName != TEXT("agent_emit_todo_plan")
			&& PrevNameCount >= PlanSameToolLimit)
		{
			const FString InvokeSignatureBlocked = (InvokeName + TEXT("|") + InvokeArgs.Left(220)).ToLower();
			const int32 SigCountBlocked = ToolInvokeCountBySignature.FindOrAdd(InvokeSignatureBlocked) + 1;
			ToolInvokeCountBySignature.FindOrAdd(InvokeSignatureBlocked) = SigCountBlocked;
			ToolInvokeCountByName.FindOrAdd(InvokeName) = PrevNameCount + 1;
			const FString BlockMsg = FString::Printf(
				TEXT("[Harness][reason=plan_node_repeated_tool] Blocked: tool \"%s\" exceeded per-plan-node limit (%d invocations per tool name)."),
				*InvokeName,
				PlanSameToolLimit);
			if (Sink.IsValid())
			{
				Sink->OnToolCallFinished(InvokeName, Tc.Id, false, UnrealAiTruncateForUi(BlockMsg), nullptr);
			}
			if (Conv.IsValid())
			{
				FUnrealAiConversationMessage Tm;
				Tm.Role = TEXT("tool");
				Tm.ToolCallId = Tc.Id;
				Tm.Content = BlockMsg;
				Conv->GetMessagesMutable().Add(Tm);
			}
			++CurrentRoundToolFailCount;
			CurrentRoundExecutedToolNames.Add(InvokeName);
			if (!Tc.Id.IsEmpty())
			{
				ExecutedToolCallIds.Add(Tc.Id);
			}
			bPendingFailPlanNodeSameToolBudget = true;
			PlanNodeSameToolBudgetName = InvokeName;
			PlanNodeSameToolBudgetCount = PrevNameCount + 1;
			PlanNodeSameToolBudgetLimit = PlanSameToolLimit;
			bCurrentRoundRepeatedToolLoop = true;
			EmitEnforcementEvent(TEXT("stream_tool_exec_done"), FString::Printf(TEXT("id=%s ok=0 blocked=same_tool_budget"), *Tc.Id));
			EmitEnforcementEvent(
				TEXT("plan_node_same_tool_blocked"),
				FString::Printf(TEXT("%s invocation=%d"), *InvokeName, PlanNodeSameToolBudgetCount));
			if (UnrealAiRuntimeDefaults::ToolUsageLogEnabled)
			{
				UnrealAiToolUsageEventLogger::AppendOperationalEvent(UsageQueryHash, InvokeName, false, Request.ThreadId);
			}
			UnrealAiToolUsagePrior::NoteSessionOutcome(InvokeName, false);
			LastConsecutiveIdenticalOkSignature.Reset();
			ConsecutiveIdenticalOkCount = 0;
			LastToolFailureSignature = InvokeSignatureBlocked;
			RepeatedToolFailureCount = FMath::Max(RepeatedToolFailureCount, 1);
			return;
		}
		const FString InvokeSignature = (InvokeName + TEXT("|") + InvokeArgs.Left(220)).ToLower();
		const int32 NameCount = ToolInvokeCountByName.FindOrAdd(InvokeName) + 1;
		ToolInvokeCountByName.FindOrAdd(InvokeName) = NameCount;
		const int32 SigCount = ToolInvokeCountBySignature.FindOrAdd(InvokeSignature) + 1;
		ToolInvokeCountBySignature.FindOrAdd(InvokeSignature) = SigCount;
		const bool bRepeatSignature = Request.Mode == EUnrealAiAgentMode::Agent && InvokeName != TEXT("agent_emit_todo_plan")
			&& (SigCount >= GHarnessRepeatedFailureStopCount || NameCount >= 6);
		EmitEnforcementEvent(TEXT("stream_tool_exec_start"), FString::Printf(TEXT("id=%s name=%s"), *Tc.Id, *InvokeName));
		if (Request.Mode == EUnrealAiAgentMode::Agent
			&& !UnrealAiBlueprintToolGate::PassesToolSurfaceFilter(Request, InvokeName, Catalog))
		{
			const FString BlockMsg = FString::Printf(
				TEXT("[Harness][reason=blueprint_tool_withheld] Blocked: tool \"%s\" is reserved for automated Blueprint Builder sub-turns. ")
				TEXT("Delegate graph edits via <unreal_ai_build_blueprint> (see system prompt), or use read-only / non-mutation blueprint tools on the main agent."),
				*InvokeName);
			if (Sink.IsValid())
			{
				Sink->OnToolCallFinished(InvokeName, Tc.Id, false, UnrealAiTruncateForUi(BlockMsg), nullptr);
			}
			if (Conv.IsValid())
			{
				FUnrealAiConversationMessage Tm;
				Tm.Role = TEXT("tool");
				Tm.ToolCallId = Tc.Id;
				Tm.Content = BlockMsg;
				Conv->GetMessagesMutable().Add(Tm);
			}
			++CurrentRoundToolFailCount;
			CurrentRoundExecutedToolNames.Add(InvokeName);
			if (!Tc.Id.IsEmpty())
			{
				ExecutedToolCallIds.Add(Tc.Id);
			}
			EmitEnforcementEvent(
				TEXT("blueprint_tool_gate_block"),
				FString::Printf(TEXT("tool=%s blueprint_builder_only=1"), *InvokeName));
			UnrealAiToolUsagePrior::NoteSessionOutcome(InvokeName, false);
			LastConsecutiveIdenticalOkSignature.Reset();
			ConsecutiveIdenticalOkCount = 0;
			return;
		}
		if (Request.bBlueprintBuilderTurn)
		{
			FUnrealAiToolInvocationResult GraphDomainBlock;
			if (UnrealAiGraphEditDomainPreflight_ShouldBlockInvocation(Request, InvokeName, InvokeArgs, GraphDomainBlock))
			{
				const FString BlockMsg = GraphDomainBlock.ErrorMessage.IsEmpty()
					? FString(TEXT("[Harness][reason=graph_domain_preflight] Blocked: asset does not match builder handoff."))
					: GraphDomainBlock.ErrorMessage;
				if (Sink.IsValid())
				{
					Sink->OnToolCallFinished(InvokeName, Tc.Id, false, UnrealAiTruncateForUi(BlockMsg), nullptr);
				}
				if (Conv.IsValid())
				{
					FUnrealAiConversationMessage Tm;
					Tm.Role = TEXT("tool");
					Tm.ToolCallId = Tc.Id;
					Tm.Content = BlockMsg;
					Conv->GetMessagesMutable().Add(Tm);
				}
				++CurrentRoundToolFailCount;
				CurrentRoundExecutedToolNames.Add(InvokeName);
				if (!Tc.Id.IsEmpty())
				{
					ExecutedToolCallIds.Add(Tc.Id);
				}
				EmitEnforcementEvent(
					TEXT("graph_domain_preflight_block"),
					FString::Printf(TEXT("tool=%s"), *InvokeName));
				UnrealAiToolUsagePrior::NoteSessionOutcome(InvokeName, false);
				LastConsecutiveIdenticalOkSignature.Reset();
				ConsecutiveIdenticalOkCount = 0;
				return;
			}
		}
		const FUnrealAiToolInvocationResult Inv = Tools->InvokeTool(InvokeName, InvokeArgs, Tc.Id);
		CurrentRoundExecutedToolNames.Add(InvokeName);
		const FString DialogFootnote = FUnrealAiEditorModalMonitor::ConsumePendingToolDialogFootnote();
		FString ModelToolContent = Inv.bOk ? Inv.ContentForModel : Inv.ErrorMessage;
		if (bRepeatSignature && !Inv.bOk)
		{
			bCurrentRoundRepeatedToolLoop = true;
		}
		if (Inv.bOk)
		{
			if (Request.Mode == EUnrealAiAgentMode::Agent && InvokeName != TEXT("agent_emit_todo_plan"))
			{
				if (!InvokeSignature.IsEmpty() && InvokeSignature == LastConsecutiveIdenticalOkSignature)
				{
					++ConsecutiveIdenticalOkCount;
				}
				else
				{
					LastConsecutiveIdenticalOkSignature = InvokeSignature;
					ConsecutiveIdenticalOkCount = 1;
				}
				if (ConsecutiveIdenticalOkCount >= GHarnessRepeatedFailureStopCount)
				{
					bPendingFailRepeatedIdenticalOk = true;
					bCurrentRoundRepeatedToolLoop = true;
				}
			}
			else
			{
				LastConsecutiveIdenticalOkSignature.Reset();
				ConsecutiveIdenticalOkCount = 0;
			}
		}
		else
		{
			LastConsecutiveIdenticalOkSignature.Reset();
			ConsecutiveIdenticalOkCount = 0;
		}
		if (!DialogFootnote.IsEmpty())
		{
			if (!ModelToolContent.IsEmpty())
			{
				ModelToolContent += TEXT("\n");
			}
			ModelToolContent += FString::Printf(TEXT("[Editor blocking dialog during tool]: %s"), *DialogFootnote);
		}
		if (Inv.bOk)
		{
			auto IsNonProgressEmptySearchResult = [&]()
			{
				if (InvokeName == TEXT("asset_index_fuzzy_search"))
				{
					const bool bLowConf = ModelToolContent.Contains(TEXT("\"low_confidence\":true")) || ModelToolContent.Contains(TEXT("\"low_confidence\": true"));
					const bool bEmptyMatches = ModelToolContent.Contains(TEXT("\"matches\":[]")) || ModelToolContent.Contains(TEXT("\"matches\": []"));
					const bool bCountZero = ModelToolContent.Contains(TEXT("\"count\":0")) || ModelToolContent.Contains(TEXT("\"count\": 0"));
					return bLowConf && (bEmptyMatches || bCountZero);
				}
				if (InvokeName == TEXT("scene_fuzzy_search"))
				{
					const bool bCountZero = ModelToolContent.Contains(TEXT("\"count\":0")) || ModelToolContent.Contains(TEXT("\"count\": 0"));
					return bCountZero;
				}
				if (InvokeName == TEXT("source_search_symbol"))
				{
					const bool bZeroFiles = ModelToolContent.Contains(TEXT("\"files_considered\":0")) || ModelToolContent.Contains(TEXT("\"files_considered\": 0"));
					const bool bZeroCandidates = ModelToolContent.Contains(TEXT("\"path_candidates\":0")) || ModelToolContent.Contains(TEXT("\"path_candidates\": 0"));
					const bool bEmptyPathMatches = ModelToolContent.Contains(TEXT("\"path_matches\":[]")) || ModelToolContent.Contains(TEXT("\"path_matches\": []"));
					return bZeroFiles || bZeroCandidates || bEmptyPathMatches;
				}
				return false;
			};
			if (IsNonProgressEmptySearchResult())
			{
				int32& C = NonProgressEmptySearchCountByToolName.FindOrAdd(InvokeName);
				++C;
				if (C >= 3)
				{
					bCurrentRoundRepeatedEmptySearch = true;
					bCurrentRoundRepeatedToolLoop = true;
				}
			}
			else
			{
				NonProgressEmptySearchCountByToolName.Remove(InvokeName);
			}
		}
		if (Inv.bOk)
		{
			++CurrentRoundToolSuccessCount;
			LastToolFailureSignature.Reset();
			RepeatedToolFailureCount = 0;
			LastSuggestedCorrectCallSerialized.Reset();
		}
		else
		{
			++CurrentRoundToolFailCount;
			{
				FString Captured;
				UnrealAiAgentHarnessPriv::TryCaptureSuggestedCorrectCallFromToolContent(ModelToolContent, Captured);
				if (!Captured.IsEmpty())
				{
					LastSuggestedCorrectCallSerialized = MoveTemp(Captured);
				}
			}
			const FString FailureSig = UnrealAiAgentHarnessPriv::NormalizeToolFailureSignatureForRepeatCount(InvokeName, InvokeArgs);
			if (!FailureSig.IsEmpty() && FailureSig == LastToolFailureSignature)
			{
				++RepeatedToolFailureCount;
			}
			else
			{
				LastToolFailureSignature = FailureSig;
				RepeatedToolFailureCount = 1;
			}
		}
		if (Sink.IsValid())
		{
			Sink->OnToolCallFinished(InvokeName, Tc.Id, Inv.bOk, UnrealAiTruncateForUi(ModelToolContent), Inv.EditorPresentation);
		}
		if (ContextService && InvokeName != TEXT("agent_emit_todo_plan")
			&& ShouldPersistToolResultToContextState(InvokeName)
			&& (Inv.bOk || ShouldPersistFailedCompileToolForContext(InvokeName)))
		{
			ContextService->LoadOrCreate(Request.ProjectId, Request.ThreadId);
			FContextRecordPolicy Policy;
			UnrealAiApplyToolSpecificRecordPolicy(Policy, FName(*InvokeName));
			ContextService->RecordToolResult(FName(*InvokeName), ModelToolContent, Policy);
		}
		if (InvokeName == TEXT("agent_emit_todo_plan") && Sink.IsValid())
		{
			++ReplanCount;
			FString PlanTitle = TEXT("Plan");
			TSharedPtr<FJsonObject> ArgsObj;
			TSharedRef<TJsonReader<>> AR = TJsonReaderFactory<>::Create(InvokeArgs);
			if (FJsonSerializer::Deserialize(AR, ArgsObj) && ArgsObj.IsValid())
			{
				ArgsObj->TryGetStringField(TEXT("title"), PlanTitle);
			}
			const FString PlanBody = (Inv.bOk && Inv.ContentForModel.TrimStart().StartsWith(TEXT("{"))) ? Inv.ContentForModel : InvokeArgs;
			Sink->OnTodoPlanEmitted(PlanTitle, PlanBody);
		}
		if (Conv.IsValid())
		{
			FUnrealAiConversationMessage Tm;
			Tm.Role = TEXT("tool");
			Tm.ToolCallId = Tc.Id;
			Tm.Content = ModelToolContent;
			Conv->GetMessagesMutable().Add(Tm);
		}
		if (!Tc.Id.IsEmpty())
		{
			ExecutedToolCallIds.Add(Tc.Id);
		}
		EmitEnforcementEvent(TEXT("stream_tool_exec_done"), FString::Printf(TEXT("id=%s ok=%d"), *Tc.Id, Inv.bOk ? 1 : 0));
		if (UnrealAiRuntimeDefaults::ToolUsageLogEnabled)
		{
			UnrealAiToolUsageEventLogger::AppendOperationalEvent(UsageQueryHash, InvokeName, Inv.bOk, Request.ThreadId);
		}
		UnrealAiToolUsagePrior::NoteSessionOutcome(InvokeName, Inv.bOk);
	}

	void FAgentTurnRunner::CompleteToolPath()
	{
		if (!Tools || !Conv.IsValid())
		{
			Fail(TEXT("Tool host missing"));
			return;
		}
		if (PendingToolCalls.Num() == 0 && ExecutedToolCallIds.Num() == 0)
		{
			Fail(TEXT("Model completed tool_calls but no valid tool name (empty function.name)"));
			return;
		}
		// In stream-first mode tools may already be executed before Finish arrives.
		// Ensure any newly complete calls execute now before finalizing this round.
		EnqueueNewlyCompleteCalls();
		StartOrContinueStreamedToolExecution();
		if (CheckIncompleteToolCallTimeout(true))
		{
			return;
		}
		// StartOrContinueStreamedToolExecution yields between tools via FTSTicker when not in headed sync runs.
		// If we proceed to DispatchLlm while tools are still queued or a ticker continuation is pending,
		// DispatchLlm cancels the transport and resets runner state — killing the in-flight LLM stream and
		// stranding the tool ticker (symptom: UI stuck on "Run started" with no assistant output).
		if (bToolExecutionInProgress || CompletedToolCallQueue.Num() > 0)
		{
			if (!bCompleteToolPathReschedulePending)
			{
				bCompleteToolPathReschedulePending = true;
				UE_LOG(
					LogTemp,
					Display,
					TEXT("UnrealAi harness: CompleteToolPath deferred until streamed tool queue idle (in_progress=%s queue=%d)."),
					bToolExecutionInProgress ? TEXT("true") : TEXT("false"),
					CompletedToolCallQueue.Num());
				const TWeakPtr<FAgentTurnRunner> WeakSelf = AsShared();
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
					[WeakSelf](float /*DeltaTime*/) -> bool
					{
						const TSharedPtr<FAgentTurnRunner> Self = WeakSelf.Pin();
						if (!Self.IsValid() || Self->bTerminal.load(std::memory_order_relaxed))
						{
							return false;
						}
						if (Self->bToolExecutionInProgress || Self->CompletedToolCallQueue.Num() > 0)
						{
							return true;
						}
						Self->bCompleteToolPathReschedulePending = false;
						Self->CompleteToolPath();
						return false;
					}));
			}
			return;
		}
		bCompleteToolPathReschedulePending = false;

		FString TodoPlanEffectiveName;
		if (PendingToolCalls.Num() == 1)
		{
			FString TmpArgs, UnwrapE;
			if (!UnwrapDispatchToolCall(PendingToolCalls[0], TodoPlanEffectiveName, TmpArgs, UnwrapE))
			{
				TodoPlanEffectiveName.Reset();
			}
		}
		const bool bTodoPlanOnly = PendingToolCalls.Num() == 1
			&& TodoPlanEffectiveName == TEXT("agent_emit_todo_plan");
		const bool bAgentMode = Request.Mode == EUnrealAiAgentMode::Agent;
		const int32 ToolBatchSize = PendingToolCalls.Num();
		const bool bLargeBatch = false; // stream-first: execute sequentially as calls complete.
		const bool bNearRoundBudget = (EffectiveMaxLlmRounds - LlmRound) <= 2;
		const bool bNearTokenBudget = EffectiveMaxTurnTokensHint > 0
			&& (static_cast<int64>(AccumulatedUsage.TotalTokens) * 100 >= static_cast<int64>(EffectiveMaxTurnTokensHint) * 85);
		const bool bNearBudget = bAgentMode && !bTodoPlanOnly && (bNearRoundBudget || bNearTokenBudget);
		const bool bDeferredQueue = false;
		const bool bRepeatedToolLoop = bCurrentRoundRepeatedToolLoop;
		const bool bRepeatedEmptySearch = bCurrentRoundRepeatedEmptySearch;
		const int32 ToolSuccessCount = CurrentRoundToolSuccessCount;
		const int32 ToolFailCount = CurrentRoundToolFailCount;
		// Number of tool calls that were actually completed in this tool path.
		// Used only for deferred-queue accounting; bDeferredQueue is currently false but this must compile.
		const int32 ToolsToExecute = ToolSuccessCount + ToolFailCount;
		const TArray<FString>& ExecutedToolNames = CurrentRoundExecutedToolNames;
		// Dynamic queue progress: when a stored plan exists and this round made progress,
		// mark one pending todo step as done (best-effort coarse progress signal).
		if (ContextService && bAgentMode && ToolSuccessCount > 0)
		{
			ContextService->LoadOrCreate(Request.ProjectId, Request.ThreadId);
			if (const FAgentContextState* St = ContextService->GetState(Request.ProjectId, Request.ThreadId))
			{
				if (!St->ActiveTodoPlanJson.IsEmpty())
				{
					for (int32 i = 0; i < St->TodoStepsDone.Num(); ++i)
					{
						if (!St->TodoStepsDone[i])
						{
							ContextService->SetTodoStepDone(i, true);
							break;
						}
					}
					ContextService->SaveNow(Request.ProjectId, Request.ThreadId);
				}
			}
		}
		if (bDeferredQueue)
		{
			QueueStepsPending = FMath::Max(QueueStepsPending, ToolBatchSize - ToolsToExecute);
		}
		else
		{
			QueueStepsPending = FMath::Max(0, QueueStepsPending - ToolSuccessCount);
		}
		TArray<FString> TriggerReasons;
		if (bLargeBatch)
		{
			TriggerReasons.Add(TEXT("many_tool_calls"));
		}
		if (bNearBudget)
		{
			TriggerReasons.Add(TEXT("budget_pressure"));
		}
		if (RepeatedToolFailureCount >= 2 || ToolFailCount >= 2)
		{
			TriggerReasons.Add(TEXT("repeated_tool_failures"));
		}
		if (bRepeatedToolLoop)
		{
			TriggerReasons.Add(TEXT("repeated_tool_loop"));
		}
		if (bRepeatedEmptySearch)
		{
			TriggerReasons.Add(TEXT("repeated_empty_search"));
		}
		if (bPendingFailRepeatedIdenticalOk)
		{
			TriggerReasons.Add(TEXT("repeated_identical_ok"));
		}
		if (bTodoPlanOnly)
		{
			TriggerReasons.Add(TEXT("explicit_todo_plan"));
		}
		if (TriggerReasons.Num() == 0)
		{
			TriggerReasons.Add(TEXT("act_now"));
		}
		EmitPlanningDecision(bTodoPlanOnly ? TEXT("explicit") : TEXT("implicit"), TriggerReasons);
		if (bPendingFailHarnessSnapshotReadSpam)
		{
			AccumulateRoundUsage();
			EmitEnforcementEvent(
				TEXT("harness_snapshot_read_budget_abort"),
				FString::Printf(TEXT("count=%d"), EditorSnapshotReadInvokeCount));
			Fail(FString::Printf(
				TEXT("[Harness][reason=harness_read_tool_spam] Stopped: editor_state_snapshot_read exceeded headed scenario limit (%d per turn)."),
				UnrealAiWaitTime::HarnessScenarioMaxReadSnapshotToolsPerTurn));
			return;
		}
		if (bPendingFailPlanNodeSameToolBudget)
		{
			AccumulateRoundUsage();
			EmitEnforcementEvent(
				TEXT("plan_node_same_tool_budget_abort"),
				FString::Printf(
					TEXT("%s x%d"),
					PlanNodeSameToolBudgetName.IsEmpty() ? TEXT("?") : *PlanNodeSameToolBudgetName,
					PlanNodeSameToolBudgetCount));
			Fail(FString::Printf(
				TEXT("[Harness][reason=plan_node_repeated_tool] Plan node stopped: tool \"%s\" exceeded the per-node repeat limit (%d invocations per tool name; blocked further calls). ")
				TEXT("This node is treated as failed so the plan executor can skip dependents and continue."),
				PlanNodeSameToolBudgetName.IsEmpty() ? TEXT("(unknown)") : *PlanNodeSameToolBudgetName,
				PlanNodeSameToolBudgetLimit > 0 ? PlanNodeSameToolBudgetLimit
												: UnrealAiWaitTime::PlanNodeMaxInvocationsSameTool));
			return;
		}
		if (bPendingFailRepeatedIdenticalOk)
		{
			AccumulateRoundUsage();
			EmitEnforcementEvent(TEXT("repeated_identical_ok_abort"), LastConsecutiveIdenticalOkSignature.Left(240));
			Fail(FString::Printf(
				TEXT("Stopped after %d consecutive identical successful tool calls with no progress (%s). Use a different tool, change arguments based on new information, state a concise blocker, end with remaining work, or use Plan chat mode for structured multi-step follow-up."),
				GHarnessRepeatedFailureStopCount,
				LastConsecutiveIdenticalOkSignature.IsEmpty() ? TEXT("unknown signature") : *LastConsecutiveIdenticalOkSignature));
			return;
		}
		const bool bPlanNodeValidationFailFast = IsPlanNodeAgentThread()
			&& RepeatedToolFailureCount >= UnrealAiWaitTime::PlanNodeRepeatedValidationFailFastThreshold;
		if (bPlanNodeValidationFailFast)
		{
			AccumulateRoundUsage();
			EmitEnforcementEvent(
				TEXT("plan_node_repeated_validation_failfast"),
				LastToolFailureSignature.Left(240));
			Fail(FString::Printf(
				TEXT("[Harness][reason=plan_node_repeated_validation] Plan node stopped after repeated identical validation failures (%d): %s. ")
				TEXT("Use suggested_correct_call exactly once or return a concise blocker."),
				RepeatedToolFailureCount,
				LastToolFailureSignature.IsEmpty() ? TEXT("unknown signature") : *LastToolFailureSignature));
			return;
		}
		const bool bRepeatedValidationLoop = RepeatedToolFailureCount >= 3;
		if (bRepeatedValidationLoop && Request.Mode != EUnrealAiAgentMode::Ask)
		{
			FUnrealAiConversationMessage RepairNudge;
			RepairNudge.Role = TEXT("user");
			RepairNudge.Content = TEXT(
				"[Harness][reason=repeated_validation_failure] The same tool validation failure repeated multiple times. Repair-or-stop now: either call one corrected tool invocation with fixed arguments, or provide a concise blocked summary with the exact failing tool and required fields. Last failing pattern: ");
			RepairNudge.Content += LastToolFailureSignature;
			if (!LastSuggestedCorrectCallSerialized.IsEmpty())
			{
				RepairNudge.Content += TEXT(" If the last tool message included `suggested_correct_call`, use that exact inner tool_id + arguments on the next `unreal_ai_dispatch` retry. suggested_correct_call: ");
				RepairNudge.Content += LastSuggestedCorrectCallSerialized.Left(1800);
				EmitEnforcementEvent(TEXT("suggested_call_validation_nudge"), TEXT("appended_last_suggested_call"));
			}
			Conv->GetMessagesMutable().Add(RepairNudge);
		}
		if (bRepeatedToolLoop && Request.Mode == EUnrealAiAgentMode::Agent && !bTodoPlanOnly)
		{
			FUnrealAiConversationMessage LoopNudge;
			LoopNudge.Role = TEXT("user");
			LoopNudge.Content = TEXT(
				"[Harness][reason=repeated_tool_loop] Repeated tool loop detected. Stop or change strategy: provide a concise blocked summary with the exact blocker, or summarize what is done vs remaining; use Plan chat mode for dependency-style multi-step work.");
			Conv->GetMessagesMutable().Add(LoopNudge);
		}
		if (ToolFailCount >= 4 && Request.Mode == EUnrealAiAgentMode::Agent && !bTodoPlanOnly && Request.bOmitMainAgentBlueprintMutationTools
			&& !Request.bBlueprintBuilderTurn)
		{
			FUnrealAiConversationMessage BpNudge;
			BpNudge.Role = TEXT("user");
			BpNudge.Content = TEXT(
				"[Harness][reason=multi_tool_failure_round] Multiple tool failures this round. For Blueprint graph mutations from the main agent, use the <unreal_ai_build_blueprint> handoff (see 12-build-blueprint-delegation). Use only tool_id values from the current tool appendix—never invent names. If still blocked, summarize the exact error text and stop.");
			Conv->GetMessagesMutable().Add(BpNudge);
			EmitEnforcementEvent(TEXT("blueprint_mutation_handoff_nudge"), FString::Printf(TEXT("tool_fail_count=%d"), ToolFailCount));
		}
		// The next round still runs (DispatchLlm below), but models often reply with text-only and end
		// the run. A synthetic user line nudges execution when the only tool was the todo plan.
		if (bTodoPlanOnly && Request.Mode != EUnrealAiAgentMode::Ask)
		{
			FUnrealAiConversationMessage Nudge;
			Nudge.Role = TEXT("user");
			Nudge.Content = TEXT(
				"[Harness][reason=todo_plan_only] Legacy todo plan recorded. When ready, continue with the first pending plan step using Unreal tools if appropriate.");
			Conv->GetMessagesMutable().Add(Nudge);
		}
		else if (Request.Mode == EUnrealAiAgentMode::Agent && bDeferredQueue)
		{
			// Rare: deferred tool queue — explain why follow-up may be needed (only when dynamic deferral is active).
			FUnrealAiConversationMessage Nudge;
			Nudge.Role = TEXT("user");
			Nudge.Content = FString::Printf(
				TEXT("[Harness][reason=tool_round_complete_deferred] Tool round complete (ok=%d, failed=%d). Additional requested tools were deferred (%d) due to dynamic planning policy. ")
				TEXT("Now emit/update a concise todo plan that queues remaining work, then execute the first pending step."),
				ToolSuccessCount,
				ToolFailCount,
				ToolBatchSize - ToolsToExecute);
			Conv->GetMessagesMutable().Add(Nudge);
		}
		// Intentionally no generic "tool_round_complete" nudge after normal agent tool rounds: headed/long-run
		// harness prioritizes tool/schema metrics; task completion is reviewed qualitatively.
		{
			const FString LastRealUser = GetLastRealUserMessage(Conv->GetMessages());
			// Plan-mode planner passes are DAG-only (no tools); do not treat as Agent action-intent turns.
			const bool bModeWantsTools =
				(Request.Mode == EUnrealAiAgentMode::Agent);
			if (bModeWantsTools && UserLikelyRequestsActionTool(LastRealUser))
			{
				if (!bActionIntentCounted)
				{
					++ActionIntentTurnCount;
					bActionIntentCounted = true;
				}
				if (!bActionToolOutcomeCounted)
				{
					++ActionTurnsWithToolCallsCount;
					bActionToolOutcomeCounted = true;
					EmitEnforcementEvent(TEXT("action_with_tool_calls"), TEXT("action-intent turn executed one or more tools"));
				}
			}
		}
		{
			const FString LastRealUser = GetLastRealUserMessage(Conv->GetMessages());
			const bool bNeedsMutationFollowthrough = Request.Mode == EUnrealAiAgentMode::Agent
				&& UserLikelyRequestsMutation(LastRealUser)
				&& ExecutedToolNames.Num() > 0;
			if (bNeedsMutationFollowthrough)
			{
				if (!bMutationIntentCounted)
				{
					++MutationIntentTurnCount;
					bMutationIntentCounted = true;
				}
				bool bAllReadOnly = true;
				for (const FString& Name : ExecutedToolNames)
				{
					if (!IsLikelyReadOnlyToolName(Name))
					{
						bAllReadOnly = false;
						break;
					}
				}
				if (bAllReadOnly && MutationFollowthroughNudgeCount < 2)
				{
					++MutationFollowthroughNudgeCount;
					EmitEnforcementEvent(
						TEXT("mutation_read_only_note"),
						TEXT("mutation-intent user message but only read/discovery tools this round; harness not injecting follow-up nudge"));
				}
			}
		}
		if (UnwrapFailuresThisRound > 0 && UnrealAiRuntimeDefaults::ToolRepairNudgeEnabled
			&& Request.Mode == EUnrealAiAgentMode::Agent && Conv.IsValid() && Catalog)
		{
			const int32 MaxRepair = UnrealAiRuntimeDefaults::ToolRepairMaxPerUserSend;
			if (ToolRepairNudgesThisRun < MaxRepair)
			{
				const FString RepairLine = UnrealAiAgentHarnessPriv::BuildToolRepairUserLine(Catalog, PendingToolCalls);
				if (!RepairLine.IsEmpty())
				{
					FUnrealAiConversationMessage R;
					R.Role = TEXT("user");
					R.Content = RepairLine;
					Conv->GetMessagesMutable().Add(R);
					++ToolRepairNudgesThisRun;
					EmitEnforcementEvent(TEXT("tool_call_repair_nudge"), TEXT("injected_schema_help"));
				}
			}
		}
		PendingToolCalls.Reset();
		AssistantBuffer.Reset();
		AccumulateRoundUsage();
		DispatchLlm();
	}

	void FAgentTurnRunner::CompleteAssistantOnly()
	{
		if (!Conv.IsValid())
		{
			Fail(TEXT("Conversation missing"));
			return;
		}
		AccumulateRoundUsage();
		const FString OrigAssist = AssistantBuffer;
		FString BbResultInner;
		FString BbResultVisible;
		const bool bBbResultParsed =
			Request.bBlueprintBuilderTurn && UnrealAiBlueprintBuilderResultTag::TryConsume(OrigAssist, BbResultInner, BbResultVisible);

		FString BpInner;
		FString BpVisible;
		const bool bBpParsed = !Request.bBlueprintBuilderTurn
			&& UnrealAiBuildBlueprintTag::TryConsume(OrigAssist, BpInner, BpVisible);
		const bool bBpHandoff = bBpParsed && (Request.Mode == EUnrealAiAgentMode::Agent) && !Request.bBlueprintBuilderTurn
			&& !Request.ThreadId.Contains(TEXT("_plan_")) && !BpInner.TrimStartAndEnd().IsEmpty();

		FUnrealAiConversationMessage Am;
		Am.Role = TEXT("assistant");
		if (bBpHandoff)
		{
			Am.Content = BpVisible;
		}
		else if (bBbResultParsed)
		{
			Am.Content = BbResultVisible;
			if (Am.Content.TrimStartAndEnd().IsEmpty())
			{
				Am.Content = TEXT("Blueprint Builder finished (structured result follows for the main agent).");
			}
		}
		else
		{
			Am.Content = OrigAssist;
		}
		// Models sometimes emit malformed tag junctions (e.g. closing handoff + opening result) that TryConsume skips.
		UnrealAiBuildBlueprintTag::StripProtocolMarkersForUi(Am.Content);
		Conv->GetMessagesMutable().Add(Am);

		// Second+ LLM rounds often end with finish_reason=stop and no tool_calls. Models sometimes return an
		// empty assistant message (streaming quirk or "done" signal) even though work is unfinished—treat
		// that as a stall, not a successful completion, and schedule another round while under the cap.
		const bool bAgentModeWantsToolExecution = (Request.Mode == EUnrealAiAgentMode::Agent);
		const bool bPlanPlannerPass = UnrealAiPlanPlannerHarness::IsPlanPlannerPass(Request.Mode);
		const FString LastRealUser = GetLastRealUserMessage(Conv->GetMessages());
		const bool bActionIntent = UserLikelyRequestsActionTool(LastRealUser);
		const bool bHasExplicitBlocker = AssistantContainsExplicitBlocker(OrigAssist);
		if (bAgentModeWantsToolExecution && bActionIntent)
		{
			if (!bActionIntentCounted)
			{
				++ActionIntentTurnCount;
				bActionIntentCounted = true;
			}
			if (bHasExplicitBlocker)
			{
				if (!bActionBlockerOutcomeCounted)
				{
					++ActionTurnsWithExplicitBlockerCount;
					bActionBlockerOutcomeCounted = true;
					EmitEnforcementEvent(TEXT("action_explicit_blocker"), TEXT("action-intent turn completed with explicit blocker"));
				}
			}
		}
		// Interactive Agent mode retries empty assistant deltas with a harness nudge. Plan DAG node threads
		// (`*_plan_*`) run in series; burning multiple LLM rounds here blocks the plan executor and looks
		// like a hang. Finish the node so the parent plan can advance.
		if (!bBpHandoff && !bBbResultParsed && bAgentModeWantsToolExecution && Am.Content.TrimStartAndEnd().IsEmpty()
			&& LlmRound < EffectiveMaxLlmRounds)
		{
			if (!Request.ThreadId.Contains(TEXT("_plan_")))
			{
				FUnrealAiConversationMessage Nudge;
				Nudge.Role = TEXT("user");
				Nudge.Content = TEXT(
					"[Harness][reason=empty_assistant] The model returned an empty assistant message. Continue the user's task: call the ")
					TEXT("next tool(s) now, or briefly explain what blocks you. Do not reply with an empty message.");
				Conv->GetMessagesMutable().Add(Nudge);
				AssistantBuffer.Reset();
				DispatchLlm();
				return;
			}
			EmitEnforcementEvent(
				TEXT("plan_node_empty_assistant_finish"),
				TEXT("plan node ended with empty assistant; failing node so plan can replan or skip dependents"));
			Fail(TEXT("Plan node finished with empty assistant output (no text and no tools)."));
			return;
		}
		if (bPlanPlannerPass && Am.Content.TrimStartAndEnd().IsEmpty() && LlmRound < EffectiveMaxLlmRounds)
		{
			FUnrealAiConversationMessage Nudge;
			Nudge.Role = TEXT("user");
			Nudge.Content = UnrealAiPlanPlannerHarness::GetEmptyPlannerNudgeUserMessage();
			Conv->GetMessagesMutable().Add(Nudge);
			AssistantBuffer.Reset();
			DispatchLlm();
			return;
		}
		// Ask mode: retry once when the assistant text is empty or looks truncated mid-thought.
		if (Request.Mode == EUnrealAiAgentMode::Ask && !bPlanPlannerPass && !Request.ThreadId.Contains(TEXT("_plan_"))
			&& LlmRound < EffectiveMaxLlmRounds)
		{
			const FString TrimAsk = Am.Content.TrimStartAndEnd();
			if (UnrealAiAgentHarnessPriv::LooksLikeIncompleteAskAnswer(TrimAsk) && AskAnswerRepairNudgeCount < 1)
			{
				++AskAnswerRepairNudgeCount;
				EmitEnforcementEvent(
					TEXT("ask_answer_repair_nudge"),
					TrimAsk.IsEmpty() ? TEXT("empty") : TEXT("incomplete_or_truncated"));
				FUnrealAiConversationMessage Nudge;
				Nudge.Role = TEXT("user");
				Nudge.Content = TEXT(
					"[Harness][reason=ask_answer_incomplete] Your previous assistant message was empty or looks truncated. ")
					TEXT("Reply with complete sentence(s) that fully answer the user's question; do not stop mid-phrase.");
				Conv->GetMessagesMutable().Add(Nudge);
				AssistantBuffer.Reset();
				DispatchLlm();
				return;
			}
			if (UnrealAiAgentHarnessPriv::LooksLikeIncompleteAskAnswer(TrimAsk) && AskAnswerRepairNudgeCount >= 1)
			{
				EmitEnforcementEvent(TEXT("ask_answer_repair_gave_up"), TEXT("still_incomplete_after_nudge"));
			}
		}
		// Lax policy: do not require tool_calls on every agent round. Text-only wrap-ups after work (or when no tool
		// applies) are allowed; qualitative review judges task success. Still emit a note when the user prompt looked
		// action-oriented and this round had no tools, for batch metrics / grep.
		if (bAgentModeWantsToolExecution && bActionIntent && !bHasExplicitBlocker)
		{
			EmitEnforcementEvent(
				TEXT("action_text_only_completion"),
				TEXT("agent round ended with assistant text only (no tool_calls); harness allows success for qualitative review"));
		}

		if (bBpHandoff)
		{
			EUnrealAiBlueprintBuilderTargetKind ParsedKind = EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint;
			UnrealAiBuildBlueprintTag::ParseAndStripHandoffMetadata(BpInner, ParsedKind);
			Request.BlueprintBuilderTargetKind = ParsedKind;
			Request.bBlueprintBuilderTurn = true;
			EmitEnforcementEvent(TEXT("blueprint_builder_chain"), TEXT("chained_subturn_from_build_blueprint_tag"));
			FUnrealAiConversationMessage Sub;
			Sub.Role = TEXT("user");
			Sub.Content = UnrealAiBlueprintBuilderToolSurface::BuildAutomatedSubturnHarnessPreamble(ParsedKind) + BpInner;
			Conv->GetMessagesMutable().Add(Sub);
			AssistantBuffer.Reset();
			DispatchLlm();
			return;
		}

		if (bBbResultParsed)
		{
			Request.bBlueprintBuilderTurn = false;
			Request.BlueprintBuilderTargetKind = EUnrealAiBlueprintBuilderTargetKind::ScriptBlueprint;
			Request.bInjectBlueprintBuilderResumeChunk = true;
			EmitEnforcementEvent(TEXT("blueprint_builder_result"), TEXT("return_to_main_agent"));
			FUnrealAiConversationMessage Ret;
			Ret.Role = TEXT("user");
			Ret.Content = FString::Printf(
				TEXT("[Blueprint Builder — result for main agent]\n%s"),
				*BbResultInner);
			Conv->GetMessagesMutable().Add(Ret);
			AssistantBuffer.Reset();
			DispatchLlm();
			return;
		}

		Succeed();
	}
}

FUnrealAiAgentHarness::FUnrealAiAgentHarness(
	IUnrealAiPersistence* InPersistence,
	IAgentContextService* InContext,
	FUnrealAiModelProfileRegistry* InProfiles,
	FUnrealAiToolCatalog* InCatalog,
	TSharedPtr<ILlmTransport> InTransport,
	IToolExecutionHost* InToolHost,
	FUnrealAiUsageTracker* InUsageTracker,
	IUnrealAiMemoryService* InMemoryService)
	: Persistence(InPersistence)
	, Context(InContext)
	, Profiles(InProfiles)
	, Catalog(InCatalog)
	, Transport(MoveTemp(InTransport))
	, ToolHost(InToolHost)
	, UsageTracker(InUsageTracker)
	, MemoryService(InMemoryService)
{
}

FUnrealAiAgentHarness::~FUnrealAiAgentHarness() = default;

void FUnrealAiAgentHarness::FailInProgressTurnForScenarioIdleAbort()
{
	if (!ActiveRunner.IsValid())
	{
		return;
	}
	if (ActiveRunner->bTerminal.load(std::memory_order_relaxed))
	{
		return;
	}
	ActiveRunner->Fail(
		TEXT("Headed harness idle abort: sync wait exceeded stream-idle threshold (HarnessSyncIdleAbortMs) with no completion."));
}

void FUnrealAiAgentHarness::CancelTurn()
{
	if (Context && ActiveRunner.IsValid())
	{
		Context->CancelRetrievalPrefetchForThread(ActiveRunner->Request.ProjectId, ActiveRunner->Request.ThreadId);
	}
	if (ActiveRunner.IsValid())
	{
		ActiveRunner->bCancelled.store(true, std::memory_order_relaxed);
	}
	if (Transport.IsValid())
	{
		Transport->CancelActiveRequest();
	}
}

bool FUnrealAiAgentHarness::IsTurnInProgress() const
{
	if (!ActiveRunner.IsValid())
	{
		return false;
	}
	return !ActiveRunner->bTerminal.load(std::memory_order_relaxed)
		&& !ActiveRunner->bCancelled.load(std::memory_order_relaxed);
}

bool FUnrealAiAgentHarness::HasActiveLlmTransportRequest() const
{
	return Transport.IsValid() && Transport->HasActiveRequest();
}

bool FUnrealAiAgentHarness::ShouldSuppressIdleAbort() const
{
	if (!ActiveRunner.IsValid())
	{
		return false;
	}
	return ActiveRunner->ShouldSuppressIdleAbort();
}

void FUnrealAiAgentHarness::NotifyPlanExecutorStarted(TSharedPtr<FUnrealAiPlanExecutor> Exec)
{
	WeakActivePlanExecutor = Exec;
}

void FUnrealAiAgentHarness::NotifyPlanExecutorEnded()
{
	WeakActivePlanExecutor.Reset();
}

bool FUnrealAiAgentHarness::IsPlanPipelineActive() const
{
	const TSharedPtr<FUnrealAiPlanExecutor> P = WeakActivePlanExecutor.Pin();
	return P.IsValid() && P->IsRunning();
}

void FUnrealAiAgentHarness::SetHeadedScenarioStrictToolBudgets(bool bEnable)
{
	bHeadedScenarioStrictToolBudgets = bEnable;
}

void FUnrealAiAgentHarness::SetHeadedScenarioSyncRun(bool bEnable)
{
	bHeadedScenarioSyncRun = bEnable;
}

void FUnrealAiAgentHarness::RunTurn(const FUnrealAiAgentTurnRequest& Request, TSharedPtr<IAgentRunSink> Sink)
{
	CancelTurn();
	if (!Sink.IsValid() || !Persistence || !Context || !Profiles || !Catalog || !Transport.IsValid() || !ToolHost)
	{
		if (Sink.IsValid())
		{
			Sink->OnRunFinished(false, TEXT("Harness not fully configured"));
		}
		return;
	}

	FUnrealAiEditorModalMonitor::NotifyAgentTurnStarted(Sink);

	const TSharedPtr<UnrealAiAgentHarnessPriv::FAgentTurnRunner> Runner = MakeShared<UnrealAiAgentHarnessPriv::FAgentTurnRunner>();
	ActiveRunner = Runner;
	Runner->Request = Request;
	Runner->Sink = Sink;
	Runner->Persistence = Persistence;
	Runner->ContextService = Context;
	Runner->Profiles = Profiles;
	Runner->Catalog = Catalog;
	Runner->Transport = Transport;
	Runner->Tools = ToolHost;
	Runner->UsageTracker = UsageTracker;
	Runner->MemoryService = MemoryService;
	Runner->bHeadedScenarioStrictToolBudgets = bHeadedScenarioStrictToolBudgets;
	Runner->bHeadedScenarioSyncRun = bHeadedScenarioSyncRun;
	Runner->AccumulatedUsage = FUnrealAiTokenUsage();
	Runner->Conv = MakeUnique<FUnrealAiConversationStore>(Persistence);
	Runner->Conv->LoadOrCreate(Request.ProjectId, Request.ThreadId);

	Context->LoadOrCreate(Request.ProjectId, Request.ThreadId);
	ToolHost->SetToolSession(Request.ProjectId, Request.ThreadId);

	Runner->RunId = FGuid::NewGuid();
	FUnrealAiRunIds Ids;
	Ids.RunId = Runner->RunId;
	Sink->OnRunStarted(Ids);

	// Chat naming is a first-turn-only instruction. We inject it into the LLM conversation,
	// but it is never shown in the chat UI because the UI transcript is driven by chat send/stream events.
	const bool bFirstUserMessageInThread = (Runner->Conv->GetMessages().Num() == 0);
	if (bFirstUserMessageInThread)
	{
		FUnrealAiConversationMessage NameInstr;
		NameInstr.Role = TEXT("system");
		NameInstr.Content = TEXT(
			"[Hidden] Chat naming:\n"
			"On your FIRST assistant reply in this chat, you MUST append exactly one token:\n"
			"<chat-name: \"<short name>\"> \n"
			"The chat name should be 3-8 words, human-friendly, and derived from the user's goal.\n"
			"Do not explain the token to the user. The application will strip the token from visible output.\n");
		Runner->Conv->GetMessagesMutable().Add(NameInstr);
	}

	FUnrealAiConversationMessage UserMsg;
	UserMsg.Role = TEXT("user");
	UserMsg.Content = Request.UserText;
	UserMsg.bHasUserAgentMode = true;
	UserMsg.UserAgentMode = Request.Mode;
	Runner->Conv->GetMessagesMutable().Add(UserMsg);

	Runner->DispatchLlm();
}
