#include "UnrealAiEditorModule.h"

#include "Modules/ModuleManager.h"
#include "Async/Async.h"
#include "App/UnrealAiEditorCommands.h"
#include "Backend/IUnrealAiPersistence.h"
#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/UnrealAiProjectId.h"
#include "Context/UnrealAiProjectTreeSampler.h"
#include "Context/UnrealAiContextRankingPolicy.h"
#include "Context/AgentContextTypes.h"
#include "Context/IAgentContextService.h"
#include "Context/UnrealAiContextDragDrop.h"
#include "Context/UnrealAiEditorContextQueries.h"
#include "Context/UnrealAiRecentUiRanking.h"
#include "Memory/IUnrealAiMemoryService.h"
#include "Memory/UnrealAiMemoryTypes.h"
#include "Style/UnrealAiEditorStyle.h"
#include "Tabs/SUnrealAiEditorChatTab.h"
#include "Framework/Docking/SDockingTabStack.h"
#include "Tabs/SUnrealAiEditorHelpTab.h"
#include "Tabs/SUnrealAiEditorQuickStartTab.h"
#include "Tabs/SUnrealAiEditorSettingsTab.h"
#include "UnrealAiEditorSettings.h"
#include "UnrealAiEditorTabIds.h"
#include "Widgets/UnrealAiChatUiSession.h"
#include "AssetRegistry/AssetData.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/SDockingTabStack.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "IContentBrowserSingleton.h"
#include "LevelEditor.h"
#include "Selection.h"
#include "Styling/AppStyle.h"
#include "ToolMenuEntry.h"
#include "ToolMenu.h"
#include "ISettingsModule.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWidget.h"
#include "WorkspaceMenuStructure.h"
#include "Misc/CoreDelegates.h"
#include "Misc/UnrealAiEditorModalMonitor.h"
#include "Misc/UnrealAiRuntimeDefaults.h"
#include "Misc/UnrealAiRecentUiTracker.h"
#include "Misc/Guid.h"
#include "Observability/UnrealAiBackgroundOpsLog.h"
#include "WorkspaceMenuStructureModule.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Tools/UnrealAiToolProjectPathAllowlist.h"
#include "Tools/UnrealAiToolCatalogMatrixRunner.h"
#include "Harness/UnrealAiHarnessScenarioRunner.h"
#include "Harness/UnrealAiHarnessTurnPaths.h"
#include "BlueprintFormat/UnrealAiBlueprintFormatEditorRegistration.h"
#include "Retrieval/IUnrealAiRetrievalService.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "Harness/IToolExecutionHost.h"

#define LOCTEXT_NAMESPACE "UnrealAiEditor"

static FUnrealAiEditorModule* GUnrealAiModule = nullptr;

static IConsoleObject* GUnrealAiCatalogMatrixConsole = nullptr;
static IConsoleObject* GUnrealAiRunAgentTurnConsole = nullptr;
static IConsoleObject* GUnrealAiRunStrictAssertionsConsole = nullptr;
static IConsoleObject* GUnrealAiForgetThreadConsole = nullptr;
static IConsoleObject* GUnrealAiDumpContextWindowConsole = nullptr;
static IConsoleObject* GUnrealAiDumpRecentUiConsole = nullptr;
static IConsoleObject* GUnrealAiDumpMemoriesConsole = nullptr;
static IConsoleObject* GUnrealAiPruneMemoriesConsole = nullptr;
static IConsoleObject* GUnrealAiDumpContextRankPolicyConsole = nullptr;
static IConsoleObject* GUnrealAiDumpContextDecisionLogsConsole = nullptr;
static IConsoleObject* GUnrealAiRetrievalRebuildConsole = nullptr;
static IConsoleObject* GUnrealAiRetrievalWaitConsole = nullptr;

static void RegisterUnrealAiEditorKeyBindings()
{
	static bool bDone = false;
	if (bDone)
	{
		return;
	}
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	const TSharedPtr<FUICommandList> Cmd = LevelEditorModule.GetGlobalLevelEditorActions();
	if (!Cmd.IsValid())
	{
		return;
	}
	const FUnrealAiEditorCommands& C = FUnrealAiEditorCommands::Get();
	Cmd->MapAction(
		C.OpenChatTab,
		FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::ChatTab);
		}));
	Cmd->MapAction(
		C.OpenSettingsTab,
		FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::SettingsTab);
		}));
	Cmd->MapAction(
		C.OpenQuickStartTab,
		FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::QuickStartTab);
		}));
	Cmd->MapAction(
		C.OpenHelpTab,
		FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::HelpTab);
		}));
	bDone = true;
}

