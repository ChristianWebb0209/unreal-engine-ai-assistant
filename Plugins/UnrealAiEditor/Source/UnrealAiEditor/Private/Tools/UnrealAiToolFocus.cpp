#include "Tools/UnrealAiToolFocus.h"

#include "Tools/UnrealAiToolDispatch_BlueprintTools.h"
#include "Tools/Presentation/UnrealAiEditorNavigation.h"
#include "Tools/UnrealAiToolProjectPathAllowlist.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Editor.h"
#include "ISourceCodeAccessor.h"
#include "ISourceCodeAccessModule.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace UnrealAiToolFocusPriv
{
	static bool TryParseObjectPathFromOkResultJson(const FString& Json, FString& OutObjectPath)
	{
		OutObjectPath.Reset();
		TSharedPtr<FJsonObject> O;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, O) || !O.IsValid())
		{
			return false;
		}
		return O->TryGetStringField(TEXT("object_path"), OutObjectPath) && !OutObjectPath.IsEmpty();
	}

	static void FocusProjectSourceFile(const FString& RelativePath)
	{
		FString Abs;
		FString Err;
		if (!UnrealAiResolveProjectFilePath(RelativePath, Abs, Err))
		{
			return;
		}
		if (!FPaths::FileExists(Abs))
		{
			return;
		}
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("SourceCodeAccess")))
		{
			FModuleManager::Get().LoadModule(TEXT("SourceCodeAccess"));
		}
		ISourceCodeAccessModule* Module = FModuleManager::GetModulePtr<ISourceCodeAccessModule>(TEXT("SourceCodeAccess"));
		if (!Module || !Module->CanAccessSourceCode())
		{
			return;
		}
		Module->GetAccessor().OpenFileAtLine(Abs, 1, 0);
	}

	static void FocusObjectPathGeneric(const FString& ObjectPath)
	{
		if (!ObjectPath.IsEmpty())
		{
			UnrealAiEditorNavigation::NavigateToAssetObjectPath(ObjectPath);
		}
	}

	static void TryFocusAssetCreate(const TSharedPtr<FJsonObject>& Args, const FUnrealAiToolInvocationResult& Result)
	{
		FString ObjPath;
		if (TryParseObjectPathFromOkResultJson(Result.ContentForModel, ObjPath))
		{
			FocusObjectPathGeneric(ObjPath);
			return;
		}
		FString PackagePath;
		FString AssetName;
		if (Args.IsValid() && Args->TryGetStringField(TEXT("package_path"), PackagePath) && !PackagePath.IsEmpty()
			&& Args->TryGetStringField(TEXT("asset_name"), AssetName) && !AssetName.IsEmpty())
		{
			FString P = PackagePath;
			if (P.StartsWith(TEXT("/Game")) && !P.Contains(TEXT(".")))
			{
				if (!P.EndsWith(TEXT("/")))
				{
					P += TEXT("/");
				}
				const FString Built = P + AssetName + TEXT(".") + AssetName;
				FocusObjectPathGeneric(Built);
			}
		}
	}
}

void UnrealAiApplyPostToolEditorFocus(
	const FString& ToolId,
	const TSharedPtr<FJsonObject>& Args,
	const FUnrealAiToolInvocationResult& Result)
{
	using namespace UnrealAiToolFocusPriv;

	if (!Result.bOk || !Args.IsValid())
	{
		return;
	}

	if (ToolId.StartsWith(TEXT("blueprint_")))
	{
		FString P;
		FString G;
		Args->TryGetStringField(TEXT("blueprint_path"), P);
		Args->TryGetStringField(TEXT("graph_name"), G);
		if (!P.IsEmpty())
		{
			UnrealAiFocusBlueprintEditor(P, G);
		}
		return;
	}

	if (ToolId == TEXT("animation_blueprint_get_graph_summary"))
	{
		FString P;
		if (Args->TryGetStringField(TEXT("anim_blueprint_path"), P) && !P.IsEmpty())
		{
			UnrealAiFocusBlueprintEditor(P, FString());
		}
		return;
	}

	if (ToolId == TEXT("asset_open_editor"))
	{
		FString Path;
		if (Args->TryGetStringField(TEXT("object_path"), Path))
		{
			FocusObjectPathGeneric(Path);
		}
		return;
	}

	if (ToolId == TEXT("asset_apply_properties"))
	{
		bool bDryRun = false;
		Args->TryGetBoolField(TEXT("dry_run"), bDryRun);
		if (!bDryRun)
		{
			FString Path;
			if (Args->TryGetStringField(TEXT("object_path"), Path))
			{
				FocusObjectPathGeneric(Path);
			}
		}
		return;
	}

	if (ToolId == TEXT("asset_export_properties"))
	{
		FString Path;
		if (Args->TryGetStringField(TEXT("object_path"), Path) || Args->TryGetStringField(TEXT("path"), Path))
		{
			FocusObjectPathGeneric(Path);
		}
		return;
	}

	if (ToolId == TEXT("asset_create"))
	{
		TryFocusAssetCreate(Args, Result);
		return;
	}

	if (ToolId == TEXT("project_file_read_text") || ToolId == TEXT("project_file_write_text"))
	{
		FString Rel;
		if (Args->TryGetStringField(TEXT("relative_path"), Rel) && !Rel.IsEmpty())
		{
			FocusProjectSourceFile(Rel);
		}
		return;
	}
	if (ToolId == TEXT("project_file_move"))
	{
		FString Rel;
		if (Args->TryGetStringField(TEXT("to_relative_path"), Rel) && !Rel.IsEmpty())
		{
			FocusProjectSourceFile(Rel);
		}
		return;
	}

	if (ToolId == TEXT("content_browser_sync_asset"))
	{
		FString Path;
		if (Args->TryGetStringField(TEXT("path"), Path) && !Path.IsEmpty())
		{
			FocusObjectPathGeneric(Path);
		}
		return;
	}
}
