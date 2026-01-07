// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldGenerator.generated.h"

UCLASS()
class PROCEDURALGENERATION_API AWorldGenerator : public AActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category="Chunk")
	int Size = 64;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Chunk")
	float Frequency = 0.01f;
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
private:
	// TQueue<AChunkBase*> chunkQueue;
	// TArray<AChunkBase*> chunks;
	//AChunkBase* actualChunk;
	
	void GenerateWorld();
};
