#include "Context/UnrealAiContextOverview.h"

#include "Context/AgentContextTypes.h"
#include "Context/AgentContextFormat.h"
#include "Context/IAgentContextService.h"
#include "Context/UnrealAiActiveTodoSummary.h"

namespace UnrealAiContextOverviewPriv
{
	static FString AttachmentOneLiner(const FContextAttachment& A)
	{
		const FString Kind = [&]() -> FString
		{
			switch (A.Type)
			{
			case EContextAttachmentType::AssetPath: return TEXT("asset");
			case EContextAttachmentType::FilePath: return TEXT("file");
			case EContextAttachmentType::FreeText: return TEXT("text");
			case EContextAttachmentType::BlueprintNodeRef: return TEXT("bp_node");
			case EContextAttachmentType::ActorReference: return TEXT("actor");
			case EContextAttachmentType::ContentFolder: return TEXT("folder");
			default: return TEXT("unknown");
			}
		}();
		FString Line = A.Label.IsEmpty() ? A.Payload : (A.Label + TEXT(" — ") + A.Payload);
		Line = Line.Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" "));
		if (Line.Len() > 140)
		{
			Line = Line.Left(137) + TEXT("...");
		}
		return FString::Printf(TEXT("  - [%s] %s"), *Kind, *Line);
	}

	static const TCHAR* ModeStr(const EUnrealAiAgentMode M)
	{
		switch (M)
		{
		case EUnrealAiAgentMode::Ask: return TEXT("Ask");
		case EUnrealAiAgentMode::Agent: return TEXT("Agent");
		case EUnrealAiAgentMode::Orchestrate: return TEXT("Orchestrate");
		default: return TEXT("Ask");
		}
	}
}

FString UnrealAiFormatContextOverviewForUi(
	IAgentContextService* Ctx,
	const FString& ProjectId,
	const FString& ThreadId,
	const FAgentContextBuildOptions& Options)
{
	using namespace UnrealAiContextOverviewPriv;

	if (!Ctx)
	{
		return TEXT("Context service is not available.");
	}

	Ctx->LoadOrCreate(ProjectId, ThreadId);
	Ctx->RefreshEditorSnapshotFromEngine();

	const FAgentContextState* St = Ctx->GetState(ProjectId, ThreadId);
	if (!St)
	{
		return TEXT("No context state for this thread.");
	}

	int32 Budget = Options.MaxContextChars;
	if (St->MaxContextChars > 0)
	{
		Budget = St->MaxContextChars;
	}

	const FAgentContextBuildResult Built = Ctx->BuildContextWindow(Options);
	const int32 TokEst = UnrealAiAgentContextFormat::EstimateTokensApprox(Built.ContextBlock);

	FString Out;
	Out.Reserve(2048);
	Out += TEXT("Context overview (no LLM call)\n\n");
	Out += FString::Printf(TEXT("Mode: %s\n"), ModeStr(Options.Mode));
	Out += FString::Printf(TEXT("Model images in context: %s\n"), Options.bModelSupportsImages ? TEXT("yes") : TEXT("no"));
	Out += FString::Printf(TEXT("Char budget for context block: %d\n"), Budget);
	Out += FString::Printf(TEXT("Assembled context block: %d chars"), Built.ContextBlock.Len());
	if (Built.bTruncated)
	{
		Out += TEXT(" (truncated to budget)");
	}
	Out += FString::Printf(TEXT("\nRough size: ~%d tokens (chars/4)\n"), TokEst);

	if (!Built.ComplexityLabel.IsEmpty())
	{
		Out += FString::Printf(
			TEXT("Complexity hint: %s (score %.2f)\n"),
			*Built.ComplexityLabel,
			Built.ComplexityScoreNormalized);
	}

	Out += TEXT("\n— Attachments —\n");
	if (St->Attachments.Num() == 0)
	{
		Out += TEXT("  (none)\n");
	}
	else
	{
		Out += FString::Printf(TEXT("  Count: %d\n"), St->Attachments.Num());
		for (const FContextAttachment& A : St->Attachments)
		{
			Out += AttachmentOneLiner(A);
			Out += TEXT("\n");
		}
	}

	Out += TEXT("\n— Tool result memory —\n");
	if (St->ToolResults.Num() == 0)
	{
		Out += TEXT("  (none stored)\n");
	}
	else
	{
		Out += FString::Printf(TEXT("  Count: %d\n"), St->ToolResults.Num());
		if (Options.Mode == EUnrealAiAgentMode::Ask)
		{
			Out += TEXT("  Note: Ask mode omits tool memory from the prompt (entries remain stored).\n");
		}
		for (const FToolContextEntry& E : St->ToolResults)
		{
			Out += FString::Printf(
				TEXT("  - %s — %d chars\n"),
				E.ToolName.IsEmpty() ? TEXT("(tool)") : *E.ToolName,
				E.TruncatedResult.Len());
		}
	}

	Out += TEXT("\n— Editor snapshot —\n");
	if (!St->EditorSnapshot.IsSet() || !St->EditorSnapshot.GetValue().bValid)
	{
		Out += TEXT("  (none or stale — refreshed this frame)\n");
	}
	else
	{
		const FEditorContextSnapshot& S = St->EditorSnapshot.GetValue();
		if (!S.SelectedActorsSummary.IsEmpty())
		{
			Out += FString::Printf(TEXT("  Level selection: %s\n"), *S.SelectedActorsSummary);
		}
		if (!S.ContentBrowserPath.IsEmpty())
		{
			Out += FString::Printf(TEXT("  Content Browser folder: %s\n"), *S.ContentBrowserPath);
		}
		if (S.ContentBrowserSelectedAssets.Num() > 0)
		{
			Out += FString::Printf(TEXT("  CB selected assets: %d\n"), S.ContentBrowserSelectedAssets.Num());
		}
		else if (!S.ActiveAssetPath.IsEmpty())
		{
			Out += FString::Printf(TEXT("  CB primary asset: %s\n"), *S.ActiveAssetPath);
		}
		if (S.OpenEditorAssets.Num() > 0)
		{
			Out += FString::Printf(TEXT("  Open editor tabs: %d\n"), S.OpenEditorAssets.Num());
		}
	}

	Out += TEXT("\n— Todo plan —\n");
	if (St->ActiveTodoPlanJson.IsEmpty())
	{
		Out += TEXT("  (none)\n");
	}
	else
	{
		const FString Sum = UnrealAiFormatActiveTodoSummary(St->ActiveTodoPlanJson, St->TodoStepsDone);
		Out += Sum.IsEmpty() ? TEXT("  (active JSON present)\n") : FString::Printf(TEXT("  %s\n"), *Sum);
	}

	if (Built.UserVisibleMessages.Num() > 0)
	{
		Out += TEXT("\n— Attachment policy —\n");
		for (const FString& L : Built.UserVisibleMessages)
		{
			Out += FString::Printf(TEXT("  %s\n"), *L);
		}
	}

	if (Built.Warnings.Num() > 0)
	{
		Out += TEXT("\n— Build warnings —\n");
		for (const FString& W : Built.Warnings)
		{
			Out += FString::Printf(TEXT("  - %s\n"), *W);
		}
	}

	return Out.TrimEnd();
}
