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
				Densities[Idx(x, y, z)] = (float)z - SurfaceZ;
			}
		}
	}
}

void AProceduralTerrain::BuildChunks()
{
	Chunks.Reset();
	const FIntVector CountChunks(
		FMath::DivideAndRoundUp(GridSize.X - 1, ChunkSize),
		FMath::DivideAndRoundUp(GridSize.Y - 1, ChunkSize),
		FMath::DivideAndRoundUp(GridSize.Z - 1, ChunkSize));

	int32 Section = 0;
	for (int32 cz = 0; cz < CountChunks.Z; ++cz)
	for (int32 cy = 0; cy < CountChunks.Y; ++cy)
	for (int32 cx = 0; cx < CountChunks.X; ++cx)
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
	Vertices.Reserve(1024);
	Triangles.Reserve(2048);
	Normals.Reserve(1024);

	for (int32 z = Info.Min.Z; z < Info.Max.Z; ++z)
	for (int32 y = Info.Min.Y; y < Info.Max.Y; ++y)
	for (int32 x = Info.Min.X; x < Info.Max.X; ++x)
	{
		float CornerValues[8];
		FVector CornerPos[8];
		FVector CornerGrads[8];
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

		for (int32 c = 0; c < 8; ++c)
		{
			const int32 ox = x + MarchingCubes::CornerOffsets[c][0];
			const int32 oy = y + MarchingCubes::CornerOffsets[c][1];
			const int32 oz = z + MarchingCubes::CornerOffsets[c][2];
			CornerGrads[c] = GradientAtCorner(ox, oy, oz);
		}

		FVector EdgeVerts[12];
		FVector EdgeNormals[12];
		for (int32 e = 0; e < 12; ++e)
		{
			if (!(Edges & (1 << e))) continue;
			const int32 a = MarchingCubes::EdgeConnection[e][0];
			const int32 b = MarchingCubes::EdgeConnection[e][1];
			const float va = CornerValues[a];
			const float vb = CornerValues[b];
			const float denom = vb - va;
			const float t = FMath::IsNearlyZero(denom) ? 0.5f : (-va / denom);
			EdgeVerts[e] = CornerPos[a] + (CornerPos[b] - CornerPos[a]) * t;
			EdgeNormals[e] = FMath::Lerp(CornerGrads[a], CornerGrads[b], t).GetSafeNormal();
		}

		for (int32 i = 0; i < 16; i += 3)
		{
			const int8 i0 = MarchingCubes::TriTable[CubeIndex][i];
			if (i0 == -1) break;
			const int8 i1 = MarchingCubes::TriTable[CubeIndex][i + 1];
			const int8 i2 = MarchingCubes::TriTable[CubeIndex][i + 2];

			const int32 Base = Vertices.Num();
			Vertices.Add(EdgeVerts[i0]);
			Vertices.Add(EdgeVerts[i1]);
			Vertices.Add(EdgeVerts[i2]);

			Normals.Add(EdgeNormals[i0]);
			Normals.Add(EdgeNormals[i1]);
			Normals.Add(EdgeNormals[i2]);

			Triangles.Add(Base);
			Triangles.Add(Base + 1);
			Triangles.Add(Base + 2);
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

	MeshComponent->CreateMeshSection(Info.SectionIndex, Data.Vertices, Data.Triangles, Data.Normals, EmptyUV, EmptyColors, EmptyTangents, true);
	Info.bCreated = true;
}

void AProceduralTerrain::FlushDirtyChunks()
{
	const int32 N = DirtyChunks.Num();
	if (N == 0) return;

	TArray<int32> Indices;
	Indices.Reserve(N);
	for (int32 Idx : DirtyChunks) Indices.Add(Idx);
	DirtyChunks.Reset();

	TArray<FChunkBuildData> Builds;
	Builds.SetNum(N);
	ParallelFor(N, [this, &Indices, &Builds](int32 i)
	{
		BuildChunkData(Indices[i], Builds[i]);
	});

	for (int32 i = 0; i < N; ++i)
	{
		UploadChunk(Indices[i], Builds[i]);
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
		FMath::Max(0, FMath::FloorToInt(GridCenter.X - GridRadius)),
		FMath::Max(0, FMath::FloorToInt(GridCenter.Y - GridRadius)),
		FMath::Max(0, FMath::FloorToInt(GridCenter.Z - GridRadius)));
	const FIntVector MaxCell(
		FMath::Min(GridSize.X - 1, FMath::CeilToInt(GridCenter.X + GridRadius)),
		FMath::Min(GridSize.Y - 1, FMath::CeilToInt(GridCenter.Y + GridRadius)),
		FMath::Min(GridSize.Z - 1, FMath::CeilToInt(GridCenter.Z + GridRadius)));

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
		Densities[Idx(x, y, z)] += Delta * Falloff;
	}

	MarkRegionDirty(MinCell, MaxCell);
}

void AProceduralTerrain::ApplyFlatten(const FVector& WorldCenter, float Radius, float TargetWorldZ, float Strength)
{
	FVector GridCenter;
	WorldToGrid(WorldCenter, GridCenter);
	const float GridRadius = Radius / VoxelSize;

	FVector LocalTarget = GetActorTransform().InverseTransformPosition(FVector(WorldCenter.X, WorldCenter.Y, TargetWorldZ));
	const float TargetGridZ = LocalTarget.Z / VoxelSize;

	const FIntVector MinCell(
		FMath::Max(0, FMath::FloorToInt(GridCenter.X - GridRadius)),
		FMath::Max(0, FMath::FloorToInt(GridCenter.Y - GridRadius)),
		FMath::Max(0, FMath::FloorToInt(GridCenter.Z - GridRadius)));
	const FIntVector MaxCell(
		FMath::Min(GridSize.X - 1, FMath::CeilToInt(GridCenter.X + GridRadius)),
		FMath::Min(GridSize.Y - 1, FMath::CeilToInt(GridCenter.Y + GridRadius)),
		FMath::Min(GridSize.Z - 1, FMath::CeilToInt(GridCenter.Z + GridRadius)));

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
		V = FMath::Lerp(V, Target, FMath::Clamp(Strength * Falloff, 0.f, 1.f));
	}

	MarkRegionDirty(MinCell, MaxCell);
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
		V = FMath::Lerp(V, Avg, FMath::Clamp(Strength * Falloff, 0.f, 1.f));
	}

	MarkRegionDirty(MinCell, MaxCell);
}
