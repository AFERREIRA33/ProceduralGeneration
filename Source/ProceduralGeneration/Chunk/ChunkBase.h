// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "ProceduralGeneration/Utils/ChunkMeshData.h"
#include "ProceduralGeneration/Utils/Enums.h"

#include "ChunkBase.generated.h"

class FastNoiseLite;
class UProceduralMeshComponent;

UCLASS(Abstract)
class PROCEDURALGENERATION_API AChunkBase : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AChunkBase();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk")
	int Size = 64;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk")
	TObjectPtr<UMaterialInterface> Material;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk")
	float Frequency = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk|Noise")
	int Seed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk|Noise", meta=(ClampMin="1", UIMin="1"))
	int FractalOctaves = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk|Noise", meta=(ClampMin="0.0", UIMin="0.0"))
	float FractalLacunarity = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk|Noise", meta=(ClampMin="0.0", UIMin="0.0", ClampMax="1.0", UIMax="1.0"))
	float FractalGain = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk")
	ProceduralGenerationType GenerationType;
	UFUNCTION(BlueprintCallable, Category="Chunk")
	void ModifyVoxel(const FVector Position);
	
	virtual void StartGeneration();
protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual ProceduralGenerationType SetGenerationType() {return GenerationType;};
	virtual void Setup() PURE_VIRTUAL(AChunkBase::Setup);
	virtual void Generate2DHeightMap(FVector Position) PURE_VIRTUAL(AChunkBase::Generate2DHeightMap);
	virtual void Generate3DHeightMap(FVector Position) PURE_VIRTUAL(AChunkBase::Generate3DHeightMap);
	virtual void GenerateMesh() PURE_VIRTUAL(AChunkBase::GenerateMesh);

	virtual void ModifyVoxelData(FVector Position) PURE_VIRTUAL(AChunkBase::RemoveVoxelData);

	TObjectPtr<UProceduralMeshComponent> Mesh;
	TUniquePtr<FastNoiseLite> Noise;
	FChunkMeshData MeshData;
	int VertexCount = 0;

private:
	void ApplyMesh() const;
	void ClearMesh();
	void GenerateHeightMap();
};
