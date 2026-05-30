#include "CaveMarchingCube.h"
#include "ProceduralMeshComponent.h"
#include "ProceduralGeneration/SurfaceGenerator/GenerateSurface.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"


ACaveMarchingCube::ACaveMarchingCube()
{
	PrimaryActorTick.bCanEverTick = true;
	mesh = CreateDefaultSubobject<UProceduralMeshComponent>("Mesh");
	SetRootComponent(mesh);
	mesh->SetCanEverAffectNavigation(false);
	noise = new FastNoiseLite();
	noise->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	noise->SetFrequency(0.02f);
}

ACaveMarchingCube::~ACaveMarchingCube()
{
	delete(noise);
}


void ACaveMarchingCube::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTemp, Log, TEXT("[CAVE] BeginPlay at %s. bAutoTrigger=%d, Delay=%.2f"), *GetActorLocation().ToString(), bAutoTriggerOnBeginPlay, AutoTriggerDelay);
	if (bAutoTriggerOnBeginPlay)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, this, &ACaveMarchingCube::TriggerCaveGeneration, AutoTriggerDelay, false);
		UE_LOG(LogTemp, Log, TEXT("[CAVE] Timer scheduled in %.2fs"), AutoTriggerDelay);
	}
}

void ACaveMarchingCube::TriggerCaveGeneration()
{
	if (bStreamingManaged)
	{
		UE_LOG(LogTemp, Log, TEXT("[CAVE] Auto-trigger skipped (streaming-managed)"));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[CAVE] TriggerCaveGeneration called"));
	GenerateCaveSystem();
}

void ACaveMarchingCube::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bCaveGenInProgress)
	{
		StepCaveGeneration();
	}
}

void ACaveMarchingCube::CarveCaveInTerrains(const FVector& WorldCenter, float WorldRadius, bool bDistorted)
{
	if (bBuildingPlan)
	{
		CaveCarvePlan.Add({ WorldCenter, WorldRadius, bDistorted, CaveMinRoofDepth });
		CaveCarveCallsTotal++;
		return;
	}

	CaveCarveCallsTotal++;
	const FBox BrushBox(WorldCenter - FVector(WorldRadius + 500.f), WorldCenter + FVector(WorldRadius + 500.f));
	for (int32 t = 0; t < CaveTerrains.Num(); ++t)
	{
		AGenerateSurface* T = CaveTerrains[t];
		if (!T) continue;
		if (CaveTerrainAABBs[t].Intersect(BrushBox))
		{
			T->CarveCaveSphere(WorldCenter, WorldRadius, bDistorted, CaveMinRoofDepth);
			CaveCarveCallsHittingTerrain++;
		}
	}
}

void ACaveMarchingCube::GenerateCaveSystem()
{
	UE_LOG(LogTemp, Log, TEXT("[CAVE] GenerateCaveSystem START"));
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AGenerateSurface::StaticClass(), Found);
	UE_LOG(LogTemp, Log, TEXT("[CAVE] Found %d AGenerateSurface actors in world"), Found.Num());
	if (Found.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[CAVE] no AGenerateSurface chunks in world, ABORTING cave generation"));
		return;
	}

	CaveTerrains.Reset();
	CaveTerrains.Reserve(Found.Num());
	for (AActor* A : Found) if (AGenerateSurface* T = Cast<AGenerateSurface>(A)) CaveTerrains.Add(T);

	CaveTerrainAABBs.Reset();
	CaveTerrainAABBs.Reserve(CaveTerrains.Num());
	FBox AggregateAABB(EForceInit::ForceInit);
	for (AGenerateSurface* T : CaveTerrains)
	{
		const FBox B = T->GetWorldAABB();
		CaveTerrainAABBs.Add(B);
		AggregateAABB += B;
		UE_LOG(LogTemp, Verbose, TEXT("[CAVE] Terrain AABB: min=%s max=%s"), *B.Min.ToString(), *B.Max.ToString());
	}

	noise->SetFrequency(wormNoiseFrequency);

	CaveWorldVoxelScale = voxelSize;

	const FVector AggCenter = AggregateAABB.GetCenter();
	const FVector AggExtent = AggregateAABB.GetExtent();
	const float SpawnZ = FMath::Lerp(AggregateAABB.Max.Z, AggregateAABB.Min.Z, FMath::Clamp(MotherSpawnDepthRatio, 0.0f, 1.0f));
	const FVector MotherWorldStart(AggCenter.X, AggCenter.Y, SpawnZ);
	CaveOrigin = MotherWorldStart - FVector(WormGlobalSize / 2, WormGlobalSize / 2, WormGlobalSize / 2) * CaveWorldVoxelScale;

	UE_LOG(LogTemp, Verbose, TEXT("[CAVE] Aggregate terrain AABB center=%s extent=%s"), *AggCenter.ToString(), *AggExtent.ToString());
	UE_LOG(LogTemp, Verbose, TEXT("[CAVE] Mother starts at WORLD %s (SpawnDepthRatio=%.2f, origin offset=%s)"), *MotherWorldStart.ToString(), MotherSpawnDepthRatio, *CaveOrigin.ToString());

	PlaceSeedsAndStart();
}

