#include "FlyingPlayerCharacter.h"
#include "TerrainEditorComponent.h"
#include "ProceduralGeneration/SurfaceGenerator/GenerateSurface.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"

AFlyingPlayerCharacter::AFlyingPlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	CollisionComponent->InitSphereRadius(40.f);
	CollisionComponent->SetCollisionProfileName(TEXT("Pawn"));
	RootComponent = CollisionComponent;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(RootComponent);
	Camera->bUsePawnControlRotation = true;

	Movement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("Movement"));
	Movement->MaxSpeed = 1500.f;
	Movement->Acceleration = 4000.f;
	Movement->Deceleration = 4000.f;

	TerrainEditor = CreateDefaultSubobject<UTerrainEditorComponent>(TEXT("TerrainEditor"));

	bUseControllerRotationPitch = true;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;
}

void AFlyingPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (MappingContext) Sub->AddMappingContext(MappingContext, 0);
		}
	}
}

void AFlyingPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction) EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AFlyingPlayerCharacter::HandleMove);
		if (LookAction) EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFlyingPlayerCharacter::HandleLook);
		if (UpDownAction) EIC->BindAction(UpDownAction, ETriggerEvent::Triggered, this, &AFlyingPlayerCharacter::HandleUpDown);
		if (ClickAction)
		{
			EIC->BindAction(ClickAction, ETriggerEvent::Started, this, &AFlyingPlayerCharacter::HandleClickStart);
			EIC->BindAction(ClickAction, ETriggerEvent::Completed, this, &AFlyingPlayerCharacter::HandleClickEnd);
			EIC->BindAction(ClickAction, ETriggerEvent::Canceled, this, &AFlyingPlayerCharacter::HandleClickEnd);
		}
		if (CycleModeAction) EIC->BindAction(CycleModeAction, ETriggerEvent::Started, this, &AFlyingPlayerCharacter::HandleCycleMode);
		if (ResetAction) EIC->BindAction(ResetAction, ETriggerEvent::Started, this, &AFlyingPlayerCharacter::HandleReset);
		if (ToggleToolAction) EIC->BindAction(ToggleToolAction, ETriggerEvent::Started, this, &AFlyingPlayerCharacter::HandleToggleTool);
		if (AdjustRadiusAction) EIC->BindAction(AdjustRadiusAction, ETriggerEvent::Triggered, this, &AFlyingPlayerCharacter::HandleAdjustRadius);
		if (AdjustStrengthAction) EIC->BindAction(AdjustStrengthAction, ETriggerEvent::Triggered, this, &AFlyingPlayerCharacter::HandleAdjustStrength);
	}
}

void AFlyingPlayerCharacter::HandleMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	if (Controller)
	{
		const FRotator YawRot(0.f, Controller->GetControlRotation().Yaw, 0.f);
		const FVector Fwd = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
		const FVector Right = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);
		AddMovementInput(Fwd, Axis.Y);
		AddMovementInput(Right, Axis.X);
	}
}

void AFlyingPlayerCharacter::HandleLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X * LookSensitivity);
	AddControllerPitchInput(Axis.Y * LookSensitivity);
}

void AFlyingPlayerCharacter::HandleUpDown(const FInputActionValue& Value)
{
	AddMovementInput(FVector::UpVector, Value.Get<float>());
}

void AFlyingPlayerCharacter::HandleClickStart(const FInputActionValue& Value)
{
	if (TerrainEditor) TerrainEditor->SetEditing(true);
}

void AFlyingPlayerCharacter::HandleClickEnd(const FInputActionValue& Value)
{
	if (TerrainEditor) TerrainEditor->SetEditing(false);
}

void AFlyingPlayerCharacter::HandleCycleMode(const FInputActionValue& Value)
{
	if (TerrainEditor) TerrainEditor->CycleMode();
}

void AFlyingPlayerCharacter::HandleReset(const FInputActionValue& Value)
{
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGenerateSurface::StaticClass(), Found);
	for (AActor* A : Found)
	{
		if (AGenerateSurface* T = Cast<AGenerateSurface>(A))
		{
			T->ResetToOriginal();
		}
	}
}

void AFlyingPlayerCharacter::HandleToggleTool(const FInputActionValue& Value)
{
	if (TerrainEditor) TerrainEditor->ToggleToolActive();
}

void AFlyingPlayerCharacter::HandleAdjustRadius(const FInputActionValue& Value)
{
	if (!TerrainEditor) return;
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift)) return;
	}
	TerrainEditor->AdjustRadius(Value.Get<float>());
}

void AFlyingPlayerCharacter::HandleAdjustStrength(const FInputActionValue& Value)
{
	if (TerrainEditor) TerrainEditor->AdjustStrength(Value.Get<float>());
}
