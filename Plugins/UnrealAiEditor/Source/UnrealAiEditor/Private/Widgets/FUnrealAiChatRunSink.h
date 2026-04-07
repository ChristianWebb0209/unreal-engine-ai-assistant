#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Harness/IAgentRunSink.h"
#include "Harness/ILlmTransport.h"
#include "Harness/UnrealAiAgentTypes.h"

class FUnrealAiChatTranscript;
struct FUnrealAiChatUiSession;
class IUnrealAiPersistence;
class FUnrealAiToolCatalog;

/** Bridges harness streaming events to FUnrealAiChatTranscript (chat UI). */
class FUnrealAiChatRunSink final : public IAgentRunSink
{
public:
	FUnrealAiChatRunSink(
		TSharedPtr<FUnrealAiChatTranscript> InTranscript,
		TSharedPtr<FUnrealAiChatUiSession> InSession,
		IUnrealAiPersistence* InPersistence,
		const FString& InProjectId,
		const FString& InThreadId,
		EUnrealAiAgentMode InAgentMode = EUnrealAiAgentMode::Agent,
		FUnrealAiToolCatalog* InToolCatalog = nullptr);

	virtual void OnRunStarted(const FUnrealAiRunIds& Ids) override;
	virtual void OnContextUserMessages(const TArray<FString>& Messages) override;
	virtual void OnAssistantDelta(const FString& Chunk) override;
	virtual void OnThinkingDelta(const FString& Chunk) override;
	virtual void OnToolCallStarted(const FString& ToolName, const FString& CallId, const FString& ArgumentsJson) override;
	virtual void OnToolCallFinished(
		const FString& ToolName,
		const FString& CallId,
		bool bSuccess,
		const FString& ResultPreview,
		const TSharedPtr<FUnrealAiToolEditorPresentation>& EditorPresentation) override;
	virtual void OnEditorBlockingDialogDuringTools(const FString& Summary) override;
	virtual void OnRunContinuation(int32 PhaseIndex, int32 TotalPhasesHint) override;
	virtual void OnTodoPlanEmitted(const FString& Title, const FString& PlanJson) override;
	virtual void OnPlanDraftReady(const FString& DagJsonText) override;
	virtual void OnPlanHarnessSubTurnComplete() override;
	virtual void OnPlanningDecision(const FString& ModeUsed, const TArray<FString>& TriggerReasons, int32 ReplanCount, int32 QueueStepsPending) override;
	virtual void OnSubagentBuilderHandoff(const FString& BuilderDisplayName) override;
	virtual void OnPlanWorkerSpanOpened(const FString& NodeId, const FText& TitleOrEmpty) override;
	virtual void OnPlanWorkerSpanClosed(const FString& NodeId, bool bSuccess, const FString& SummaryOneLine) override;
	virtual void OnEnforcementEvent(const FString& EventType, const FString& Detail) override;
	virtual void OnEnforcementSummary(
		int32 ActionIntentTurns,
		int32 ActionTurnsWithToolCalls,
		int32 ActionTurnsWithExplicitBlocker,
		int32 ActionNoToolNudges,
		int32 MutationIntentTurns,
		int32 MutationReadOnlyNudges) override;
	virtual void OnHarnessProgressLog(const FString& Line) override;
	virtual void OnLlmRequestPreparedForHttp(
		const FUnrealAiAgentTurnRequest& TurnRequest,
		const FGuid& RunId,
		int32 LlmRound,
		int32 EffectiveMaxLlmRounds,
		const FUnrealAiLlmRequest& LlmRequest) override;
	virtual void OnRunFinished(bool bSuccess, const FString& ErrorMessage) override;

private:
	static void AppendStreamChunkFilteringTranscriptEchoLines(
		FString& LineCarry,
		const FString& Chunk,
		TFunctionRef<void(const FString&)> EmitDelta);

	void FlushStreamLineCarries();

	TSharedPtr<FUnrealAiChatTranscript> Transcript;
	TSharedPtr<FUnrealAiChatUiSession> Session;
	IUnrealAiPersistence* Persistence = nullptr;
	FString ProjectId;
	FString ThreadId;
	EUnrealAiAgentMode AgentMode = EUnrealAiAgentMode::Agent;

	/** Incomplete line tails from streamed tokens; strip "--- Section ---" only on full lines. */
	FString AssistantStreamLineCarry;
	FString ThinkingStreamLineCarry;
	FUnrealAiToolCatalog* ToolCatalog = nullptr;
};
