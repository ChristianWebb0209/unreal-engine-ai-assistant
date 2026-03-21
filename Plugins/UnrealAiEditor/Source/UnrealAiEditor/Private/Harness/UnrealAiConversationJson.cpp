#include "Harness/UnrealAiConversationJson.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealAiConversationJson
{
	static TSharedPtr<FJsonObject> MessageToJsonObject(const FUnrealAiConversationMessage& M)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("role"), M.Role);
		if (M.Role == TEXT("tool"))
		{
			O->SetStringField(TEXT("tool_call_id"), M.ToolCallId);
			O->SetStringField(TEXT("content"), M.Content);
			return O;
		}
		if (M.ToolCalls.Num() > 0)
		{
			if (!M.Content.IsEmpty())
			{
				O->SetStringField(TEXT("content"), M.Content);
			}
			else
			{
				O->SetField(TEXT("content"), MakeShared<FJsonValueNull>());
			}
			TArray<TSharedPtr<FJsonValue>> TcArr;
			for (const FUnrealAiToolCallSpec& Tc : M.ToolCalls)
			{
				TSharedPtr<FJsonObject> Tco = MakeShared<FJsonObject>();
				Tco->SetStringField(TEXT("id"), Tc.Id);
				Tco->SetStringField(TEXT("type"), TEXT("function"));
				TSharedPtr<FJsonObject> Fn = MakeShared<FJsonObject>();
				Fn->SetStringField(TEXT("name"), Tc.Name);
				Fn->SetStringField(TEXT("arguments"), Tc.ArgumentsJson);
				Tco->SetObjectField(TEXT("function"), Fn);
				TcArr.Add(MakeShared<FJsonValueObject>(Tco.ToSharedRef()));
			}
			O->SetArrayField(TEXT("tool_calls"), TcArr);
		}
		else
		{
			O->SetStringField(TEXT("content"), M.Content);
		}
		return O;
	}

	bool MessagesToJson(const TArray<FUnrealAiConversationMessage>& Messages, FString& OutJson)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("schemaVersion"), 1);
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FUnrealAiConversationMessage& M : Messages)
		{
			Arr.Add(MakeShared<FJsonValueObject>(MessageToJsonObject(M).ToSharedRef()));
		}
		Root->SetArrayField(TEXT("messages"), Arr);
		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		if (!FJsonSerializer::Serialize(Root.ToSharedRef(), W))
		{
			return false;
		}
		OutJson = Out;
		return true;
	}

	static bool ParseToolCalls(const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FUnrealAiToolCallSpec>& Out)
	{
		if (!Arr)
		{
			return true;
		}
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject>* Oo;
			if (!V.IsValid() || !V->TryGetObject(Oo))
			{
				continue;
			}
			FUnrealAiToolCallSpec Tc;
			(*Oo)->TryGetStringField(TEXT("id"), Tc.Id);
			const TSharedPtr<FJsonObject>* Fn;
			if ((*Oo)->TryGetObjectField(TEXT("function"), Fn))
			{
				(*Fn)->TryGetStringField(TEXT("name"), Tc.Name);
				(*Fn)->TryGetStringField(TEXT("arguments"), Tc.ArgumentsJson);
			}
			Out.Add(MoveTemp(Tc));
		}
		return true;
	}

	bool JsonToMessages(const FString& Json, TArray<FUnrealAiConversationMessage>& OutMessages, TArray<FString>& OutWarnings)
	{
		OutMessages.Reset();
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(R, Root) || !Root.IsValid())
		{
			OutWarnings.Add(TEXT("conversation.json: invalid JSON"));
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* MsgArr = nullptr;
		if (!Root->TryGetArrayField(TEXT("messages"), MsgArr))
		{
			return true;
		}
		for (const TSharedPtr<FJsonValue>& V : *MsgArr)
		{
			const TSharedPtr<FJsonObject>* Mo;
			if (!V.IsValid() || !V->TryGetObject(Mo))
			{
				continue;
			}
			FUnrealAiConversationMessage M;
			(*Mo)->TryGetStringField(TEXT("role"), M.Role);
			(*Mo)->TryGetStringField(TEXT("content"), M.Content);
			(*Mo)->TryGetStringField(TEXT("tool_call_id"), M.ToolCallId);
			const TArray<TSharedPtr<FJsonValue>>* Tc = nullptr;
			if ((*Mo)->TryGetArrayField(TEXT("tool_calls"), Tc))
			{
				ParseToolCalls(Tc, M.ToolCalls);
			}
			OutMessages.Add(MoveTemp(M));
		}
		return true;
	}

	bool MessagesToOpenAiJsonArray(const TArray<FUnrealAiConversationMessage>& Messages, FString& OutJsonArray)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FUnrealAiConversationMessage& M : Messages)
		{
			Arr.Add(MakeShared<FJsonValueObject>(MessageToJsonObject(M).ToSharedRef()));
		}
		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		if (!FJsonSerializer::Serialize(Arr, W))
		{
			return false;
		}
		OutJsonArray = Out;
		return true;
	}
}
