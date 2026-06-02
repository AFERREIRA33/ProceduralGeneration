#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "TerrainEditorHUD.generated.h"

UENUM(BlueprintType)
enum class EHUDAnchor : uint8
{
	TopLeft,
	TopRight,
	BottomLeft,
	BottomRight
};

UCLASS()
class PROCEDURALGENERATION_API ATerrainEditorHUD : public AHUD
{
	GENERATED_BODY()

public:
	ATerrainEditorHUD();

	virtual void DrawHUD() override;

	UPROPERTY(EditAnywhere, Category="HUD|Layout")
	float PanelPadding = 14.f;

	UPROPERTY(EditAnywhere, Category="HUD|Layout")
	float PanelMargin = 18.f;

	UPROPERTY(EditAnywhere, Category="HUD|Layout")
	float LineHeight = 22.f;

	UPROPERTY(EditAnywhere, Category="HUD|Layout")
	float PanelExtraBottom = 10.f;

	UPROPERTY(EditAnywhere, Category="HUD|Style")
	FLinearColor PanelColor = FLinearColor(0.f, 0.f, 0.f, 0.55f);

	UPROPERTY(EditAnywhere, Category="HUD|FPS")
	bool bShowFPS = true;

	UPROPERTY(EditAnywhere, Category="HUD|FPS")
	EHUDAnchor FpsAnchor = EHUDAnchor::TopLeft;

	UPROPERTY(EditAnywhere, Category="HUD|FPS")
	float FpsPanelWidth = 180.f;

	UPROPERTY(EditAnywhere, Category="HUD|FPS")
	float FpsGoodThreshold = 55.f;

	UPROPERTY(EditAnywhere, Category="HUD|FPS")
	float FpsWarnThreshold = 30.f;

	UPROPERTY(EditAnywhere, Category="HUD|FPS")
	FLinearColor FpsGoodColor = FLinearColor(0.3f, 1.f, 0.3f);

	UPROPERTY(EditAnywhere, Category="HUD|FPS")
	FLinearColor FpsWarnColor = FLinearColor(1.f, 0.85f, 0.2f);

	UPROPERTY(EditAnywhere, Category="HUD|FPS")
	FLinearColor FpsBadColor = FLinearColor(1.f, 0.3f, 0.3f);

	UPROPERTY(EditAnywhere, Category="HUD|Brush")
	bool bShowBrushPanel = true;

	UPROPERTY(EditAnywhere, Category="HUD|Brush")
	EHUDAnchor BrushAnchor = EHUDAnchor::TopRight;

	UPROPERTY(EditAnywhere, Category="HUD|Brush")
	float BrushPanelWidth = 320.f;

	UPROPERTY(EditAnywhere, Category="HUD|Brush")
	bool bShowEditPrompt = true;

	UPROPERTY(EditAnywhere, Category="HUD|Brush")
	FString EditPrompt = TEXT("Press TAB to Edit Terrain");

	UPROPERTY(EditAnywhere, Category="HUD|Brush")
	float EditPromptPanelWidth = 280.f;

	UPROPERTY(EditAnywhere, Category="HUD|Brush")
	FLinearColor EditPromptColor = FLinearColor(1.f, 0.85f, 0.15f);

	UPROPERTY(EditAnywhere, Category="HUD|Brush|KeyHints")
	bool bShowKeyHints = true;

	UPROPERTY(EditAnywhere, Category="HUD|Brush|KeyHints")
	FString HintToggle = TEXT("Tab: Toggle Tool");

	UPROPERTY(EditAnywhere, Category="HUD|Brush|KeyHints")
	FString HintCycle = TEXT("R: Cycle Mode");

	UPROPERTY(EditAnywhere, Category="HUD|Brush|KeyHints")
	FString HintRadius = TEXT("Mouse Wheel: Radius");

	UPROPERTY(EditAnywhere, Category="HUD|Brush|KeyHints")
	FString HintStrength = TEXT("Shift + Wheel: Strength");

	UPROPERTY(EditAnywhere, Category="HUD|Brush|KeyHints")
	FString HintEdit = TEXT("LMB: Sculpt");

	UPROPERTY(EditAnywhere, Category="HUD|Brush|KeyHints")
	FString HintReset = TEXT("Backspace: Reset Terrain");

private:
	float SmoothedFPS = 60.f;

	void DrawPanel(EHUDAnchor Anchor, float Width, float Height, FVector2D& OutInnerTopLeft);
	FLinearColor GetFpsColor(float Fps) const;
};
