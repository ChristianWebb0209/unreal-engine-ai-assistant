#include "Tools/Presentation/UnrealAiBlueprintThumbnailCapture.h"

#include "ImageUtils.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"

namespace
{
	static bool SaveThumbnailToPng(const FObjectThumbnail& Thumbnail, const FString& OutAbsPngPath)
	{
		const int32 W = Thumbnail.GetImageWidth();
		const int32 H = Thumbnail.GetImageHeight();
		if (W <= 0 || H <= 0)
		{
			return false;
		}

		const TArray<uint8>& RawBgra = Thumbnail.GetUncompressedImageData();
		if (RawBgra.Num() <= 0)
		{
			return false;
		}

		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid())
		{
			return false;
		}

		Wrapper->SetRaw(RawBgra.GetData(), RawBgra.Num(), W, H, ERGBFormat::BGRA, 8);
		const TArray64<uint8>& PngData = Wrapper->GetCompressed(100);
		return FFileHelper::SaveArrayToFile(PngData, *OutAbsPngPath);
	}
}

bool UnrealAiBlueprintThumbnailCapture::TryCaptureBlueprintThumbnailPng(
	const FString& BlueprintObjectPath,
	const FString& OutAbsPngPath,
	uint32 MaxWidth,
	uint32 MaxHeight)
{
	if (BlueprintObjectPath.IsEmpty() || OutAbsPngPath.IsEmpty())
	{
		return false;
	}
	if (BlueprintObjectPath.Contains(TEXT("..")))
	{
		return false;
	}

	UObject* Obj = LoadObject<UObject>(nullptr, *BlueprintObjectPath);
	if (!Obj)
	{
		return false;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutAbsPngPath), true);

	FObjectThumbnail Thumb;
	ThumbnailTools::RenderThumbnail(
		Obj,
		// Max width/height are maximum; render may pick smaller based on aspect.
		MaxWidth,
		MaxHeight,
		ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush,
		nullptr,
		&Thumb);

	if (Thumb.IsEmpty() || !Thumb.HasValidImageData())
	{
		return false;
	}

	return SaveThumbnailToPng(Thumb, OutAbsPngPath);
}

