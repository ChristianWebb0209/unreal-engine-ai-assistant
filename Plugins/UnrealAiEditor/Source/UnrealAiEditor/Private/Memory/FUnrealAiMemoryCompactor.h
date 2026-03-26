#pragma once

#include "CoreMinimal.h"
#include "Memory/UnrealAiMemoryTypes.h"

class IUnrealAiMemoryService;

class FUnrealAiMemoryCompactor
{
public:
	explicit FUnrealAiMemoryCompactor(IUnrealAiMemoryService* InMemoryService);

	FUnrealAiMemoryCompactionResult Run(const FUnrealAiMemoryCompactionInput& Input, int32 MaxToCreate, float MinConfidence);

private:
	IUnrealAiMemoryService* MemoryService = nullptr;
};