void FUnrealAiEditorModule::StartupModule()
{
	GUnrealAiModule = this;

	BackendRegistry = MakeShared<FUnrealAiBackendRegistry>();

	if (IUnrealAiPersistence* P = BackendRegistry->GetPersistence())
	{
		FString Json;
		if (P->LoadSettingsJson(Json) && !Json.IsEmpty())
		{
			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
			if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
			{
				FUnrealAiEditorModule::HydrateEditorFocusFromJsonRoot(Root);
				FUnrealAiEditorModule::HydrateSubagentsFromJsonRoot(Root);
				FUnrealAiEditorModule::HydrateAgentCodeTypePreferenceFromJsonRoot(Root);
				FUnrealAiEditorModule::HydrateAutoConfirmDestructiveFromJsonRoot(Root);
			}
		}
	}

	FUnrealAiEditorStyle::Initialize();

	FUnrealAiEditorCommands::Register();

	const TSharedPtr<FUnrealAiBackendRegistry> Reg = BackendRegistry;

	RegisterTabs(Reg);
	RegisterMenus();
	UnrealAiBlueprintFormatEditorRegistrationStartup();
	RegisterSettings();
	RegisterOpenChatOnStartup();
	RegisterSaveOpenChatsOnExit();
	FUnrealAiEditorModalMonitor::Startup(BackendRegistry);
	if (BackendRegistry.IsValid())
	{
		FUnrealAiRecentUiTracker::Startup(
			BackendRegistry->GetPersistence(),
			UnrealAiProjectId::GetCurrentProjectId());
	}

	GUnrealAiCatalogMatrixConsole = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("UnrealAi.RunCatalogMatrix"),
		TEXT("Invoke catalog tools (optional filter substring), write Saved/UnrealAiEditor/Automation/tool_matrix_last.json"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			const FString Filter = (Args.Num() > 0) ? Args[0] : FString();
			TArray<FString> Violations;
			const bool bOk = UnrealAiToolCatalogMatrixRunner::RunAndWriteJson(Filter, &Violations);
			for (const FString& V : Violations)
			{
				UE_LOG(LogTemp, Warning, TEXT("%s"), *V);
			}
			const FString OutPath =
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAiEditor/Automation/tool_matrix_last.json"));
			UE_LOG(LogTemp, Display, TEXT("UnrealAi.RunCatalogMatrix: %s — wrote %s"),
				bOk ? TEXT("OK") : TEXT("contract violations (see log)"),
				*OutPath);
		}),
		ECVF_Default);

	GUnrealAiRunAgentTurnConsole = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("UnrealAi.RunAgentTurn"),
		TEXT("Run one agent harness turn (same path as Agent Chat). Args: <MessageFilePath> [ThreadGuid] [agent|ask|plan] [OutputDir] [dumpcontext|nodump]. UTF-8 text file required as first arg. Default per-tool context dumps follow UnrealAiRuntimeDefaults::HarnessDumpContextAfterEachToolDefault unless the 5th arg overrides. Writes Saved/UnrealAiEditor/HarnessRuns/<ts>/run.jsonl and optional context_window_*.txt."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1 || Args[0].IsEmpty())
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.RunAgentTurn: pass a UTF-8 message file as first arg"));
				return;
			}
			const FString& MsgFilePath = Args[0];
			const int32 BaseIdx = 1;
			FString Msg;
			if (!FFileHelper::LoadFileToString(Msg, *MsgFilePath))
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.RunAgentTurn: could not read %s"), *MsgFilePath);
				return;
			}
			FString ThreadId;
			if (Args.Num() > BaseIdx && !Args[BaseIdx].IsEmpty())
			{
				ThreadId = Args[BaseIdx];
			}
			else
			{
				ThreadId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
			}
			EUnrealAiAgentMode Mode = EUnrealAiAgentMode::Agent;
			if (Args.Num() > BaseIdx + 1)
			{
				if (Args[BaseIdx + 1].Equals(TEXT("ask"), ESearchCase::IgnoreCase))
				{
					Mode = EUnrealAiAgentMode::Ask;
				}
				else if (Args[BaseIdx + 1].Equals(TEXT("plan"), ESearchCase::IgnoreCase))
				{
					Mode = EUnrealAiAgentMode::Plan;
				}
			}
			FString OutDir = (Args.Num() > BaseIdx + 2) ? Args[BaseIdx + 2] : FString();
			bool bDumpContextAfterEachTool = UnrealAiRuntimeDefaults::HarnessDumpContextAfterEachToolDefault;
			if (Args.Num() > BaseIdx + 3)
			{
				if (Args[BaseIdx + 3].Equals(TEXT("dumpcontext"), ESearchCase::IgnoreCase))
				{
					bDumpContextAfterEachTool = true;
				}
				else if (Args[BaseIdx + 3].Equals(TEXT("nodump"), ESearchCase::IgnoreCase))
				{
					bDumpContextAfterEachTool = false;
				}
			}
			FString Jsonl;
			FString RunDir;
			bool bSucc = false;
			FString Err;
			const bool bRan = UnrealAiHarnessScenarioRunner::RunAgentTurnSync(
				Msg,
				ThreadId,
				Mode,
				OutDir,
				Jsonl,
				RunDir,
				bSucc,
				Err,
				bDumpContextAfterEachTool);
			UE_LOG(LogTemp, Display, TEXT("UnrealAi.RunAgentTurn: completed=%s harness_ok=%s dump_context=%s dir=%s jsonl=%s err=%s"),
				bRan ? TEXT("yes") : TEXT("no"),
				bSucc ? TEXT("yes") : TEXT("no"),
				bDumpContextAfterEachTool ? TEXT("yes") : TEXT("no"),
				*RunDir,
				*Jsonl,
				*Err);
		}),
		ECVF_Default);

	GUnrealAiRunStrictAssertionsConsole = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("UnrealAi.RunStrictAssertions"),
		TEXT("Run deterministic strict assertions using editor-side tools/state. Args: <AssertionsJsonPath> <OutputDirStepOrFolder>. Writes <OutputDir>/strict_assertions_result.json."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 2 || Args[0].IsEmpty() || Args[1].IsEmpty())
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.RunStrictAssertions: expected args <AssertionsJsonPath> <OutputDir>"));
				return;
			}
			const FString AssertionsPath = Args[0];
			const FString OutDir = Args[1];

			const TSharedPtr<FUnrealAiBackendRegistry> Reg = FUnrealAiEditorModule::GetBackendRegistry();
			if (!Reg.IsValid())
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.RunStrictAssertions: backend registry unavailable"));
				return;
			}

			IAgentContextService* Context = Reg->GetContextService();
			// Tools do not require context for read-only queries, but if assertion needs context-backed tools
			// it will still be safe to invoke since tool implementations typically only use editor state.
			(void)Context;

			IToolExecutionHost* Tools = Reg->GetToolExecutionHost();
			if (!Tools)
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.RunStrictAssertions: tool execution host unavailable"));
				return;
			}

			const FScopedHarnessStepOutputDir HarnessStepScopeForAssertions(OutDir);

			FString JsonText;
			if (!FFileHelper::LoadFileToString(JsonText, *AssertionsPath))
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.RunStrictAssertions: could not read assertions json %s"), *AssertionsPath);
				return;
			}

			bool bAllPass = true;
			TArray<TSharedPtr<FJsonValue>> AssertionResults;

			auto FailOne = [&](const FString& Type, const FString& Msg) -> TSharedPtr<FJsonObject>
			{
				TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
				R->SetStringField(TEXT("type"), Type);
				R->SetBoolField(TEXT("pass"), false);
				R->SetStringField(TEXT("message"), Msg);
				bAllPass = false;
				return R;
			};

			auto OkOne = [&](const FString& Type, const FString& Msg) -> TSharedPtr<FJsonObject>
			{
				TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
				R->SetStringField(TEXT("type"), Type);
				R->SetBoolField(TEXT("pass"), true);
				R->SetStringField(TEXT("message"), Msg);
				return R;
			};

			auto InvokeToolFromAssertion = [&](const TSharedPtr<FJsonObject>& Obj, FString& OutToolName, FUnrealAiToolInvocationResult& OutInv, FString& OutErr) -> bool
			{
				OutErr.Reset();
				OutToolName.Reset();
				if (!Obj.IsValid())
				{
					OutErr = TEXT("assertion object invalid");
					return false;
				}
				if (!Obj->TryGetStringField(TEXT("tool"), OutToolName) || OutToolName.TrimStartAndEnd().IsEmpty())
				{
					OutErr = TEXT("requires tool");
					return false;
				}
				const TSharedPtr<FJsonObject>* ArgsObjPtr = nullptr;
				if (!Obj->TryGetObjectField(TEXT("arguments"), ArgsObjPtr) || !ArgsObjPtr || !ArgsObjPtr->IsValid())
				{
					OutErr = TEXT("requires arguments (JSON object)");
					return false;
				}
				FString ArgsJson;
				{
					const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ArgsJson);
					if (!FJsonSerializer::Serialize((*ArgsObjPtr).ToSharedRef(), Writer))
					{
						OutErr = TEXT("failed to serialize arguments JSON");
						return false;
					}
				}
				OutInv = Tools->InvokeTool(OutToolName, ArgsJson, FString());
				return true;
			};

			auto ParseToolContentJson = [&](const FUnrealAiToolInvocationResult& Inv, TSharedPtr<FJsonObject>& OutObj) -> bool
			{
				OutObj.Reset();
				if (!Inv.bOk || Inv.ContentForModel.IsEmpty())
				{
					return false;
				}
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Inv.ContentForModel);
				return FJsonSerializer::Deserialize(Reader, OutObj) && OutObj.IsValid();
			};

			auto ResolvePathValue = [&](const TSharedPtr<FJsonObject>& Root, const FString& Path, TSharedPtr<FJsonValue>& OutVal) -> bool
			{
				OutVal.Reset();
				if (!Root.IsValid() || Path.TrimStartAndEnd().IsEmpty())
				{
					return false;
				}
				TArray<FString> Segments;
				Path.ParseIntoArray(Segments, TEXT("."), true);
				if (Segments.Num() == 0)
				{
					return false;
				}

				TSharedPtr<FJsonValue> Cur = MakeShared<FJsonValueObject>(Root);
				for (const FString& RawSeg : Segments)
				{
					FString Seg = RawSeg;
					Seg.TrimStartAndEndInline();
					if (!Cur.IsValid())
					{
						return false;
					}
					const TSharedPtr<FJsonObject>* CurObj = nullptr;
					if (Cur->TryGetObject(CurObj) && CurObj && (*CurObj).IsValid())
					{
						const TSharedPtr<FJsonValue>* Next = (*CurObj)->Values.Find(Seg);
						if (!Next || !(*Next).IsValid())
						{
							return false;
						}
						Cur = *Next;
						continue;
					}
					const TArray<TSharedPtr<FJsonValue>>* CurArr = nullptr;
					if (Cur->TryGetArray(CurArr) && CurArr)
					{
						if (!Seg.IsNumeric())
						{
							return false;
						}
						const int32 Idx = FCString::Atoi(*Seg);
						if (Idx < 0 || Idx >= CurArr->Num() || !(*CurArr)[Idx].IsValid())
						{
							return false;
						}
						Cur = (*CurArr)[Idx];
						continue;
					}
					return false;
				}

				OutVal = Cur;
				return OutVal.IsValid();
			};

			// Parse assertions as either JSON array or { "assertions": [...] } object.
			TArray<TSharedPtr<FJsonValue>> AssertionsArr;
			{
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
				if (!FJsonSerializer::Deserialize(Reader, AssertionsArr))
				{
					// Try wrapper object shape.
					TSharedPtr<FJsonObject> WrapObj;
					const TSharedRef<TJsonReader<>> Reader2 = TJsonReaderFactory<>::Create(JsonText);
					if (!FJsonSerializer::Deserialize(Reader2, WrapObj) || !WrapObj.IsValid())
					{
						TSharedPtr<FJsonObject> R = FailOne(TEXT("parse_error"), TEXT("assertions.json must be a JSON array or an object with assertions[]"));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						goto WriteOut;
					}
					const TArray<TSharedPtr<FJsonValue>>* Inner = nullptr;
					if (!WrapObj->TryGetArrayField(TEXT("assertions"), Inner) || !Inner)
					{
						TSharedPtr<FJsonObject> R = FailOne(TEXT("parse_error"), TEXT("assertions wrapper object missing assertions[]"));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						goto WriteOut;
					}
					AssertionsArr = *Inner;
				}
			}

			for (int32 i = 0; i < AssertionsArr.Num(); ++i)
			{
				const TSharedPtr<FJsonValue>& V = AssertionsArr[i];
				const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
				if (!V.IsValid() || !V->TryGetObject(ObjPtr) || !ObjPtr || !(*ObjPtr).IsValid())
				{
					TSharedPtr<FJsonObject> R = FailOne(TEXT("invalid_entry"), TEXT("assertion entry must be a JSON object"));
					AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
					continue;
				}
				TSharedPtr<FJsonObject> Obj = *ObjPtr;
				FString Type;
				if (!Obj->TryGetStringField(TEXT("type"), Type))
				{
					TSharedPtr<FJsonObject> R = FailOne(TEXT("missing_type"), TEXT("assertion.type is required"));
					AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
					continue;
				}
				Type.TrimStartAndEndInline();
				if (Type.IsEmpty())
				{
					TSharedPtr<FJsonObject> R = FailOne(TEXT("missing_type"), TEXT("assertion.type is required"));
					AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
					continue;
				}

				// ---- Assertion: asset_exists ----
				if (Type == TEXT("asset_exists"))
				{
					FString ObjectPath;
					if (!Obj->TryGetStringField(TEXT("object_path"), ObjectPath) || ObjectPath.TrimStartAndEnd().IsEmpty())
					{
						TSharedPtr<FJsonObject> R = FailOne(Type, TEXT("asset_exists requires object_path"));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}
					ObjectPath.TrimStartAndEndInline();
					UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
					const bool bExists = Loaded != nullptr;
					if (!bExists)
					{
						TSharedPtr<FJsonObject> R = FailOne(Type, FString::Printf(TEXT("asset does not exist: %s"), *ObjectPath));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}
					AssertionResults.Add(MakeShared<FJsonValueObject>(OkOne(Type, FString::Printf(TEXT("asset exists: %s"), *ObjectPath)).ToSharedRef()));
					continue;
				}

				// ---- Assertion: asset_not_exists ----
				if (Type == TEXT("asset_not_exists"))
				{
					FString ObjectPath;
					if (!Obj->TryGetStringField(TEXT("object_path"), ObjectPath) || ObjectPath.TrimStartAndEnd().IsEmpty())
					{
						TSharedPtr<FJsonObject> R = FailOne(Type, TEXT("asset_not_exists requires object_path"));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}
					ObjectPath.TrimStartAndEndInline();
					UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
					if (Loaded != nullptr)
					{
						TSharedPtr<FJsonObject> R = FailOne(Type, FString::Printf(TEXT("asset unexpectedly exists: %s"), *ObjectPath));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}
					AssertionResults.Add(MakeShared<FJsonValueObject>(OkOne(Type, FString::Printf(TEXT("asset not present: %s"), *ObjectPath)).ToSharedRef()));
					continue;
				}

				// ---- Assertion: project_file_exists ----
				if (Type == TEXT("project_file_exists"))
				{
					FString RelativePath;
					if (!Obj->TryGetStringField(TEXT("relative_path"), RelativePath) || RelativePath.TrimStartAndEnd().IsEmpty())
					{
						TSharedPtr<FJsonObject> R = FailOne(Type, TEXT("project_file_exists requires relative_path"));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}
					FString Abs;
					FString ResolveErr;
					if (!UnrealAiResolveProjectFilePath(RelativePath, Abs, ResolveErr))
					{
						TSharedPtr<FJsonObject> R = FailOne(Type, FString::Printf(TEXT("project_file_exists: %s"), *ResolveErr));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}
					if (!IFileManager::Get().FileExists(*Abs))
					{
						TSharedPtr<FJsonObject> R = FailOne(Type, FString::Printf(TEXT("file missing: %s"), *RelativePath));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}
					AssertionResults.Add(MakeShared<FJsonValueObject>(OkOne(Type, FString::Printf(TEXT("file exists: %s"), *RelativePath)).ToSharedRef()));
					continue;
				}

				// ---- Assertion: project_dir_exists ----
				if (Type == TEXT("project_dir_exists"))
				{
					FString RelativePath;
					if (!Obj->TryGetStringField(TEXT("relative_path"), RelativePath) || RelativePath.TrimStartAndEnd().IsEmpty())
					{
						TSharedPtr<FJsonObject> R = FailOne(Type, TEXT("project_dir_exists requires relative_path"));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}
					const FString Abs = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), RelativePath);
					if (!IFileManager::Get().DirectoryExists(*Abs))
					{
						TSharedPtr<FJsonObject> R = FailOne(Type, FString::Printf(TEXT("dir missing: %s"), *RelativePath));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}
					AssertionResults.Add(MakeShared<FJsonValueObject>(OkOne(Type, FString::Printf(TEXT("dir exists: %s"), *RelativePath)).ToSharedRef()));
					continue;
				}

				// ---- Assertion: tool_invoke_ok ----
				if (Type == TEXT("tool_invoke_ok"))
				{
					FString ToolName;
					FUnrealAiToolInvocationResult Inv;
					FString InvErr;
					if (!InvokeToolFromAssertion(Obj, ToolName, Inv, InvErr))
					{
						TSharedPtr<FJsonObject> R = FailOne(Type, FString::Printf(TEXT("tool_invoke_ok %s"), *InvErr));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}
					if (!Inv.bOk)
					{
						const FString Msg = FString::Printf(TEXT("tool invocation failed: %s (error_len=%d)"), *ToolName, Inv.ErrorMessage.Len());
						TSharedPtr<FJsonObject> R = FailOne(Type, Msg);
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}
					AssertionResults.Add(MakeShared<FJsonValueObject>(OkOne(Type, FString::Printf(TEXT("tool ok: %s"), *ToolName)).ToSharedRef()));
					continue;
				}

				// ---- Assertion: tool_invoke_fail ----
				if (Type == TEXT("tool_invoke_fail"))
				{
					FString ToolName;
					FUnrealAiToolInvocationResult Inv;
					FString InvErr;
					if (!InvokeToolFromAssertion(Obj, ToolName, Inv, InvErr))
					{
						TSharedPtr<FJsonObject> R = FailOne(Type, FString::Printf(TEXT("tool_invoke_fail %s"), *InvErr));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}
					if (Inv.bOk)
					{
						TSharedPtr<FJsonObject> R = FailOne(Type, FString::Printf(TEXT("expected tool failure but succeeded: %s"), *ToolName));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}
					AssertionResults.Add(MakeShared<FJsonValueObject>(OkOne(Type, FString::Printf(TEXT("tool failed as expected: %s"), *ToolName)).ToSharedRef()));
					continue;
				}

				// ---- Assertion: tool_result_path_exists ----
				if (Type == TEXT("tool_result_path_exists"))
				{
					FString ToolName;
					FUnrealAiToolInvocationResult Inv;
					FString InvErr;
					if (!InvokeToolFromAssertion(Obj, ToolName, Inv, InvErr))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("tool_result_path_exists %s"), *InvErr)).ToSharedRef()));
						continue;
					}
					TSharedPtr<FJsonObject> Root;
					if (!ParseToolContentJson(Inv, Root))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, TEXT("tool result is not parseable JSON")).ToSharedRef()));
						continue;
					}
					FString Path;
					if (!Obj->TryGetStringField(TEXT("path"), Path) || Path.TrimStartAndEnd().IsEmpty())
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, TEXT("requires path")).ToSharedRef()));
						continue;
					}
					TSharedPtr<FJsonValue> VPath;
					const bool bFound = ResolvePathValue(Root, Path, VPath);
					if (!bFound)
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("path missing: %s"), *Path)).ToSharedRef()));
						continue;
					}
					AssertionResults.Add(MakeShared<FJsonValueObject>(OkOne(Type, FString::Printf(TEXT("path exists: %s"), *Path)).ToSharedRef()));
					continue;
				}

				// ---- Assertion: tool_result_path_equals_string ----
				if (Type == TEXT("tool_result_path_equals_string"))
				{
					FString ToolName;
					FUnrealAiToolInvocationResult Inv;
					FString InvErr;
					if (!InvokeToolFromAssertion(Obj, ToolName, Inv, InvErr))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("tool_result_path_equals_string %s"), *InvErr)).ToSharedRef()));
						continue;
					}
					TSharedPtr<FJsonObject> Root;
					if (!ParseToolContentJson(Inv, Root))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, TEXT("tool result is not parseable JSON")).ToSharedRef()));
						continue;
					}
					FString Path;
					FString Expected;
					if (!Obj->TryGetStringField(TEXT("path"), Path) || !Obj->TryGetStringField(TEXT("equals"), Expected))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, TEXT("requires path and equals")).ToSharedRef()));
						continue;
					}
					TSharedPtr<FJsonValue> VPath;
					if (!ResolvePathValue(Root, Path, VPath))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("path missing: %s"), *Path)).ToSharedRef()));
						continue;
					}
					const FString Actual = VPath->AsString();
					if (!Actual.Equals(Expected, ESearchCase::CaseSensitive))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("string mismatch path=%s expected=%s actual=%s"), *Path, *Expected, *Actual)).ToSharedRef()));
						continue;
					}
					AssertionResults.Add(MakeShared<FJsonValueObject>(OkOne(Type, FString::Printf(TEXT("string equals at path=%s"), *Path)).ToSharedRef()));
					continue;
				}

				// ---- Assertion: tool_result_path_contains ----
				if (Type == TEXT("tool_result_path_contains"))
				{
					FString ToolName;
					FUnrealAiToolInvocationResult Inv;
					FString InvErr;
					if (!InvokeToolFromAssertion(Obj, ToolName, Inv, InvErr))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("tool_result_path_contains %s"), *InvErr)).ToSharedRef()));
						continue;
					}
					TSharedPtr<FJsonObject> Root;
					if (!ParseToolContentJson(Inv, Root))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, TEXT("tool result is not parseable JSON")).ToSharedRef()));
						continue;
					}
					FString Path;
					FString Needle;
					if (!Obj->TryGetStringField(TEXT("path"), Path) || !Obj->TryGetStringField(TEXT("contains"), Needle))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, TEXT("requires path and contains")).ToSharedRef()));
						continue;
					}
					TSharedPtr<FJsonValue> VPath;
					if (!ResolvePathValue(Root, Path, VPath))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("path missing: %s"), *Path)).ToSharedRef()));
						continue;
					}
					const FString Actual = VPath->AsString();
					if (!Actual.Contains(Needle))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("string does not contain needle path=%s needle=%s"), *Path, *Needle)).ToSharedRef()));
						continue;
					}
					AssertionResults.Add(MakeShared<FJsonValueObject>(OkOne(Type, FString::Printf(TEXT("string contains at path=%s"), *Path)).ToSharedRef()));
					continue;
				}

				// ---- Assertion: tool_result_array_min_length ----
				if (Type == TEXT("tool_result_array_min_length"))
				{
					FString ToolName;
					FUnrealAiToolInvocationResult Inv;
					FString InvErr;
					if (!InvokeToolFromAssertion(Obj, ToolName, Inv, InvErr))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("tool_result_array_min_length %s"), *InvErr)).ToSharedRef()));
						continue;
					}
					TSharedPtr<FJsonObject> Root;
					if (!ParseToolContentJson(Inv, Root))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, TEXT("tool result is not parseable JSON")).ToSharedRef()));
						continue;
					}
					FString Path;
					double MinD = 1.0;
					if (!Obj->TryGetStringField(TEXT("path"), Path) || !Obj->TryGetNumberField(TEXT("min"), MinD))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, TEXT("requires path and min")).ToSharedRef()));
						continue;
					}
					const int32 MinLen = FMath::Max(0, static_cast<int32>(MinD));
					TSharedPtr<FJsonValue> VPath;
					if (!ResolvePathValue(Root, Path, VPath))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("path missing: %s"), *Path)).ToSharedRef()));
						continue;
					}
					const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
					if (!VPath->TryGetArray(Arr) || !Arr)
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("path is not array: %s"), *Path)).ToSharedRef()));
						continue;
					}
					if (Arr->Num() < MinLen)
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("array too short path=%s min=%d actual=%d"), *Path, MinLen, Arr->Num())).ToSharedRef()));
						continue;
					}
					AssertionResults.Add(MakeShared<FJsonValueObject>(OkOne(Type, FString::Printf(TEXT("array min length satisfied path=%s"), *Path)).ToSharedRef()));
					continue;
				}

				// ---- Assertion: tool_result_number_gte ----
				if (Type == TEXT("tool_result_number_gte"))
				{
					FString ToolName;
					FUnrealAiToolInvocationResult Inv;
					FString InvErr;
					if (!InvokeToolFromAssertion(Obj, ToolName, Inv, InvErr))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("tool_result_number_gte %s"), *InvErr)).ToSharedRef()));
						continue;
					}
					TSharedPtr<FJsonObject> Root;
					if (!ParseToolContentJson(Inv, Root))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, TEXT("tool result is not parseable JSON")).ToSharedRef()));
						continue;
					}
					FString Path;
					double MinV = 0.0;
					if (!Obj->TryGetStringField(TEXT("path"), Path) || !Obj->TryGetNumberField(TEXT("min"), MinV))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, TEXT("requires path and min")).ToSharedRef()));
						continue;
					}
					TSharedPtr<FJsonValue> VPath;
					if (!ResolvePathValue(Root, Path, VPath))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("path missing: %s"), *Path)).ToSharedRef()));
						continue;
					}
					const double Actual = VPath->AsNumber();
					if (Actual < MinV)
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("number too small path=%s min=%g actual=%g"), *Path, MinV, Actual)).ToSharedRef()));
						continue;
					}
					AssertionResults.Add(MakeShared<FJsonValueObject>(OkOne(Type, FString::Printf(TEXT("number gte satisfied path=%s"), *Path)).ToSharedRef()));
					continue;
				}

				// ---- Assertion: tool_result_number_lte ----
				if (Type == TEXT("tool_result_number_lte"))
				{
					FString ToolName;
					FUnrealAiToolInvocationResult Inv;
					FString InvErr;
					if (!InvokeToolFromAssertion(Obj, ToolName, Inv, InvErr))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("tool_result_number_lte %s"), *InvErr)).ToSharedRef()));
						continue;
					}
					TSharedPtr<FJsonObject> Root;
					if (!ParseToolContentJson(Inv, Root))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, TEXT("tool result is not parseable JSON")).ToSharedRef()));
						continue;
					}
					FString Path;
					double MaxV = 0.0;
					if (!Obj->TryGetStringField(TEXT("path"), Path) || !Obj->TryGetNumberField(TEXT("max"), MaxV))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, TEXT("requires path and max")).ToSharedRef()));
						continue;
					}
					TSharedPtr<FJsonValue> VPath;
					if (!ResolvePathValue(Root, Path, VPath))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("path missing: %s"), *Path)).ToSharedRef()));
						continue;
					}
					const double Actual = VPath->AsNumber();
					if (Actual > MaxV)
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("number too large path=%s max=%g actual=%g"), *Path, MaxV, Actual)).ToSharedRef()));
						continue;
					}
					AssertionResults.Add(MakeShared<FJsonValueObject>(OkOne(Type, FString::Printf(TEXT("number lte satisfied path=%s"), *Path)).ToSharedRef()));
					continue;
				}

				// ---- Assertion: tool_result_bool_equals ----
				if (Type == TEXT("tool_result_bool_equals"))
				{
					FString ToolName;
					FUnrealAiToolInvocationResult Inv;
					FString InvErr;
					if (!InvokeToolFromAssertion(Obj, ToolName, Inv, InvErr))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("tool_result_bool_equals %s"), *InvErr)).ToSharedRef()));
						continue;
					}
					TSharedPtr<FJsonObject> Root;
					if (!ParseToolContentJson(Inv, Root))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, TEXT("tool result is not parseable JSON")).ToSharedRef()));
						continue;
					}
					FString Path;
					bool Expected = false;
					if (!Obj->TryGetStringField(TEXT("path"), Path) || !Obj->TryGetBoolField(TEXT("equals"), Expected))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, TEXT("requires path and equals(bool)")).ToSharedRef()));
						continue;
					}
					TSharedPtr<FJsonValue> VPath;
					if (!ResolvePathValue(Root, Path, VPath))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("path missing: %s"), *Path)).ToSharedRef()));
						continue;
					}
					const bool Actual = VPath->AsBool();
					if (Actual != Expected)
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("bool mismatch path=%s expected=%s actual=%s"), *Path, Expected ? TEXT("true") : TEXT("false"), Actual ? TEXT("true") : TEXT("false"))).ToSharedRef()));
						continue;
					}
					AssertionResults.Add(MakeShared<FJsonValueObject>(OkOne(Type, FString::Printf(TEXT("bool equals satisfied path=%s"), *Path)).ToSharedRef()));
					continue;
				}

				// ---- Assertion: tool_result_string_nonempty ----
				if (Type == TEXT("tool_result_string_nonempty"))
				{
					FString ToolName;
					FUnrealAiToolInvocationResult Inv;
					FString InvErr;
					if (!InvokeToolFromAssertion(Obj, ToolName, Inv, InvErr))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("tool_result_string_nonempty %s"), *InvErr)).ToSharedRef()));
						continue;
					}
					TSharedPtr<FJsonObject> Root;
					if (!ParseToolContentJson(Inv, Root))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, TEXT("tool result is not parseable JSON")).ToSharedRef()));
						continue;
					}
					FString Path;
					if (!Obj->TryGetStringField(TEXT("path"), Path))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, TEXT("requires path")).ToSharedRef()));
						continue;
					}
					TSharedPtr<FJsonValue> VPath;
					if (!ResolvePathValue(Root, Path, VPath))
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("path missing: %s"), *Path)).ToSharedRef()));
						continue;
					}
					FString Actual = VPath->AsString();
					Actual.TrimStartAndEndInline();
					if (Actual.IsEmpty())
					{
						AssertionResults.Add(MakeShared<FJsonValueObject>(FailOne(Type, FString::Printf(TEXT("string empty at path=%s"), *Path)).ToSharedRef()));
						continue;
					}
					AssertionResults.Add(MakeShared<FJsonValueObject>(OkOne(Type, FString::Printf(TEXT("string non-empty path=%s"), *Path)).ToSharedRef()));
					continue;
				}

				// ---- Assertion: blueprint_export_ir_node_count_min ----
				if (Type == TEXT("blueprint_export_ir_node_count_min"))
				{
					FString BlueprintPath;
					if (!Obj->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.TrimStartAndEnd().IsEmpty())
					{
						TSharedPtr<FJsonObject> R = FailOne(Type, TEXT("blueprint_export_ir_node_count_min requires blueprint_path"));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}
					FString GraphName;
					Obj->TryGetStringField(TEXT("graph_name"), GraphName);
					int32 MinNodes = 1;
					double MinNodesD = static_cast<double>(MinNodes);
					if (Obj->TryGetNumberField(TEXT("min_nodes"), MinNodesD))
					{
						MinNodes = FMath::Max(0, static_cast<int32>(MinNodesD));
					}

					// Prepare args for blueprint_export_ir.
					TSharedPtr<FJsonObject> ArgsObj = MakeShared<FJsonObject>();
					ArgsObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
					if (!GraphName.TrimStartAndEnd().IsEmpty())
					{
						ArgsObj->SetStringField(TEXT("graph_name"), GraphName);
					}
					FString ArgsJson;
					{
						const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ArgsJson);
						if (!FJsonSerializer::Serialize(ArgsObj.ToSharedRef(), Writer))
						{
							TSharedPtr<FJsonObject> R = FailOne(Type, TEXT("failed to serialize tool args for blueprint_export_ir"));
							AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
							continue;
						}
					}

					const FUnrealAiToolInvocationResult Inv = Tools->InvokeTool(TEXT("blueprint_export_ir"), ArgsJson, FString());
					if (!Inv.bOk)
					{
						TSharedPtr<FJsonObject> R = FailOne(Type, FString::Printf(TEXT("blueprint_export_ir failed: %s"), *Inv.ErrorMessage));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}

					TSharedPtr<FJsonObject> Root;
					{
						const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Inv.ContentForModel);
						if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
						{
							TSharedPtr<FJsonObject> R = FailOne(Type, TEXT("failed to parse blueprint_export_ir JSON output"));
							AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
							continue;
						}
					}

					const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
					bool bFoundNodes = false;
					if (Root->TryGetArrayField(TEXT("nodes"), NodesArr) && NodesArr)
					{
						bFoundNodes = true;
					}
					else
					{
						// Some tools return { "ok": true, "ir": { ... "nodes": [...] } }
						const TSharedPtr<FJsonObject>* IrObj = nullptr;
						if (Root->TryGetObjectField(TEXT("ir"), IrObj) && IrObj && (*IrObj).IsValid())
						{
							if ((*IrObj)->TryGetArrayField(TEXT("nodes"), NodesArr) && NodesArr)
							{
								bFoundNodes = true;
							}
						}
					}
					if (!bFoundNodes)
					{
						const FString Prefix = Inv.ContentForModel.Left(600);
						TSharedPtr<FJsonObject> R = FailOne(
							Type,
							FString::Printf(TEXT("blueprint_export_ir JSON missing nodes[] (searched root and ir.nodes); content_prefix=%s"), *Prefix));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}

					const int32 NodeCount = NodesArr->Num();
					const bool bOk = NodeCount >= MinNodes;
					if (!bOk)
					{
						TSharedPtr<FJsonObject> R = FailOne(Type, FString::Printf(TEXT("node_count too small: %d < %d"), NodeCount, MinNodes));
						AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
						continue;
					}
					AssertionResults.Add(MakeShared<FJsonValueObject>(OkOne(Type, FString::Printf(TEXT("node_count ok: %d >= %d"), NodeCount, MinNodes)).ToSharedRef()));
					continue;
				}

				// Unknown assertion type.
				{
					TSharedPtr<FJsonObject> R = FailOne(Type, TEXT("unknown assertion type"));
					AssertionResults.Add(MakeShared<FJsonValueObject>(R.ToSharedRef()));
				}
			}

		WriteOut:
			{
				TSharedPtr<FJsonObject> OutObj = MakeShared<FJsonObject>();
				OutObj->SetBoolField(TEXT("pass"), bAllPass);
				OutObj->SetStringField(TEXT("assertions_path"), AssertionsPath);
				OutObj->SetStringField(TEXT("output_dir"), OutDir);
				TArray<TSharedPtr<FJsonValue>> ResArr;
				for (const TSharedPtr<FJsonValue>& V2 : AssertionResults)
				{
					ResArr.Add(V2);
				}
				OutObj->SetArrayField(TEXT("assertions"), ResArr);
				FString OutJson;
				{
					const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
					FJsonSerializer::Serialize(OutObj.ToSharedRef(), Writer);
				}
				const FString OutPath = FPaths::Combine(OutDir, TEXT("strict_assertions_result.json"));
				IFileManager::Get().MakeDirectory(*OutDir, true);
				FFileHelper::SaveStringToFile(OutJson + TEXT("\n"), *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
				UE_LOG(LogTemp, Display, TEXT("UnrealAi.RunStrictAssertions: pass=%s wrote %s"), bAllPass ? TEXT("yes") : TEXT("no"), *OutPath);
			}
		}),
		ECVF_Default);

	GUnrealAiForgetThreadConsole = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("UnrealAi.ForgetThread"),
		TEXT("Remove persisted thread data and clear in-memory context for that thread. Args: <ThreadGuid>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1 || Args[0].IsEmpty())
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.ForgetThread: pass thread GUID (digits-with-hyphens)"));
				return;
			}
			FGuid G;
			if (!FGuid::Parse(Args[0], G) || !G.IsValid())
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.ForgetThread: invalid GUID: %s"), *Args[0]);
				return;
			}
			const FString ThreadId = G.ToString(EGuidFormats::DigitsWithHyphens);
			const TSharedPtr<FUnrealAiBackendRegistry> Reg = FUnrealAiEditorModule::GetBackendRegistry();
			if (!Reg.IsValid())
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.ForgetThread: backend registry unavailable"));
				return;
			}
			const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
			if (IAgentContextService* Ctx = Reg->GetContextService())
			{
				Ctx->ClearSession(ProjectId, ThreadId);
			}
			if (IUnrealAiPersistence* Persist = Reg->GetPersistence())
			{
				Persist->ForgetThread(ProjectId, ThreadId);
			}
			UE_LOG(LogTemp, Display, TEXT("UnrealAi.ForgetThread: cleared thread=%s"), *ThreadId);
		}),
		ECVF_Default);

	GUnrealAiDumpContextWindowConsole = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("UnrealAi.DumpContextWindow"),
		TEXT("Write built context block for a thread without running the LLM. Args: <ThreadGuid> [reason_slug]. Output: Saved/UnrealAiEditor/ContextSnapshots/<reason>_<utc_ts>.txt. Call LoadOrCreate implicitly; refreshes editor snapshot first."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1 || Args[0].IsEmpty())
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.DumpContextWindow: first arg must be thread GUID (digits-with-hyphens)"));
				return;
			}
			const TSharedPtr<FUnrealAiBackendRegistry> Reg = FUnrealAiEditorModule::GetBackendRegistry();
			if (!Reg.IsValid())
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.DumpContextWindow: backend registry not available"));
				return;
			}
			IAgentContextService* Ctx = Reg->GetContextService();
			if (!Ctx)
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.DumpContextWindow: context service not available"));
				return;
			}
			FString Reason = (Args.Num() > 1 && !Args[1].IsEmpty()) ? Args[1] : FString(TEXT("manual"));
			for (int32 i = 0; i < Reason.Len(); ++i)
			{
				TCHAR& C = Reason[i];
				if (!FChar::IsAlnum(C) && C != TEXT('_') && C != TEXT('-'))
				{
					C = TEXT('_');
				}
			}
			const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
			const FString& ThreadId = Args[0];
			Ctx->LoadOrCreate(ProjectId, ThreadId);
			Ctx->RefreshEditorSnapshotFromEngine();
			FAgentContextBuildOptions Opt;
			Opt.Mode = EUnrealAiAgentMode::Agent;
			Opt.ContextBuildInvocationReason = TEXT("console_dump_context_window");
			Opt.bVerboseContextBuild = UnrealAiRuntimeDefaults::ContextVerboseDefault;
			const FAgentContextBuildResult Built = Ctx->BuildContextWindow(Opt);
			const FString Ts = FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
			const FString OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAiEditor/ContextSnapshots"));
			IFileManager::Get().MakeDirectory(*OutDir, true);
			const FString OutPath = FPaths::Combine(OutDir, FString::Printf(TEXT("%s_%s.txt"), *Reason, *Ts));
			if (!FFileHelper::SaveStringToFile(Built.ContextBlock, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.DumpContextWindow: failed to write %s"), *OutPath);
				return;
			}
			if (Opt.bVerboseContextBuild && Built.VerboseTraceLines.Num() > 0)
			{
				const FString TracePath = FPaths::Combine(OutDir, FString::Printf(TEXT("%s_%s_trace.txt"), *Reason, *Ts));
				FString Trace;
				Trace += FString::Printf(TEXT("Context build trace (reason=%s)\n"), *Reason);
				if (Built.Warnings.Num() > 0)
				{
					Trace += FString::Printf(TEXT("Warnings (%d)\n"), Built.Warnings.Num());
					for (const FString& W : Built.Warnings)
					{
						Trace += FString::Printf(TEXT("- %s\n"), *W);
					}
					Trace += TEXT("\n");
				}
				for (const FString& L : Built.VerboseTraceLines)
				{
					Trace += L + TEXT("\n");
				}
				FFileHelper::SaveStringToFile(Trace, *TracePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			}
			UE_LOG(LogTemp, Display, TEXT("UnrealAi.DumpContextWindow: wrote %s (chars=%d warnings=%d)"),
				*OutPath,
				Built.ContextBlock.Len(),
				Built.Warnings.Num());
			for (const FString& W : Built.Warnings)
			{
				UE_LOG(LogTemp, Warning, TEXT("UnrealAi.DumpContextWindow: %s"), *W);
			}
		}),
		ECVF_Default);

	GUnrealAiDumpRecentUiConsole = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("UnrealAi.DumpRecentUi"),
		TEXT("Dump ranked recent UI entries (global + active-thread overlay)."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			TArray<FRecentUiEntry> GlobalHistory;
			TArray<FRecentUiEntry> ThreadOverlay;
			FUnrealAiRecentUiTracker::GetProjectGlobalHistory(GlobalHistory);
			if (const TSharedPtr<FUnrealAiChatUiSession> Sess = FUnrealAiEditorModule::GetActiveChatSession())
			{
				if (Sess->ThreadId.IsValid())
				{
					FUnrealAiRecentUiTracker::GetThreadOverlay(Sess->ThreadId.ToString(EGuidFormats::DigitsWithHyphens), ThreadOverlay);
				}
			}
			TArray<FRecentUiEntry> Ranked;
			UnrealAiRecentUiRanking::MergeAndRank(GlobalHistory, ThreadOverlay, 24, Ranked);
			UE_LOG(LogTemp, Display, TEXT("UnrealAi.DumpRecentUi: global=%d thread=%d ranked=%d"), GlobalHistory.Num(), ThreadOverlay.Num(), Ranked.Num());
			const FDateTime NowUtc = FDateTime::UtcNow();
			for (const FRecentUiEntry& E : Ranked)
			{
				const UnrealAiRecentUiRanking::FScoreBreakdown B = UnrealAiRecentUiRanking::ScoreEntry(E, NowUtc);
				UE_LOG(
					LogTemp,
					Display,
					TEXT("  score=%.1f base=%.1f recency=%.1f freq=%.1f active=%.1f thread=%.1f kind=%d label=%s id=%s"),
					B.Score,
					B.BaseImportance,
					B.Recency,
					B.Frequency,
					B.ActiveBonus,
					B.ThreadOverlayBonus,
					static_cast<int32>(E.UiKind),
					*E.DisplayName,
					*E.StableId);
			}
		}),
		ECVF_Default);

	GUnrealAiDumpContextRankPolicyConsole = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("UnrealAi.DumpContextRankPolicy"),
		TEXT("Dump unified context ranking policy knobs (single-source manual tuning values)."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			using namespace UnrealAiContextRankingPolicy;
			const FScoreWeights W = GetScoreWeights();
			UE_LOG(LogTemp, Display, TEXT("Context ranking policy:"));
			UE_LOG(LogTemp, Display, TEXT("  weights mention=%.1f semantic=%.1f recency=%.1f freshness=%.1f safety_penalty=%.1f active=%.1f thread=%.1f frequency=%.1f"),
				W.MentionHit, W.HeuristicSemantic, W.Recency, W.FreshnessReliability, W.SafetyPenalty, W.ActiveBonus, W.ThreadOverlayBonus, W.Frequency);
			UE_LOG(LogTemp, Display, TEXT("  base_importance recent_tab=%.1f attachment=%.1f tool_result=%.1f editor_snapshot=%.1f todo=%.1f plan=%.1f"),
				GetBaseTypeImportance(ECandidateType::RecentTab),
				GetBaseTypeImportance(ECandidateType::Attachment),
				GetBaseTypeImportance(ECandidateType::ToolResult),
				GetBaseTypeImportance(ECandidateType::EditorSnapshotField),
				GetBaseTypeImportance(ECandidateType::TodoState),
				GetBaseTypeImportance(ECandidateType::PlanState));
			UE_LOG(LogTemp, Display, TEXT("  caps recent=%d attachment=%d tool=%d snapshot=%d todo=%d plan=%d"),
				GetPerTypeCap(ECandidateType::RecentTab),
				GetPerTypeCap(ECandidateType::Attachment),
				GetPerTypeCap(ECandidateType::ToolResult),
				GetPerTypeCap(ECandidateType::EditorSnapshotField),
				GetPerTypeCap(ECandidateType::TodoState),
				GetPerTypeCap(ECandidateType::PlanState));
		}),
		ECVF_Default);

	GUnrealAiDumpMemoriesConsole = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("UnrealAi.DumpMemories"),
		TEXT("Dump indexed memory records."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			const TSharedPtr<FUnrealAiBackendRegistry> Reg = FUnrealAiEditorModule::GetBackendRegistry();
			if (!Reg.IsValid())
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.DumpMemories: backend registry unavailable"));
				return;
			}
			IUnrealAiMemoryService* Memory = Reg->GetMemoryService();
			if (!Memory)
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.DumpMemories: memory service unavailable"));
				return;
			}
			TArray<FUnrealAiMemoryIndexRow> Rows;
			Memory->ListMemories(Rows);
			UE_LOG(LogTemp, Display, TEXT("UnrealAi.DumpMemories: count=%d"), Rows.Num());
			for (const FUnrealAiMemoryIndexRow& Row : Rows)
			{
				UE_LOG(LogTemp, Display, TEXT("  id=%s conf=%.2f title=%s"), *Row.Id, Row.Confidence, *Row.Title);
			}
		}),
		ECVF_Default);

	GUnrealAiPruneMemoriesConsole = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("UnrealAi.PruneMemories"),
		TEXT("Prune memories using defaults (maxItems=500 retentionDays=30 minConfidence=0.55)."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			const TSharedPtr<FUnrealAiBackendRegistry> Reg = FUnrealAiEditorModule::GetBackendRegistry();
			if (!Reg.IsValid())
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.PruneMemories: backend registry unavailable"));
				return;
			}
			IUnrealAiMemoryService* Memory = Reg->GetMemoryService();
			if (!Memory)
			{
				UE_LOG(LogTemp, Error, TEXT("UnrealAi.PruneMemories: memory service unavailable"));
				return;
			}
			const int32 Removed = Memory->Prune(500, 30, 0.55f);
			UE_LOG(LogTemp, Display, TEXT("UnrealAi.PruneMemories: removed=%d"), Removed);
		}),
		ECVF_Default);

	GUnrealAiDumpContextDecisionLogsConsole = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("UnrealAi.DumpContextDecisionLogs"),
		TEXT("Print latest context decision log files under Saved/UnrealAiEditor/ContextDecisionLogs."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealAiEditor/ContextDecisionLogs"));
			if (!IFileManager::Get().DirectoryExists(*Root))
			{
				UE_LOG(LogTemp, Display, TEXT("UnrealAi.DumpContextDecisionLogs: no logs yet (%s)"), *Root);
				return;
			}
			TArray<FString> ThreadDirs;
			IFileManager::Get().FindFiles(ThreadDirs, *Root, false, true);
			UE_LOG(LogTemp, Display, TEXT("UnrealAi.DumpContextDecisionLogs: root=%s thread_dirs=%d"), *Root, ThreadDirs.Num());
			for (const FString& D : ThreadDirs)
			{
				const FString Dir = FPaths::Combine(Root, D);
				TArray<FString> JsonlFiles;
				IFileManager::Get().FindFiles(JsonlFiles, *FPaths::Combine(Dir, TEXT("*.jsonl")), true, false);
				JsonlFiles.Sort([](const FString& A, const FString& B) { return A > B; });
				UE_LOG(LogTemp, Display, TEXT("  thread=%s files=%d"), *D, JsonlFiles.Num());
				for (int32 i = 0; i < JsonlFiles.Num() && i < 5; ++i)
				{
					UE_LOG(LogTemp, Display, TEXT("    %s"), *FPaths::Combine(Dir, JsonlFiles[i]));
				}
			}
		}),
		ECVF_Default);

	GUnrealAiRetrievalRebuildConsole = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("UnrealAi.Retrieval.RebuildNow"),
		TEXT("Request a local retrieval index rebuild for the current project."),
		FConsoleCommandDelegate::CreateLambda([Reg]()
		{
			if (!Reg.IsValid() || !Reg->GetRetrievalService())
			{
				UE_LOG(LogTemp, Warning, TEXT("UnrealAi.Retrieval.RebuildNow: retrieval service unavailable"));
				return;
			}
			const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
			Reg->GetRetrievalService()->RequestRebuild(ProjectId);
			UE_LOG(LogTemp, Display, TEXT("UnrealAi.Retrieval.RebuildNow: requested rebuild for project_id=%s"), *ProjectId);
		}),
		ECVF_Default);

	GUnrealAiRetrievalWaitConsole = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("UnrealAi.Retrieval.WaitForReady"),
		TEXT("Block until retrieval index is ready or timeout. Args: [TimeoutSec=120]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([Reg](const TArray<FString>& Args)
		{
			if (!Reg.IsValid() || !Reg->GetRetrievalService())
			{
				UE_LOG(LogTemp, Warning, TEXT("UnrealAi.Retrieval.WaitForReady: retrieval service unavailable"));
				return;
			}

			const int32 TimeoutSec = (Args.Num() > 0) ? FCString::Atoi(*Args[0]) : 120;
			const double Deadline = FPlatformTime::Seconds() + FMath::Max(1, TimeoutSec);
			const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();

			IUnrealAiRetrievalService* Retrieval = Reg->GetRetrievalService();
			while (FPlatformTime::Seconds() < Deadline)
			{
				const FUnrealAiRetrievalProjectStatus S = Retrieval->GetProjectStatus(ProjectId);
				if (!S.bBusy && S.StateText.Equals(TEXT("ready"), ESearchCase::IgnoreCase))
				{
					UE_LOG(LogTemp, Display, TEXT("UnrealAi.Retrieval.WaitForReady: ready files=%d chunks=%d"),
						S.FilesIndexed, S.ChunksIndexed);
					return;
				}
				FPlatformProcess::Sleep(0.2f);
			}

			const FUnrealAiRetrievalProjectStatus S = Retrieval->GetProjectStatus(ProjectId);
			UE_LOG(LogTemp, Warning, TEXT("UnrealAi.Retrieval.WaitForReady: timeout state=%s busy=%s files=%d chunks=%d"),
				*S.StateText,
				S.bBusy ? TEXT("true") : TEXT("false"),
				S.FilesIndexed,
				S.ChunksIndexed);
		}),
		ECVF_Default);
}

