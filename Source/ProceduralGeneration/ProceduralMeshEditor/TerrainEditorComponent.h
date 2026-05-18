#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TerrainEditorComponent.generated.h"

class AProceduralTerrain;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush")
	float BrushRadius = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush")
	float BrushStrength = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Brush")
	float MaxTraceDistance = 20000.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Brush")
	ETerrainBrushMode Mode = ETerrainBrushMode::Add;

	UFUNCTION(BlueprintCallable, Category="Brush")
	void SetMode(ETerrainBrushMode NewMode);

	UFUNCTION(BlueprintCallable, Category="Brush")
	void CycleMode();

	UFUNCTION(BlueprintCallable, Category="Brush")
	void SetEditing(bool bEditing);

	UFUNCTION(BlueprintCallable, Category="Brush")
	FString GetCurrentModeName() const;

	UFUNCTION(BlueprintCallable, Category="Brush")
	FLinearColor GetCurrentModeColor() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	bool bIsEditing = false;
	float FlattenTargetZ = 0.f;
	bool bHasFlattenTarget = false;

	bool TraceFromCamera(FHitResult& OutHit, AProceduralTerrain*& OutTerrain) const;
};
