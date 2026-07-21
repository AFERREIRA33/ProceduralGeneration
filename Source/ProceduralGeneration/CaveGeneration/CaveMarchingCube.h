#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralGeneration/Utils/FastNoiseLite.h"
#include "ProceduralGeneration/CaveGeneration/CaveCarveOp.h"
#include "CaveMarchingCube.generated.h"

class UProceduralMeshComponent;
class AGenerateSurface;

struct FWorm
{
	FVector Position;
	FVector Direction; // Current movement vector (for smoothness)
	int32 RemainingSteps;
	float Radius;
	float NoiseOffset; // Unique offset so they don't follow the same path
	FVector SpawnPos = FVector::ZeroVector;
	bool bEntrance = false;
};

UCLASS()
class PROCEDURALGENERATION_API ACaveMarchingCube : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ACaveMarchingCube();
	~ACaveMarchingCube();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	int32 gridSize = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	float voxelSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Settings")
	float surfaceLevel = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worm Settings")
	int32 wormSteps = 200;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worm Settings")
	float wormRadius = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worm Settings")
	float wormNoiseFrequency = 0.08f;

	UPROPERTY(EditAnywhere, Category = "Cave Settings")
	float BranchProbability = 0.025f;

	UPROPERTY(EditAnywhere, Category = "Cave Settings")
	int32 MaxWormsTotal = 6;

	UPROPERTY(EditAnywhere, Category = "Cave Settings")
	float HorizontalScale = 2.5f;

	UPROPERTY(EditAnywhere, Category = "Cave Settings")
	float VerticalScale = 0.6f;

	UPROPERTY(EditAnywhere, Category = "Cave Settings")
	float DownwardBias = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Cave Settings", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MotherSpawnDepthRatio = 0.75f;

	UPROPERTY(EditAnywhere, Category = "Cave Settings", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DirectionInertia = 0.75f;

	UPROPERTY(EditAnywhere, Category = "Cave Settings")
	float RoomProbability = 0.004f;

	UPROPERTY(EditAnywhere, Category = "Cave Settings")
	float RoomRadius = 7.0f;

	UPROPERTY(EditAnywhere, Category = "Cave Settings", meta=(ClampMin="0.0"))
	float CaveMinRoofDepth = 4.0f;

	UPROPERTY(EditAnywhere, Category = "Chunk Settings")
	int32 ChunkSize = 32;

	UPROPERTY(EditAnywhere, Category = "Chunk Settings")
	int32 WorldWidthInChunks = 4;

	UPROPERTY(EditAnywhere, Category = "Cave Generation")
	bool bAutoTriggerOnBeginPlay = true;

	UPROPERTY(EditAnywhere, Category = "Cave Generation", meta=(ClampMin="0.0"))
	float AutoTriggerDelay = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Cave Generation")
	int32 WormGlobalSize = 96;

	UPROPERTY(EditAnywhere, Category = "Cave Generation")
	int32 MotherWormSteps = 300;

	UPROPERTY(EditAnywhere, Category = "Cave Generation")
	float MotherWormRadius = 3.5f;

	UPROPERTY(EditAnywhere, Category = "Cave Generation", meta=(ClampMin="1", ClampMax="20"))
	int32 NumSeedWorms = 2;

	UPROPERTY(EditAnywhere, Category = "Cave Generation", meta=(ClampMin="2.0"))
	float CaveMinZVoxel = 3.f;

	UPROPERTY(EditAnywhere, Category = "Cave Generation", meta=(ClampMin="3.0"))
	float CaveMaxZVoxel = 54.f;

	UPROPERTY(EditAnywhere, Category = "Cave Generation", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SurfaceEntranceChance = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Cave Generation", meta=(ClampMin="3.0"))
	float EntranceCeilVoxel = 72.f;

	UPROPERTY(EditAnywhere, Category = "Cave Generation", meta=(ClampMin="0.0"))
	float EntranceUpBias = 1.2f;

	UPROPERTY(EditAnywhere, Category = "Cave Generation", meta=(ClampMin="16.0"))
	float MaxWormReachVoxels = 80.f;

	UPROPERTY(EditAnywhere, Category = "Cave Generation")
	int32 CaveSeed = 1337;

	float TerrainHeightOffsetVoxels = 0.f;

	UPROPERTY(EditAnywhere, Category = "Cave Generation", meta=(ClampMin="0.0"))
	float MinSeedSpacing = 28.f;

	UPROPERTY(EditAnywhere, Category = "Cave Generation", meta=(ClampMin="1"))
	int32 SeedPlacementMaxTries = 32;

	UPROPERTY(EditAnywhere, Category = "Cave Generation", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SeedMinDepthRatio = 0.55f;

	UPROPERTY(EditAnywhere, Category = "Cave Generation", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SeedMaxDepthRatio = 0.95f;

	UPROPERTY(EditAnywhere, Category = "Cave Generation", meta=(ClampMin="1"))
	int32 CaveStepsPerFrame = 24;

	UFUNCTION(BlueprintCallable, Category = "Cave Generation")
	void TriggerCaveGeneration();

	void GenerateCaveSystem();

	TArray<FCaveCarveOp> GetTileOps(const FBox& ChunkAABB, float FloorOverrideZ = 3.4e38f, float BandTopVoxelsOverride = -1.0f);
	float GetCaveMinRoofDepth() const { return CaveMinRoofDepth; }

	bool bStreamingManaged = false;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	FastNoiseLite* noise;
	TObjectPtr<UProceduralMeshComponent> mesh;

private:
	void StepCaveGeneration();
	bool PlaceSeedsAndStart();

	void BuildTileCarveOps(const FIntPoint& Tile, float FloorWorldZ, float TileVoxWidth, TArray<FCaveCarveOp>& Out, float BandTopOverride = -1.0f);
	int32 GetTileSeed(const FIntPoint& Tile) const;

	void CarveCaveInTerrains(const FVector& WorldCenter, float WorldRadius, bool bDistorted);

	TMap<FIntPoint, TArray<FCaveCarveOp>> CavePlansByTile;

	bool bCaveGenInProgress = false;
	FVector CaveOrigin = FVector::ZeroVector;
	float CaveWorldVoxelScale = 100.f;

	TArray<AGenerateSurface*> CaveTerrains;
	TArray<FBox> CaveTerrainAABBs;
	TArray<FWorm> CaveActiveWorms;
	TArray<FVector> CaveSpawnPositions;
	TArray<float> CaveWormHorizReach;

	int32 CaveTotalWormsSpawned = 0;
	int32 CaveCarveCallsTotal = 0;
	int32 CaveCarveCallsHittingTerrain = 0;
	int32 CaveTotalSteps = 0;
	double CaveTotalHorizStep = 0.0;
	double CaveTotalVertStep = 0.0;
	float CaveMaxHorizDistFromAnyStart = 0.f;
	int32 CaveWormsThatTraveledHoriz = 0;
};