bool ACaveMarchingCube::PlaceSeedsAndStart()
{
	CaveActiveWorms.Reset();
	CaveSpawnPositions.Reset();
	CaveWormHorizReach.Reset();
	CaveTotalWormsSpawned = 0;
	CaveCarveCallsTotal = 0;
	CaveCarveCallsHittingTerrain = 0;
	CaveTotalHorizStep = 0.0;
	CaveTotalVertStep = 0.0;
	CaveTotalSteps = 0;
	CaveMaxHorizDistFromAnyStart = 0.f;
	CaveWormsThatTraveledHoriz = 0;

	const int32 NumSeeds = FMath::Max(1, NumSeedWorms);
	const float WormCenter = WormGlobalSize * 0.5f;
	const float WormHorizontalRange = WormGlobalSize * 0.35f;
	const float WormMinZ = WormGlobalSize * (1.f - FMath::Max(SeedMinDepthRatio, SeedMaxDepthRatio));
	const float WormMaxZ = WormGlobalSize * (1.f - FMath::Min(SeedMinDepthRatio, SeedMaxDepthRatio));

	const float MinSpacingSq = MinSeedSpacing * MinSeedSpacing;
	const int32 MaxTries = FMath::Max(1, SeedPlacementMaxTries);
	int32 RejectedPlacements = 0;
	for (int32 s = 0; s < NumSeeds; ++s)
	{
		FWorm Seed;
		FVector Candidate = FVector::ZeroVector;
		bool bAccepted = false;
		for (int32 Try = 0; Try < MaxTries; ++Try)
		{
			Candidate = FVector(
				WormCenter + FMath::FRandRange(-WormHorizontalRange, WormHorizontalRange),
				WormCenter + FMath::FRandRange(-WormHorizontalRange, WormHorizontalRange),
				FMath::FRandRange(WormMinZ, WormMaxZ));
			bool bTooClose = false;
			for (const FVector& Existing : CaveSpawnPositions)
			{
				if (FVector::DistSquared(Candidate, Existing) < MinSpacingSq) { bTooClose = true; break; }
			}
			if (!bTooClose) { bAccepted = true; break; }
			RejectedPlacements++;
		}
		if (!bAccepted) { RejectedPlacements++; continue; }
		Seed.Position = Candidate;
		Seed.Direction = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-0.5f, 0.2f)).GetSafeNormal();
		Seed.RemainingSteps = MotherWormSteps;
		Seed.Radius = MotherWormRadius;
		Seed.NoiseOffset = FMath::RandRange(0.0f, 1000.0f);

		CaveActiveWorms.Add(Seed);
		CaveSpawnPositions.Add(Seed.Position);
		CaveWormHorizReach.Add(0.f);
		CaveTotalWormsSpawned++;
	}

	UE_LOG(LogTemp, Log, TEXT("[CAVE] Seeds placed=%d/%d, rejected attempts=%d, minSpacing=%.1f"), CaveActiveWorms.Num(), NumSeeds, RejectedPlacements, MinSeedSpacing);

	if (CaveActiveWorms.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[CAVE] No worms were placed, aborting cave generation"));
		return false;
	}

	bCaveGenInProgress = true;
	return true;
}

