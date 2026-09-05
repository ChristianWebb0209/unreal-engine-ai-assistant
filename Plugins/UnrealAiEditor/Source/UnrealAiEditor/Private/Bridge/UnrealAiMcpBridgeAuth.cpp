#include "Bridge/UnrealAiMcpBridgeAuth.h"

#include "Backend/IUnrealAiPersistence.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

namespace UnrealAiMcpBridgeAuth
{
	static FString McpTokenRelativePath()
	{
		return TEXT("mcp/bridge.token");
	}

	FString GetTokenFilePath(IUnrealAiPersistence* Persistence)
	{
		if (!Persistence)
		{
			return FString();
		}
		return FPaths::Combine(Persistence->GetDataRootDirectory(), McpTokenRelativePath());
	}

	FString EnsureTokenFile(IUnrealAiPersistence* Persistence, FString& OutTokenFilePath)
	{
		OutTokenFilePath = GetTokenFilePath(Persistence);
		if (OutTokenFilePath.IsEmpty())
		{
			return FString();
		}

		const FString Dir = FPaths::GetPath(OutTokenFilePath);
		IFileManager::Get().MakeDirectory(*Dir, true);

		FString Existing;
		if (FFileHelper::LoadFileToString(Existing, *OutTokenFilePath))
		{
			Existing.TrimStartAndEndInline();
			if (!Existing.IsEmpty())
			{
				return Existing;
			}
		}

		const FString Token = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens)
			+ FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
		if (!FFileHelper::SaveStringToFile(Token, *OutTokenFilePath))
		{
			return FString();
		}
		return Token;
	}

	bool ValidateBearerToken(const FString& AuthorizationHeader, const FString& ExpectedToken)
	{
		if (ExpectedToken.IsEmpty())
		{
			return false;
		}
		const FString Prefix = TEXT("Bearer ");
		if (!AuthorizationHeader.StartsWith(Prefix, ESearchCase::IgnoreCase))
		{
			return false;
		}
		const FString Provided = AuthorizationHeader.Mid(Prefix.Len()).TrimStartAndEnd();
		return Provided == ExpectedToken;
	}
}
