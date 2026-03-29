#include "Widgets/UnrealAiImagePreview.h"

#include "Misc/Paths.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWindow.h"

namespace UnrealAiImagePreviewPrivate
{
	static TMap<FString, TStrongObjectPtr<UTexture2D>> GTextureByPath;
	static TMap<FString, TSharedPtr<FSlateDynamicImageBrush>> GThumbBrushByPath;

	static bool IsLikelyMagentaPlaceholder(UTexture2D* Tex)
	{
		if (!Tex || !Tex->GetPlatformData() || Tex->GetPlatformData()->Mips.Num() == 0)
		{
			return false;
		}
		const FTexture2DMipMap& Mip0 = Tex->GetPlatformData()->Mips[0];
		const int32 W = Mip0.SizeX;
		const int32 H = Mip0.SizeY;
		if (W <= 0 || H <= 0 || Tex->GetPixelFormat() != PF_B8G8R8A8)
		{
			return false;
		}
		const int64 PixelCount = static_cast<int64>(W) * static_cast<int64>(H);
		if (PixelCount <= 0 || PixelCount > 8'000'000)
		{
			return false;
		}
		const void* Raw = Mip0.BulkData.LockReadOnly();
		if (!Raw)
		{
			Mip0.BulkData.Unlock();
			return false;
		}
		const uint8* Bytes = static_cast<const uint8*>(Raw);
		int64 MagentaishCount = 0;
		for (int64 I = 0; I < PixelCount; ++I)
		{
			const uint8 B = Bytes[(I * 4) + 0];
			const uint8 G = Bytes[(I * 4) + 1];
			const uint8 R = Bytes[(I * 4) + 2];
			// Engine placeholders are often very magenta-heavy.
			if (R >= 220 && B >= 220 && G <= 80)
			{
				++MagentaishCount;
			}
		}
		Mip0.BulkData.Unlock();
		const double Ratio = static_cast<double>(MagentaishCount) / static_cast<double>(PixelCount);
		return Ratio >= 0.70;
	}

	static UTexture2D* LoadTexture(const FString& Path)
	{
		if (TStrongObjectPtr<UTexture2D>* Found = GTextureByPath.Find(Path))
		{
			if (Found->IsValid())
			{
				return Found->Get();
			}
		}
		UTexture2D* Tex = FImageUtils::ImportFileAsTexture2D(Path);
		if (!Tex)
		{
			return nullptr;
		}
		if (IsLikelyMagentaPlaceholder(Tex))
		{
			// Suppress known bad placeholder images in chat cards.
			return nullptr;
		}
		GTextureByPath.Add(Path, TStrongObjectPtr<UTexture2D>(Tex));
		return Tex;
	}
} // namespace UnrealAiImagePreviewPrivate

const FSlateBrush* UnrealAiGetOrCreateScreenshotThumbnailBrush(const FString& AbsolutePathPng)
{
	if (AbsolutePathPng.IsEmpty())
	{
		return FAppStyle::GetBrush(TEXT("Icons.Document"));
	}
	if (TSharedPtr<FSlateDynamicImageBrush>* Existing = UnrealAiImagePreviewPrivate::GThumbBrushByPath.Find(AbsolutePathPng))
	{
		return Existing->Get();
	}
	UTexture2D* Tex = UnrealAiImagePreviewPrivate::LoadTexture(AbsolutePathPng);
	if (!Tex)
	{
		return FAppStyle::GetBrush(TEXT("Icons.Document"));
	}
	const int32 TW = Tex->GetSizeX();
	const int32 TH = Tex->GetSizeY();
	if (TW <= 0 || TH <= 0)
	{
		return FAppStyle::GetBrush(TEXT("Icons.Document"));
	}
	static constexpr float MaxW = 56.f;
	static constexpr float MaxH = 40.f;
	float Dw = static_cast<float>(TW);
	float Dh = static_cast<float>(TH);
	const float Scale = FMath::Min(MaxW / Dw, MaxH / Dh);
	Dw *= Scale;
	Dh *= Scale;
	const TSharedPtr<FSlateDynamicImageBrush> Brush = MakeShared<FSlateDynamicImageBrush>(
		Tex,
		FVector2D(Dw, Dh),
		FName(*FString::Printf(TEXT("UnrealAiThumb_%s"), *FPaths::GetCleanFilename(AbsolutePathPng))));
	UnrealAiImagePreviewPrivate::GThumbBrushByPath.Add(AbsolutePathPng, Brush);
	return Brush.Get();
}

void UnrealAiOpenImagePreviewWindow(const FString& AbsolutePath)
{
	UTexture2D* Tex = UnrealAiImagePreviewPrivate::LoadTexture(AbsolutePath);
	if (!Tex)
	{
		return;
	}
	const float TW = static_cast<float>(Tex->GetSizeX());
	const float TH = static_cast<float>(Tex->GetSizeY());
	if (TW <= 1.f || TH <= 1.f)
	{
		return;
	}
	static constexpr float MaxW = 1200.f;
	static constexpr float MaxH = 800.f;
	float Cw = TW;
	float Ch = TH;
	const float Scale = FMath::Min(MaxW / Cw, MaxH / Ch);
	Cw *= Scale;
	Ch *= Scale;
	const FVector2D ClientSize(Cw, Ch);
	const TSharedPtr<FSlateDynamicImageBrush> Brush = MakeShared<FSlateDynamicImageBrush>(
		Tex,
		ClientSize,
		FName(TEXT("UnrealAiPreviewFull")));
	const TSharedRef<SWindow> Win = SNew(SWindow)
		.Title(FText::FromString(FPaths::GetCleanFilename(AbsolutePath)))
		.ClientSize(ClientSize)
		.SupportsMaximize(true)
		.SupportsMinimize(false)
		[
			SNew(SBorder)
				.Padding(8.f)
				[
					SNew(SBox)
						.WidthOverride(ClientSize.X)
						.HeightOverride(ClientSize.Y)
						[
							SNew(SImage).Image(Brush.Get())
						]
				]
		];
	FSlateApplication::Get().AddWindow(Win, true);
}
