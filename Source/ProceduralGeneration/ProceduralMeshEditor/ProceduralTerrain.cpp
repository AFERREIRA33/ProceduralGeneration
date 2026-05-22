#include "ProceduralTerrain.h"
#include "MarchingCubesTables.h"
#include "../../Utils/FastNoiseLite.h"
#include "Async/ParallelFor.h"

AProceduralTerrain::AProceduralTerrain()
{
	PrimaryActorTick.bCanEverTick = true;

	MeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;
	MeshComponent->bUseAsyncCooking = true;
	MeshComponent->bUseComplexAsSimpleCollision = true;
	MeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
}

void AProceduralTerrain::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (DirtyChunks.Num() == 0) return;
	RemeshAccumulator += DeltaTime;
	if (RemeshAccumulator < RemeshInterval) return;
	RemeshAccumulator = 0.f;
	FlushDirtyChunks();
}

void AProceduralTerrain::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
}

void AProceduralTerrain::BeginPlay()
{
	Super::BeginPlay();
	GenerateDensities();
	OriginalDensities = Densities;
	BuildChunks();
	RebuildAllChunks();
	if (Material)
	{
		MeshComponent->SetMaterial(0, Material);
	}
}

void AProceduralTerrain::GenerateDensities()
{
	const int32 Total = GridSize.X * GridSize.Y * GridSize.Z;
	Densities.SetNumUninitialized(Total);

	FastNoiseLite Noise;
	Noise.SetSeed(NoiseSeed);
	Noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	Noise.SetFrequency(NoiseFrequency);

	const float HalfZ = GridSize.Z * 0.5f;

	for (int32 z = 0; z < GridSize.Z; ++z)
	{
		for (int32 y = 0; y < GridSize.Y; ++y)
		{
			for (int32 x = 0; x < GridSize.X; ++x)
			{
				const float n = Noise.GetNoise((float)x, (float)y);
				const float SurfaceZ = HalfZ + n * HeightMultiplier;
				Densities[Idx(x, y, z)] = FMath::Clamp((float)z - SurfaceZ, DensityClampMin, DensityClampMax);
			}
		}
	}
}

void AProceduralTerrain::BuildChunks()
{
	Chunks.Reset();
	ChunkCount = FIntVector(
		FMath::DivideAndRoundUp(GridSize.X - 1, ChunkSize),
		FMath::DivideAndRoundUp(GridSize.Y - 1, ChunkSize),
		FMath::DivideAndRoundUp(GridSize.Z - 1, ChunkSize));

	int32 Section = 0;
	for (int32 cz = 0; cz < ChunkCount.Z; ++cz)
	for (int32 cy = 0; cy < ChunkCount.Y; ++cy)
	for (int32 cx = 0; cx < ChunkCount.X; ++cx)
	{
		FChunkInfo Info;
		Info.Min = FIntVector(cx * ChunkSize, cy * ChunkSize, cz * ChunkSize);
		Info.Max = FIntVector(
			FMath::Min(Info.Min.X + ChunkSize, GridSize.X - 1),
			FMath::Min(Info.Min.Y + ChunkSize, GridSize.Y - 1),
			FMath::Min(Info.Min.Z + ChunkSize, GridSize.Z - 1));
		Info.SectionIndex = Section++;
		Info.bCreated = false;
		Chunks.Add(Info);
	}
}

void AProceduralTerrain::RebuildAllChunks()
{
	TArray<FChunkBuildData> Builds;
	Builds.SetNum(Chunks.Num());
	ParallelFor(Chunks.Num(), [this, &Builds](int32 i)
	{
		BuildChunkData(i, Builds[i]);
	});
	for (int32 i = 0; i < Chunks.Num(); ++i)
	{
		UploadChunk(i, Builds[i]);
	}
}

FVector AProceduralTerrain::GradientAtCorner(int32 x, int32 y, int32 z) const
{
	const int32 xm = FMath::Max(0, x - 1);
	const int32 xp = FMath::Min(GridSize.X - 1, x + 1);
	const int32 ym = FMath::Max(0, y - 1);
	const int32 yp = FMath::Min(GridSize.Y - 1, y + 1);
	const int32 zm = FMath::Max(0, z - 1);
	const int32 zp = FMath::Min(GridSize.Z - 1, z + 1);
	return FVector(
		Densities[Idx(xp, y, z)] - Densities[Idx(xm, y, z)],
		Densities[Idx(x, yp, z)] - Densities[Idx(x, ym, z)],
		Densities[Idx(x, y, zp)] - Densities[Idx(x, y, zm)]);
}

