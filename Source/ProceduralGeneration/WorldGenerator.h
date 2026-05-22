// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Utils/FastNoiseLite.h"
#include "WorldGenerator.generated.h"

UCLASS()
class PROCEDURALGENERATION_API AWorldGenerator : public AActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category="Chunk")
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface", meta=(ClampMin="0.0", UIMin="0.0"))
	float HeightScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface")
	float HeightOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface")
	float SeaLevel = 20.0f;

	UPROPERTY(editAnywhere, BlueprintReadWrite)
	int SurfaceLevel = 0;
	UPROPERTY(editAnywhere, BlueprintReadWrite, Category="Chunk")
	int MapRange = 10;
	
	// Sets default values for this actor's properties
	AWorldGenerator();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	FastNoiseLite* Noise;
private:
	// TQueue<AChunkBase*> chunkQueue;
	// TArray<AChunkBase*> chunks;
	//AChunkBase* actualChunk;
	
	void GenerateWorld();
};
