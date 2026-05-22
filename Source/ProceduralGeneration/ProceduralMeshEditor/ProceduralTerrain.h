#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "ProceduralTerrain.generated.h"

UCLASS()
class PROCEDURALGENERATION_API AProceduralTerrain : public AActor
{
	GENERATED_BODY()

public:
	AProceduralTerrain();

	UPROPERTY(EditAnywhere, Category="Terrain|Grid")
	FIntVector GridSize = FIntVector(48, 48, 32);

	UPROPERTY(EditAnywhere, Category="Terrain|Grid")
	float VoxelSize = 100.f;

	UPROPERTY(EditAnywhere, Category="Terrain|Noise")
	float NoiseFrequency = 0.025f;

	UPROPERTY(EditAnywhere, Category="Terrain|Noise")
	float HeightMultiplier = 12.f;

	UPROPERTY(EditAnywhere, Category="Terrain|Noise")
	int32 NoiseSeed = 1337;

	UPROPERTY(EditAnywhere, Category="Terrain|Density")
	float DensityClampMin = -1.f;

	UPROPERTY(EditAnywhere, Category="Terrain|Density")
	float DensityClampMax = 1.f;

	UPROPERTY(EditAnywhere, Category="Terrain|Material")
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(VisibleAnywhere, Category="Terrain")
	TObjectPtr<UProceduralMeshComponent> MeshComponent;

	void ApplyBrush(const FVector& WorldCenter, float Radius, float Delta);
	void ApplyFlatten(const FVector& WorldCenter, float Radius, float TargetWorldZ, float Strength);
	void ApplySmooth(const FVector& WorldCenter, float Radius, float Strength);

	UFUNCTION(BlueprintCallable, Category="Terrain")
	void ResetTerrain();

	bool WorldToGrid(const FVector& World, FVector& OutGrid) const;
	FVector GridToWorld(const FVector& Grid) const;

	bool TraceDensityField(const FVector& WorldStart, const FVector& WorldDir, float MaxDist, FVector& OutHitPoint, FVector& OutNormal) const;

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, Category="Terrain|Perf")
	float RemeshInterval = 0.033f;

	UPROPERTY(EditDefaultsOnly, Category="Terrain|Perf", meta=(ClampMin="8", ClampMax="64"))
	int32 ChunkSize = 8;

	UPROPERTY(EditAnywhere, Category="Terrain|Perf")
	bool bDeferCollisionDuringEdit = true;

public:
	void BeginEditStroke();
	void EndEditStroke();

private:
	struct FChunkInfo
	{
		FIntVector Min;
		FIntVector Max;
		int32 SectionIndex;
		bool bCreated;
	};

	struct FChunkBuildData
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<int32> EdgeToVertex;
	};

	TArray<float> Densities;
	TArray<float> OriginalDensities;
	TArray<FChunkInfo> Chunks;
	TSet<int32> DirtyChunks;
	TSet<int32> CollisionPendingChunks;
	TArray<FChunkBuildData> CachedBuilds;
	TArray<int32> CachedIndices;
	FIntVector ChunkCount = FIntVector::ZeroValue;
	float RemeshAccumulator = 0.f;
	bool bInEditStroke = false;

	void GenerateDensities();
	void BuildChunks();
	void RebuildAllChunks();
	void BuildChunkData(int32 ChunkIndex, FChunkBuildData& Out) const;
	void UploadChunk(int32 ChunkIndex, const FChunkBuildData& Data);
	void FlushDirtyChunks();

	FORCEINLINE int32 Idx(int32 x, int32 y, int32 z) const
	{
		return x + GridSize.X * (y + GridSize.Y * z);
	}

	FORCEINLINE bool InBounds(int32 x, int32 y, int32 z) const
	{
		return x >= 0 && y >= 0 && z >= 0 && x < GridSize.X && y < GridSize.Y && z < GridSize.Z;
	}

	void MarkRegionDirty(const FIntVector& MinCell, const FIntVector& MaxCell);
	void MarkCellDirty(int32 x, int32 y, int32 z);
	FVector GradientAtCorner(int32 x, int32 y, int32 z) const;
	float SampleDensityTrilinear(float gx, float gy, float gz) const;
};
