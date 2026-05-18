#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "TerrainEditorHUD.generated.h"

UCLASS()
class PROCEDURALGENERATION_API ATerrainEditorHUD : public AHUD
{
	GENERATED_BODY()

public:
	ATerrainEditorHUD();

	virtual void DrawHUD() override;

	UPROPERTY(EditAnywhere, Category="HUD")
	float PanelPadding = 14.f;

	UPROPERTY(EditAnywhere, Category="HUD")
	float PanelWidth = 280.f;

	UPROPERTY(EditAnywhere, Category="HUD")
	FLinearColor PanelColor = FLinearColor(0.f, 0.f, 0.f, 0.55f);

	UPROPERTY(EditAnywhere, Category="HUD")
	bool bShowFPS = true;

private:
	float SmoothedFPS = 60.f;
};
