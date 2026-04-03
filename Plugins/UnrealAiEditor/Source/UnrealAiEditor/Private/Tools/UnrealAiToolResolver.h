#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Harness/IToolExecutionHost.h"

class FUnrealAiToolCatalog;

struct FUnrealAiResolvedToolInvocation
{
	bool bResolved = false;
	FString RequestedToolId;
	FString CanonicalToolId;
	FString LegacyToolId;
	TSharedPtr<FJsonObject> ResolvedArguments;
	TSharedPtr<FJsonObject> RequestedToolDefinition;
	TSharedPtr<FJsonObject> LegacyToolDefinition;
	TSharedPtr<FJsonObject> Audit;
	FUnrealAiToolInvocationResult FailureResult;
};

/** Catalog-aware normalization / family dispatch that sits ahead of the legacy tool router. */
class FUnrealAiToolResolver
{
public:
	explicit FUnrealAiToolResolver(const FUnrealAiToolCatalog& InCatalog);

	FUnrealAiResolvedToolInvocation Resolve(const FString& ToolId, const TSharedPtr<FJsonObject>& Arguments) const;

	static void AttachAuditToResult(const TSharedPtr<FJsonObject>& Audit, FUnrealAiToolInvocationResult& InOutResult);

private:
	const FUnrealAiToolCatalog& Catalog;
};
