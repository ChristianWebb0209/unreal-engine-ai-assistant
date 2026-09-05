#include "Harness/UnrealAiVisionMessageContent.h"

#include "Context/UnrealAiEditorContextQueries.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UnrealAiVisionMessageContent
{
	bool TryEncodePngFileAsDataUrl(const FString& AbsolutePathPng, FString& OutDataUrl, FString& OutError)
	{
		OutDataUrl.Reset();
		OutError.Reset();
		const FString Path = FPaths::ConvertRelativePathToFull(AbsolutePathPng);
		if (Path.IsEmpty() || !FPaths::FileExists(Path))
		{
			OutError = FString::Printf(TEXT("Image file not found: %s"), *AbsolutePathPng);
			return false;
		}
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			OutError = FString::Printf(TEXT("Could not read image file: %s"), *Path);
			return false;
		}
		if (Bytes.Num() <= 0)
		{
			OutError = FString::Printf(TEXT("Image file is empty: %s"), *Path);
			return false;
		}
		if (Bytes.Num() > MaxImageFileBytes)
		{
			OutError = FString::Printf(
				TEXT("Image file too large (%d bytes, max %d): %s"),
				Bytes.Num(),
				MaxImageFileBytes,
				*Path);
			return false;
		}
		FString B64;
		B64 = FBase64::Encode(Bytes);
		OutDataUrl = FString::Printf(TEXT("data:image/png;base64,%s"), *B64);
		return true;
	}

	void ApplyContextImageAttachmentsToApiMessages(
		TArray<FUnrealAiConversationMessage>& ApiMsgs,
		const TArray<FContextAttachment>& Attachments,
		const bool bModelSupportsImages,
		TArray<FString>& OutUserVisibleMessages)
	{
		if (!bModelSupportsImages || Attachments.Num() == 0 || ApiMsgs.Num() == 0)
		{
			return;
		}

		int32 LastUserIdx = INDEX_NONE;
		for (int32 i = ApiMsgs.Num() - 1; i >= 0; --i)
		{
			if (ApiMsgs[i].Role == TEXT("user"))
			{
				LastUserIdx = i;
				break;
			}
		}
		if (LastUserIdx == INDEX_NONE)
		{
			return;
		}

		FUnrealAiConversationMessage& UserMsg = ApiMsgs[LastUserIdx];
		int32 Encoded = 0;
		for (const FContextAttachment& A : Attachments)
		{
			if (Encoded >= MaxVisionImagesPerTurn)
			{
				break;
			}
			if (!UnrealAiEditorContextQueries::IsImageLikeAttachment(A))
			{
				continue;
			}
			if (A.Type != EContextAttachmentType::FilePath || A.Payload.IsEmpty())
			{
				continue;
			}
			FString DataUrl;
			FString Err;
			if (!TryEncodePngFileAsDataUrl(A.Payload, DataUrl, Err))
			{
				const FString Display = !A.Label.IsEmpty() ? A.Label : A.Payload;
				OutUserVisibleMessages.Add(FString::Printf(
					TEXT("Screenshot/image could not be sent to the model: %s (%s)"),
					*Display,
					*Err));
				continue;
			}
			UserMsg.VisionImageDataUrls.Add(MoveTemp(DataUrl));
			++Encoded;
		}

		if (Encoded > 0 && UserMsg.Content.TrimStartAndEnd().IsEmpty())
		{
			UserMsg.Content = TEXT("See the attached screenshot(s).");
		}
	}
}
