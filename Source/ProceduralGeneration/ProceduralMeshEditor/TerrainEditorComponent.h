#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TerrainEditorComponent.generated.h"

class AGenerateSurface;

UENUM(BlueprintType)
enum class ETerrainBrushMode : uint8
{
	Add		UMETA(DisplayName = "Add"),
	Subtract	UMETA(DisplayName = "Subtract"),
	Flatten	UMETA(DisplayName = "Flatten"),
	Smooth	UMETA(DisplayName = "Smooth")
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROCEDURALGENERATION_API UTerrainEditorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTerrainEditorComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Values", meta=(ClampMin="0", ClampMax="750", UIMin="0", UIMax="750"))
	float BrushRadius = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Values", meta=(ClampMin="0", ClampMax="20", UIMin="0", UIMax="20"))
	float BrushStrength = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Values", meta=(ClampMin="0.05", ClampMax="0.5"))
	float MaxStrengthPerStep = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Limits", meta=(ClampMin="100", ClampMax="750", UIMin="100", UIMax="750"))
	float BrushRadiusMin = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Limits", meta=(ClampMin="0", ClampMax="750", UIMin="0", UIMax="750"))
	float BrushRadiusMax = 750.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Limits")
	float BrushRadiusStep = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Limits", meta=(ClampMin="0", ClampMax="20", UIMin="0", UIMax="20"))
	float BrushStrengthMin = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Limits", meta=(ClampMin="0", ClampMax="20", UIMin="0", UIMax="20"))
	float BrushStrengthMax = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Limits")
	float BrushStrengthStep = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Trace")
	float MaxTraceDistance = 20000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|State")
	bool bStartActive = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Cursor")
	int32 CursorSphereSegments = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Cursor")
	float CursorLineThickness = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Colors")
	FLinearColor AddColor = FLinearColor(0.2f, 0.9f, 0.2f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Colors")
	FLinearColor SubtractColor = FLinearColor(0.9f, 0.2f, 0.2f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Colors")
	FLinearColor FlattenColor = FLinearColor(0.95f, 0.85f, 0.2f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Colors")
	FLinearColor SmoothColor = FLinearColor(0.3f, 0.6f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush|Colors")
	FLinearColor InactiveColor = FLinearColor(0.5f, 0.5f, 0.5f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Brush|Runtime")
	ETerrainBrushMode Mode = ETerrainBrushMode::Add;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Brush|Runtime")
	bool bToolActive = false;

	UFUNCTION(BlueprintCallable, Category="Brush")
	void SetMode(ETerrainBrushMode NewMode);

	UFUNCTION(BlueprintCallable, Category="Brush")
	void CycleMode();

	UFUNCTION(BlueprintCallable, Category="Brush")
	void SetEditing(bool bEditing);

	UFUNCTION(BlueprintCallable, Category="Brush")
	void SetToolActive(bool bActive);

	UFUNCTION(BlueprintCallable, Category="Brush")
	void ToggleToolActive();

	UFUNCTION(BlueprintCallable, Category="Brush")
	void AdjustRadius(float Delta);

	UFUNCTION(BlueprintCallable, Category="Brush")
	void AdjustStrength(float Delta);

	UFUNCTION(BlueprintCallable, Category="Brush")
	FString GetCurrentModeName() const;

	UFUNCTION(BlueprintCallable, Category="Brush")
	FLinearColor GetCurrentModeColor() const;

	UFUNCTION(BlueprintCallable, Category="Brush")
	bool IsToolActive() const { return bToolActive; }

	UPROPERTY(EditAnywhere, Category="Brush|Perf")
	float TraceMinInterval = 0.016f;

	UPROPERTY(EditAnywhere, Category="Brush|Perf")
	float TraceCamDeltaThreshold = 1.0f;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	bool bIsEditing = false;
	float FlattenTargetZ = 0.f;
	bool bHasFlattenTarget = false;
	bool bStrokeActive = false;
	TArray<TWeakObjectPtr<AGenerateSurface>> StrokeTouchedTerrains;

	TArray<TWeakObjectPtr<AGenerateSurface>> CachedTerrains;
	float TerrainCacheAge = 999.f;

	float TraceAccumulator = 999.f;
	FVector LastTraceCamPos = FVector::ZeroVector;
	FVector LastTraceCamDir = FVector::ZeroVector;
	bool bHasLastTrace = false;
	bool bLastTraceHit = false;
	FHitResult LastHit;
	TWeakObjectPtr<AGenerateSurface> LastHitTerrain;

	void RefreshTerrainCacheIfNeeded(float DeltaTime);
	bool TraceFromCamera(FHitResult& OutHit, AGenerateSurface*& OutTerrain);
};
