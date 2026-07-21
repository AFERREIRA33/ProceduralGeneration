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
#include "DrawDebugHelpers.h"


// Sets default values
AWorldGenerator::AWorldGenerator()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
#if WITH_EDITOR
	SetIsSpatiallyLoaded(false);
#endif
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
	if (bUseOctreeStreaming)
	{
		UpdateStreamingOctree();
	}
	else
	{
		UpdateStreaming();
	}
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

void AWorldGenerator::ConfigureChunkCommon(AGenerateSurface* chunk, bool bWantCollision)
{
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
	chunk->BeachWidthVoxels = BeachWidthVoxels;
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
	chunk->SurfaceRefZ = StreamSurfaceZ;
	chunk->bCubicNode = false;
	chunk->bUseTransvoxelMesher = bUseTransvoxelMesher;
	chunk->TransitionWidthScale = TransitionWidthScale;
	chunk->bDebugTransitionColor = bDebugTransitionColor;
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
	ConfigureChunkCommon(chunk, bWantCollision);
	chunk->LODLevel = ComputeChunkLOD(ChunkX, ChunkY);

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

AGenerateSurface* AWorldGenerator::SpawnNodeChunk(int32 CellX, int32 CellY, int32 CellZ, int32 NodeScale, int32 TransMask, int32 FinerMask, bool bWantCollision)
{
	const FOctreeNodeKey Key{ CellX, CellY, CellZ, NodeScale };
	if (OctreeChunks.Contains(Key))
	{
		return OctreeChunks[Key];
	}

	const float BaseChunk = Size * 100.0f;
	const FVector position = FVector((float)CellX * BaseChunk, (float)CellY * BaseChunk, (float)CellZ * BaseChunk);
	AGenerateSurface* chunk = GetWorld()->SpawnActor<AGenerateSurface>(position, FRotator::ZeroRotator);
	if (!chunk)
	{
		return nullptr;
	}
	ConfigureChunkCommon(chunk, bWantCollision);
	chunk->LODLevel = 0;
	chunk->NodeScale = NodeScale;
	chunk->VoxelSize = 100.0f * (float)NodeScale;
	chunk->bCubicNode = true;
	chunk->UndergroundDepth = 0;
	chunk->NodeOriginZ = (float)CellZ * BaseChunk;
	chunk->bUseTransvoxelMesher = true;
	chunk->TransitionFaceMask = TransMask;
	chunk->FinerNeighbourMask = FinerMask;

	if (bEnableCaves && CaveActor && NodeScale == 1)
	{
		const float RockDepthWorld = BaseChunk * OctreeRockDepthScale;
		const float CaveFloorZ = StreamSurfaceZ - RockDepthWorld;
		const float CaveBandTopVoxels = RockDepthWorld / 100.0f + 20.0f;
		TArray<FCaveCarveOp> Ops = CaveActor->GetTileOps(chunk->GetWorldAABB(), CaveFloorZ, CaveBandTopVoxels);
		chunk->SetPendingCaveData(MoveTemp(Ops), CaveActor->GetCaveMinRoofDepth());
	}

	if (bPersistEdits)
	{
		if (const TMap<int32, float>* Edits = OctreeEditStore.Find(Key))
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
	}

	OctreeChunks.Add(Key, chunk);
	return chunk;
}

float AWorldGenerator::SampleSurfaceWorldZ(float WorldX, float WorldY)
{
	const float n = StreamHeightNoise.GetNoise(WorldX / 100.0f, WorldY / 100.0f);
	const float n01 = FMath::Pow(FMath::Clamp((n + 1.0f) * 0.5f + MountainBias, 0.0f, 1.0f), HeightRedistribution);
	const float base = n01 * (float)Size * HeightScale * MountainBoost;
	return StreamSurfaceZ + base * 100.0f;
}

bool AWorldGenerator::NodeIntersectsSurface(int32 CellX, int32 CellY, int32 CellZ, int32 NodeScale)
{
	const float BaseChunk = Size * 100.0f;
	const float X0 = (float)CellX * BaseChunk;
	const float Y0 = (float)CellY * BaseChunk;
	const float Span = (float)NodeScale * BaseChunk;
	const float NodeMinZ = (float)CellZ * BaseChunk;
	const float NodeMaxZ = NodeMinZ + Span;

	float MaxS = -FLT_MAX;
	for (int32 i = 0; i <= 2; ++i)
	for (int32 j = 0; j <= 2; ++j)
	{
		const float s = SampleSurfaceWorldZ(X0 + Span * 0.5f * (float)i, Y0 + Span * 0.5f * (float)j);
		MaxS = FMath::Max(MaxS, s);
	}
	const float MinS = StreamSurfaceZ - BaseChunk * OctreeRockDepthScale;
	return NodeMaxZ > MinS && NodeMinZ < MaxS + BaseChunk * 0.25f;
}

int32 AWorldGenerator::NaturalScale(const FVector& Point, const FVector& PlayerLoc)
{
	const float BaseChunk = Size * 100.0f;
	const float d = FVector::Dist2D(Point, PlayerLoc);
	const int32 MaxScale = 1 << FMath::Clamp(MaxLOD, 0, 5);
	const float ratio = d / (OctreeSubdivFactor * BaseChunk);
	int32 sc = 1;
	while (sc * 2 <= ratio && sc < MaxScale)
	{
		sc *= 2;
	}
	return sc;
}

int32 AWorldGenerator::ComputeNodeTransitionMask(int32 CellX, int32 CellY, int32 CellZ, int32 NodeScale, const FVector& PlayerLoc)
{
	const float BaseChunk = Size * 100.0f;
	const float NodeWorld = (float)NodeScale * BaseChunk;
	const float cx = ((float)CellX + (float)NodeScale * 0.5f) * BaseChunk;
	const float cy = ((float)CellY + (float)NodeScale * 0.5f) * BaseChunk;
	const float cz = ((float)CellZ + (float)NodeScale * 0.5f) * BaseChunk;
	const float Off = NodeWorld * 0.75f;
	int32 Mask = 0;
	if (NaturalScale(FVector(cx + Off, cy, cz), PlayerLoc) > NodeScale) Mask |= 1;
	if (NaturalScale(FVector(cx - Off, cy, cz), PlayerLoc) > NodeScale) Mask |= 2;
	if (NaturalScale(FVector(cx, cy + Off, cz), PlayerLoc) > NodeScale) Mask |= 4;
	if (NaturalScale(FVector(cx, cy - Off, cz), PlayerLoc) > NodeScale) Mask |= 8;
	return Mask;
}

int32 AWorldGenerator::ComputeNodeFinerMask(int32 CellX, int32 CellY, int32 CellZ, int32 NodeScale, const FVector& PlayerLoc)
{
	const float BaseChunk = Size * 100.0f;
	const float NodeWorld = (float)NodeScale * BaseChunk;
	const float cx = ((float)CellX + (float)NodeScale * 0.5f) * BaseChunk;
	const float cy = ((float)CellY + (float)NodeScale * 0.5f) * BaseChunk;
	const float cz = ((float)CellZ + (float)NodeScale * 0.5f) * BaseChunk;
	const float Off = NodeWorld * 0.75f;
	int32 Mask = 0;
	if (NaturalScale(FVector(cx + Off, cy, cz), PlayerLoc) < NodeScale) Mask |= 1;
	if (NaturalScale(FVector(cx - Off, cy, cz), PlayerLoc) < NodeScale) Mask |= 2;
	if (NaturalScale(FVector(cx, cy + Off, cz), PlayerLoc) < NodeScale) Mask |= 4;
	if (NaturalScale(FVector(cx, cy - Off, cz), PlayerLoc) < NodeScale) Mask |= 8;
	return Mask;
}

void AWorldGenerator::SubdivideOctree(int32 CellX, int32 CellY, int32 CellZ, int32 NodeScale, const FVector& PlayerLoc, TArray<FOctreeNodeKey>& Out)
{
	if (!NodeIntersectsSurface(CellX, CellY, CellZ, NodeScale))
	{
		return;
	}
	if (NodeScale > 1)
	{
		const float BaseChunk = Size * 100.0f;
		const float NodeWorld = (float)NodeScale * BaseChunk;
		const float cx = ((float)CellX + (float)NodeScale * 0.5f) * BaseChunk;
		const float cy = ((float)CellY + (float)NodeScale * 0.5f) * BaseChunk;
		const float cz = ((float)CellZ + (float)NodeScale * 0.5f) * BaseChunk;
		const float dx = PlayerLoc.X - cx;
		const float dy = PlayerLoc.Y - cy;
		const float Dist = FMath::Sqrt(dx * dx + dy * dy);
		bool bSubdivide = (Dist < NodeWorld * OctreeSubdivFactor);
		if (!bSubdivide)
		{
			const float Off = NodeWorld * 0.75f;
			if (NaturalScale(FVector(cx + Off, cy, cz), PlayerLoc) * 2 < NodeScale ||
				NaturalScale(FVector(cx - Off, cy, cz), PlayerLoc) * 2 < NodeScale ||
				NaturalScale(FVector(cx, cy + Off, cz), PlayerLoc) * 2 < NodeScale ||
				NaturalScale(FVector(cx, cy - Off, cz), PlayerLoc) * 2 < NodeScale)
			{
				bSubdivide = true;
			}
		}
		if (bSubdivide)
		{
			const int32 h = NodeScale / 2;
			for (int32 oz = 0; oz <= h; oz += h)
			for (int32 oy = 0; oy <= h; oy += h)
			for (int32 ox = 0; ox <= h; ox += h)
			{
				SubdivideOctree(CellX + ox, CellY + oy, CellZ + oz, h, PlayerLoc, Out);
			}
			return;
		}
	}
	Out.Add(FOctreeNodeKey{ CellX, CellY, CellZ, NodeScale });
}

void AWorldGenerator::CollectOctreeLeaves(const FVector& PlayerLoc, TArray<FOctreeNodeKey>& Out)
{
	const float BaseChunk = Size * 100.0f;
	const int32 MaxScale = 1 << FMath::Clamp(MaxLOD, 0, 5);
	auto AlignDown = [](int32 v, int32 a) { return (int32)(FMath::FloorToInt((float)v / (float)a)) * a; };
	const int32 pcx = FMath::FloorToInt(PlayerLoc.X / BaseChunk);
	const int32 pcy = FMath::FloorToInt(PlayerLoc.Y / BaseChunk);
	const int32 minCX = AlignDown(pcx - StreamRadius, MaxScale);
	const int32 maxCX = AlignDown(pcx + StreamRadius, MaxScale);
	const int32 minCY = AlignDown(pcy - StreamRadius, MaxScale);
	const int32 maxCY = AlignDown(pcy + StreamRadius, MaxScale);
	const float BandMaxZ = StreamSurfaceZ + (float)Size * HeightScale * MountainBoost * 100.0f + BaseChunk;
	const float BandMinZ = StreamSurfaceZ - BaseChunk * OctreeRockDepthScale - BaseChunk;
	const int32 minCZ = AlignDown(FMath::FloorToInt(BandMinZ / BaseChunk), MaxScale);
	const int32 maxCZ = AlignDown(FMath::FloorToInt(BandMaxZ / BaseChunk), MaxScale);
	for (int32 cx = minCX; cx <= maxCX; cx += MaxScale)
	for (int32 cy = minCY; cy <= maxCY; cy += MaxScale)
	for (int32 cz = minCZ; cz <= maxCZ; cz += MaxScale)
	{
		SubdivideOctree(cx, cy, cz, MaxScale, PlayerLoc, Out);
	}
}

bool AWorldGenerator::IsReplacementReady(const FOctreeNodeKey& Key, const TSet<FOctreeNodeKey>& DesiredSet)
{
	const int32 h = Key.S / 2;
	if (h >= 1)
	{
		bool bAnyChild = false;
		bool bAllReady = true;
		for (int32 oz = 0; oz <= h; oz += h)
		for (int32 oy = 0; oy <= h; oy += h)
		for (int32 ox = 0; ox <= h; ox += h)
		{
			const FOctreeNodeKey C{ Key.X + ox, Key.Y + oy, Key.Z + oz, h };
			if (DesiredSet.Contains(C))
			{
				bAnyChild = true;
				AGenerateSurface* CC = OctreeChunks.FindRef(C);
				if (!CC || CC->IsGenerating()) bAllReady = false;
			}
		}
		if (bAnyChild) return bAllReady;
	}
	const int32 P = Key.S * 2;
	auto AlignDown = [](int32 v, int32 a) { return (int32)(FMath::FloorToInt((float)v / (float)a)) * a; };
	const FOctreeNodeKey Par{ AlignDown(Key.X, P), AlignDown(Key.Y, P), AlignDown(Key.Z, P), P };
	if (DesiredSet.Contains(Par))
	{
		AGenerateSurface* PC = OctreeChunks.FindRef(Par);
		return PC != nullptr && !PC->IsGenerating();
	}
	return true;
}

void AWorldGenerator::UpdateStreamingOctree()
{
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn)
	{
		return;
	}
	const FVector PlayerLocation = PlayerPawn->GetActorLocation();

	bool bRecomputed = false;
	if (CachedDesired.Num() == 0 || ++StreamRecomputeCounter >= FMath::Max(1, StreamRecomputeEveryNFrames))
	{
		StreamRecomputeCounter = 0;
		CachedDesired.Reset();
		CollectOctreeLeaves(PlayerLocation, CachedDesired);
		bRecomputed = true;
	}
	TArray<FOctreeNodeKey>& Desired = CachedDesired;
	const float BaseChunk = Size * 100.0f;

	if (bRecomputed)
	{
		TSet<FOctreeNodeKey> DesiredSet(Desired);

		TArray<FOctreeNodeKey> ToUnload;
		for (const TPair<FOctreeNodeKey, TObjectPtr<AGenerateSurface>>& Pair : OctreeChunks)
		{
			if (!DesiredSet.Contains(Pair.Key))
			{
				if (Pair.Value && Pair.Value->IsGenerating()) continue;
				if (!IsReplacementReady(Pair.Key, DesiredSet)) continue;
				ToUnload.Add(Pair.Key);
			}
		}
		for (const FOctreeNodeKey& Key : ToUnload)
		{
			if (AGenerateSurface* Chunk = OctreeChunks.FindRef(Key))
			{
				if (bPersistEdits) CaptureChunkEdits(Key, Chunk);
				Chunk->Destroy();
			}
			OctreeChunks.Remove(Key);
		}

		CachedMissing.Reset();
		CachedMissingDist.Reset();
		for (const FOctreeNodeKey& Key : Desired)
		{
			if (OctreeChunks.Contains(Key)) continue;
			const float cx = ((float)Key.X + (float)Key.S * 0.5f) * BaseChunk;
			const float cy = ((float)Key.Y + (float)Key.S * 0.5f) * BaseChunk;
			const float cz = ((float)Key.Z + (float)Key.S * 0.5f) * BaseChunk;
			const float dx = PlayerLocation.X - cx;
			const float dy = PlayerLocation.Y - cy;
			const float dz = PlayerLocation.Z - cz;
			CachedMissing.Add(Key);
			CachedMissingDist.Add(dx * dx + dy * dy + dz * dz);
		}
	}

	const int Budget = FMath::Clamp(CachedMissing.Num() / 8, FMath::Max(1, MaxChunksPerFrame), FMath::Max(1, InitialFillChunksPerFrame));
	int Spawned = 0;
	while (Spawned < Budget && CachedMissing.Num() > 0)
	{
		int BestIdx = 0;
		for (int i = 1; i < CachedMissing.Num(); ++i)
		{
			if (CachedMissingDist[i] < CachedMissingDist[BestIdx]) BestIdx = i;
		}
		const FOctreeNodeKey Key = CachedMissing[BestIdx];
		const int32 TransMask = ComputeNodeTransitionMask(Key.X, Key.Y, Key.Z, Key.S, PlayerLocation);
		const int32 FinerMask = ComputeNodeFinerMask(Key.X, Key.Y, Key.Z, Key.S, PlayerLocation);
		const float ncx = ((float)Key.X + 0.5f) * BaseChunk;
		const float ncy = ((float)Key.Y + 0.5f) * BaseChunk;
		const float ncz = ((float)Key.Z + 0.5f) * BaseChunk;
		const float CollR = OctreeCollisionRadiusChunks * BaseChunk;
		const bool bWantCollision = bChunkCollision && (Key.S == 1) && FVector::DistSquared(FVector(ncx, ncy, ncz), PlayerLocation) < CollR * CollR;
		SpawnNodeChunk(Key.X, Key.Y, Key.Z, Key.S, TransMask, FinerMask, bWantCollision);
		CachedMissing.RemoveAtSwap(BestIdx);
		CachedMissingDist.RemoveAtSwap(BestIdx);
		++Spawned;
	}

	if (bChunkCollision)
	{
		const float CollRadius = OctreeCollisionRadiusChunks * BaseChunk;
		const float CollRadius2 = CollRadius * CollRadius;
		for (const TPair<FOctreeNodeKey, TObjectPtr<AGenerateSurface>>& Pair : OctreeChunks)
		{
			AGenerateSurface* Chunk = Pair.Value;
			if (!Chunk || Pair.Key.S != 1) continue;
			const float ccx = ((float)Pair.Key.X + 0.5f) * BaseChunk;
			const float ccy = ((float)Pair.Key.Y + 0.5f) * BaseChunk;
			const float ccz = ((float)Pair.Key.Z + 0.5f) * BaseChunk;
			if (FVector::DistSquared(FVector(ccx, ccy, ccz), PlayerLocation) < CollRadius2)
			{
				Chunk->SetChunkCollisionEnabled(true);
			}
		}
	}

	if (bDebugDrawOctree)
	{
		for (const TPair<FOctreeNodeKey, TObjectPtr<AGenerateSurface>>& Pair : OctreeChunks)
		{
			const float Half = (float)Pair.Key.S * BaseChunk * 0.5f;
			const FVector Center(
				((float)Pair.Key.X + (float)Pair.Key.S * 0.5f) * BaseChunk,
				((float)Pair.Key.Y + (float)Pair.Key.S * 0.5f) * BaseChunk,
				((float)Pair.Key.Z + (float)Pair.Key.S * 0.5f) * BaseChunk);
			FColor C;
			switch (Pair.Key.S)
			{
			case 1: C = FColor::Green; break;
			case 2: C = FColor::Yellow; break;
			case 4: C = FColor::Orange; break;
			case 8: C = FColor::Red; break;
			default: C = FColor::Purple; break;
			}
			DrawDebugBox(GetWorld(), Center, FVector(Half * 0.98f), C, false, -1.f, 0, 40.f);
		}
		DrawDebugLine(GetWorld(), PlayerLocation - FVector(0, 0, 800.f), PlayerLocation - FVector(0, 0, 100000.f), FColor::Cyan, false, -1.f, 0, 8.f);
	}
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
	const int32 EffUndergroundDepth = bUseOctreeStreaming ? 0 : UndergroundDepth;
	const float PlayerTerrainHeight = (PlayerN01 * Size * HeightScale * MountainBoost + HeightOffset + EffUndergroundDepth) * 100.0f;
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

	StreamHeightNoise.SetSeed(Seed);
	StreamHeightNoise.SetFrequency(Frequency);
	StreamHeightNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	StreamHeightNoise.SetFractalType(FastNoiseLite::FractalType_FBm);
	StreamHeightNoise.SetFractalOctaves(FractalOctaves);
	StreamHeightNoise.SetFractalLacunarity(FractalLacunarity);
	StreamHeightNoise.SetFractalGain(FractalGain);

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

void AWorldGenerator::CaptureChunkEdits(const FOctreeNodeKey& Key, AGenerateSurface* Chunk)
{
	if (!Chunk) return;
	TMap<int32, float> Edits;
	Chunk->ExtractEditedVoxels(Edits);
	if (Edits.Num() > 0)
	{
		OctreeEditStore.Add(Key, MoveTemp(Edits));
	}
	else
	{
		OctreeEditStore.Remove(Key);
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
	for (const TPair<FOctreeNodeKey, TObjectPtr<AGenerateSurface>>& Pair : OctreeChunks)
	{
		if (Pair.Value)
		{
			CaptureChunkEdits(Pair.Key, Pair.Value);
		}
	}

	FBufferArchive Ar;
	int32 Version = 3;
	int32 SavedSeed = Seed;
	int32 SavedSize = Size;
	Ar << Version;
	Ar << SavedSeed;
	Ar << SavedSize;
	Ar << EditStore;
	Ar << OctreeEditStore;

	const FString Path = EditSavePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (FFileHelper::SaveArrayToFile(Ar, *Path))
	{
		UE_LOG(LogTemp, Display, TEXT("[PERSIST] Saved %d + %d edited chunk(s) -> %s"), EditStore.Num(), OctreeEditStore.Num(), *Path);
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
	OctreeEditStore.Reset();

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
	if (Version != 3)
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
	Ar << OctreeEditStore;
	UE_LOG(LogTemp, Display, TEXT("[PERSIST] Loaded %d + %d edited chunk(s) <- %s"), EditStore.Num(), OctreeEditStore.Num(), *Path);
}