void AProceduralTerrain::BuildChunkData(int32 ChunkIndex, FChunkBuildData& Out) const
{
	const FChunkInfo& Info = Chunks[ChunkIndex];

	TArray<FVector>& Vertices = Out.Vertices;
	TArray<int32>& Triangles = Out.Triangles;
	TArray<FVector>& Normals = Out.Normals;
	Vertices.Reset();
	Triangles.Reset();
	Normals.Reset();
	const int32 EstimatedVerts = ChunkSize * ChunkSize * ChunkSize * 2;
	Vertices.Reserve(EstimatedVerts);
	Triangles.Reserve(EstimatedVerts * 3);
	Normals.Reserve(EstimatedVerts);

	static const int8 EdgeOffsetTable[12][4] =
	{
		{0,0,0,0}, {1,0,0,1}, {0,1,0,0}, {0,0,0,1},
		{0,0,1,0}, {1,0,1,1}, {0,1,1,0}, {0,0,1,1},
		{0,0,0,2}, {1,0,0,2}, {1,1,0,2}, {0,1,0,2}
	};

	const int32 EdgeGridSize = ChunkSize + 1;
	const int32 NumEdgeIds = EdgeGridSize * EdgeGridSize * EdgeGridSize * 3;
	TArray<int32>& EdgeToVertex = Out.EdgeToVertex;
	EdgeToVertex.SetNumUninitialized(NumEdgeIds);
	FMemory::Memset(EdgeToVertex.GetData(), 0xFF, NumEdgeIds * sizeof(int32));

	for (int32 z = Info.Min.Z; z < Info.Max.Z; ++z)
	for (int32 y = Info.Min.Y; y < Info.Max.Y; ++y)
	for (int32 x = Info.Min.X; x < Info.Max.X; ++x)
	{
		float CornerValues[8];
		FVector CornerPos[8];
		int32 CubeIndex = 0;
		for (int32 c = 0; c < 8; ++c)
		{
			const int32 ox = x + MarchingCubes::CornerOffsets[c][0];
			const int32 oy = y + MarchingCubes::CornerOffsets[c][1];
			const int32 oz = z + MarchingCubes::CornerOffsets[c][2];
			CornerValues[c] = Densities[Idx(ox, oy, oz)];
			CornerPos[c] = FVector(ox, oy, oz) * VoxelSize;
			if (CornerValues[c] < 0.f) CubeIndex |= (1 << c);
		}

		const int32 Edges = MarchingCubes::EdgeTable[CubeIndex];
		if (Edges == 0) continue;

		FVector CornerGrads[8];
		bool bGradsComputed = false;

		const int32 lcx = x - Info.Min.X;
		const int32 lcy = y - Info.Min.Y;
		const int32 lcz = z - Info.Min.Z;

		int32 EdgeVertIndex[12];
		for (int32 e = 0; e < 12; ++e)
		{
			if (!(Edges & (1 << e))) { EdgeVertIndex[e] = -1; continue; }

			const int32 ex = lcx + EdgeOffsetTable[e][0];
			const int32 ey = lcy + EdgeOffsetTable[e][1];
			const int32 ez = lcz + EdgeOffsetTable[e][2];
			const int32 axis = EdgeOffsetTable[e][3];
			const int32 Key = ((ex * EdgeGridSize + ey) * EdgeGridSize + ez) * 3 + axis;

			int32 ExistingIdx = EdgeToVertex[Key];
			if (ExistingIdx != -1)
			{
				EdgeVertIndex[e] = ExistingIdx;
				continue;
			}

			if (!bGradsComputed)
			{
				for (int32 c = 0; c < 8; ++c)
				{
					const int32 ox = x + MarchingCubes::CornerOffsets[c][0];
					const int32 oy = y + MarchingCubes::CornerOffsets[c][1];
					const int32 oz = z + MarchingCubes::CornerOffsets[c][2];
					CornerGrads[c] = GradientAtCorner(ox, oy, oz);
				}
				bGradsComputed = true;
			}

			const int32 a = MarchingCubes::EdgeConnection[e][0];
			const int32 b = MarchingCubes::EdgeConnection[e][1];
			const float va = CornerValues[a];
			const float vb = CornerValues[b];
			const float denom = vb - va;
			const float t = FMath::IsNearlyZero(denom) ? 0.5f : (-va / denom);

			const int32 NewIdx = Vertices.Num();
			Vertices.Add(CornerPos[a] + (CornerPos[b] - CornerPos[a]) * t);
			Normals.Add(FMath::Lerp(CornerGrads[a], CornerGrads[b], t).GetSafeNormal());
			EdgeToVertex[Key] = NewIdx;
			EdgeVertIndex[e] = NewIdx;
		}

		for (int32 i = 0; i < 16; i += 3)
		{
			const int8 i0 = MarchingCubes::TriTable[CubeIndex][i];
			if (i0 == -1) break;
			const int8 i1 = MarchingCubes::TriTable[CubeIndex][i + 1];
			const int8 i2 = MarchingCubes::TriTable[CubeIndex][i + 2];

			Triangles.Add(EdgeVertIndex[i0]);
			Triangles.Add(EdgeVertIndex[i1]);
			Triangles.Add(EdgeVertIndex[i2]);
		}
	}
}

