#include "TerrainEditorHUD.h"
#include "TerrainEditorComponent.h"
#include "FlyingPlayerCharacter.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Engine.h"

ATerrainEditorHUD::ATerrainEditorHUD()
{
}

void ATerrainEditorHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas) return;

	APawn* OwnerPawn = GetOwningPawn();
	AFlyingPlayerCharacter* Player = Cast<AFlyingPlayerCharacter>(OwnerPawn);
	if (!Player || !Player->TerrainEditor) return;

	UTerrainEditorComponent* Editor = Player->TerrainEditor;

	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	const float InstantFPS = Dt > 0.f ? 1.f / Dt : 0.f;
	SmoothedFPS = FMath::Lerp(SmoothedFPS, InstantFPS, 0.1f);

	const float X = PanelPadding;
	const float Y = PanelPadding;
	const float LineH = 22.f;
	const int32 LineCount = bShowFPS ? 5 : 4;
	const float PanelH = LineH * LineCount + PanelPadding * 2;

	FCanvasTileItem Bg(FVector2D(X, Y), FVector2D(PanelWidth, PanelH), PanelColor);
	Bg.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Bg);

	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	if (!Font) return;

	const FLinearColor ModeColor = Editor->GetCurrentModeColor();
	const float TX = X + PanelPadding;
	float TY = Y + PanelPadding;

	FCanvasTextItem Header(FVector2D(TX, TY), FText::FromString(TEXT("Terrain Editor")), Font, FLinearColor::White);
	Header.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(Header);
	TY += LineH;

	FCanvasTextItem ModeText(FVector2D(TX, TY),
		FText::FromString(FString::Printf(TEXT("Mode: %s"), *Editor->GetCurrentModeName())),
		Font, ModeColor);
	ModeText.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(ModeText);
	TY += LineH;

	FCanvasTextItem RadiusText(FVector2D(TX, TY),
		FText::FromString(FString::Printf(TEXT("Radius: %.0f"), Editor->BrushRadius)),
		Font, FLinearColor::White);
	RadiusText.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(RadiusText);
	TY += LineH;

	FCanvasTextItem StrengthText(FVector2D(TX, TY),
		FText::FromString(FString::Printf(TEXT("Strength: %.2f"), Editor->BrushStrength)),
		Font, FLinearColor::White);
	StrengthText.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(StrengthText);
	TY += LineH;

	if (bShowFPS)
	{
		const FLinearColor FpsColor = SmoothedFPS > 55.f ? FLinearColor(0.3f, 1.f, 0.3f)
			: (SmoothedFPS > 30.f ? FLinearColor(1.f, 0.85f, 0.2f) : FLinearColor(1.f, 0.3f, 0.3f));
		FCanvasTextItem FpsText(FVector2D(TX, TY),
			FText::FromString(FString::Printf(TEXT("FPS: %.0f  (%.2f ms)"), SmoothedFPS, Dt * 1000.f)),
			Font, FpsColor);
		FpsText.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(FpsText);
	}
}
