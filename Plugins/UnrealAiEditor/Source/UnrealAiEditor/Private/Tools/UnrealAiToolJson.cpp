#include "Tools/UnrealAiToolJson.h"

#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

FString UnrealAiToolJson::SerializeObject(const TSharedPtr<FJsonObject>& Obj)
{
	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	return Out;
}

FUnrealAiToolInvocationResult UnrealAiToolJson::Ok(const TSharedPtr<FJsonObject>& Payload)
{
	FUnrealAiToolInvocationResult R;
	R.bOk = true;
	R.ContentForModel = SerializeObject(Payload);
	R.EditorPresentation = nullptr;
	return R;
}

FUnrealAiToolInvocationResult UnrealAiToolJson::OkWithEditorPresentation(
	const TSharedPtr<FJsonObject>& Payload,
	const TSharedPtr<FUnrealAiToolEditorPresentation>& EditorPresentation)
{
	FUnrealAiToolInvocationResult R = Ok(Payload);
	R.EditorPresentation = EditorPresentation;
	return R;
}

FUnrealAiToolInvocationResult UnrealAiToolJson::Error(const FString& Message)
{
	FUnrealAiToolInvocationResult R;
	R.bOk = false;
	R.ErrorMessage = Message;
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("error"), Message);
	O->SetBoolField(TEXT("ok"), false);
	R.ContentForModel = SerializeObject(O);
	R.EditorPresentation = nullptr;
	return R;
}

FUnrealAiToolInvocationResult UnrealAiToolJson::ErrorWithSuggestedCall(
	const FString& Message,
	const FString& ToolId,
	const TSharedPtr<FJsonObject>& SuggestedArguments)
{
	FUnrealAiToolInvocationResult R;
	R.bOk = false;
	R.ErrorMessage = Message;
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("error"), Message);
	O->SetBoolField(TEXT("ok"), false);
	if (!ToolId.IsEmpty() && SuggestedArguments.IsValid())
	{
		TSharedPtr<FJsonObject> Suggested = MakeShared<FJsonObject>();
		Suggested->SetStringField(TEXT("tool_id"), ToolId);
		Suggested->SetObjectField(TEXT("arguments"), SuggestedArguments);
		O->SetObjectField(TEXT("suggested_correct_call"), Suggested);
	}
	R.ContentForModel = SerializeObject(O);
	R.EditorPresentation = nullptr;
	return R;
}