void FUnrealAiEditorModule::ShutdownModule()
{
	if (GUnrealAiDumpContextWindowConsole)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GUnrealAiDumpContextWindowConsole);
		GUnrealAiDumpContextWindowConsole = nullptr;
	}
	if (GUnrealAiDumpRecentUiConsole)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GUnrealAiDumpRecentUiConsole);
		GUnrealAiDumpRecentUiConsole = nullptr;
	}
	if (GUnrealAiDumpMemoriesConsole)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GUnrealAiDumpMemoriesConsole);
		GUnrealAiDumpMemoriesConsole = nullptr;
	}
	if (GUnrealAiPruneMemoriesConsole)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GUnrealAiPruneMemoriesConsole);
		GUnrealAiPruneMemoriesConsole = nullptr;
	}
	if (GUnrealAiDumpContextRankPolicyConsole)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GUnrealAiDumpContextRankPolicyConsole);
		GUnrealAiDumpContextRankPolicyConsole = nullptr;
	}
	if (GUnrealAiDumpContextDecisionLogsConsole)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GUnrealAiDumpContextDecisionLogsConsole);
		GUnrealAiDumpContextDecisionLogsConsole = nullptr;
	}
	if (GUnrealAiRetrievalRebuildConsole)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GUnrealAiRetrievalRebuildConsole);
		GUnrealAiRetrievalRebuildConsole = nullptr;
	}
	if (GUnrealAiRetrievalWaitConsole)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GUnrealAiRetrievalWaitConsole);
		GUnrealAiRetrievalWaitConsole = nullptr;
	}
	if (GUnrealAiRunAgentTurnConsole)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GUnrealAiRunAgentTurnConsole);
		GUnrealAiRunAgentTurnConsole = nullptr;
	}
	if (GUnrealAiRunStrictAssertionsConsole)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GUnrealAiRunStrictAssertionsConsole);
		GUnrealAiRunStrictAssertionsConsole = nullptr;
	}
	if (GUnrealAiForgetThreadConsole)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GUnrealAiForgetThreadConsole);
		GUnrealAiForgetThreadConsole = nullptr;
	}
	if (GUnrealAiCatalogMatrixConsole)
	{
		IConsoleManager::Get().UnregisterConsoleObject(GUnrealAiCatalogMatrixConsole);
		GUnrealAiCatalogMatrixConsole = nullptr;
	}

	FUnrealAiEditorModalMonitor::Shutdown();
	FUnrealAiRecentUiTracker::Shutdown();
	CancelDeferredAgentChatInsertsForShutdown();

	if (BackendRegistry.IsValid())
	{
		if (IAgentContextService* Ctx = BackendRegistry->GetContextService())
		{
			Ctx->FlushAllSessionsToDisk();
		}
		if (IUnrealAiMemoryService* Memory = BackendRegistry->GetMemoryService())
		{
			Memory->Flush();
		}
	}

	UnregisterSettings();
	UnregisterOpenChatOnStartup();
	UnregisterSaveOpenChatsOnExit();
	SaveOpenAgentChatTabsNow();
	UnrealAiBlueprintFormatEditorRegistrationShutdown();
	UnregisterMenus();
	UnregisterTabs();

	FUnrealAiEditorCommands::Unregister();

	FUnrealAiEditorStyle::Shutdown();

	BackendRegistry.Reset();

	GUnrealAiModule = nullptr;
}

