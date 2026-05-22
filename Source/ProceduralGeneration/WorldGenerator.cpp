// Fill out your copyright notice in the Description page of Project Settings.


#include "WorldGenerator.h"

#include "Chunk/ChunkBase.h"
#include "SurfaceGenerator/GenerateSurface.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Utils/FastNoiseLite.h"


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
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("World generation skipped: no player pawn found"));
		return;
	}

	const float ChunkWorldSize = Size * 100.0f;
	const FVector PlayerLocation = PlayerPawn->GetActorLocation();
	FVector PlayerOrigin;
	FVector PlayerExtent;
	PlayerPawn->GetActorBounds(false, PlayerOrigin, PlayerExtent);
	const int HalfRange = MapRange / 2;

	const int CenterChunkX = FMath::FloorToInt(PlayerLocation.X / ChunkWorldSize);
	const int CenterChunkY = FMath::FloorToInt(PlayerLocation.Y / ChunkWorldSize);
	const float PlayerFootZ = PlayerOrigin.Z - PlayerExtent.Z;

	FastNoiseLite HeightNoise;
	HeightNoise.SetSeed(Seed);
	HeightNoise.SetFrequency(Frequency);
	HeightNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	HeightNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
	HeightNoise.SetFractalOctaves(FractalOctaves);
	HeightNoise.SetFractalLacunarity(FractalLacunarity);
	HeightNoise.SetFractalGain(FractalGain);

	const float PlayerTerrainNoise = HeightNoise.GetNoise(PlayerLocation.X / 100.0f, PlayerLocation.Y / 100.0f);
	const float PlayerTerrainHeight = ((PlayerTerrainNoise + 1.0f) * 0.5f * Size * HeightScale + HeightOffset) * 100.0f;
	const float SurfaceZ = PlayerFootZ - PlayerTerrainHeight - SurfaceLevel * 100.0f;

	//TO DO: Generate Chunks based on player position
	for (int x = 0; x < MapRange; ++x)
	{
		for (int y = 0; y < MapRange; ++y)
		{
			const int ChunkX = CenterChunkX + x - HalfRange;
			const int ChunkY = CenterChunkY + y - HalfRange;
			FVector position = FVector(ChunkX * ChunkWorldSize, ChunkY * ChunkWorldSize, SurfaceZ);
			UE_LOG( LogTemp, Warning, TEXT("Spawning Chunk at Position: %s"), *position.ToString());
			AGenerateSurface* chunk = GetWorld()->SpawnActor<AGenerateSurface>(position, FRotator::ZeroRotator);
			if (!chunk)
			{
				continue;
			}
			chunk->Material = Material;
			chunk->Frequency = Frequency;
			chunk->Seed = Seed;
			chunk->FractalOctaves = FractalOctaves;
			chunk->FractalLacunarity = FractalLacunarity;
			chunk->FractalGain = FractalGain;
			chunk->SurfaceLevel = SurfaceLevel;
			chunk->HeightScale = HeightScale;
			chunk->HeightOffset = HeightOffset;
			chunk->SeaLevel = SeaLevel;
			chunk->Size = Size;
			chunk->StartGeneration();
		}
	}
}

