// Fill out your copyright notice in the Description page of Project Settings.


#include "WorldGenerator.h"

#include "Chunk/ChunkBase.h"
#include "SurfaceGenerator/GenerateSurface.h"
#include "CaveGeneration/CaveMarchingCube.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Utils/FastNoiseLite.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"


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
	if (bPersistEdits && !RandomizeSeedOnPlay)
	{
		LoadEdits();
	}
	if (bStreamingEnabled)
	{
		AnchorStreaming();
	}
	else
	{
		GenerateWorld();
	}
}

void AWorldGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bPersistEdits && !RandomizeSeedOnPlay)
	{
		SaveEdits();
	}
	Super::EndPlay(EndPlayReason);
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
	const float PlayerTerrainHeight = (PlayerN01 * Size * HeightScale * MountainBoost + HeightOffset + UndergroundDepth) * 100.0f;
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
	chunk->UndergroundDepth = UndergroundDepth;
	chunk->SizeZ = Size + UndergroundDepth;
	chunk->bCastShadows = bChunksCastShadows;
	chunk->bCollisionEnabled = bWantCollision;
	chunk->bEnableAutoLODGeneration = false;
	chunk->LODLevel = ComputeChunkLOD(ChunkX, ChunkY);
	chunk->bUseTransvoxelMesher = bUseTransvoxelMesher;
	chunk->TransitionWidthScale = TransitionWidthScale;

	if (bEnableCaves && CaveActor)
	{
		TArray<FCaveCarveOp> Ops = CaveActor->GetTileOps(chunk->GetWorldAABB());
		chunk->SetPendingCaveData(MoveTemp(Ops), CaveActor->GetCaveMinRoofDepth());
	}

	if (bPersistEdits)
	{
		if (const TMap<int32, float>* Edits = EditStore.Find(Key))
		{
			chunk->SetPersistedEdits(*Edits);
		}
	}

	if (bAsyncGeneration)
	{
		chunk->StartGenerationAsync();
	}
	else
	{
		chunk->StartGeneration();
		chunk->ApplyPendingCavesSync();
		chunk->ApplyPersistedEditsSync();
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
	const float PlayerTerrainHeight = (PlayerN01 * Size * HeightScale * MountainBoost + HeightOffset + UndergroundDepth) * 100.0f;
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
				CaveActor->TerrainHeightOffsetVoxels = HeightOffset;
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
	CurrentCenterChunk = FIntPoint(CenterChunkX, CenterChunkY);

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
			if (bPersistEdits)
			{
				CaptureChunkEdits(Key, Chunk);
			}
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

	if (bUseOctreeLOD)
	{
		TMap<FIntPoint, int32> DesiredLOD;
		DesiredLOD.Reserve(LoadedChunks.Num());
		for (const TPair<FIntPoint, TObjectPtr<AGenerateSurface>>& Pair : LoadedChunks)
		{
			DesiredLOD.Add(Pair.Key, ComputeChunkLOD(Pair.Key.X, Pair.Key.Y));
		}

		if (bBalanceLOD)
		{
			bool bChanged = true;
			int32 Guard = 0;
			while (bChanged && Guard++ < 8)
			{
				bChanged = false;
				for (TPair<FIntPoint, int32>& Entry : DesiredLOD)
				{
					const FIntPoint K = Entry.Key;
					const FIntPoint Neighbours[4] = {
						FIntPoint(K.X + 1, K.Y), FIntPoint(K.X - 1, K.Y),
						FIntPoint(K.X, K.Y + 1), FIntPoint(K.X, K.Y - 1) };
					int32 MinNeighbour = 5;
					for (const FIntPoint& N : Neighbours)
					{
						if (const int32* NL = DesiredLOD.Find(N))
						{
							MinNeighbour = FMath::Min(MinNeighbour, *NL);
						}
					}
					const int32 Capped = FMath::Min(Entry.Value, MinNeighbour + 1);
					if (Capped != Entry.Value)
					{
						Entry.Value = Capped;
						bChanged = true;
					}
				}
			}
		}

		auto NeighbourCoarser = [&DesiredLOD](int32 NX, int32 NY, int32 Lod) -> bool
		{
			const int32* L = DesiredLOD.Find(FIntPoint(NX, NY));
			return L != nullptr && (*L > Lod);
		};

		auto NeighbourFiner = [&DesiredLOD](int32 NX, int32 NY, int32 Lod) -> bool
		{
			const int32* L = DesiredLOD.Find(FIntPoint(NX, NY));
			return L != nullptr && (*L < Lod);
		};

		for (const TPair<FIntPoint, int32>& Entry : DesiredLOD)
		{
			if (AGenerateSurface* C = LoadedChunks.FindRef(Entry.Key))
			{
				const FIntPoint K = Entry.Key;
				const int32 Lod = Entry.Value;
				int32 Mask = 0;
				if (NeighbourCoarser(K.X + 1, K.Y, Lod)) Mask |= 1;
				if (NeighbourCoarser(K.X - 1, K.Y, Lod)) Mask |= 2;
				if (NeighbourCoarser(K.X, K.Y + 1, Lod)) Mask |= 4;
				if (NeighbourCoarser(K.X, K.Y - 1, Lod)) Mask |= 8;
				int32 FinerMask = 0;
				if (NeighbourFiner(K.X + 1, K.Y, Lod)) FinerMask |= 1;
				if (NeighbourFiner(K.X - 1, K.Y, Lod)) FinerMask |= 2;
				if (NeighbourFiner(K.X, K.Y + 1, Lod)) FinerMask |= 4;
				if (NeighbourFiner(K.X, K.Y - 1, Lod)) FinerMask |= 8;
				C->SetLODAndTransitions(Lod, Mask, FinerMask);
			}
		}
	}
}