FUnrealAiEditorModule& FUnrealAiEditorModule::Get()
{
	check(GUnrealAiModule);
	return *GUnrealAiModule;
}

TSharedPtr<FUnrealAiBackendRegistry> FUnrealAiEditorModule::GetBackendRegistry()
{
	return GUnrealAiModule ? GUnrealAiModule->BackendRegistry : nullptr;
}

void FUnrealAiEditorModule::SetActiveChatSession(TSharedPtr<FUnrealAiChatUiSession> Session)
{
	if (GUnrealAiModule)
	{
		GUnrealAiModule->ActiveChatSession = Session;
		if (Session.IsValid() && Session->ThreadId.IsValid())
		{
			FUnrealAiRecentUiTracker::SetActiveThreadId(Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens));
		}
		else
		{
			FUnrealAiRecentUiTracker::SetActiveThreadId(FString());
		}
	}
}

TSharedPtr<FUnrealAiChatUiSession> FUnrealAiEditorModule::GetActiveChatSession()
{
	return GUnrealAiModule ? GUnrealAiModule->ActiveChatSession.Pin() : nullptr;
}

void FUnrealAiEditorModule::NotifyContextAttachmentsChanged()
{
	OnContextAttachmentsChanged().Broadcast();
}

FSimpleMulticastDelegate& FUnrealAiEditorModule::OnContextAttachmentsChanged()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