void AProceduralTerrain::UploadChunk(int32 ChunkIndex, const FChunkBuildData& Data)
{
	FChunkInfo& Info = Chunks[ChunkIndex];
	static const TArray<FVector2D> EmptyUV;
	static const TArray<FColor> EmptyColors;
	static const TArray<FProcMeshTangent> EmptyTangents;

	if (Data.Vertices.Num() == 0)
	{
		if (Info.bCreated)
		{
			MeshComponent->ClearMeshSection(Info.SectionIndex);
		}
		return;
	}

	const bool bWithCollision = !bInEditStroke;
	MeshComponent->CreateMeshSection(Info.SectionIndex, Data.Vertices, Data.Triangles, Data.Normals, EmptyUV, EmptyColors, EmptyTangents, bWithCollision);
	Info.bCreated = true;

	if (bWithCollision)
	{
		CollisionPendingChunks.Remove(ChunkIndex);
	}
	else
	{
		CollisionPendingChunks.Add(ChunkIndex);
	}
}

void AProceduralTerrain::BeginEditStroke()
{
	bInEditStroke = true;
}

void AProceduralTerrain::EndEditStroke()
{
	bInEditStroke = false;
	if (CollisionPendingChunks.Num() == 0) return;

	const int32 N = CollisionPendingChunks.Num();
	CachedIndices.Reset(N);
	for (int32 Idx : CollisionPendingChunks) CachedIndices.Add(Idx);
	CollisionPendingChunks.Reset();

	if (CachedBuilds.Num() < N) CachedBuilds.SetNum(N);

	ParallelFor(N, [this](int32 i)
	{
		BuildChunkData(CachedIndices[i], CachedBuilds[i]);
	});

	for (int32 i = 0; i < N; ++i)
	{
		UploadChunk(CachedIndices[i], CachedBuilds[i]);
	}
}

float AProceduralTerrain::SampleDensityTrilinear(float gx, float gy, float gz) const
{
	const int32 x0 = FMath::FloorToInt(gx);
	const int32 y0 = FMath::FloorToInt(gy);
	const int32 z0 = FMath::FloorToInt(gz);
	if (x0 < 0 || y0 < 0 || z0 < 0) return 1.f;
	if (x0 >= GridSize.X - 1 || y0 >= GridSize.Y - 1 || z0 >= GridSize.Z - 1) return 1.f;

	const float tx = gx - x0;
	const float ty = gy - y0;
	const float tz = gz - z0;

	const float c000 = Densities[Idx(x0,     y0,     z0)];
	const float c100 = Densities[Idx(x0 + 1, y0,     z0)];
	const float c010 = Densities[Idx(x0,     y0 + 1, z0)];
	const float c110 = Densities[Idx(x0 + 1, y0 + 1, z0)];
	const float c001 = Densities[Idx(x0,     y0,     z0 + 1)];
	const float c101 = Densities[Idx(x0 + 1, y0,     z0 + 1)];
	const float c011 = Densities[Idx(x0,     y0 + 1, z0 + 1)];
	const float c111 = Densities[Idx(x0 + 1, y0 + 1, z0 + 1)];

	const float c00 = FMath::Lerp(c000, c100, tx);
	const float c10 = FMath::Lerp(c010, c110, tx);
	const float c01 = FMath::Lerp(c001, c101, tx);
	const float c11 = FMath::Lerp(c011, c111, tx);

	const float c0 = FMath::Lerp(c00, c10, ty);
	const float c1 = FMath::Lerp(c01, c11, ty);

	return FMath::Lerp(c0, c1, tz);
}

