#include "Context/UnrealAiContextMentionParser.h"

#include "Context/AgentContextTypes.h"
#include "Context/IAgentContextService.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Internationalization/Regex.h"
#include "Modules/ModuleManager.h"
#include "UObject/SoftObjectPath.h"

namespace UnrealAiContextMentionParser
{
	static bool ResolveTokenToAssetPath(const FString& Token, FString& OutPath)
	{
		const FSoftObjectPath Direct(Token);
		if (Direct.IsValid())
		{
			OutPath = Direct.ToString();
			return true;
		}

		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AR = ARM.Get();
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(TEXT("/Game")));
		Filter.bRecursivePaths = true;
		Filter.bRecursiveClasses = true;
		TArray<FAssetData> Found;
		AR.GetAssets(Filter, Found);
		const FName WantName(*Token);
		for (const FAssetData& AD : Found)
		{
			if (AD.AssetName == WantName)
			{
				OutPath = AD.GetObjectPathString();
				return true;
			}
		}
		return false;
	}

	void ApplyMentionsFromPrompt(
		IAgentContextService* Ctx,
		const FString& ProjectId,
		const FString& ThreadId,
		const FString& Prompt)
	{
		if (!Ctx || Prompt.IsEmpty())
		{
			return;
		}
		Ctx->LoadOrCreate(ProjectId, ThreadId);
		const FAgentContextState* St = Ctx->GetState(ProjectId, ThreadId);
		TSet<FString> SeenAssetPayloads;
		if (St)
		{
			for (const FContextAttachment& A : St->Attachments)
			{
				if (A.Type == EContextAttachmentType::AssetPath && !A.Payload.IsEmpty())
				{
					SeenAssetPayloads.Add(FSoftObjectPath(A.Payload).ToString());
				}
			}
		}
		static const FRegexPattern MentionPattern(TEXT("@([A-Za-z0-9_./]+)"));
		FRegexMatcher M(MentionPattern, Prompt);
		while (M.FindNext())
		{
			const FString Token = M.GetCaptureGroup(1);
			if (Token.IsEmpty())
			{
				continue;
			}
			FString Resolved;
			if (ResolveTokenToAssetPath(Token, Resolved))
			{
				const FString PayloadNorm = FSoftObjectPath(Resolved).ToString();
				if (SeenAssetPayloads.Contains(PayloadNorm))
				{
					continue;
				}
				FContextAttachment A;
				A.Type = EContextAttachmentType::AssetPath;
				A.Payload = PayloadNorm;
				A.Label = FString(TEXT("@")) + Token;
				Ctx->AddAttachment(A);
				SeenAssetPayloads.Add(PayloadNorm);
			}
		}
	}
}
