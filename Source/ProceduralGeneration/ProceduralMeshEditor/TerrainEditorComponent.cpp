#include "TerrainEditorComponent.h"
#include "ProceduralGeneration/SurfaceGenerator/GenerateSurface.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"

UTerrainEditorComponent::UTerrainEditorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UTerrainEditorComponent::BeginPlay()
{
	Super::BeginPlay();
	bToolActive = bStartActive;
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
	bIsEditing = bEditing && bToolActive;
	if (!bIsEditing)
	{
		bHasFlattenTarget = false;
		if (bStrokeActive)
		{
			for (auto& W : StrokeTouchedTerrains)
			{
				if (W.IsValid()) W->EndEditStroke();
			}
			StrokeTouchedTerrains.Reset();
			bStrokeActive = false;
		}
	}
}

void UTerrainEditorComponent::SetToolActive(bool bActive)
{
	bToolActive = bActive;
	if (!bToolActive)
	{
		bIsEditing = false;
		bHasFlattenTarget = false;
	}
}

void UTerrainEditorComponent::ToggleToolActive()
{
	SetToolActive(!bToolActive);
}

void UTerrainEditorComponent::AdjustRadius(float Delta)
{
	BrushRadius = FMath::Clamp(BrushRadius + Delta * BrushRadiusStep, BrushRadiusMin, BrushRadiusMax);
}

void UTerrainEditorComponent::AdjustStrength(float Delta)
{
	BrushStrength = FMath::Clamp(BrushStrength + Delta * BrushStrengthStep, BrushStrengthMin, BrushStrengthMax);
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
	if (!bToolActive) return InactiveColor;
	switch (Mode)
	{
	case ETerrainBrushMode::Add: return AddColor;
	case ETerrainBrushMode::Subtract: return SubtractColor;
	case ETerrainBrushMode::Flatten: return FlattenColor;
	case ETerrainBrushMode::Smooth: return SmoothColor;
	}
	return FLinearColor::White;
}

bool UTerrainEditorComponent::TraceFromCamera(FHitResult& OutHit, AGenerateSurface*& OutTerrain) const
{
	OutTerrain = nullptr;
	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn) return false;

	UCameraComponent* Cam = OwnerPawn->FindComponentByClass<UCameraComponent>();
	if (!Cam) return false;

	const FVector Start = Cam->GetComponentLocation();
	const FVector Dir = Cam->GetForwardVector();

	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGenerateSurface::StaticClass(), Found);

	float BestDist = MaxTraceDistance;
	bool bHitAny = false;
	for (AActor* A : Found)
	{
		AGenerateSurface* T = Cast<AGenerateSurface>(A);
		if (!T) continue;
		FVector HitPos, Normal;
		if (T->TraceDensityField(Start, Dir, MaxTraceDistance, HitPos, Normal))
		{
			const float D = (HitPos - Start).Size();
			if (D < BestDist)
			{
				BestDist = D;
				OutHit.ImpactPoint = HitPos;
				OutHit.Location = HitPos;
				OutHit.ImpactNormal = Normal;
				OutHit.Normal = Normal;
				OutHit.Distance = D;
				OutTerrain = T;
				bHitAny = true;
			}
		}
	}
	return bHitAny;
}

void UTerrainEditorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bToolActive) return;

	FHitResult Hit;
	AGenerateSurface* Terrain = nullptr;
	const bool bHit = TraceFromCamera(Hit, Terrain);

	if (bHit)
	{
		const FColor C = GetCurrentModeColor().ToFColor(true);
		DrawDebugSphere(GetWorld(), Hit.ImpactPoint, BrushRadius, CursorSphereSegments, C, false, 0.f, 0, CursorLineThickness);
	}

	if (!bIsEditing || !bHit) return;

	const FBox BrushAABB(Hit.ImpactPoint - FVector(BrushRadius), Hit.ImpactPoint + FVector(BrushRadius));

	TArray<AActor*> AllChunks;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGenerateSurface::StaticClass(), AllChunks);

	TArray<AGenerateSurface*> Targets;
	Targets.Reserve(AllChunks.Num());
	for (AActor* A : AllChunks)
	{
		AGenerateSurface* T = Cast<AGenerateSurface>(A);
		if (!T) continue;
		if (T->GetWorldAABB().Intersect(BrushAABB))
		{
			Targets.Add(T);
		}
	}

	if (Targets.Num() == 0) return;

	if (!bStrokeActive)
	{
		bStrokeActive = true;
	}

	for (AGenerateSurface* T : Targets)
	{
		bool bAlreadyTouched = false;
		for (auto& W : StrokeTouchedTerrains)
		{
			if (W.Get() == T) { bAlreadyTouched = true; break; }
		}
		if (!bAlreadyTouched)
		{
			T->BeginEditStroke();
			StrokeTouchedTerrains.Add(T);
		}
	}

	const float Step = FMath::Min(BrushStrength * DeltaTime, MaxStrengthPerStep);

	switch (Mode)
	{
	case ETerrainBrushMode::Add:
		for (AGenerateSurface* T : Targets) T->ApplyBrush(Hit.ImpactPoint, BrushRadius, -Step);
		break;
	case ETerrainBrushMode::Subtract:
		for (AGenerateSurface* T : Targets) T->ApplyBrush(Hit.ImpactPoint, BrushRadius, +Step);
		break;
	case ETerrainBrushMode::Flatten:
		if (!bHasFlattenTarget)
		{
			FlattenTargetZ = Hit.ImpactPoint.Z;
			bHasFlattenTarget = true;
		}
		for (AGenerateSurface* T : Targets)
			T->ApplyFlatten(Hit.ImpactPoint, BrushRadius, FlattenTargetZ, FMath::Clamp(Step * 0.5f, 0.f, 1.f));
		break;
	case ETerrainBrushMode::Smooth:
		for (AGenerateSurface* T : Targets)
			T->ApplySmooth(Hit.ImpactPoint, BrushRadius, FMath::Clamp(Step * 0.3f, 0.f, 1.f));
		break;
	}
}