void ACaveMarchingCube::BuildCarvePlan(const FBox& TerrainAABB)
{
	CaveCarvePlan.Reset();
	bCarvePlanReady = false;
	bBuildingPlan = true;

	noise->SetFrequency(wormNoiseFrequency);
	CaveWorldVoxelScale = voxelSize;

	const FVector AggCenter = TerrainAABB.GetCenter();
	const float SpawnZ = FMath::Lerp(TerrainAABB.Max.Z, TerrainAABB.Min.Z, FMath::Clamp(MotherSpawnDepthRatio, 0.0f, 1.0f));
	const FVector MotherWorldStart(AggCenter.X, AggCenter.Y, SpawnZ);
	CaveOrigin = MotherWorldStart - FVector(WormGlobalSize / 2, WormGlobalSize / 2, WormGlobalSize / 2) * CaveWorldVoxelScale;

	if (!PlaceSeedsAndStart())
	{
		bBuildingPlan = false;
		return;
	}

	int32 SafetyIterations = 100000;
	while (bCaveGenInProgress && SafetyIterations-- > 0)
	{
		StepCaveGeneration();
	}

	bBuildingPlan = false;
	bCarvePlanReady = true;
	UE_LOG(LogTemp, Log, TEXT("[CAVE] Carve plan built: %d sphere ops, origin=%s"), CaveCarvePlan.Num(), *CaveOrigin.ToString());
}

int32 ACaveMarchingCube::GetTileSeed(const FIntPoint& Tile) const
{
	uint32 h = (uint32)CaveSeed * 2654435761u;
	h ^= (uint32)(Tile.X) * 73856093u;
	h ^= (uint32)(Tile.Y) * 19349663u;
	h ^= (h >> 13);
	h *= 1274126177u;
	h ^= (h >> 16);
	return (int32)h;
}

TArray<FCaveCarveOp> ACaveMarchingCube::GetTileOps(const FBox& ChunkAABB)
{
	const FVector AABBSize = ChunkAABB.GetSize();
	const float TileWorld = (AABBSize.X > 1.f) ? AABBSize.X : (WormGlobalSize * voxelSize);
	const float TileVoxWidth = TileWorld / voxelSize;
	const FIntPoint Tile(FMath::RoundToInt(ChunkAABB.Min.X / TileWorld), FMath::RoundToInt(ChunkAABB.Min.Y / TileWorld));
	const float FloorWorldZ = ChunkAABB.Min.Z;

	TArray<FCaveCarveOp> Result;
	for (int32 dy = -1; dy <= 1; ++dy)
	for (int32 dx = -1; dx <= 1; ++dx)
	{
		const FIntPoint NT(Tile.X + dx, Tile.Y + dy);
		const TArray<FCaveCarveOp>* Ops = CavePlansByTile.Find(NT);
		if (!Ops)
		{
			TArray<FCaveCarveOp> Built;
			BuildTileCarveOps(NT, FloorWorldZ, TileVoxWidth, Built);
			Ops = &CavePlansByTile.Add(NT, MoveTemp(Built));
		}

		for (const FCaveCarveOp& Op : *Ops)
		{
			const float Reach = Op.WorldRadius + (Op.bDistorted ? 600.f : 150.f);
			const FBox OpBox(Op.WorldCenter - FVector(Reach), Op.WorldCenter + FVector(Reach));
			if (ChunkAABB.Intersect(OpBox))
			{
				Result.Add(Op);
			}
		}
	}

	return Result;
}

