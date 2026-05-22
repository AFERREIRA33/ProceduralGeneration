#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "FlyingPlayerCharacter.generated.h"

class UCameraComponent;
class USphereComponent;
class UFloatingPawnMovement;
class UInputMappingContext;
class UInputAction;
class UTerrainEditorComponent;
struct FInputActionValue;

UCLASS()
class PROCEDURALGENERATION_API AFlyingPlayerCharacter : public APawn
{
	GENERATED_BODY()

public:
	AFlyingPlayerCharacter();

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USphereComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UFloatingPawnMovement> Movement;

	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<UTerrainEditorComponent> TerrainEditor;

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> UpDownAction;

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> ClickAction;

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> CycleModeAction;

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> ResetAction;

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> ToggleToolAction;

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> AdjustRadiusAction;

	UPROPERTY(EditAnywhere, Category="Input")
	TObjectPtr<UInputAction> AdjustStrengthAction;

	UPROPERTY(EditAnywhere, Category="Input")
	float LookSensitivity = 1.f;

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	void HandleMove(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleUpDown(const FInputActionValue& Value);
	void HandleClickStart(const FInputActionValue& Value);
	void HandleClickEnd(const FInputActionValue& Value);
	void HandleCycleMode(const FInputActionValue& Value);
	void HandleReset(const FInputActionValue& Value);
	void HandleToggleTool(const FInputActionValue& Value);
	void HandleAdjustRadius(const FInputActionValue& Value);
	void HandleAdjustStrength(const FInputActionValue& Value);
};