namespace UnrealAiEditorModulePriv
{
	static TSharedPtr<FJsonObject> CloneJsonObjectShallow(const TSharedPtr<FJsonObject>& Src)
	{
		if (!Src.IsValid())
		{
			return MakeShared<FJsonObject>();
		}
		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		if (!FJsonSerializer::Serialize(Src.ToSharedRef(), W))
		{
			return MakeShared<FJsonObject>();
		}
		TSharedPtr<FJsonObject> Dst;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Out);
		if (!FJsonSerializer::Deserialize(Reader, Dst) || !Dst.IsValid())
		{
			return MakeShared<FJsonObject>();
		}
		return Dst;
	}
}

bool FUnrealAiEditorModule::IsEditorFocusEnabled()
{
	return GUnrealAiModule ? GUnrealAiModule->bEditorFocusEnabled : false;
}

bool FUnrealAiEditorModule::IsSubagentsEnabled()
{
	return GUnrealAiModule ? GUnrealAiModule->bSubagentsEnabled : true;
}

void FUnrealAiEditorModule::HydrateEditorFocusFromJsonRoot(const TSharedPtr<FJsonObject>& Root)
{
	if (!GUnrealAiModule || !Root.IsValid())
	{
		return;
	}
	bool b = false;
	const TSharedPtr<FJsonObject>* UiObj = nullptr;
	if (Root->TryGetObjectField(TEXT("ui"), UiObj) && UiObj && UiObj->IsValid())
	{
		if (!(*UiObj)->TryGetBoolField(TEXT("editorFocus"), b))
		{
			b = false;
		}
	}
	GUnrealAiModule->bEditorFocusEnabled = b;
}

void FUnrealAiEditorModule::SetEditorFocusEnabled(bool bEnabled)
{
	if (!GUnrealAiModule)
	{
		return;
	}
	if (GUnrealAiModule->bEditorFocusEnabled == bEnabled)
	{
		return;
	}
	GUnrealAiModule->bEditorFocusEnabled = bEnabled;

	if (GUnrealAiModule->BackendRegistry.IsValid())
	{
		if (IUnrealAiPersistence* P = GUnrealAiModule->BackendRegistry->GetPersistence())
		{
			FString Json;
			if (P->LoadSettingsJson(Json) && !Json.IsEmpty())
			{
				TSharedPtr<FJsonObject> Root;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
				if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
				{
					TSharedPtr<FJsonObject> UiObj = MakeShared<FJsonObject>();
					const TSharedPtr<FJsonObject>* ExistingUi = nullptr;
					if (Root->TryGetObjectField(TEXT("ui"), ExistingUi) && ExistingUi && ExistingUi->IsValid())
					{
						UiObj = UnrealAiEditorModulePriv::CloneJsonObjectShallow(*ExistingUi);
					}
					UiObj->SetBoolField(TEXT("editorFocus"), bEnabled);
					Root->SetObjectField(TEXT("ui"), UiObj);
					FString Out;
					const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
					if (FJsonSerializer::Serialize(Root.ToSharedRef(), W))
					{
						P->SaveSettingsJson(Out);
					}
				}
			}
		}
	}

	OnEditorFocusPolicyChanged().Broadcast();
}

FSimpleMulticastDelegate& FUnrealAiEditorModule::OnEditorFocusPolicyChanged()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

void FUnrealAiEditorModule::HydrateSubagentsFromJsonRoot(const TSharedPtr<FJsonObject>& Root)
{
	if (!GUnrealAiModule || !Root.IsValid())
	{
		return;
	}
	bool b = true;
	const TSharedPtr<FJsonObject>* AgentObj = nullptr;
	if (Root->TryGetObjectField(TEXT("agent"), AgentObj) && AgentObj && AgentObj->IsValid())
	{
		(*AgentObj)->TryGetBoolField(TEXT("useSubagents"), b);
	}
	GUnrealAiModule->bSubagentsEnabled = b;
}

bool FUnrealAiEditorModule::IsPieToolsEnabled()
{
	return GUnrealAiModule ? GUnrealAiModule->bPieToolsEnabled : false;
}

void FUnrealAiEditorModule::HydratePieToolsFromJsonRoot(const TSharedPtr<FJsonObject>& Root)
{
	if (!GUnrealAiModule || !Root.IsValid())
	{
		return;
	}
	bool b = false;
	const TSharedPtr<FJsonObject>* AgentObj = nullptr;
	if (Root->TryGetObjectField(TEXT("agent"), AgentObj) && AgentObj && AgentObj->IsValid())
	{
		bool Tmp = false;
		if ((*AgentObj)->TryGetBoolField(TEXT("enablePieTools"), Tmp))
		{
			b = Tmp;
		}
	}
	GUnrealAiModule->bPieToolsEnabled = b;
}

namespace UnrealAiEditorModuleAgentPolicy
{
	static FString SanitizeCodeTypePreference(FString In)
	{
		In.TrimStartAndEndInline();
		const FString L = In.ToLower();
		if (L == TEXT("blueprint_first"))
		{
			return TEXT("blueprint_first");
		}
		if (L == TEXT("cpp_first"))
		{
			return TEXT("cpp_first");
		}
		if (L == TEXT("blueprint_only"))
		{
			return TEXT("blueprint_only");
		}
		if (L == TEXT("cpp_only"))
		{
			return TEXT("cpp_only");
		}
		return TEXT("auto");
	}
}

FString FUnrealAiEditorModule::GetAgentCodeTypePreference()
{
	return GUnrealAiModule ? GUnrealAiModule->AgentCodeTypePreference : TEXT("auto");
}

void FUnrealAiEditorModule::HydrateAgentCodeTypePreferenceFromJsonRoot(const TSharedPtr<FJsonObject>& Root)
{
	if (!GUnrealAiModule || !Root.IsValid())
	{
		return;
	}
	FString Raw(TEXT("auto"));
	const TSharedPtr<FJsonObject>* AgentObj = nullptr;
	if (Root->TryGetObjectField(TEXT("agent"), AgentObj) && AgentObj && AgentObj->IsValid())
	{
		FString Tmp;
		if ((*AgentObj)->TryGetStringField(TEXT("codeTypePreference"), Tmp) && !Tmp.IsEmpty())
		{
			Raw = Tmp;
		}
	}
	GUnrealAiModule->AgentCodeTypePreference = UnrealAiEditorModuleAgentPolicy::SanitizeCodeTypePreference(Raw);
}

void FUnrealAiEditorModule::SetAgentCodeTypePreference(const FString& Preference)
{
	if (!GUnrealAiModule)
	{
		return;
	}
	const FString Canon = UnrealAiEditorModuleAgentPolicy::SanitizeCodeTypePreference(Preference);
	if (GUnrealAiModule->AgentCodeTypePreference == Canon)
	{
		return;
	}
	GUnrealAiModule->AgentCodeTypePreference = Canon;

	if (GUnrealAiModule->BackendRegistry.IsValid())
	{
		if (IUnrealAiPersistence* P = GUnrealAiModule->BackendRegistry->GetPersistence())
		{
			FString Json;
			if (P->LoadSettingsJson(Json) && !Json.IsEmpty())
			{
				TSharedPtr<FJsonObject> Root;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
				if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
				{
					TSharedPtr<FJsonObject> AgentObj = MakeShared<FJsonObject>();
					const TSharedPtr<FJsonObject>* ExistingAgent = nullptr;
					if (Root->TryGetObjectField(TEXT("agent"), ExistingAgent) && ExistingAgent && ExistingAgent->IsValid())
					{
						AgentObj = UnrealAiEditorModulePriv::CloneJsonObjectShallow(*ExistingAgent);
					}
					AgentObj->SetStringField(TEXT("codeTypePreference"), Canon);
					Root->SetObjectField(TEXT("agent"), AgentObj);
					FString Out;
					const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
					if (FJsonSerializer::Serialize(Root.ToSharedRef(), W))
					{
						P->SaveSettingsJson(Out);
					}
				}
			}
		}
	}

	OnAgentCodeTypePreferenceChanged().Broadcast();
}

FSimpleMulticastDelegate& FUnrealAiEditorModule::OnAgentCodeTypePreferenceChanged()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

bool FUnrealAiEditorModule::IsAutoConfirmDestructiveEnabled()
{
	return GUnrealAiModule ? GUnrealAiModule->bAutoConfirmDestructive : true;
}

void FUnrealAiEditorModule::HydrateAutoConfirmDestructiveFromJsonRoot(const TSharedPtr<FJsonObject>& Root)
{
	if (!GUnrealAiModule || !Root.IsValid())
	{
		return;
	}
	bool b = true;
	const TSharedPtr<FJsonObject>* AgentObj = nullptr;
	if (Root->TryGetObjectField(TEXT("agent"), AgentObj) && AgentObj && AgentObj->IsValid())
	{
		bool Tmp = true;
		if ((*AgentObj)->TryGetBoolField(TEXT("autoConfirmDestructive"), Tmp))
		{
			b = Tmp;
		}
	}
	GUnrealAiModule->bAutoConfirmDestructive = b;
}

void FUnrealAiEditorModule::SetAutoConfirmDestructiveEnabled(bool bEnabled)
{
	if (!GUnrealAiModule)
	{
		return;
	}
	if (GUnrealAiModule->bAutoConfirmDestructive == bEnabled)
	{
		return;
	}
	GUnrealAiModule->bAutoConfirmDestructive = bEnabled;

	if (GUnrealAiModule->BackendRegistry.IsValid())
	{
		if (IUnrealAiPersistence* P = GUnrealAiModule->BackendRegistry->GetPersistence())
		{
			FString Json;
			if (P->LoadSettingsJson(Json) && !Json.IsEmpty())
			{
				TSharedPtr<FJsonObject> Root;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
				if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
				{
					TSharedPtr<FJsonObject> AgentObj = MakeShared<FJsonObject>();
					const TSharedPtr<FJsonObject>* ExistingAgent = nullptr;
					if (Root->TryGetObjectField(TEXT("agent"), ExistingAgent) && ExistingAgent && ExistingAgent->IsValid())
					{
						AgentObj = UnrealAiEditorModulePriv::CloneJsonObjectShallow(*ExistingAgent);
					}
					AgentObj->SetBoolField(TEXT("autoConfirmDestructive"), bEnabled);
					Root->SetObjectField(TEXT("agent"), AgentObj);
					FString Out;
					const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
					if (FJsonSerializer::Serialize(Root.ToSharedRef(), W))
					{
						P->SaveSettingsJson(Out);
					}
				}
			}
		}
	}

	OnAutoConfirmDestructivePolicyChanged().Broadcast();
}

FSimpleMulticastDelegate& FUnrealAiEditorModule::OnAutoConfirmDestructivePolicyChanged()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