void ACaveMarchingCube::BuildTileCarveOps(const FIntPoint& Tile, float FloorWorldZ, float TileVoxWidth, TArray<FCaveCarveOp>& Out)
{
	Out.Reset();
	noise->SetFrequency(wormNoiseFrequency);

	FRandomStream Rng(GetTileSeed(Tile));

	const float OriginVX = (float)Tile.X * TileVoxWidth;
	const float OriginVY = (float)Tile.Y * TileVoxWidth;
	const float Margin = TileVoxWidth * 0.18f;
	const float MinZ = FMath::Max(2.f, CaveMinZVoxel);
	const float MaxZ = FMath::Max(MinZ + 6.f, CaveMaxZVoxel);
	const float EntranceCeil = FMath::Max(MaxZ + 2.f, EntranceCeilVoxel);
	const float ReachLimit = FMath::Max(16.f, MaxWormReachVoxels);

	const int32 NumSeeds = FMath::Max(1, NumSeedWorms);
	TArray<FWorm> Worms;
	Worms.Reserve(NumSeeds * 3);

	int32 TotalSpawned = 0;
	bool bHasEntrance = false;

	for (int32 s = 0; s < NumSeeds; ++s)
	{
		FWorm W;
		W.Position.X = OriginVX + Rng.FRandRange(Margin, TileVoxWidth - Margin);
		W.Position.Y = OriginVY + Rng.FRandRange(Margin, TileVoxWidth - Margin);
		W.NoiseOffset = Rng.FRandRange(0.f, 1000.f);

		const bool bEntrance = !bHasEntrance && (Rng.FRand() < SurfaceEntranceChance);
		if (bEntrance)
		{
			bHasEntrance = true;
			W.Position.Z = Rng.FRandRange(MinZ + 6.f, (MinZ + MaxZ) * 0.45f);
			W.Direction = FVector(Rng.FRandRange(-0.4f, 0.4f), Rng.FRandRange(-0.4f, 0.4f), 1.f).GetSafeNormal();
			W.Radius = FMath::Max(2.0f, MotherWormRadius * 0.65f);
			W.RemainingSteps = FMath::Max(40, MotherWormSteps / 2);
			W.bEntrance = true;
		}
		else
		{
			const float DepthAlpha = (NumSeeds > 1) ? ((float)s / (float)(NumSeeds - 1)) : Rng.FRand();
			W.Position.Z = FMath::Lerp(MinZ + 1.f, MaxZ - 4.f, DepthAlpha) + Rng.FRandRange(-3.f, 3.f);
			W.Direction = FVector(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-0.4f, 0.2f)).GetSafeNormal();
			W.Radius = MotherWormRadius;
			W.RemainingSteps = MotherWormSteps;
			W.bEntrance = false;
		}

		W.Position.Z = FMath::Clamp(W.Position.Z, MinZ, MaxZ);
		W.SpawnPos = W.Position;
		Worms.Add(W);
		TotalSpawned++;
	}

	int32 Safety = 300000;
	while (Worms.Num() > 0 && Safety-- > 0)
	{
		for (int32 i = Worms.Num() - 1; i >= 0; --i)
		{
			FWorm Worm = Worms[i];

			const float NX = noise->GetNoise(Worm.Position.X + Worm.NoiseOffset, Worm.Position.Y, Worm.Position.Z);
			const float NY = noise->GetNoise(Worm.Position.X, Worm.Position.Y + Worm.NoiseOffset + 1000.f, Worm.Position.Z);
			const float NZ = noise->GetNoise(Worm.Position.X, Worm.Position.Y, Worm.Position.Z + Worm.NoiseOffset + 2000.f);

			FVector NoiseDir;
			if (Worm.bEntrance)
			{
				NoiseDir = FVector(NX * 1.0f, NY * 1.0f, NZ * 0.5f + EntranceUpBias);
			}
			else
			{
				NoiseDir = FVector(NX * HorizontalScale, NY * HorizontalScale, (NZ * VerticalScale) + DownwardBias);
			}

			Worm.Direction = (Worm.Direction * DirectionInertia + NoiseDir.GetSafeNormal() * (1.f - DirectionInertia)).GetSafeNormal();
			Worm.Position += Worm.Direction * (Worm.Radius * 0.7f);

			bool bKill = false;
			const float CeilZ = Worm.bEntrance ? EntranceCeil : MaxZ;
			if (Worm.Position.Z >= CeilZ)
			{
				if (Worm.bEntrance)
				{
					bKill = true;
				}
				else
				{
					Worm.Position.Z = CeilZ;
					Worm.Direction.Z = FMath::Min(Worm.Direction.Z, -0.2f);
				}
			}
			if (Worm.Position.Z <= MinZ) bKill = true;
			if (FMath::Abs(Worm.Position.X - Worm.SpawnPos.X) > ReachLimit) bKill = true;
			if (FMath::Abs(Worm.Position.Y - Worm.SpawnPos.Y) > ReachLimit) bKill = true;

			if (!bKill)
			{
				const FVector WorldCenter(Worm.Position.X * voxelSize, Worm.Position.Y * voxelSize, FloorWorldZ + Worm.Position.Z * voxelSize);
				const float OpRoof = Worm.bEntrance ? 0.f : CaveMinRoofDepth;
				Out.Add({ WorldCenter, Worm.Radius * voxelSize, false, OpRoof });

				if (!Worm.bEntrance && (MotherWormSteps - Worm.RemainingSteps) > 20 && Rng.FRand() < RoomProbability)
				{
					Out.Add({ WorldCenter, RoomRadius * voxelSize, true, CaveMinRoofDepth });
				}

				if (!Worm.bEntrance && TotalSpawned < MaxWormsTotal && Rng.FRand() < BranchProbability)
				{
					FWorm Child = Worm;
					Child.Direction = FVector(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-0.5f, 0.3f)).GetSafeNormal();
					Child.RemainingSteps = Worm.RemainingSteps / 2;
					Child.Radius = FMath::Max(2.2f, Worm.Radius * 0.8f);
					Child.NoiseOffset = Rng.FRandRange(0.f, 1000.f);
					Child.SpawnPos = Child.Position;
					Child.bEntrance = false;
					Worms.Add(Child);
					TotalSpawned++;
				}
			}

			Worm.RemainingSteps--;
			if (bKill || Worm.RemainingSteps <= 0)
			{
				Worms.RemoveAtSwap(i);
			}
			else
			{
				Worms[i] = Worm;
			}
		}
	}
}

