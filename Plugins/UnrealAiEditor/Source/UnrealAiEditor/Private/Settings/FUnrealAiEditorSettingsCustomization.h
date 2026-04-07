#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class FUnrealAiEditorSettingsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