void FUnrealAiEditorModule::SetSubagentsEnabled(bool bEnabled)
{
	if (!GUnrealAiModule)
	{
		return;
	}
	if (GUnrealAiModule->bSubagentsEnabled == bEnabled)
	{
		return;
	}
	GUnrealAiModule->bSubagentsEnabled = bEnabled;

	if (GUnrealAiModule->BackendRegistry.IsValid())
	{
		if (IUnrealAiPersistence* P = GUnrealAiModule->BackendRegistry->GetPersistence())
		{
			FString Json;
			if (P->LoadSettingsJson(Json) && !Json.IsEmpty())
			{
				TSharedPtr<FJsonObject> Root;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
				if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
				{
					TSharedPtr<FJsonObject> AgentObj = MakeShared<FJsonObject>();
					const TSharedPtr<FJsonObject>* ExistingAgent = nullptr;
					if (Root->TryGetObjectField(TEXT("agent"), ExistingAgent) && ExistingAgent && ExistingAgent->IsValid())
					{
						AgentObj = UnrealAiEditorModulePriv::CloneJsonObjectShallow(*ExistingAgent);
					}
					AgentObj->SetBoolField(TEXT("useSubagents"), bEnabled);
					Root->SetObjectField(TEXT("agent"), AgentObj);
					FString Out;
					const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
					if (FJsonSerializer::Serialize(Root.ToSharedRef(), W))
					{
						P->SaveSettingsJson(Out);
					}
				}
			}
		}
	}

	OnSubagentsPolicyChanged().Broadcast();
}

FSimpleMulticastDelegate& FUnrealAiEditorModule::OnSubagentsPolicyChanged()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

void FUnrealAiEditorModule::SetPieToolsEnabled(bool bEnabled)
{
	if (!GUnrealAiModule)
	{
		return;
	}
	if (GUnrealAiModule->bPieToolsEnabled == bEnabled)
	{
		return;
	}
	GUnrealAiModule->bPieToolsEnabled = bEnabled;

	if (GUnrealAiModule->BackendRegistry.IsValid())
	{
		if (IUnrealAiPersistence* P = GUnrealAiModule->BackendRegistry->GetPersistence())
		{
			FString Json;
			if (P->LoadSettingsJson(Json) && !Json.IsEmpty())
			{
				TSharedPtr<FJsonObject> Root;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
				if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
				{
					TSharedPtr<FJsonObject> AgentObj = MakeShared<FJsonObject>();
					const TSharedPtr<FJsonObject>* ExistingAgent = nullptr;
					if (Root->TryGetObjectField(TEXT("agent"), ExistingAgent) && ExistingAgent && ExistingAgent->IsValid())
					{
						AgentObj = UnrealAiEditorModulePriv::CloneJsonObjectShallow(*ExistingAgent);
					}
					AgentObj->SetBoolField(TEXT("enablePieTools"), bEnabled);
					Root->SetObjectField(TEXT("agent"), AgentObj);
					FString Out;
					const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
					if (FJsonSerializer::Serialize(Root.ToSharedRef(), W))
					{
						P->SaveSettingsJson(Out);
					}
				}
			}
		}
	}

	OnPieToolsPolicyChanged().Broadcast();
}

FSimpleMulticastDelegate& FUnrealAiEditorModule::OnPieToolsPolicyChanged()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

#if WITH_EDITOR
static void UnrealAi_RegisterContextMenu_AddToChat(const FName MenuName)
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(MenuName);
	if (!Menu)
	{
		return;
	}
	FToolMenuSection& Section = Menu->AddSection(
		"UnrealAiContext",
		LOCTEXT("UnrealAiHeading", "Unreal AI"),
		FToolMenuInsert(NAME_None, EToolMenuInsertType::First));
	Section.AddMenuEntry(
		FName(*FString::Printf(TEXT("UnrealAiAddToContext_%s"), *MenuName.ToString().Replace(TEXT("."), TEXT("_")))),
		LOCTEXT("AddToContext", "Add to context"),
		LOCTEXT(
			"AddToContextTip",
			"Add the current selection to the active Agent Chat thread (same as dragging into the chat)"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"),
		FUIAction(FExecuteAction::CreateLambda(
			[MenuName]()
			{
				const TSharedPtr<FUnrealAiBackendRegistry> Reg = FUnrealAiEditorModule::GetBackendRegistry();
				const TSharedPtr<FUnrealAiChatUiSession> Session = FUnrealAiEditorModule::GetActiveChatSession();
				if (!Reg.IsValid() || !Session.IsValid())
				{
					return;
				}
				TArray<FContextAttachment> Atts;
				if (MenuName == FName(TEXT("ContentBrowser.AssetContextMenu")))
				{
					if (FModuleManager::Get().IsModuleLoaded(TEXT("ContentBrowser")))
					{
						FContentBrowserModule& CBM = FModuleManager::GetModuleChecked<FContentBrowserModule>(
							TEXT("ContentBrowser"));
						TArray<FAssetData> Selected;
						CBM.Get().GetSelectedAssets(Selected);
						for (const FAssetData& AD : Selected)
						{
							if (AD.IsValid())
							{
								Atts.Add(UnrealAiEditorContextQueries::AttachmentFromAssetData(AD));
							}
						}
					}
				}
				else if (
					MenuName == FName(TEXT("LevelEditor.ActorContextMenu"))
					|| MenuName == FName(TEXT("LevelEditor.SceneOutlinerContextMenu")))
				{
					if (GEditor)
					{
						if (USelection* ActorSelection = GEditor->GetSelectedActors())
						{
							for (FSelectionIterator It(*ActorSelection); It; ++It)
							{
								if (AActor* A = Cast<AActor>(*It))
								{
									Atts.Add(UnrealAiEditorContextQueries::AttachmentFromActor(A));
								}
							}
						}
					}
				}
				if (Atts.Num() == 0)
				{
					return;
				}
				UnrealAiContextDragDrop::AddAttachmentsToActiveChat(Reg, Session, Atts);
				FUnrealAiEditorModule::NotifyContextAttachmentsChanged();
			})));
}
#endif

namespace UnrealAiAgentChatTabSpawn
{
	struct FContentBox
	{
		TSharedPtr<SUnrealAiEditorChatTab> ChatTab;
	};
}

static TSharedPtr<SDockTab> FindParentDockTabForWidget(const TSharedRef<const SWidget>& Widget)
{
	TSharedPtr<SWidget> Current = Widget->GetParentWidget();
	while (Current.IsValid())
	{
		if (Current->GetType() == FName(TEXT("SDockTab")))
		{
			return StaticCastSharedPtr<SDockTab>(Current);
		}
		Current = Current->GetParentWidget();
	}
	return nullptr;
}

static TSharedPtr<SDockingTabStack> FindParentDockingTabStackForWidget(const TSharedRef<const SWidget>& Widget)
{
	TSharedPtr<SWidget> Current = Widget->GetParentWidget();
	while (Current.IsValid())
	{
		if (Current->GetType() == FName(TEXT("SDockingTabStack")))
		{
			return StaticCastSharedPtr<SDockingTabStack>(Current);
		}
		Current = Current->GetParentWidget();
	}
	return nullptr;
}

static TSharedRef<SDockTab> SpawnAgentChatDockTab(
	const TSharedPtr<FUnrealAiBackendRegistry>& Reg,
	const FSpawnTabArgs& Args,
	TSharedPtr<SUnrealAiEditorChatTab>* OutChatTab)
{
	(void)Args;
	FGuid ExplicitThreadId;
	const bool bUseExplicit = FUnrealAiEditorModule::ConsumePendingExplicitChatThreadId(ExplicitThreadId);
	TSharedPtr<UnrealAiAgentChatTabSpawn::FContentBox> Box = MakeShared<UnrealAiAgentChatTabSpawn::FContentBox>();
	TSharedPtr<SUnrealAiEditorChatTab> ChatTab;
	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("ChatTabLabel", "Agent Chat"))
		.OnExtendContextMenu(SDockTab::FExtendContextMenu::CreateLambda(
			[Box](FMenuBuilder& MenuBuilder)
			{
				const TSharedPtr<SUnrealAiEditorChatTab> Chat = Box->ChatTab;
				if (!Chat.IsValid())
				{
					return;
				}
				MenuBuilder.BeginSection("UnrealAiChat", LOCTEXT("UnrealAiChatCtxSection", "Agent Chat"));
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CtxNewChat", "New chat"),
					LOCTEXT("CtxNewChatTip", "Open a new Agent Chat tab to the right of this one."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([Chat]()
					{
						Chat->MenuNewChat();
					})));
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CtxExportChat", "Export chat…"),
					LOCTEXT("CtxExportChatTip", "Save the transcript as a text file."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([Chat]()
					{
						Chat->MenuExportChat();
					})));
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CtxCopyChat", "Copy chat to clipboard"),
					LOCTEXT("CtxCopyChatTip", "Copy the transcript as plain text."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([Chat]()
					{
						Chat->MenuCopyChatToClipboard();
					})));
				MenuBuilder.AddMenuEntry(
					LOCTEXT("CtxDeleteChat", "Delete chat"),
					LOCTEXT(
						"CtxDeleteChatTip",
						"Remove this conversation from memory and delete persisted thread data on disk."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([Chat]()
					{
						Chat->MenuDeleteChat();
					})));
				MenuBuilder.EndSection();
			}))
		[
			SAssignNew(ChatTab, SUnrealAiEditorChatTab)
				.BackendRegistry(Reg)
				.bUseExplicitThreadId(bUseExplicit && ExplicitThreadId.IsValid())
				.ExplicitThreadId(ExplicitThreadId)
		];
	Box->ChatTab = ChatTab;
	if (OutChatTab)
	{
		*OutChatTab = ChatTab;
	}
	if (ChatTab.IsValid())
	{
		ChatTab->SetHostDockTab(Tab);
		FUnrealAiEditorModule::RegisterAgentChatTabForPersistence(ChatTab);
	}
	/** Defer until SDockTab is parented (InsertNewDocumentTab / OpenTab may happen on later frames during startup). */
	if (GUnrealAiModule && ChatTab.IsValid())
	{
		const TWeakPtr<SUnrealAiEditorChatTab> WeakChat(ChatTab);
		struct FRestoreNotifyWait
		{
			int32 Ticks = 0;
		};
		const TSharedRef<FRestoreNotifyWait> Wait = MakeShared<FRestoreNotifyWait>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakChat, Wait](float /*DeltaTime*/) -> bool
		{
			const TSharedPtr<SUnrealAiEditorChatTab> T = WeakChat.Pin();
			if (!T.IsValid())
			{
				return false;
			}
			++Wait->Ticks;
			static constexpr int32 MaxTicks = 900;
			if (FindParentDockTabForWidget(T.ToSharedRef()).IsValid())
			{
				FUnrealAiEditorModule::NotifyAgentChatTabSpawnedForRestoreChain(T);
				return false;
			}
			return Wait->Ticks < MaxTicks;
		}));
	}
	return Tab;
}

TSharedPtr<SUnrealAiEditorChatTab> FUnrealAiEditorModule::OpenNewAgentChatTabBeside(const TSharedPtr<SWidget>& FromWidget)
{
	if (!FromWidget.IsValid())
	{
		return nullptr;
	}
	const TSharedPtr<FUnrealAiBackendRegistry> Reg = GetBackendRegistry();
	if (!Reg.IsValid())
	{
		return nullptr;
	}
	TSharedPtr<SDockTab> ParentDock = FindParentDockTabForWidget(FromWidget.ToSharedRef());
	TSharedPtr<SDockingTabStack> ParentStack = ParentDock.IsValid() ? ParentDock->GetParentDockTabStack() : nullptr;
	if (!ParentStack.IsValid())
	{
		// Some Slate hierarchies (during rebuild / startup) may not surface a working GetParentDockTabStack(),
		// but the docking stack still exists in the parent chain.
		ParentStack = FindParentDockingTabStackForWidget(FromWidget.ToSharedRef());
	}
	TSharedPtr<SWindow> OwnerWindow;
	if (ParentDock.IsValid())
	{
		OwnerWindow = ParentDock->GetParentWindow();
	}
	if (!OwnerWindow.IsValid())
	{
		OwnerWindow = FGlobalTabmanager::Get()->GetRootWindow();
	}
	const FSpawnTabArgs Args(OwnerWindow, FTabId(UnrealAiEditorTabIds::ChatTab, INDEX_NONE));
	TSharedPtr<SUnrealAiEditorChatTab> NewChatTab;
	TSharedRef<SDockTab> NewTab = SpawnAgentChatDockTab(Reg, Args, &NewChatTab);
	NewTab->SetTabIcon(FUnrealAiEditorStyle::GetAgentChatTabIconBrush());
	// Note: SDockTab::SetLayoutIdentifier is protected (friend FTabManager only). Extra Agent Chat tabs
	// are spawned manually so FTabManager::SpawnTab is not used (nomad spawner tracks a single SpawnedTabPtr).

	if (ParentStack.IsValid())
	{
		int32 InsertIdx = INDEX_NONE;
		const TSlotlessChildren<SDockTab>& Tabs = ParentStack->GetTabs();
		if (ParentDock.IsValid())
		{
			const FTabId CurrentId = ParentDock->GetLayoutIdentifier();
			for (int32 i = 0; i < Tabs.Num(); ++i)
			{
				if (Tabs[i]->GetLayoutIdentifier() == CurrentId)
				{
					InsertIdx = i + 1;
					break;
				}
			}
		}
		ParentStack->OpenTab(NewTab, InsertIdx, false);
	}
	else
	{
		FGlobalTabmanager::Get()->InsertNewDocumentTab(
			UnrealAiEditorTabIds::ChatTab,
			FTabManager::ESearchPreference::PreferLiveTab,
			NewTab);
	}
	return NewChatTab;
}

