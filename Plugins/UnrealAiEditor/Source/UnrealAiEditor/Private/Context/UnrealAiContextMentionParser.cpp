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

	void ApplyMentionsFromPrompt(IAgentContextService* Ctx, const FString& Prompt)
	{
		if (!Ctx || Prompt.IsEmpty())
		{
			return;
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
				FContextAttachment A;
				A.Type = EContextAttachmentType::AssetPath;
				A.Payload = Resolved;
				A.Label = FString(TEXT("@")) + Token;
				Ctx->AddAttachment(A);
			}
		}
	}
}