bool AProceduralTerrain::TraceDensityField(const FVector& WorldStart, const FVector& WorldDir, float MaxDist, FVector& OutHitPoint, FVector& OutNormal) const
{
	const FTransform Xf = GetActorTransform();
	const FVector LocalStart = Xf.InverseTransformPosition(WorldStart);
	const FVector LocalDir = Xf.InverseTransformVector(WorldDir).GetSafeNormal();

	const FVector GridStart = LocalStart / VoxelSize;
	const float StepGrid = 0.5f;
	const float StepWorld = StepGrid * VoxelSize;
	const int32 MaxSteps = FMath::CeilToInt(MaxDist / StepWorld);

	float PrevD = SampleDensityTrilinear(GridStart.X, GridStart.Y, GridStart.Z);
	FVector PrevG = GridStart;

	for (int32 i = 1; i <= MaxSteps; ++i)
	{
		const FVector G = GridStart + LocalDir * (StepGrid * i);
		const float D = SampleDensityTrilinear(G.X, G.Y, G.Z);

		if (PrevD > 0.f && D <= 0.f)
		{
			const float denom = PrevD - D;
			const float t = FMath::IsNearlyZero(denom) ? 0.5f : PrevD / denom;
			const FVector HitG = FMath::Lerp(PrevG, G, t);
			OutHitPoint = Xf.TransformPosition(HitG * VoxelSize);

			const float h = 0.5f;
			const FVector Grad(
				SampleDensityTrilinear(HitG.X + h, HitG.Y, HitG.Z) - SampleDensityTrilinear(HitG.X - h, HitG.Y, HitG.Z),
				SampleDensityTrilinear(HitG.X, HitG.Y + h, HitG.Z) - SampleDensityTrilinear(HitG.X, HitG.Y - h, HitG.Z),
				SampleDensityTrilinear(HitG.X, HitG.Y, HitG.Z + h) - SampleDensityTrilinear(HitG.X, HitG.Y, HitG.Z - h));
			OutNormal = Xf.TransformVector(Grad).GetSafeNormal();
			return true;
		}
		PrevD = D;
		PrevG = G;
	}
	return false;
}

void AProceduralTerrain::FlushDirtyChunks()
{
	const int32 N = DirtyChunks.Num();
	if (N == 0) return;

	CachedIndices.Reset(N);
	for (int32 Idx : DirtyChunks) CachedIndices.Add(Idx);
	DirtyChunks.Reset();

	if (CachedBuilds.Num() < N) CachedBuilds.SetNum(N);

	ParallelFor(N, [this](int32 i)
	{
		BuildChunkData(CachedIndices[i], CachedBuilds[i]);
	});

	for (int32 i = 0; i < N; ++i)
	{
		UploadChunk(CachedIndices[i], CachedBuilds[i]);
	}
}

bool AProceduralTerrain::WorldToGrid(const FVector& World, FVector& OutGrid) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(World);
	OutGrid = Local / VoxelSize;
	return true;
}

FVector AProceduralTerrain::GridToWorld(const FVector& Grid) const
{
	return GetActorTransform().TransformPosition(Grid * VoxelSize);
}

void AProceduralTerrain::MarkRegionDirty(const FIntVector& MinCell, const FIntVector& MaxCell)
{
	for (int32 i = 0; i < Chunks.Num(); ++i)
	{
		const FChunkInfo& C = Chunks[i];
		if (C.Max.X < MinCell.X || C.Min.X > MaxCell.X) continue;
		if (C.Max.Y < MinCell.Y || C.Min.Y > MaxCell.Y) continue;
		if (C.Max.Z < MinCell.Z || C.Min.Z > MaxCell.Z) continue;
		DirtyChunks.Add(i);
	}
}

