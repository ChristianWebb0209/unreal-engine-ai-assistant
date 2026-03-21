#include "Context/UnrealAiContextDragDrop.h"

#include "Backend/UnrealAiBackendRegistry.h"
#include "Context/IAgentContextService.h"
#include "Context/UnrealAiEditorContextQueries.h"
#include "Context/UnrealAiProjectId.h"
#include "Widgets/UnrealAiChatUiSession.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ContentBrowserDataDragDropOp.h"
#include "ContentBrowserItem.h"
#include "DragAndDrop/ActorDragDropOp.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "Input/DragAndDrop.h"
#include "Modules/ModuleManager.h"
#include "UObject/SoftObjectPath.h"
#endif

namespace UnrealAiContextDragDropPriv
{
#if WITH_EDITOR
	static FContextAttachment MakeFolderAttachment(const FContentBrowserItem& Item)
	{
		FContextAttachment A;
		A.Type = EContextAttachmentType::ContentFolder;
		const FName V = Item.GetVirtualPath();
		A.Payload = V.IsNone() ? FString() : V.ToString();
		A.Label = FString::Printf(TEXT("Folder: %s"), A.Payload.IsEmpty() ? TEXT("(invalid)") : *A.Payload);
		return A;
	}

	static void AddResolvedPath(
		IAssetRegistry& Reg,
		const FString& PathStr,
		TArray<FContextAttachment>& Out,
		TSet<FString>& Seen)
	{
		if (PathStr.IsEmpty() || Seen.Contains(PathStr))
		{
			return;
		}
		const FSoftObjectPath S(PathStr);
		const FAssetData AD = Reg.GetAssetByObjectPath(S);
		if (AD.IsValid())
		{
			const FString Key = AD.GetObjectPathString();
			if (!Seen.Contains(Key))
			{
				Seen.Add(Key);
				Out.Add(UnrealAiEditorContextQueries::AttachmentFromAssetData(AD));
			}
		}
		else
		{
			Seen.Add(PathStr);
			FContextAttachment A;
			A.Type = EContextAttachmentType::FreeText;
			A.Payload = FString::Printf(TEXT("Unresolved asset path: %s"), *PathStr);
			A.Label = PathStr;
			Out.Add(A);
		}
	}
#endif
} // namespace UnrealAiContextDragDropPriv

bool UnrealAiContextDragDrop::TryParseDragDrop(
	const FDragDropEvent& DragDropEvent,
	TArray<FContextAttachment>& OutAttachments)
{
	OutAttachments.Reset();
#if WITH_EDITOR
	const TSharedPtr<FDragDropOperation> Op = DragDropEvent.GetOperation();
	if (!Op.IsValid())
	{
		return false;
	}

	TSet<FString> Seen;

	if (Op->IsOfType<FActorDragDropOp>())
	{
		const TSharedPtr<FActorDragDropOp> ActOp = StaticCastSharedPtr<FActorDragDropOp>(Op);
		for (const TWeakObjectPtr<AActor>& W : ActOp->Actors)
		{
			if (AActor* A = W.Get())
			{
				const FString Key = A->GetPathName();
				if (!Seen.Contains(Key))
				{
					Seen.Add(Key);
					OutAttachments.Add(UnrealAiEditorContextQueries::AttachmentFromActor(A));
				}
			}
		}
		return OutAttachments.Num() > 0;
	}

	if (Op->IsOfType<FAssetDragDropOp>())
	{
		const TSharedPtr<FAssetDragDropOp> AssetOp = StaticCastSharedPtr<FAssetDragDropOp>(Op);
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& Reg = ARM.Get();

		for (const FAssetData& AD : AssetOp->GetAssets())
		{
			if (AD.IsValid())
			{
				const FString Key = AD.GetObjectPathString();
				if (!Seen.Contains(Key))
				{
					Seen.Add(Key);
					OutAttachments.Add(UnrealAiEditorContextQueries::AttachmentFromAssetData(AD));
				}
			}
		}
		for (const FString& P : AssetOp->GetAssetPaths())
		{
			UnrealAiContextDragDropPriv::AddResolvedPath(Reg, P, OutAttachments, Seen);
		}

		if (Op->IsOfType<FContentBrowserDataDragDropOp>())
		{
			const TSharedPtr<FContentBrowserDataDragDropOp> CbOp = StaticCastSharedPtr<FContentBrowserDataDragDropOp>(Op);
			for (const FContentBrowserItem& Item : CbOp->GetDraggedFolders())
			{
				if (Item.IsValid())
				{
					const FContextAttachment F = UnrealAiContextDragDropPriv::MakeFolderAttachment(Item);
					if (!F.Payload.IsEmpty() && !Seen.Contains(F.Payload))
					{
						Seen.Add(F.Payload);
						OutAttachments.Add(F);
					}
				}
			}
		}

		return OutAttachments.Num() > 0;
	}
#endif
	(void)DragDropEvent;
	return false;
}

void UnrealAiContextDragDrop::AddAttachmentsToActiveChat(
	TSharedPtr<FUnrealAiBackendRegistry> BackendRegistry,
	TSharedPtr<FUnrealAiChatUiSession> Session,
	const TArray<FContextAttachment>& Attachments)
{
	if (!BackendRegistry.IsValid() || !Session.IsValid() || Attachments.Num() == 0)
	{
		return;
	}
	IAgentContextService* Ctx = BackendRegistry->GetContextService();
	if (!Ctx)
	{
		return;
	}
	const FString ProjectId = UnrealAiProjectId::GetCurrentProjectId();
	const FString Tid = Session->ThreadId.ToString(EGuidFormats::DigitsWithHyphens);
	Ctx->LoadOrCreate(ProjectId, Tid);

	const FAgentContextState* St = Ctx->GetState(ProjectId, Tid);
	const int32 Existing = St ? St->Attachments.Num() : 0;
	const int32 Room = FMath::Max(0, MaxAttachmentsPerAction - Existing);
	const int32 ToAdd = FMath::Min(Attachments.Num(), Room);
	for (int32 i = 0; i < ToAdd; ++i)
	{
		Ctx->AddAttachment(Attachments[i]);
	}
}