void ACaveMarchingCube::CarveChunk(AGenerateSurface* Chunk) const
{
	if (!Chunk || !bCarvePlanReady) return;

	const FBox ChunkAABB = Chunk->GetWorldAABB();
	for (const FCaveCarveOp& Op : CaveCarvePlan)
	{
		const float Reach = Op.WorldRadius + (Op.bDistorted ? 500.f : 100.f);
		const FBox OpBox(Op.WorldCenter - FVector(Reach), Op.WorldCenter + FVector(Reach));
		if (ChunkAABB.Intersect(OpBox))
		{
			Chunk->CarveCaveSphere(Op.WorldCenter, Op.WorldRadius, Op.bDistorted, CaveMinRoofDepth);
		}
	}
}

void ACaveMarchingCube::StepCaveGeneration()
{
	int32 Budget = FMath::Max(1, CaveStepsPerFrame);

	while (Budget > 0 && CaveActiveWorms.Num() > 0)
	{
		for (int32 i = CaveActiveWorms.Num() - 1; i >= 0 && Budget > 0; i--)
		{
			FWorm Worm = CaveActiveWorms[i];

			const float NX = noise->GetNoise(Worm.Position.X + Worm.NoiseOffset, Worm.Position.Y, Worm.Position.Z);
			const float NY = noise->GetNoise(Worm.Position.X, Worm.Position.Y + Worm.NoiseOffset + 1000.f, Worm.Position.Z);
			const float NZ = noise->GetNoise(Worm.Position.X, Worm.Position.Y, Worm.Position.Z + Worm.NoiseOffset + 2000.f);

			const FVector NoiseDir = FVector(NX * HorizontalScale, NY * HorizontalScale, (NZ * VerticalScale) + DownwardBias);
			Worm.Direction = (Worm.Direction * DirectionInertia + NoiseDir.GetSafeNormal() * (1.f - DirectionInertia)).GetSafeNormal();
			const FVector PrevPos = Worm.Position;
			Worm.Position += Worm.Direction * (Worm.Radius * 0.7f);
			const FVector Delta = Worm.Position - PrevPos;
			CaveTotalHorizStep += FMath::Sqrt(Delta.X * Delta.X + Delta.Y * Delta.Y);
			CaveTotalVertStep += FMath::Abs(Delta.Z);
			CaveTotalSteps++;
			if (i < CaveSpawnPositions.Num())
			{
				const FVector FromSpawn = Worm.Position - CaveSpawnPositions[i];
				const float HorizFromSpawn = FMath::Sqrt(FromSpawn.X * FromSpawn.X + FromSpawn.Y * FromSpawn.Y);
				CaveWormHorizReach[i] = FMath::Max(CaveWormHorizReach[i], HorizFromSpawn);
				CaveMaxHorizDistFromAnyStart = FMath::Max(CaveMaxHorizDistFromAnyStart, HorizFromSpawn);
			}

			if (Worm.Position.Z >= WormGlobalSize - 2)
			{
				Worm.Position.Z = WormGlobalSize - 2;
				Worm.Direction.Z = FMath::Min(Worm.Direction.Z, -0.2f);
			}
			if (Worm.Position.X <= 2 || Worm.Position.X >= WormGlobalSize - 2 ||
				Worm.Position.Y <= 2 || Worm.Position.Y >= WormGlobalSize - 2 ||
				Worm.Position.Z <= 2)
			{
				UE_LOG(LogTemp, Verbose, TEXT("[CAVE] Worm %d died at LocalPos=%s (out of bounds), maxHorizReach=%.1f"), i, *Worm.Position.ToString(), i < CaveWormHorizReach.Num() ? CaveWormHorizReach[i] : -1.f);
				if (i < CaveWormHorizReach.Num() && CaveWormHorizReach[i] > 8.f) CaveWormsThatTraveledHoriz++;
				CaveActiveWorms.RemoveAt(i);
				if (i < CaveSpawnPositions.Num()) CaveSpawnPositions.RemoveAt(i);
				if (i < CaveWormHorizReach.Num()) CaveWormHorizReach.RemoveAt(i);
				Budget--;
				continue;
			}

			const FVector WorldCarvePos = CaveOrigin + Worm.Position * CaveWorldVoxelScale;
			const float WorldCarveRadius = Worm.Radius * CaveWorldVoxelScale;

			CarveCaveInTerrains(WorldCarvePos, WorldCarveRadius, false);

			if ((MotherWormSteps - Worm.RemainingSteps) > 20 && FMath::FRand() < RoomProbability)
			{
				CarveCaveInTerrains(WorldCarvePos, RoomRadius * CaveWorldVoxelScale, true);
			}

			if (CaveTotalWormsSpawned < MaxWormsTotal && FMath::FRand() < BranchProbability)
			{
				FWorm Child = Worm;
				Child.Direction = FMath::VRand();
				Child.RemainingSteps = Worm.RemainingSteps / 2;
				Child.Radius = FMath::Max(2.5f, Worm.Radius * 0.8f);
				Child.NoiseOffset = FMath::RandRange(0.0f, 1000.0f);
				CaveActiveWorms.Add(Child);
				CaveSpawnPositions.Add(Child.Position);
				CaveWormHorizReach.Add(0.f);
				CaveTotalWormsSpawned++;
			}

			Worm.RemainingSteps--;
			if (Worm.RemainingSteps <= 0)
			{
				UE_LOG(LogTemp, Verbose, TEXT("[CAVE] Worm %d died of old age, maxHorizReach=%.1f"), i, i < CaveWormHorizReach.Num() ? CaveWormHorizReach[i] : -1.f);
				if (i < CaveWormHorizReach.Num() && CaveWormHorizReach[i] > 8.f) CaveWormsThatTraveledHoriz++;
				CaveActiveWorms.RemoveAt(i);
				if (i < CaveSpawnPositions.Num()) CaveSpawnPositions.RemoveAt(i);
				if (i < CaveWormHorizReach.Num()) CaveWormHorizReach.RemoveAt(i);
			}
			else
			{
				CaveActiveWorms[i] = Worm;
			}

			Budget--;
		}
	}

	if (CaveActiveWorms.Num() == 0)
	{
		bCaveGenInProgress = false;

		const double HorizRatio = (CaveTotalHorizStep + CaveTotalVertStep) > 0.0 ? CaveTotalHorizStep / (CaveTotalHorizStep + CaveTotalVertStep) : 0.0;
		UE_LOG(LogTemp, Log, TEXT("[CAVE] DONE. Worms spawned: %d. Terrains: %d. Carve calls total: %d. Hit terrain: %d"), CaveTotalWormsSpawned, CaveTerrains.Num(), CaveCarveCallsTotal, CaveCarveCallsHittingTerrain);
		UE_LOG(LogTemp, Log, TEXT("[CAVE] Movement stats: steps=%d, totalHoriz=%.1f, totalVert=%.1f, horizRatio=%.2f%%, maxHorizReachFromSpawn=%.1f voxels, wormsWithHorizReach>8=%d/%d"),
			CaveTotalSteps, CaveTotalHorizStep, CaveTotalVertStep, HorizRatio * 100.0, CaveMaxHorizDistFromAnyStart, CaveWormsThatTraveledHoriz, CaveTotalWormsSpawned);
	}
}
