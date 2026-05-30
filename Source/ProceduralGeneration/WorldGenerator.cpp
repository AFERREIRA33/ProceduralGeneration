// Fill out your copyright notice in the Description page of Project Settings.


#include "WorldGenerator.h"

#include "Chunk/ChunkBase.h"
#include "SurfaceGenerator/GenerateSurface.h"
#include "CaveGeneration/CaveMarchingCube.h"
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
	if (bStreamingEnabled)
	{
		AnchorStreaming();
	}
	else
	{
		GenerateWorld();
	}
}

// Called every frame
void AWorldGenerator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bStreamingEnabled) return;
	if (!bStreamAnchored)
	{
		AnchorStreaming();
		return;
	}
	UpdateStreaming();
}
void AWorldGenerator::GenerateWorld()
{
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("World generation skipped: no player pawn found"));
		return;
	}

	if (RandomizeSeedOnPlay)
	{
		Seed = static_cast<int>(FDateTime::Now().GetTicks() % 2147483647);
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
	const float PlayerN01 = FMath::Pow(FMath::Clamp((PlayerTerrainNoise + 1.0f) * 0.5f + MountainBias, 0.0f, 1.0f), HeightRedistribution);
	const float PlayerTerrainHeight = (PlayerN01 * Size * HeightScale * MountainBoost + HeightOffset) * 100.0f;
	const float SurfaceZ = PlayerFootZ - PlayerTerrainHeight - SurfaceLevel * 100.0f;
	StreamSurfaceZ = SurfaceZ;

	const int32 TotalChunks = MapRange * MapRange;
	const double GenStartTime = FPlatformTime::Seconds();
	for (int x = 0; x < MapRange; ++x)
	{
		for (int y = 0; y < MapRange; ++y)
		{
			const int ChunkX = CenterChunkX + x - HalfRange;
			const int ChunkY = CenterChunkY + y - HalfRange;
			if (bVerboseGenerationLog) UE_LOG(LogTemp, Warning, TEXT("Spawning Chunk at (%d,%d)"), ChunkX, ChunkY);
			SpawnChunkAt(ChunkX, ChunkY, bChunkCollision);
		}
	}

	const double GenMs = (FPlatformTime::Seconds() - GenStartTime) * 1000.0;
	UE_LOG(LogTemp, Display, TEXT("[PERF] World gen: %d chunks in %.1f ms (avg %.2f ms/chunk)"), TotalChunks, GenMs, GenMs / FMath::Max(1, TotalChunks));
}

AGenerateSurface* AWorldGenerator::SpawnChunkAt(int ChunkX, int ChunkY, bool bWantCollision)
{
	const FIntPoint Key(ChunkX, ChunkY);
	if (LoadedChunks.Contains(Key))
	{
		return LoadedChunks[Key];
	}

	const float ChunkWorldSize = Size * 100.0f;
	const FVector position = FVector(ChunkX * ChunkWorldSize, ChunkY * ChunkWorldSize, StreamSurfaceZ);
	AGenerateSurface* chunk = GetWorld()->SpawnActor<AGenerateSurface>(position, FRotator::ZeroRotator);
	if (!chunk)
	{
		return nullptr;
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
	chunk->HeightRedistribution = HeightRedistribution;
	chunk->MountainBoost = MountainBoost;
	chunk->MountainBias = MountainBias;
	chunk->SeaLevel = SeaLevel;
	chunk->BiomeFrequency = BiomeFrequency;
	chunk->SnowLevel = SnowLevel;
	chunk->BiomeHeightCooling = BiomeHeightCooling;
	chunk->BiomeHeightDrying = BiomeHeightDrying;
	chunk->BiomeDebugView = BiomeDebugView;
	chunk->bEnableOceans = bEnableOceans;
	chunk->ContinentFrequency = ContinentFrequency;
	chunk->OceanThreshold = OceanThreshold;
	chunk->CoastWidth = CoastWidth;
	chunk->OceanFloorVoxel = OceanFloorVoxel;
	chunk->bEnableRivers = bEnableRivers;
	chunk->RiverFrequency = RiverFrequency;
	chunk->RiverWidth = RiverWidth;
	chunk->RiverBedVoxel = RiverBedVoxel;
	chunk->RiverStrength = RiverStrength;
	chunk->RiverMaxTerrain = RiverMaxTerrain;
	chunk->Size = Size;
	chunk->bCastShadows = bChunksCastShadows;
	chunk->bCollisionEnabled = bWantCollision;
	chunk->bEnableAutoLODGeneration = false;

	if (bEnableCaves && CaveActor)
	{
		TArray<FCaveCarveOp> Ops = CaveActor->GetTileOps(chunk->GetWorldAABB());
		chunk->SetPendingCaveData(MoveTemp(Ops), CaveActor->GetCaveMinRoofDepth());
	}

	if (bAsyncGeneration)
	{
		chunk->StartGenerationAsync();
	}
	else
	{
		chunk->StartGeneration();
		chunk->ApplyPendingCavesSync();
	}

	LoadedChunks.Add(Key, chunk);
	return chunk;
}

void AWorldGenerator::AnchorStreaming()
{
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn)
	{
		return;
	}

	if (RandomizeSeedOnPlay)
	{
		Seed = static_cast<int>(FDateTime::Now().GetTicks() % 2147483647);
	}

	const FVector PlayerLocation = PlayerPawn->GetActorLocation();
	FVector PlayerOrigin;
	FVector PlayerExtent;
	PlayerPawn->GetActorBounds(false, PlayerOrigin, PlayerExtent);
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
	const float PlayerN01 = FMath::Pow(FMath::Clamp((PlayerTerrainNoise + 1.0f) * 0.5f + MountainBias, 0.0f, 1.0f), HeightRedistribution);
	const float PlayerTerrainHeight = (PlayerN01 * Size * HeightScale * MountainBoost + HeightOffset) * 100.0f;
	StreamSurfaceZ = PlayerFootZ - PlayerTerrainHeight - SurfaceLevel * 100.0f;

	if (bEnableCaves)
	{
		TArray<AActor*> FoundCaves;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ACaveMarchingCube::StaticClass(), FoundCaves);
		if (FoundCaves.Num() > 0)
		{
			CaveActor = Cast<ACaveMarchingCube>(FoundCaves[0]);
			if (CaveActor)
			{
				CaveActor->bStreamingManaged = true;
				CaveActor->CaveSeed = Seed;
			}
		}
	}

	bStreamAnchored = true;
}

