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

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, Category="Terrain|Perf")
	float RemeshInterval = 0.033f;

private:
	TArray<float> Densities;
	TArray<float> OriginalDensities;

	struct FChunkInfo
	{
		FIntVector Min;
		FIntVector Max;
		int32 SectionIndex;
		bool bCreated;
	};

	TArray<FChunkInfo> Chunks;
	TSet<int32> DirtyChunks;
	float RemeshAccumulator = 0.f;

	static constexpr int32 ChunkSize = 8;

	struct FChunkBuildData
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
	};

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
	FVector GradientAtCorner(int32 x, int32 y, int32 z) const;
};
