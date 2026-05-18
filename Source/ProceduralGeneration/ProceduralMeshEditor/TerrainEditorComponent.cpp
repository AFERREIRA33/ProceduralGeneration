#include "TerrainEditorComponent.h"
#include "ProceduralTerrain.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

UTerrainEditorComponent::UTerrainEditorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UTerrainEditorComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UTerrainEditorComponent::SetMode(ETerrainBrushMode NewMode)
{
	Mode = NewMode;
	bHasFlattenTarget = false;
}

void UTerrainEditorComponent::CycleMode()
{
	const uint8 Next = ((uint8)Mode + 1) % 4;
	SetMode((ETerrainBrushMode)Next);
}

void UTerrainEditorComponent::SetEditing(bool bEditing)
{
	bIsEditing = bEditing;
	if (!bEditing) bHasFlattenTarget = false;
}

FString UTerrainEditorComponent::GetCurrentModeName() const
{
	switch (Mode)
	{
	case ETerrainBrushMode::Add: return TEXT("Add");
	case ETerrainBrushMode::Subtract: return TEXT("Subtract");
	case ETerrainBrushMode::Flatten: return TEXT("Flatten");
	case ETerrainBrushMode::Smooth: return TEXT("Smooth");
	}
	return TEXT("Unknown");
}

FLinearColor UTerrainEditorComponent::GetCurrentModeColor() const
{
	switch (Mode)
	{
	case ETerrainBrushMode::Add: return FLinearColor(0.2f, 0.9f, 0.2f);
	case ETerrainBrushMode::Subtract: return FLinearColor(0.9f, 0.2f, 0.2f);
	case ETerrainBrushMode::Flatten: return FLinearColor(0.95f, 0.85f, 0.2f);
	case ETerrainBrushMode::Smooth: return FLinearColor(0.3f, 0.6f, 1.0f);
	}
	return FLinearColor::White;
}

bool UTerrainEditorComponent::TraceFromCamera(FHitResult& OutHit, AProceduralTerrain*& OutTerrain) const
{
	OutTerrain = nullptr;
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn) return false;

	UCameraComponent* Cam = OwnerPawn->FindComponentByClass<UCameraComponent>();
	if (!Cam) return false;

	const FVector Start = Cam->GetComponentLocation();
	const FVector End = Start + Cam->GetForwardVector() * MaxTraceDistance;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(OwnerPawn);

	if (!GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params)) return false;

	OutTerrain = Cast<AProceduralTerrain>(OutHit.GetActor());
	return OutTerrain != nullptr;
}

void UTerrainEditorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	FHitResult Hit;
	AProceduralTerrain* Terrain = nullptr;
	const bool bHit = TraceFromCamera(Hit, Terrain);

	if (bHit)
	{
		const FColor C = GetCurrentModeColor().ToFColor(true);
		DrawDebugSphere(GetWorld(), Hit.ImpactPoint, BrushRadius, 24, C, false, 0.f, 0, 2.f);
	}

	if (!bIsEditing || !bHit || !Terrain) return;

	const float Step = BrushStrength * DeltaTime;

	switch (Mode)
	{
	case ETerrainBrushMode::Add:
		Terrain->ApplyBrush(Hit.ImpactPoint, BrushRadius, -Step);
		break;
	case ETerrainBrushMode::Subtract:
		Terrain->ApplyBrush(Hit.ImpactPoint, BrushRadius, +Step);
		break;
	case ETerrainBrushMode::Flatten:
		if (!bHasFlattenTarget)
		{
			FlattenTargetZ = Hit.ImpactPoint.Z;
			bHasFlattenTarget = true;
		}
		Terrain->ApplyFlatten(Hit.ImpactPoint, BrushRadius, FlattenTargetZ, FMath::Clamp(Step * 0.5f, 0.f, 1.f));
		break;
	case ETerrainBrushMode::Smooth:
		Terrain->ApplySmooth(Hit.ImpactPoint, BrushRadius, FMath::Clamp(Step * 0.3f, 0.f, 1.f));
		break;
	}
}
