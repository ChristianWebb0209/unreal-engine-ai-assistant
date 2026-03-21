#include "Context/AgentContextFormat.h"

#include "Context/UnrealAiEditorContextQueries.h"
#include "Misc/EngineVersion.h"

namespace UnrealAiAgentContextFormat
{
	FString GetProjectEngineVersionLabel()
	{
		const FEngineVersion& E = FEngineVersion::Current();
		return FString::Printf(TEXT("Unreal Engine %d.%d"), E.GetMajor(), E.GetMinor());
	}

	static FString TypeToString(EContextAttachmentType T)
	{
		switch (T)
		{
		case EContextAttachmentType::AssetPath: return TEXT("asset");
		case EContextAttachmentType::FilePath: return TEXT("file");
		case EContextAttachmentType::FreeText: return TEXT("text");
		case EContextAttachmentType::BlueprintNodeRef: return TEXT("bp_node");
		case EContextAttachmentType::ActorReference: return TEXT("actor");
		case EContextAttachmentType::ContentFolder: return TEXT("folder");
		default: return TEXT("unknown");
		}
	}

	FString FormatContextBlock(const FAgentContextState& State, const FAgentContextBuildOptions& Options)
	{
		FString Out;
		// First block: double-paren engine version only (authoritative editor runtime).
		Out += FString::Printf(TEXT("### ((%s))\n\n"), *GetProjectEngineVersionLabel());
		if (Options.bIncludeAttachments)
		{
			for (const FContextAttachment& A : State.Attachments)
			{
				const FString Body = UnrealAiEditorContextQueries::BuildRichAttachmentPayload(A);
				Out += FString::Printf(
					TEXT("### Attachment (%s)\n%s\n\n"),
					*TypeToString(A.Type),
					*Body);
			}
		}
		if (Options.bIncludeToolResults && Options.Mode != EUnrealAiAgentMode::Ask)
		{
			for (const FToolContextEntry& E : State.ToolResults)
			{
				Out += FString::Printf(
					TEXT("### Tool: %s\n%s\n\n"),
					*E.ToolName,
					*E.TruncatedResult);
			}
		}
		if (Options.bIncludeEditorSnapshot && State.EditorSnapshot.IsSet() && State.EditorSnapshot.GetValue().bValid)
		{
			const FEditorContextSnapshot& S = State.EditorSnapshot.GetValue();
			Out += TEXT("### Editor snapshot\n");
			if (!S.SelectedActorsSummary.IsEmpty())
			{
				Out += FString::Printf(TEXT("Level selection: %s\n"), *S.SelectedActorsSummary);
			}
			if (!S.ContentBrowserPath.IsEmpty())
			{
				Out += FString::Printf(TEXT("Content Browser folder: %s\n"), *S.ContentBrowserPath);
			}
			if (S.ContentBrowserSelectedAssets.Num() > 0)
			{
				Out += TEXT("Content Browser selected assets:\n");
				for (const FString& P : S.ContentBrowserSelectedAssets)
				{
					Out += FString::Printf(TEXT("- %s\n"), *P);
				}
			}
			else if (!S.ActiveAssetPath.IsEmpty())
			{
				Out += FString::Printf(TEXT("Content Browser primary asset: %s\n"), *S.ActiveAssetPath);
			}
			if (S.OpenEditorAssets.Num() > 0)
			{
				Out += TEXT("Open asset editors:\n");
				for (const FString& P : S.OpenEditorAssets)
				{
					Out += FString::Printf(TEXT("- %s\n"), *P);
				}
			}
			Out += TEXT("\n");
		}
		return Out.TrimEnd();
	}

	FString TruncateToBudget(const FString& Text, int32 MaxChars, TArray<FString>& OutWarnings)
	{
		if (MaxChars <= 0 || Text.Len() <= MaxChars)
		{
			return Text;
		}
		OutWarnings.Add(FString::Printf(TEXT("Context truncated from %d to %d characters."), Text.Len(), MaxChars));
		return Text.Left(MaxChars);
	}

	void ApplyModeToStateForBuild(FAgentContextState& State, const FAgentContextBuildOptions& Options)
	{
		if (Options.Mode == EUnrealAiAgentMode::Ask)
		{
			State.ToolResults.Reset();
		}
	}

	int32 EstimateTokensApprox(const FString& Text)
	{
		return FMath::Max(1, Text.Len() / 4);
	}
}