void AWorldGenerator::UpdateStreaming()
{
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn)
	{
		return;
	}

	const float ChunkWorldSize = Size * 100.0f;
	const FVector PlayerLocation = PlayerPawn->GetActorLocation();
	const int CenterChunkX = FMath::FloorToInt(PlayerLocation.X / ChunkWorldSize);
	const int CenterChunkY = FMath::FloorToInt(PlayerLocation.Y / ChunkWorldSize);

	const int UnloadRadius = StreamRadius + UnloadMargin;
	const int UnloadRadiusSq = UnloadRadius * UnloadRadius;
	TArray<FIntPoint> ToUnload;
	for (const TPair<FIntPoint, TObjectPtr<AGenerateSurface>>& Pair : LoadedChunks)
	{
		const int dx = Pair.Key.X - CenterChunkX;
		const int dy = Pair.Key.Y - CenterChunkY;
		if (dx * dx + dy * dy > UnloadRadiusSq)
		{
			if (Pair.Value && Pair.Value->IsGenerating()) continue;
			ToUnload.Add(Pair.Key);
		}
	}
	for (const FIntPoint& Key : ToUnload)
	{
		if (AGenerateSurface* Chunk = LoadedChunks.FindRef(Key))
		{
			Chunk->Destroy();
		}
		LoadedChunks.Remove(Key);
	}

	const int LoadRadiusSq = StreamRadius * StreamRadius;
	TArray<FIntPoint> Missing;
	TArray<int> MissingDistSq;
	for (int x = -StreamRadius; x <= StreamRadius; ++x)
	{
		for (int y = -StreamRadius; y <= StreamRadius; ++y)
		{
			const int DistSq = x * x + y * y;
			if (DistSq > LoadRadiusSq)
			{
				continue;
			}
			const FIntPoint Key(CenterChunkX + x, CenterChunkY + y);
			if (LoadedChunks.Contains(Key))
			{
				continue;
			}
			Missing.Add(Key);
			MissingDistSq.Add(DistSq);
		}
	}

	const int Budget = FMath::Max(1, MaxChunksPerFrame);
	int Spawned = 0;
	while (Spawned < Budget && Missing.Num() > 0)
	{
		int BestIdx = 0;
		for (int i = 1; i < Missing.Num(); ++i)
		{
			if (MissingDistSq[i] < MissingDistSq[BestIdx])
			{
				BestIdx = i;
			}
		}
		const bool bWantCollision = bChunkCollision && (MissingDistSq[BestIdx] <= CollisionRadius * CollisionRadius);
		SpawnChunkAt(Missing[BestIdx].X, Missing[BestIdx].Y, bWantCollision);
		Missing.RemoveAtSwap(BestIdx);
		MissingDistSq.RemoveAtSwap(BestIdx);
		++Spawned;
	}

	if (bChunkCollision)
	{
		const int CollRadiusSq = CollisionRadius * CollisionRadius;
		int Upgrades = 0;
		for (const TPair<FIntPoint, TObjectPtr<AGenerateSurface>>& Pair : LoadedChunks)
		{
			if (Upgrades >= 1) break;
			const int dx = Pair.Key.X - CenterChunkX;
			const int dy = Pair.Key.Y - CenterChunkY;
			if (dx * dx + dy * dy <= CollRadiusSq)
			{
				AGenerateSurface* C = Pair.Value;
				if (C && !C->HasChunkCollision())
				{
					C->SetChunkCollisionEnabled(true);
					++Upgrades;
				}
			}
		}
	}
}

