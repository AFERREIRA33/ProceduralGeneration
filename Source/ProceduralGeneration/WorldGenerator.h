// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Utils/FastNoiseLite.h"
#include "WorldGenerator.generated.h"

class AGenerateSurface;
class ACaveMarchingCube;

struct FOctreeNodeKey
{
	int32 X = 0;
	int32 Y = 0;
	int32 Z = 0;
	int32 S = 0;
	bool operator==(const FOctreeNodeKey& O) const { return X == O.X && Y == O.Y && Z == O.Z && S == O.S; }
	friend FArchive& operator<<(FArchive& Ar, FOctreeNodeKey& K) { return Ar << K.X << K.Y << K.Z << K.S; }
};

FORCEINLINE uint32 GetTypeHash(const FOctreeNodeKey& K)
{
	return HashCombine(HashCombine(::GetTypeHash(K.X), ::GetTypeHash(K.Y)), HashCombine(::GetTypeHash(K.Z), ::GetTypeHash(K.S)));
}

UCLASS()
class PROCEDURALGENERATION_API AWorldGenerator : public AActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk")
	int Size = 96;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk")
	TObjectPtr<UMaterialInterface> Material;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk")
	float Frequency = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk|Noise")
	int Seed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk|Noise")
	bool RandomizeSeedOnPlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk|Noise", meta=(ClampMin="1", UIMin="1"))
	int FractalOctaves = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk|Noise", meta=(ClampMin="0.0", UIMin="0.0"))
	float FractalLacunarity = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk|Noise", meta=(ClampMin="0.0", UIMin="0.0", ClampMax="1.0", UIMax="1.0"))
	float FractalGain = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(ClampMin="0.0", UIMin="0.0"))
	float HeightScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface")
	float HeightOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(ClampMin="0", ClampMax="96"))
	int UndergroundDepth = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(ClampMin="0.1", UIMin="1.0", UIMax="6.0"))
	float HeightRedistribution = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(ClampMin="0.1", UIMin="0.5", UIMax="3.0"))
	float MountainBoost = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(UIMin="-0.5", UIMax="0.5"))
	float MountainBias = -0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface")
	float SeaLevel = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Biome", meta=(ClampMin="0.0"))
	float BiomeFrequency = 0.012f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Biome")
	float SnowLevel = 88.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Biome", meta=(ClampMin="0.0"))
	float BeachWidthVoxels = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Biome", meta=(ClampMin="0.0", ClampMax="1.0"))
	float BiomeHeightCooling = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Biome", meta=(ClampMin="0", ClampMax="4"))
	int32 BiomeDebugView = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Biome", meta=(ClampMin="0.0", ClampMax="1.0"))
	float BiomeHeightDrying = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Water")
	bool bEnableOceans = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Water", meta=(ClampMin="0.0001"))
	float ContinentFrequency = 0.0025f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Water", meta=(ClampMin="0.0", ClampMax="1.0"))
	float OceanThreshold = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Water", meta=(ClampMin="0.01", ClampMax="1.0"))
	float CoastWidth = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Water")
	float OceanFloorVoxel = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Water")
	bool bEnableRivers = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Water", meta=(ClampMin="0.0001"))
	float RiverFrequency = 0.006f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Water", meta=(ClampMin="0.001", ClampMax="0.5"))
	float RiverWidth = 0.045f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Water")
	float RiverBedVoxel = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Water", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RiverStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface|Water")
	float RiverMaxTerrain = 40.0f;

	UPROPERTY(editAnywhere, BlueprintReadWrite)
	int SurfaceLevel = 0;
	UPROPERTY(editAnywhere, BlueprintReadWrite, Category="Chunk")
	int MapRange = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perf")
	bool bChunksCastShadows = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perf")
	bool bVerboseGenerationLog = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Streaming")
	bool bStreamingEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Streaming", meta=(ClampMin="1"))
	int StreamRadius = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Streaming", meta=(ClampMin="0"))
	int UnloadMargin = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Streaming", meta=(ClampMin="1"))
	int MaxChunksPerFrame = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perf")
	bool bChunkCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perf")
	bool bAsyncGeneration = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Streaming", meta=(ClampMin="0"))
	int CollisionRadius = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Streaming")
	bool bEnableCaves = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LOD")
	bool bUseOctreeLOD = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LOD")
	bool bUseOctreeStreaming = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LOD", meta=(ClampMin="0.5"))
	float OctreeSubdivFactor = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LOD", meta=(ClampMin="0.25"))
	float OctreeRockDepthScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LOD", meta=(ClampMin="0.5"))
	float OctreeCollisionRadiusChunks = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LOD", meta=(ClampMin="0", ClampMax="5"))
	int DebugForceLOD = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LOD", meta=(ClampMin="1"))
	int LODChunkRadius0 = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LOD", meta=(ClampMin="1"))
	int LODChunksPerLevel = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LOD", meta=(ClampMin="0", ClampMax="5"))
	int MaxLOD = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LOD")
	bool bBalanceLOD = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LOD")
	bool bUseTransvoxelMesher = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LOD", meta=(ClampMin="0.0", UIMin="0.0", UIMax="4.0"))
	float TransitionWidthScale = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LOD")
	bool bDebugTransitionColor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Persistence")
	bool bPersistEdits = true;

	// Sets default values for this actor's properties
	AWorldGenerator();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	FastNoiseLite* Noise;