void AProceduralTerrain::MarkCellDirty(int32 x, int32 y, int32 z)
{
	const int32 cx = x / ChunkSize;
	const int32 cy = y / ChunkSize;
	const int32 cz = z / ChunkSize;
	const bool bx = (x % ChunkSize == 0) && (cx > 0);
	const bool by = (y % ChunkSize == 0) && (cy > 0);
	const bool bz = (z % ChunkSize == 0) && (cz > 0);

	auto Add = [&](int32 ax, int32 ay, int32 az)
	{
		if (ax < 0 || ay < 0 || az < 0) return;
		if (ax >= ChunkCount.X || ay >= ChunkCount.Y || az >= ChunkCount.Z) return;
		DirtyChunks.Add(ax + ChunkCount.X * (ay + ChunkCount.Y * az));
	};

	Add(cx, cy, cz);
	if (bx) Add(cx - 1, cy, cz);
	if (by) Add(cx, cy - 1, cz);
	if (bz) Add(cx, cy, cz - 1);
	if (bx && by) Add(cx - 1, cy - 1, cz);
	if (bx && bz) Add(cx - 1, cy, cz - 1);
	if (by && bz) Add(cx, cy - 1, cz - 1);
	if (bx && by && bz) Add(cx - 1, cy - 1, cz - 1);
}

void AProceduralTerrain::ResetTerrain()
{
	if (OriginalDensities.Num() != Densities.Num()) return;
	Densities = OriginalDensities;
	RebuildAllChunks();
}

void AProceduralTerrain::ApplyBrush(const FVector& WorldCenter, float Radius, float Delta)
{
	FVector GridCenter;
	WorldToGrid(WorldCenter, GridCenter);
	const float GridRadius = Radius / VoxelSize;

	const FIntVector MinCell(
		FMath::Max(1, FMath::FloorToInt(GridCenter.X - GridRadius)),
		FMath::Max(1, FMath::FloorToInt(GridCenter.Y - GridRadius)),
		FMath::Max(1, FMath::FloorToInt(GridCenter.Z - GridRadius)));
	const FIntVector MaxCell(
		FMath::Min(GridSize.X - 2, FMath::CeilToInt(GridCenter.X + GridRadius)),
		FMath::Min(GridSize.Y - 2, FMath::CeilToInt(GridCenter.Y + GridRadius)),
		FMath::Min(GridSize.Z - 2, FMath::CeilToInt(GridCenter.Z + GridRadius)));

	const float InvR = 1.f / FMath::Max(GridRadius, KINDA_SMALL_NUMBER);
	const float R2 = GridRadius * GridRadius;

	for (int32 z = MinCell.Z; z <= MaxCell.Z; ++z)
	for (int32 y = MinCell.Y; y <= MaxCell.Y; ++y)
	for (int32 x = MinCell.X; x <= MaxCell.X; ++x)
	{
		const float dx = x - GridCenter.X;
		const float dy = y - GridCenter.Y;
		const float dz = z - GridCenter.Z;
		const float D2 = dx*dx + dy*dy + dz*dz;
		if (D2 > R2) continue;
		const float Falloff = 1.f - FMath::Sqrt(D2) * InvR;
		float& V = Densities[Idx(x, y, z)];
		const float NewV = FMath::Clamp(V + Delta * Falloff, DensityClampMin, DensityClampMax);
		if (NewV == V) continue;
		V = NewV;
		MarkCellDirty(x, y, z);
	}
}

