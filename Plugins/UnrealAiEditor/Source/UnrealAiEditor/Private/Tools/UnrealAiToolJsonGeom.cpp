#include "Tools/UnrealAiToolJsonGeom.h"

#include "Dom/JsonValue.h"

bool UnrealAiParseVec3NumberArray(const TArray<TSharedPtr<FJsonValue>>& Arr, FVector& Out)
{
	if (Arr.Num() < 3)
	{
		return false;
	}
	double X = 0.0, Y = 0.0, Z = 0.0;
	Arr[0]->TryGetNumber(X);
	Arr[1]->TryGetNumber(Y);
	Arr[2]->TryGetNumber(Z);
	Out = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
	return true;
}

bool UnrealAiTryGetVec3Field(const TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldName, FVector& Out)
{
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Obj.IsValid() || !Obj->TryGetArrayField(FieldName, Arr) || !Arr || Arr->Num() < 3)
	{
		return false;
	}
	return UnrealAiParseVec3NumberArray(*Arr, Out);
}
