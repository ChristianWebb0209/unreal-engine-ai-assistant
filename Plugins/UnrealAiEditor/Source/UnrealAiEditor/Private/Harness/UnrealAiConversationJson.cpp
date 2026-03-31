#include "Harness/UnrealAiConversationJson.h"

#include "Context/AgentContextTypes.h"
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
			TArray<TSharedPtr<FJsonValue>> TcArr;
			for (const FUnrealAiToolCallSpec& Tc : M.ToolCalls)
			{
				if (Tc.Name.TrimStartAndEnd().IsEmpty())
				{
					continue;
				}
				TSharedPtr<FJsonObject> Tco = MakeShared<FJsonObject>();
				Tco->SetStringField(TEXT("id"), Tc.Id);
				Tco->SetStringField(TEXT("type"), TEXT("function"));
				TSharedPtr<FJsonObject> Fn = MakeShared<FJsonObject>();
				Fn->SetStringField(TEXT("name"), Tc.Name);
				Fn->SetStringField(TEXT("arguments"), Tc.ArgumentsJson);
				Tco->SetObjectField(TEXT("function"), Fn);
				TcArr.Add(MakeShared<FJsonValueObject>(Tco.ToSharedRef()));
			}
			if (TcArr.Num() > 0)
			{
				if (!M.Content.IsEmpty())
				{
					O->SetStringField(TEXT("content"), M.Content);
				}
				else
				{
					O->SetField(TEXT("content"), MakeShared<FJsonValueNull>());
				}
				O->SetArrayField(TEXT("tool_calls"), TcArr);
			}
			else
			{
				O->SetStringField(TEXT("content"), M.Content);
			}
		}
		else
		{
			O->SetStringField(TEXT("content"), M.Content);
		}
		return O;
	}

	static TSharedPtr<FJsonObject> MessageToPersistedJsonObject(const FUnrealAiConversationMessage& M)
	{
		TSharedPtr<FJsonObject> O = MessageToJsonObject(M);
		if (M.Role == TEXT("user") && M.bHasUserAgentMode)
		{
			const TCHAR* S = TEXT("agent");
			switch (M.UserAgentMode)
			{
			case EUnrealAiAgentMode::Ask:
				S = TEXT("ask");
				break;
			case EUnrealAiAgentMode::Plan:
				S = TEXT("plan");
				break;
			default:
				S = TEXT("agent");
				break;
			}
			O->SetStringField(TEXT("ui_agent_mode"), FString(S));
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
			Arr.Add(MakeShared<FJsonValueObject>(MessageToPersistedJsonObject(M).ToSharedRef()));
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
			if (Tc.Name.TrimStartAndEnd().IsEmpty())
			{
				continue;
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
			FString UiMode;
			if ((*Mo)->TryGetStringField(TEXT("ui_agent_mode"), UiMode))
			{
				UiMode.TrimStartAndEndInline();
				if (UiMode.Equals(TEXT("ask"), ESearchCase::IgnoreCase))
				{
					M.bHasUserAgentMode = true;
					M.UserAgentMode = EUnrealAiAgentMode::Ask;
				}
				else if (UiMode.Equals(TEXT("plan"), ESearchCase::IgnoreCase))
				{
					M.bHasUserAgentMode = true;
					M.UserAgentMode = EUnrealAiAgentMode::Plan;
				}
				else if (UiMode.Equals(TEXT("agent"), ESearchCase::IgnoreCase))
				{
					M.bHasUserAgentMode = true;
					M.UserAgentMode = EUnrealAiAgentMode::Agent;
				}
			}
			OutMessages.Add(MoveTemp(M));
		}
		return true;
	}

	/** OpenAI 400 if role=tool is not immediately preceded by assistant tool_calls containing that tool_call_id (after skipping prior tool rows). */
	static void RemoveOrphanToolMessages(TArray<FUnrealAiConversationMessage>& Ms)
	{
		bool bChanged = true;
		while (bChanged)
		{
			bChanged = false;
			for (int32 i = 0; i < Ms.Num(); ++i)
			{
				if (Ms[i].Role != TEXT("tool"))
				{
					continue;
				}
				int32 Prev = i - 1;
				while (Prev >= 0 && Ms[Prev].Role == TEXT("tool"))
				{
					--Prev;
				}
				if (Prev < 0)
				{
					Ms.RemoveAt(i);
					bChanged = true;
					break;
				}
				if (Ms[Prev].Role != TEXT("assistant"))
				{
					Ms.RemoveAt(i);
					bChanged = true;
					break;
				}
				const FUnrealAiConversationMessage& Asst = Ms[Prev];
				bool bOk = false;
				for (const FUnrealAiToolCallSpec& Tc : Asst.ToolCalls)
				{
					if (Tc.Name.TrimStartAndEnd().IsEmpty())
					{
						continue;
					}
					if (Tc.Id == Ms[i].ToolCallId)
					{
						bOk = true;
						break;
					}
				}
				if (!bOk)
				{
					Ms.RemoveAt(i);
					bChanged = true;
					break;
				}
			}
		}
	}

	bool MessagesToChatCompletionsJsonArray(const TArray<FUnrealAiConversationMessage>& Messages, FString& OutJsonArray)
	{
		TArray<FUnrealAiConversationMessage> Sanitized = Messages;
		if (Sanitized.Num() > 1 && Sanitized[0].Role == TEXT("system"))
		{
			while (Sanitized.Num() > 1 && Sanitized[1].Role == TEXT("tool"))
			{
				Sanitized.RemoveAt(1);
			}
		}
		RemoveOrphanToolMessages(Sanitized);
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FUnrealAiConversationMessage& M : Sanitized)
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
