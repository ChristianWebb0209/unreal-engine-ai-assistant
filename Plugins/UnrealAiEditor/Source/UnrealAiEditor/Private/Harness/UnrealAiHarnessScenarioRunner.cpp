#include "Harness/UnrealAiHarnessScenarioRunner.h"

#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/IAgentContextService.h"
#include "Context/UnrealAiContextMentionParser.h"
#include "Context/UnrealAiProjectId.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Harness/FAgentRunFileSink.h"
#include "Harness/FUnrealAiModelProfileRegistry.h"
#include "Harness/IUnrealAiAgentHarness.h"
#include "Harness/UnrealAiAgentTypes.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealAiEditorModule.h"

namespace UnrealAiHarnessScenarioRunnerPriv
{
	static bool RunAgentTurnSync_GameThread(
		const FString& UserMessage,
		const FString& ThreadIdDigitsWithHyphens,
		const EUnrealAiAgentMode Mode,
		const FString& OutputRootDir,
		FString& OutJsonlPath,
		FString& OutRunDir,
		bool& bOutSuccess,
		FString& OutError,
		const bool bDumpContextAfterEachTool)
	{
		const TSharedPtr<FUnrealAiBackendRegistry> Reg = FUnrealAiEditorModule::GetBackendRegistry();
		if (!Reg.IsValid())
		{
			OutError = TEXT("Backend registry not available");
			return false;
		}
		IUnrealAiAgentHarness* Harness = Reg->GetAgentHarness();
		IAgentContextService* Ctx = Reg->GetContextService();
		FUnrealAiModelProfileRegistry* Profiles = Reg->GetModelProfileRegistry();
		if (!Harness || !Ctx || !Profiles)
		{
			OutError = TEXT("Harness or context not available");
			return false;
		}

		const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
		FString RunDir = OutputRootDir;
		if (RunDir.IsEmpty())
		{
			RunDir = FPaths::Combine(
				FPaths::ProjectSavedDir(),
				TEXT("UnrealAiEditor"),
				TEXT("HarnessRuns"),
				FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S")));
		}
		IFileManager::Get().MakeDirectory(*RunDir, true);
		OutRunDir = RunDir;
		OutJsonlPath = FPaths::Combine(RunDir, TEXT("run.jsonl"));
		FFileHelper::SaveStringToFile(FString(), *OutJsonlPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

		FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
		bool bFinishSuccess = false;
		FString FinishErr;

		Ctx->LoadOrCreate(ProjectId, ThreadIdDigitsWithHyphens);
		UnrealAiContextMentionParser::ApplyMentionsFromPrompt(Ctx, ProjectId, ThreadIdDigitsWithHyphens, UserMessage);
		Ctx->RefreshEditorSnapshotFromEngine();

		FUnrealAiAgentTurnRequest Req;
		Req.ProjectId = ProjectId;
		Req.ThreadId = ThreadIdDigitsWithHyphens;
		Req.Mode = Mode;
		Req.UserText = UserMessage;
		Req.ModelProfileId = Profiles->GetDefaultModelId();
		Req.bRecordAssistantAsStubToolResult = false;

		const TSharedPtr<FAgentRunFileSink> Sink = MakeShared<FAgentRunFileSink>(
			OutJsonlPath,
			Ctx,
			ProjectId,
			ThreadIdDigitsWithHyphens,
			bDumpContextAfterEachTool,
			true,
			DoneEvent,
			&bFinishSuccess,
			&FinishErr);

		Harness->RunTurn(Req, Sink);

		const uint32 WaitMs = 30 * 60 * 1000;
		const bool bSignaled = DoneEvent->Wait(WaitMs);
		FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

		if (!bSignaled)
		{
			OutError = TEXT("Harness run timed out waiting for completion");
			bOutSuccess = false;
			return false;
		}

		bOutSuccess = bFinishSuccess;
		OutError = FinishErr;
		return true;
	}
}

bool UnrealAiHarnessScenarioRunner::RunAgentTurnSync(
	const FString& UserMessage,
	const FString& ThreadIdDigitsWithHyphens,
	const EUnrealAiAgentMode Mode,
	const FString& OutputRootDir,
	FString& OutJsonlPath,
	FString& OutRunDir,
	bool& bOutSuccess,
	FString& OutError,
	const bool bDumpContextAfterEachTool)
{
	if (!IsInGameThread())
	{
		OutError = TEXT("RunAgentTurnSync must run on the game thread (editor console or automation).");
		bOutSuccess = false;
		return false;
	}
	return UnrealAiHarnessScenarioRunnerPriv::RunAgentTurnSync_GameThread(
		UserMessage,
		ThreadIdDigitsWithHyphens,
		Mode,
		OutputRootDir,
		OutJsonlPath,
		OutRunDir,
		bOutSuccess,
		OutError,
		bDumpContextAfterEachTool);
}
