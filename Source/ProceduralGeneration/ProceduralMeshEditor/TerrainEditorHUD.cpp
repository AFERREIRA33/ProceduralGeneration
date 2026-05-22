#include "TerrainEditorHUD.h"
#include "TerrainEditorComponent.h"
#include "FlyingPlayerCharacter.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Engine.h"

ATerrainEditorHUD::ATerrainEditorHUD()
{
}

FLinearColor ATerrainEditorHUD::GetFpsColor(float Fps) const
{
	if (Fps > FpsGoodThreshold) return FpsGoodColor;
	if (Fps > FpsWarnThreshold) return FpsWarnColor;
	return FpsBadColor;
}

void ATerrainEditorHUD::DrawPanel(EHUDAnchor Anchor, float Width, float Height, FVector2D& OutInnerTopLeft)
{
	const float W = Canvas->SizeX;
	const float H = Canvas->SizeY;
	float X = 0.f, Y = 0.f;
	switch (Anchor)
	{
	case EHUDAnchor::TopLeft:     X = PanelMargin;                    Y = PanelMargin;                    break;
	case EHUDAnchor::TopRight:    X = W - PanelMargin - Width;        Y = PanelMargin;                    break;
	case EHUDAnchor::BottomLeft:  X = PanelMargin;                    Y = H - PanelMargin - Height;       break;
	case EHUDAnchor::BottomRight: X = W - PanelMargin - Width;        Y = H - PanelMargin - Height;       break;
	}
	FCanvasTileItem Bg(FVector2D(X, Y), FVector2D(Width, Height), PanelColor);
	Bg.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Bg);
	OutInnerTopLeft = FVector2D(X + PanelPadding, Y + PanelPadding);
}

void ATerrainEditorHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas) return;

	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	if (!Font) return;

	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	const float InstantFPS = Dt > 0.f ? 1.f / Dt : 0.f;
	SmoothedFPS = FMath::Lerp(SmoothedFPS, InstantFPS, 0.1f);

	if (bShowFPS)
	{
		const float PanelH = LineHeight * 2 + PanelPadding * 2;
		FVector2D Inner;
		DrawPanel(FpsAnchor, FpsPanelWidth, PanelH, Inner);

		FCanvasTextItem Header(Inner, FText::FromString(TEXT("Performance")), Font, FLinearColor::White);
		Header.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(Header);

		const FLinearColor FpsColor = GetFpsColor(SmoothedFPS);
		FCanvasTextItem FpsText(FVector2D(Inner.X, Inner.Y + LineHeight),
			FText::FromString(FString::Printf(TEXT("FPS: %.0f  (%.2f ms)"), SmoothedFPS, Dt * 1000.f)),
			Font, FpsColor);
		FpsText.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(FpsText);
	}

	APawn* OwnerPawn = GetOwningPawn();
	AFlyingPlayerCharacter* Player = Cast<AFlyingPlayerCharacter>(OwnerPawn);
	if (!Player || !Player->TerrainEditor || !bShowBrushPanel) return;

	UTerrainEditorComponent* Editor = Player->TerrainEditor;
	if (!Editor->IsToolActive()) return;

	int32 BrushLines = 3;
	if (bShowKeyHints) BrushLines += 6;
	const float BrushPanelH = LineHeight * BrushLines + PanelPadding * 2 + PanelExtraBottom;

	FVector2D Inner;
	DrawPanel(BrushAnchor, BrushPanelWidth, BrushPanelH, Inner);

	float TY = Inner.Y;

	FCanvasTextItem Header(FVector2D(Inner.X, TY),
		FText::FromString(TEXT("Terrain Editor")), Font, FLinearColor::White);
	Header.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(Header);
	TY += LineHeight;

	const FLinearColor ModeColor = Editor->GetCurrentModeColor();
	FCanvasTextItem ModeText(FVector2D(Inner.X, TY),
		FText::FromString(FString::Printf(TEXT("Mode: %s"), *Editor->GetCurrentModeName())),
		Font, ModeColor);
	ModeText.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(ModeText);
	TY += LineHeight;

	FCanvasTextItem RadiusText(FVector2D(Inner.X, TY),
		FText::FromString(FString::Printf(TEXT("Radius: %.0f / %.0f"), Editor->BrushRadius, Editor->BrushRadiusMax)),
		Font, FLinearColor::White);
	RadiusText.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(RadiusText);
	TY += LineHeight;

	FCanvasTextItem StrengthText(FVector2D(Inner.X, TY),
		FText::FromString(FString::Printf(TEXT("Strength: %.2f / %.2f"), Editor->BrushStrength, Editor->BrushStrengthMax)),
		Font, FLinearColor::White);
	StrengthText.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(StrengthText);
	TY += LineHeight;

	if (bShowKeyHints)
	{
		const FLinearColor HintCol(0.75f, 0.75f, 0.75f);
		const FString Hints[6] = { HintToggle, HintCycle, HintRadius, HintStrength, HintEdit, HintReset };
		for (int32 i = 0; i < 6; ++i)
		{
			FCanvasTextItem T(FVector2D(Inner.X, TY), FText::FromString(Hints[i]), Font, HintCol);
			T.EnableShadow(FLinearColor::Black);
			Canvas->DrawItem(T);
			TY += LineHeight;
		}
	}
}