private:
	// TQueue<AChunkBase*> chunkQueue;
	// TArray<AChunkBase*> chunks;
	//AChunkBase* actualChunk;
	
	void GenerateWorld();

	void AnchorStreaming();
	void UpdateStreaming();
	AGenerateSurface* SpawnChunkAt(int ChunkX, int ChunkY, bool bWantCollision);
	int32 ComputeChunkLOD(int ChunkX, int ChunkY) const;

	void ConfigureChunkCommon(AGenerateSurface* chunk, bool bWantCollision);
	void UpdateStreamingOctree();
	void CollectOctreeLeaves(const FVector& PlayerLoc, TArray<FOctreeNodeKey>& Out);
	bool IsReplacementReady(const FOctreeNodeKey& Key, const TSet<FOctreeNodeKey>& DesiredSet);
	void SubdivideOctree(int32 CellX, int32 CellY, int32 CellZ, int32 NodeScale, const FVector& PlayerLoc, TArray<FOctreeNodeKey>& Out);
	bool NodeIntersectsSurface(int32 CellX, int32 CellY, int32 CellZ, int32 NodeScale);
	float SampleSurfaceWorldZ(float WorldX, float WorldY);
	int32 NaturalScale(const FVector& Point, const FVector& PlayerLoc);
	int32 ComputeNodeTransitionMask(int32 CellX, int32 CellY, int32 CellZ, int32 NodeScale, const FVector& PlayerLoc);
	AGenerateSurface* SpawnNodeChunk(int32 CellX, int32 CellY, int32 CellZ, int32 NodeScale, int32 TransMask, bool bWantCollision);

	void CaptureChunkEdits(const FIntPoint& Key, AGenerateSurface* Chunk);
	void CaptureChunkEdits(const FOctreeNodeKey& Key, AGenerateSurface* Chunk);
	void LoadEdits();
	void SaveEdits();
	FString EditSavePath() const;

	UPROPERTY()
	TMap<FIntPoint, TObjectPtr<AGenerateSurface>> LoadedChunks;

	TMap<FOctreeNodeKey, TObjectPtr<AGenerateSurface>> OctreeChunks;
	FastNoiseLite StreamHeightNoise;

	TMap<FIntPoint, TMap<int32, float>> EditStore;
	TMap<FOctreeNodeKey, TMap<int32, float>> OctreeEditStore;

	UPROPERTY()
	TObjectPtr<ACaveMarchingCube> CaveActor;

	float StreamSurfaceZ = 0.0f;
	bool bStreamAnchored = false;
	FIntPoint CurrentCenterChunk = FIntPoint::ZeroValue;
};