void FUnrealAiEditorModule::RegisterTabs(const TSharedPtr<FUnrealAiBackendRegistry>& Reg)
{
	const auto SpawnChat = [Reg](const FSpawnTabArgs& Args) -> TSharedRef<SDockTab>
	{
		return SpawnAgentChatDockTab(Reg, Args, nullptr);
	};

	const auto SpawnSettings = [Reg](const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			.Label(LOCTEXT("SettingsTabLabel", "AI Settings"))
			[
				SNew(SUnrealAiEditorSettingsTab).BackendRegistry(Reg)
			];
	};

	const auto SpawnQuick = [](const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			.Label(LOCTEXT("QuickTabLabel", "Quick Start"))
			[
				SNew(SUnrealAiEditorQuickStartTab)
			];
	};

	const auto SpawnHelp = [](const FSpawnTabArgs& Args)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			.Label(LOCTEXT("HelpTabLabel", "Help"))
			[
				SNew(SUnrealAiEditorHelpTab)
			];
	};

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
							 UnrealAiEditorTabIds::ChatTab,
							 FOnSpawnTab::CreateLambda(SpawnChat))
		.SetDisplayName(LOCTEXT("ChatTab", "Agent Chat"))
		.SetIcon(FSlateIcon(FUnrealAiEditorStyle::GetStyleSetName(), FUnrealAiEditorStyle::AgentChatTabIconName()))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
							 UnrealAiEditorTabIds::SettingsTab,
							 FOnSpawnTab::CreateLambda(SpawnSettings))
		.SetDisplayName(LOCTEXT("SettingsTab", "AI Settings"))
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Settings")))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
							 UnrealAiEditorTabIds::QuickStartTab,
							 FOnSpawnTab::CreateLambda(SpawnQuick))
		.SetDisplayName(LOCTEXT("QuickStartTab", "Quick Start"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
							 UnrealAiEditorTabIds::HelpTab,
							 FOnSpawnTab::CreateLambda(SpawnHelp))
		.SetDisplayName(LOCTEXT("HelpTab", "Help"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());

}

void FUnrealAiEditorModule::UnregisterTabs()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UnrealAiEditorTabIds::ChatTab);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UnrealAiEditorTabIds::SettingsTab);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UnrealAiEditorTabIds::QuickStartTab);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UnrealAiEditorTabIds::HelpTab);
}

void FUnrealAiEditorModule::RegisterMenus()
{
	static const FName UnrealAiOwner("UnrealAiEditorOwner");
	FToolMenuOwnerScoped OwnerScoped(UnrealAiOwner);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda(
		[]()
		{
			// Bind commands before toolbar/menu entries that reference the command list.
			RegisterUnrealAiEditorKeyBindings();

			// Top-level Unreal AI menu in the main menu bar (next to built-in menus like Help).
			if (UToolMenu* MainMenuBar = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu"))
			{
				FToolMenuSection& MenuBarSection = MainMenuBar->FindOrAddSection("MainMenuBar");
				MenuBarSection.AddSubMenu(
					"UnrealAiTopLevel",
					LOCTEXT("UnrealAiTopLevel", "Unreal AI"),
					LOCTEXT("UnrealAiTopLevelTip", "Unreal AI Editor windows"),
					FNewToolMenuDelegate::CreateLambda(
						[](UToolMenu* SubMenu)
						{
							FToolMenuSection& S = SubMenu->AddSection("UnrealAiTopItems");
							S.AddMenuEntry(
								"UnrealAiTopChat",
								LOCTEXT("MenuTopChat", "Agent Chat"),
								LOCTEXT("MenuTopChatTip", "Open Agent Chat"),
								FSlateIcon(FUnrealAiEditorStyle::GetStyleSetName(), FUnrealAiEditorStyle::AgentChatTabIconName()),
								FUIAction(FExecuteAction::CreateLambda([]()
								{
									FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::ChatTab);
								})));
							S.AddMenuEntry(
								"UnrealAiTopSettings",
								LOCTEXT("MenuTopSettings", "AI Settings"),
								LOCTEXT("MenuTopSettingsTip", "Open AI Settings"),
								FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Settings")),
								FUIAction(FExecuteAction::CreateLambda([]()
								{
									FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::SettingsTab);
								})));
						}));
			}

			UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
			FToolMenuSection& Section = WindowMenu->FindOrAddSection("WindowGlobal");
			Section.AddSubMenu(
				"UnrealAiRoot",
				LOCTEXT("UnrealAiRoot", "Unreal AI"),
				LOCTEXT("UnrealAiRootTip", "Unreal AI Editor windows"),
				FNewToolMenuDelegate::CreateLambda(
					[](UToolMenu* SubMenu)
					{
						FToolMenuSection& S = SubMenu->AddSection("UnrealAiItems");
						S.AddMenuEntry(
							"UnrealAiChat",
							LOCTEXT("MenuChat", "Agent Chat"),
							LOCTEXT("MenuChatTip", "Open Agent Chat"),
							FSlateIcon(FUnrealAiEditorStyle::GetStyleSetName(), FUnrealAiEditorStyle::AgentChatTabIconName()),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::ChatTab);
							})));
						S.AddMenuEntry(
							"UnrealAiSettings",
							LOCTEXT("MenuSettings", "AI Settings"),
							LOCTEXT("MenuSettingsTip", ""),
							FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Settings")),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::SettingsTab);
							})));
						S.AddMenuEntry(
							"UnrealAiQuick",
							LOCTEXT("MenuQuick", "Quick Start"),
							LOCTEXT("MenuQuickTip", ""),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::QuickStartTab);
							})));
						S.AddMenuEntry(
							"UnrealAiHelp",
							LOCTEXT("MenuHelp", "Help"),
							LOCTEXT("MenuHelpTip", ""),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::HelpTab);
							})));
					}));

			UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
			FToolMenuSection& ToolsSection = ToolsMenu->FindOrAddSection("UnrealAiTools");
			ToolsSection.AddSubMenu(
				"UnrealAiToolsRoot",
				LOCTEXT("UnrealAiToolsRoot", "Unreal AI"),
				LOCTEXT("UnrealAiToolsRootTip", "Unreal AI Editor windows"),
				FNewToolMenuDelegate::CreateLambda(
					[](UToolMenu* SubMenu)
					{
						FToolMenuSection& S = SubMenu->AddSection("UnrealAiToolItems");
						S.AddMenuEntry(
							"UnrealAiChatT",
							LOCTEXT("MenuChatT", "Agent Chat"),
							LOCTEXT("MenuChatTipT", "Open Agent Chat"),
							FSlateIcon(FUnrealAiEditorStyle::GetStyleSetName(), FUnrealAiEditorStyle::AgentChatTabIconName()),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::ChatTab);
							})));
						S.AddMenuEntry(
							"UnrealAiSettingsT",
							LOCTEXT("MenuSettingsT", "AI Settings"),
							LOCTEXT("MenuSettingsTipT", ""),
							FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Settings")),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::SettingsTab);
							})));
						S.AddMenuEntry(
							"UnrealAiQuickT",
							LOCTEXT("MenuQuickT", "Quick Start"),
							LOCTEXT("MenuQuickTipT", ""),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::QuickStartTab);
							})));
						S.AddMenuEntry(
							"UnrealAiHelpT",
							LOCTEXT("MenuHelpT", "Help"),
							LOCTEXT("MenuHelpTipT", ""),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([]()
							{
								FGlobalTabmanager::Get()->TryInvokeTab(UnrealAiEditorTabIds::HelpTab);
							})));
					}));

			// Main Level Editor toolbar (top) — Nomad tabs only list under Window until opened once; this makes the UI discoverable.
			if (UToolMenu* ToolBarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar"))
			{
				FToolMenuSection& ToolBarSection = ToolBarMenu->FindOrAddSection("UnrealAiToolbar");
				ToolBarSection.AddEntry(FToolMenuEntry::InitToolBarButton(
					FUnrealAiEditorCommands::Get().OpenChatTab,
					LOCTEXT("ToolbarUnrealAi", "Unreal AI"),
					LOCTEXT("ToolbarUnrealAiTip", "Open Agent Chat"),
					FSlateIcon(FUnrealAiEditorStyle::GetStyleSetName(), FUnrealAiEditorStyle::AgentChatTabIconName()),
					NAME_None,
					TOptional<FName>(FName(TEXT("UnrealAiToolbarChat")))));
			}

			UnrealAiBlueprintFormatEditorExtendBlueprintToolbar();

#if WITH_EDITOR
			UnrealAi_RegisterContextMenu_AddToChat(FName(TEXT("ContentBrowser.AssetContextMenu")));
			UnrealAi_RegisterContextMenu_AddToChat(FName(TEXT("LevelEditor.ActorContextMenu")));
			UnrealAi_RegisterContextMenu_AddToChat(FName(TEXT("LevelEditor.SceneOutlinerContextMenu")));
#endif
		}));
}

void FUnrealAiEditorModule::UnregisterMenus()
{
	static const FName UnrealAiOwner("UnrealAiEditorOwner");
	UToolMenus::UnregisterOwner(UnrealAiOwner);
}

void FUnrealAiEditorModule::RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings(
			"Project",
			"Plugins",
			"UnrealAiEditor",
			LOCTEXT("UnrealAiSettingsName", "Unreal AI Editor"),
			LOCTEXT("UnrealAiSettingsDesc", "Local-first AI plugin settings"),
			GetMutableDefault<UUnrealAiEditorSettings>());
	}
}

void FUnrealAiEditorModule::UnregisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "UnrealAiEditor");
	}
}

void FUnrealAiEditorModule::RegisterOpenChatOnStartup()
{
	// Automation runs (especially with -unattended) should not restore/open UI tabs.
	// Restoring chat tabs can trigger additional async behavior and can interfere with Automation RunTests.
	{
		const FString Cmd = FCommandLine::Get();
		if (Cmd.Contains(TEXT("Automation RunTests")) || Cmd.Contains(TEXT("Automation ListTests")))
		{
			return;
		}
	}

	OpenChatOnStartupHandle = FEditorDelegates::OnEditorInitialized.AddLambda([](double /*DeltaTime*/)
	{
		const TSharedPtr<FUnrealAiBackendRegistry> Reg = FUnrealAiEditorModule::GetBackendRegistry();
		if (!Reg.IsValid())
		{
			return;
		}
		IUnrealAiPersistence* P = Reg->GetPersistence();
		if (!P)
		{
			return;
		}
		const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
		if (GUnrealAiModule)
		{
			GUnrealAiModule->TryKickoffStartupDiscoveryAndIndexing(Reg, ProjectId);
		}
		TArray<FGuid> OpenThreads;
		if (P->LoadOpenChatTabsState(ProjectId, OpenThreads) && OpenThreads.Num() > 0)
		{
			if (GUnrealAiModule)
			{
				GUnrealAiModule->BeginRestoreOpenChats(OpenThreads);
			}
			return;
		}
		if (!GetDefault<UUnrealAiEditorSettings>()->bOpenAgentChatOnStartup)
		{
			return;
		}

		FGuid FallbackThread;
		if (P->TryGetMostRecentThreadWithConversation(ProjectId, FallbackThread) && FallbackThread.IsValid())
		{
			SetPendingExplicitChatThreadId(FallbackThread);
		}

		if (GUnrealAiModule)
		{
			GUnrealAiModule->ScheduleDeferredAgentChatDocumentInsert(Reg);
		}
	});
}

void FUnrealAiEditorModule::TryKickoffStartupDiscoveryAndIndexing(
	const TSharedPtr<FUnrealAiBackendRegistry>& Reg,
	const FString& ProjectId)
{
	const FString Cmd = FCommandLine::Get();
	if (FApp::IsUnattended()
		|| Cmd.Contains(TEXT("UnrealAi.RunAgentTurn"))
		|| Cmd.Contains(TEXT("UnrealAi.RunStrictAssertions"))
		|| Cmd.Contains(TEXT("Automation RunTests"))
		|| Cmd.Contains(TEXT("Automation ListTests")))
	{
		return;
	}

	if (bStartupDiscoveryBootstrapTriggered || !Reg.IsValid() || ProjectId.IsEmpty())
	{
		return;
	}
	bStartupDiscoveryBootstrapTriggered = true;

	const TWeakPtr<FUnrealAiBackendRegistry> WeakReg = Reg;
	const FString ProjectIdCopy(ProjectId);
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakReg, ProjectIdCopy](float /*DeltaTime*/) -> bool
		{
			const TSharedPtr<FUnrealAiBackendRegistry> RegStrong = WeakReg.Pin();
			if (!RegStrong.IsValid())
			{
				return false;
			}
			const FProjectTreeSummary& Summary = UnrealAiProjectTreeSampler::GetOrRefreshProjectSummary(
				ProjectIdCopy,
				true,
				TEXT("startup_bootstrap"));
			const FString DiscoveryDetail = UnrealAiBackgroundOpsLog::BuildDetailJson(
				TEXT("startup_project_tree_refresh"),
				TEXT("ok"),
				ProjectIdCopy,
				TEXT(""),
				Summary.LastQueryDurationMs,
				[&Summary](const TSharedPtr<FJsonObject>& O)
				{
					O->SetStringField(TEXT("status"), Summary.LastQueryStatus);
					O->SetNumberField(TEXT("top_folder_count"), Summary.TopLevelFolders.Num());
				});
			UnrealAiBackgroundOpsLog::EmitLogLine(
				TEXT("startup_project_tree_refresh"),
				TEXT("ok"),
				Summary.LastQueryDurationMs,
				DiscoveryDetail);

			IUnrealAiRetrievalService* Retrieval = RegStrong->GetRetrievalService();
			if (!Retrieval)
			{
				return false;
			}
			const FUnrealAiRetrievalSettings RetrievalSettings = Retrieval->LoadSettings();
			if (!RetrievalSettings.bEnabled || !RetrievalSettings.bAutoIndexOnProjectOpen)
			{
				return false;
			}
			{
				const FUnrealAiRetrievalProjectStatus IndexSt = Retrieval->GetProjectStatus(ProjectIdCopy);
				if (IndexSt.StateText.Equals(TEXT("ready"), ESearchCase::IgnoreCase) && IndexSt.ChunksIndexed > 0)
				{
					return false;
				}
			}
			Retrieval->RequestRebuild(ProjectIdCopy);
			const FString RetrievalDetail = UnrealAiBackgroundOpsLog::BuildDetailJson(
				TEXT("startup_retrieval_rebuild"),
				TEXT("queued"),
				ProjectIdCopy,
				TEXT(""),
				0.0,
				[](const TSharedPtr<FJsonObject>& O)
				{
					O->SetStringField(TEXT("reason"), TEXT("autoIndexOnProjectOpen"));
				});
			UnrealAiBackgroundOpsLog::EmitLogLine(
				TEXT("startup_retrieval_rebuild"),
				TEXT("queued"),
				0.0,
				RetrievalDetail);
			return false;
		}),
		0.15f);
}