int32 AWorldGenerator::ComputeChunkLOD(int ChunkX, int ChunkY) const
{
	if (!bUseOctreeLOD) return 0;
	if (DebugForceLOD > 0) return FMath::Clamp(DebugForceLOD, 0, 5);
	const int dx = ChunkX - CurrentCenterChunk.X;
	const int dy = ChunkY - CurrentCenterChunk.Y;
	const float Dist = FMath::Sqrt((float)(dx * dx + dy * dy));
	const float Over = Dist - (float)LODChunkRadius0;
	if (Over <= 0.f) return 0;
	const int32 Lod = FMath::CeilToInt(Over / FMath::Max(1.f, (float)LODChunksPerLevel));
	return FMath::Clamp(Lod, 0, FMath::Clamp(MaxLOD, 0, 5));
}

FString AWorldGenerator::EditSavePath() const
{
	return FPaths::ProjectSavedDir() / TEXT("VoxelEdits") / FString::Printf(TEXT("edits_%d.bin"), Seed);
}

void AWorldGenerator::CaptureChunkEdits(const FIntPoint& Key, AGenerateSurface* Chunk)
{
	if (!Chunk) return;
	TMap<int32, float> Edits;
	Chunk->ExtractEditedVoxels(Edits);
	if (Edits.Num() > 0)
	{
		EditStore.Add(Key, MoveTemp(Edits));
	}
	else
	{
		EditStore.Remove(Key);
	}
}

void AWorldGenerator::SaveEdits()
{
	for (const TPair<FIntPoint, TObjectPtr<AGenerateSurface>>& Pair : LoadedChunks)
	{
		if (Pair.Value)
		{
			CaptureChunkEdits(Pair.Key, Pair.Value);
		}
	}

	FBufferArchive Ar;
	int32 Version = 2;
	int32 SavedSeed = Seed;
	int32 SavedSize = Size;
	Ar << Version;
	Ar << SavedSeed;
	Ar << SavedSize;
	Ar << EditStore;

	const FString Path = EditSavePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (FFileHelper::SaveArrayToFile(Ar, *Path))
	{
		UE_LOG(LogTemp, Display, TEXT("[PERSIST] Saved %d edited chunk(s) -> %s"), EditStore.Num(), *Path);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[PERSIST] Failed to save edits -> %s"), *Path);
	}
	Ar.FlushCache();
	Ar.Empty();
}

void AWorldGenerator::LoadEdits()
{
	EditStore.Reset();

	const FString Path = EditSavePath();
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		return;
	}

	FMemoryReader Ar(Bytes, true);
	int32 Version = 0;
	int32 SavedSeed = 0;
	int32 SavedSize = 0;
	Ar << Version;
	if (Version != 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PERSIST] Edit file version mismatch (%d), ignoring %s"), Version, *Path);
		return;
	}
	Ar << SavedSeed;
	Ar << SavedSize;
	if (SavedSize != Size)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PERSIST] Edit file size mismatch (%d vs %d), ignoring %s"), SavedSize, Size, *Path);
		return;
	}
	Ar << EditStore;
	UE_LOG(LogTemp, Display, TEXT("[PERSIST] Loaded %d edited chunk(s) <- %s"), EditStore.Num(), *Path);
}

