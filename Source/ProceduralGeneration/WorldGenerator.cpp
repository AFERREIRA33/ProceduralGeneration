// Fill out your copyright notice in the Description page of Project Settings.


#include "WorldGenerator.h"

#include "Chunk/ChunkBase.h"
#include "SurfaceGenerator/GenerateSurface.h"


// Sets default values
AWorldGenerator::AWorldGenerator()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AWorldGenerator::BeginPlay()
{
	Super::BeginPlay();
	GenerateWorld();
	
}

// Called every frame
void AWorldGenerator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}
void AWorldGenerator::GenerateWorld()
{
	//TO DO: Generate Chunks based on player position
	for (int x = 0; x < MapRange; ++x)
	{
		for (int y = 0; y < MapRange; ++y)
		{
			FVector position = FVector(x * Size*100, y * Size*100, -SurfaceLevel*100);
			UE_LOG( LogTemp, Warning, TEXT("Spawning Chunk at Position: %s"), *position.ToString());
			AGenerateSurface* chunk = GetWorld()->SpawnActor<AGenerateSurface>(position, FRotator::ZeroRotator);
			chunk->Material = Material;
			chunk->Frequency = Frequency;
			chunk->SurfaceLevel = SurfaceLevel;
			chunk->Size = Size;
			chunk->StartGeneration();
		}
	}
}