void FUnrealAiEditorModule::RegisterSaveOpenChatsOnExit()
{
	SaveOpenChatsOnExitHandle = FCoreDelegates::OnPreExit.AddLambda([]()
	{
		if (GUnrealAiModule)
		{
			GUnrealAiModule->SaveOpenAgentChatTabsNow();
		}
	});
}

void FUnrealAiEditorModule::UnregisterSaveOpenChatsOnExit()
{
	if (SaveOpenChatsOnExitHandle.IsValid())
	{
		FCoreDelegates::OnPreExit.Remove(SaveOpenChatsOnExitHandle);
		SaveOpenChatsOnExitHandle.Reset();
	}
}

bool FUnrealAiEditorModule::ConsumePendingExplicitChatThreadId(FGuid& Out)
{
	return GUnrealAiModule && GUnrealAiModule->ConsumePendingExplicitChatThreadId_Impl(Out);
}

bool FUnrealAiEditorModule::ConsumePendingExplicitChatThreadId_Impl(FGuid& Out)
{
	Out = FGuid();
	if (!bPendingExplicitChatThreadId)
	{
		return false;
	}
	Out = PendingExplicitChatThreadId;
	bPendingExplicitChatThreadId = false;
	PendingExplicitChatThreadId = FGuid();
	return Out.IsValid();
}

void FUnrealAiEditorModule::SetPendingExplicitChatThreadId(const FGuid& ThreadId)
{
	if (!GUnrealAiModule)
	{
		return;
	}
	if (!ThreadId.IsValid())
	{
		GUnrealAiModule->bPendingExplicitChatThreadId = false;
		GUnrealAiModule->PendingExplicitChatThreadId = FGuid();
		return;
	}
	GUnrealAiModule->bPendingExplicitChatThreadId = true;
	GUnrealAiModule->PendingExplicitChatThreadId = ThreadId;
}

void FUnrealAiEditorModule::NotifyAgentChatTabSpawnedForRestoreChain(const TSharedPtr<SUnrealAiEditorChatTab>& Tab)
{
	if (GUnrealAiModule)
	{
		GUnrealAiModule->OnAgentChatTabSpawnedForRestoreChain(Tab);
	}
}

void FUnrealAiEditorModule::RegisterAgentChatTabForPersistence(const TSharedPtr<SUnrealAiEditorChatTab>& Tab)
{
	if (GUnrealAiModule && Tab.IsValid())
	{
		GUnrealAiModule->RegisteredAgentChatTabsForPersistence.Add(Tab);
	}
}

void FUnrealAiEditorModule::UnregisterAgentChatTabForPersistence(const SUnrealAiEditorChatTab* Tab)
{
	if (!GUnrealAiModule || !Tab)
	{
		return;
	}
	TArray<TWeakPtr<SUnrealAiEditorChatTab>>& Arr = GUnrealAiModule->RegisteredAgentChatTabsForPersistence;
	for (int32 i = Arr.Num() - 1; i >= 0; --i)
	{
		const TSharedPtr<SUnrealAiEditorChatTab> Pinned = Arr[i].Pin();
		if (!Pinned.IsValid() || Pinned.Get() == Tab)
		{
			Arr.RemoveAtSwap(i);
		}
	}
}

void FUnrealAiEditorModule::GetOpenAgentChatThreadIds(TArray<FGuid>& OutOrdered)
{
	OutOrdered.Reset();
	if (!GUnrealAiModule)
	{
		return;
	}
	for (const TWeakPtr<SUnrealAiEditorChatTab>& Weak : GUnrealAiModule->RegisteredAgentChatTabsForPersistence)
	{
		const TSharedPtr<SUnrealAiEditorChatTab> Tab = Weak.Pin();
		if (!Tab.IsValid())
		{
			continue;
		}
		const TSharedPtr<FUnrealAiChatUiSession> Sess = Tab->GetSession();
		if (Sess.IsValid() && Sess->ThreadId.IsValid())
		{
			OutOrdered.AddUnique(Sess->ThreadId);
		}
	}
}

void FUnrealAiEditorModule::OpenAgentChatTabWithPersistedThread(const FString& ThreadIdDigitsWithHyphens)
{
	FGuid G;
	if (!FGuid::Parse(ThreadIdDigitsWithHyphens, G) || !G.IsValid())
	{
		return;
	}
	const TSharedPtr<FUnrealAiBackendRegistry> Reg = GetBackendRegistry();
	AsyncTask(ENamedThreads::GameThread, [Reg, G]()
	{
		if (!Reg.IsValid() || !GUnrealAiModule)
		{
			return;
		}
		SetPendingExplicitChatThreadId(G);
		GUnrealAiModule->ScheduleDeferredAgentChatDocumentInsert(Reg);
	});
}

void FUnrealAiEditorModule::BeginRestoreOpenChats(const TArray<FGuid>& Ids)
{
	PendingRestoreTail.Reset();
	SetPendingExplicitChatThreadId(FGuid());
	if (Ids.Num() == 0)
	{
		return;
	}
	SetPendingExplicitChatThreadId(Ids[0]);
	for (int32 i = 1; i < Ids.Num(); ++i)
	{
		if (Ids[i].IsValid())
		{
			PendingRestoreTail.Add(Ids[i]);
		}
	}
	ScheduleDeferredAgentChatDocumentInsert(BackendRegistry);
}

void FUnrealAiEditorModule::RemoveDeferredAgentChatInsertTicker()
{
	if (DeferredAgentChatInsertTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DeferredAgentChatInsertTickerHandle);
		DeferredAgentChatInsertTickerHandle.Reset();
	}
}

void FUnrealAiEditorModule::CancelDeferredAgentChatInsertsForShutdown()
{
	RemoveDeferredAgentChatInsertTicker();
	++DeferredAgentChatInsertTicket;
}

void FUnrealAiEditorModule::ScheduleDeferredAgentChatDocumentInsert(const TSharedPtr<FUnrealAiBackendRegistry>& Reg)
{
	if (!Reg.IsValid() || !GUnrealAiModule)
	{
		return;
	}

	RemoveDeferredAgentChatInsertTicker();
	++DeferredAgentChatInsertTicket;
	const int32 Ticket = DeferredAgentChatInsertTicket;

	struct FDeferredInsertState
	{
		TWeakPtr<FUnrealAiBackendRegistry> WeakReg;
		int32 TotalTicks = 0;
		int32 StableRootFrames = 0;
		int8 Phase = 0;
		bool bDidInsert = false;
		int32 PostInsertTicks = 0;
		TSharedPtr<SUnrealAiEditorChatTab> ChatTab;
		TSharedPtr<SDockTab> DockTab;
	};

	const TSharedRef<FDeferredInsertState> State = MakeShared<FDeferredInsertState>();
	State->WeakReg = Reg;

	DeferredAgentChatInsertTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this, Ticket, State](float /*DeltaTime*/) -> bool
		{
			if (!GUnrealAiModule || Ticket != DeferredAgentChatInsertTicket)
			{
				return false;
			}

			const TSharedPtr<FUnrealAiBackendRegistry> R = State->WeakReg.Pin();
			if (!R.IsValid() || !GEditor)
			{
				RemoveDeferredAgentChatInsertTicker();
				return false;
			}

			++State->TotalTicks;
			static constexpr int32 MaxWaitTicks = 1200;
			if (State->TotalTicks > MaxWaitTicks)
			{
				UE_LOG(
					LogLoad,
					Warning,
					TEXT("UnrealAiEditor: Timed out waiting to embed Agent Chat; open it from Window -> Unreal AI."));
				RemoveDeferredAgentChatInsertTicker();
				return false;
			}

			if (!FSlateApplication::IsInitialized())
			{
				State->StableRootFrames = 0;
				return true;
			}

			const TSharedRef<FGlobalTabmanager> TabMgr = FGlobalTabmanager::Get();
			const TSharedPtr<SWindow> Root = TabMgr->GetRootWindow();
			if (!Root.IsValid())
			{
				State->StableRootFrames = 0;
				return true;
			}

			if (State->Phase == 0)
			{
				++State->StableRootFrames;
				static constexpr int32 NeedStableFrames = 20;
				if (State->StableRootFrames < NeedStableFrames)
				{
					return true;
				}

				const FSpawnTabArgs Args(Root, FTabId(UnrealAiEditorTabIds::ChatTab, INDEX_NONE));
				TSharedPtr<SUnrealAiEditorChatTab> ChatTab;
				const TSharedRef<SDockTab> NewTab = SpawnAgentChatDockTab(R, Args, &ChatTab);
				NewTab->SetTabIcon(FUnrealAiEditorStyle::GetAgentChatTabIconBrush());
				State->DockTab = NewTab;
				State->ChatTab = ChatTab;
				State->Phase = 1;
				State->bDidInsert = false;
				State->PostInsertTicks = 0;
				return true;
			}

			if (!State->bDidInsert)
			{
				TabMgr->InsertNewDocumentTab(
					UnrealAiEditorTabIds::ChatTab,
					FTabManager::ESearchPreference::PreferLiveTab,
					State->DockTab.ToSharedRef());
				State->bDidInsert = true;
			}

			++State->PostInsertTicks;
			if (State->ChatTab.IsValid() && FindParentDockTabForWidget(State->ChatTab.ToSharedRef()).IsValid())
			{
				RemoveDeferredAgentChatInsertTicker();
				return false;
			}

			static constexpr int32 MaxPostInsertTicks = 180;
			if (State->PostInsertTicks >= MaxPostInsertTicks)
			{
				UE_LOG(
					LogLoad,
					Warning,
					TEXT("UnrealAiEditor: Agent Chat tab was spawned but did not dock; use Window -> Unreal AI -> Agent Chat."));
				RemoveDeferredAgentChatInsertTicker();
				return false;
			}
			return true;
		}));
}

void FUnrealAiEditorModule::OnAgentChatTabSpawnedForRestoreChain(const TSharedPtr<SUnrealAiEditorChatTab>& Tab)
{
	if (!Tab.IsValid() || bAgentChatRestoreChainBusy || PendingRestoreTail.Num() == 0)
	{
		return;
	}
	bAgentChatRestoreChainBusy = true;
	TArray<FGuid> Tail = MoveTemp(PendingRestoreTail);
	PendingRestoreTail.Reset();
	TSharedPtr<SWidget> From = Tab;
	for (const FGuid& G : Tail)
	{
		if (!G.IsValid())
		{
			continue;
		}
		SetPendingExplicitChatThreadId(G);
		TSharedPtr<SUnrealAiEditorChatTab> Next = OpenNewAgentChatTabBeside(From);
		From = Next;
		if (!From.IsValid())
		{
			break;
		}
	}
	SetPendingExplicitChatThreadId(FGuid());
	bAgentChatRestoreChainBusy = false;
}

void FUnrealAiEditorModule::SaveOpenAgentChatTabsNow()
{
	if (!BackendRegistry.IsValid())
	{
		return;
	}
	IUnrealAiPersistence* P = BackendRegistry->GetPersistence();
	if (!P)
	{
		return;
	}
	struct FSavedTabSortRow
	{
		TSharedPtr<SDockingTabStack> Stack;
		int32 TabIndex = MAX_int32;
		FGuid ThreadId;
	};
	TArray<FSavedTabSortRow> Rows;
	for (TWeakPtr<SUnrealAiEditorChatTab> Weak : RegisteredAgentChatTabsForPersistence)
	{
		TSharedPtr<SUnrealAiEditorChatTab> ChatTab = Weak.Pin();
		if (!ChatTab.IsValid())
		{
			continue;
		}
		const TSharedPtr<FUnrealAiChatUiSession> Session = ChatTab->GetSession();
		if (!Session.IsValid() || !Session->ThreadId.IsValid())
		{
			continue;
		}
		TSharedPtr<SDockTab> Dock = FindParentDockTabForWidget(ChatTab.ToSharedRef());
		TSharedPtr<SDockingTabStack> Stack = Dock.IsValid() ? Dock->GetParentDockTabStack() : nullptr;
		FSavedTabSortRow Row;
		Row.Stack = Stack;
		Row.ThreadId = Session->ThreadId;
		if (Stack.IsValid() && Dock.IsValid())
		{
			const TSlotlessChildren<SDockTab>& Tabs = Stack->GetTabs();
			for (int32 i = 0; i < Tabs.Num(); ++i)
			{
				if (Tabs[i] == Dock.ToSharedRef())
				{
					Row.TabIndex = i;
					break;
				}
			}
		}
		Rows.Add(Row);
	}
	Rows.Sort([](const FSavedTabSortRow& A, const FSavedTabSortRow& B)
	{
		const UPTRINT PA = reinterpret_cast<UPTRINT>(A.Stack.Get());
		const UPTRINT PB = reinterpret_cast<UPTRINT>(B.Stack.Get());
		if (PA != PB)
		{
			return PA < PB;
		}
		return A.TabIndex < B.TabIndex;
	});
	TArray<FGuid> OutIds;
	for (const FSavedTabSortRow& R : Rows)
	{
		OutIds.Add(R.ThreadId);
	}
	P->SaveOpenChatTabsState(UnrealAiProjectId::GetCurrentProjectId(), OutIds);
}

void FUnrealAiEditorModule::UnregisterOpenChatOnStartup()
{
	if (OpenChatOnStartupHandle.IsValid())
	{
		FEditorDelegates::OnEditorInitialized.Remove(OpenChatOnStartupHandle);
		OpenChatOnStartupHandle.Reset();
	}
}

IMPLEMENT_MODULE(FUnrealAiEditorModule, UnrealAiEditor)

#undef LOCTEXT_NAMESPACE