void AProceduralTerrain::ApplyFlatten(const FVector& WorldCenter, float Radius, float TargetWorldZ, float Strength)
{
	FVector GridCenter;
	WorldToGrid(WorldCenter, GridCenter);
	const float GridRadius = Radius / VoxelSize;

	FVector LocalTarget = GetActorTransform().InverseTransformPosition(FVector(WorldCenter.X, WorldCenter.Y, TargetWorldZ));
	const float TargetGridZ = LocalTarget.Z / VoxelSize;

	const FIntVector MinCell(
		FMath::Max(1, FMath::FloorToInt(GridCenter.X - GridRadius)),
		FMath::Max(1, FMath::FloorToInt(GridCenter.Y - GridRadius)),
		FMath::Max(1, FMath::FloorToInt(GridCenter.Z - GridRadius)));
	const FIntVector MaxCell(
		FMath::Min(GridSize.X - 2, FMath::CeilToInt(GridCenter.X + GridRadius)),
		FMath::Min(GridSize.Y - 2, FMath::CeilToInt(GridCenter.Y + GridRadius)),
		FMath::Min(GridSize.Z - 2, FMath::CeilToInt(GridCenter.Z + GridRadius)));

	const float InvR = 1.f / FMath::Max(GridRadius, KINDA_SMALL_NUMBER);
	const float R2 = GridRadius * GridRadius;

	for (int32 z = MinCell.Z; z <= MaxCell.Z; ++z)
	for (int32 y = MinCell.Y; y <= MaxCell.Y; ++y)
	for (int32 x = MinCell.X; x <= MaxCell.X; ++x)
	{
		const float dx = x - GridCenter.X;
		const float dy = y - GridCenter.Y;
		const float D2 = dx*dx + dy*dy;
		if (D2 > R2) continue;
		const float Falloff = 1.f - FMath::Sqrt(D2) * InvR;
		const float Target = (float)z - TargetGridZ;
		float& V = Densities[Idx(x, y, z)];
		const float NewV = FMath::Clamp(FMath::Lerp(V, Target, FMath::Clamp(Strength * Falloff, 0.f, 1.f)), DensityClampMin, DensityClampMax);
		if (NewV == V) continue;
		V = NewV;
		MarkCellDirty(x, y, z);
	}
}

void AProceduralTerrain::ApplySmooth(const FVector& WorldCenter, float Radius, float Strength)
{
	FVector GridCenter;
	WorldToGrid(WorldCenter, GridCenter);
	const float GridRadius = Radius / VoxelSize;

	const FIntVector MinCell(
		FMath::Max(1, FMath::FloorToInt(GridCenter.X - GridRadius)),
		FMath::Max(1, FMath::FloorToInt(GridCenter.Y - GridRadius)),
		FMath::Max(1, FMath::FloorToInt(GridCenter.Z - GridRadius)));
	const FIntVector MaxCell(
		FMath::Min(GridSize.X - 2, FMath::CeilToInt(GridCenter.X + GridRadius)),
		FMath::Min(GridSize.Y - 2, FMath::CeilToInt(GridCenter.Y + GridRadius)),
		FMath::Min(GridSize.Z - 2, FMath::CeilToInt(GridCenter.Z + GridRadius)));

	const float InvR = 1.f / FMath::Max(GridRadius, KINDA_SMALL_NUMBER);

	TArray<float> Snapshot;
	const int32 SX = MaxCell.X - MinCell.X + 1;
	const int32 SY = MaxCell.Y - MinCell.Y + 1;
	const int32 SZ = MaxCell.Z - MinCell.Z + 1;
	Snapshot.SetNumUninitialized(SX * SY * SZ);

	for (int32 z = MinCell.Z; z <= MaxCell.Z; ++z)
	for (int32 y = MinCell.Y; y <= MaxCell.Y; ++y)
	for (int32 x = MinCell.X; x <= MaxCell.X; ++x)
	{
		const int32 li = (x - MinCell.X) + SX * ((y - MinCell.Y) + SY * (z - MinCell.Z));
		Snapshot[li] = Densities[Idx(x, y, z)];
	}

	const float R2 = GridRadius * GridRadius;
	for (int32 z = MinCell.Z; z <= MaxCell.Z; ++z)
	for (int32 y = MinCell.Y; y <= MaxCell.Y; ++y)
	for (int32 x = MinCell.X; x <= MaxCell.X; ++x)
	{
		const float dx = x - GridCenter.X;
		const float dy = y - GridCenter.Y;
		const float dz = z - GridCenter.Z;
		const float D2 = dx*dx + dy*dy + dz*dz;
		if (D2 > R2) continue;
		const float Falloff = 1.f - FMath::Sqrt(D2) * InvR;

		const float Avg = (
			Densities[Idx(x - 1, y, z)] + Densities[Idx(x + 1, y, z)] +
			Densities[Idx(x, y - 1, z)] + Densities[Idx(x, y + 1, z)] +
			Densities[Idx(x, y, z - 1)] + Densities[Idx(x, y, z + 1)]) / 6.f;

		float& V = Densities[Idx(x, y, z)];
		const float NewV = FMath::Clamp(FMath::Lerp(V, Avg, FMath::Clamp(Strength * Falloff, 0.f, 1.f)), DensityClampMin, DensityClampMax);
		if (NewV == V) continue;
		V = NewV;
		MarkCellDirty(x, y, z);
	}
}
