#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Math/Vector.h"

bool UnrealAiParseVec3NumberArray(const TArray<TSharedPtr<FJsonValue>>& Arr, FVector& Out);

bool UnrealAiTryGetVec3Field(const TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldName, FVector& Out);
