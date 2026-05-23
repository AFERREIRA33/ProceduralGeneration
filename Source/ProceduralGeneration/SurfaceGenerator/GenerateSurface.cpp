#include "GenerateSurface.h"
#include"ProceduralGeneration/Utils/FastNoiseLite.h"
#include "ProceduralMeshComponent.h"
#include "Async/ParallelFor.h"

static const float kVoxelScale = 100.f;

AGenerateSurface::AGenerateSurface()
{
	PrimaryActorTick.bCanEverTick = true;
	if (Mesh)
	{
		Mesh->bUseAsyncCooking = true;
		Mesh->bUseComplexAsSimpleCollision = true;
	}
}

void AGenerateSurface::StartGeneration()
{
	Noise->SetSeed(Seed);
	Noise->SetFrequency(Frequency);
	Noise->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	Noise->SetFractalType(FastNoiseLite::FractalType_FBm);
	Noise->SetFractalOctaves(FractalOctaves);
	Noise->SetFractalLacunarity(FractalLacunarity);
	Noise->SetFractalGain(FractalGain);

	GenerationType = SetGenerationType();
	Setup();
	Generate2DHeightMap(GetActorLocation() / kVoxelScale);

	if (Material) Mesh->SetMaterial(0, Material);

	BuildSubChunks();
	RebuildAllSubChunks();
}

void AGenerateSurface::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (DirtySubChunks.Num() == 0) return;
	RemeshAccumulator += DeltaTime;
	if (RemeshAccumulator < RemeshInterval) return;
	RemeshAccumulator = 0.f;
	FlushDirtySubChunks();
}

void AGenerateSurface::BuildSubChunks()
{
	SubChunks.Reset();
	SubChunkCount = FIntVector(
		FMath::DivideAndRoundUp(Size, SubChunkSize),
		FMath::DivideAndRoundUp(Size, SubChunkSize),
		FMath::DivideAndRoundUp(Size, SubChunkSize));

	int32 Section = 0;
	for (int32 cz = 0; cz < SubChunkCount.Z; ++cz)
	for (int32 cy = 0; cy < SubChunkCount.Y; ++cy)
	for (int32 cx = 0; cx < SubChunkCount.X; ++cx)
	{
		FSubChunk Info;
		Info.Min = FIntVector(cx * SubChunkSize, cy * SubChunkSize, cz * SubChunkSize);
		Info.Max = FIntVector(
			FMath::Min(Info.Min.X + SubChunkSize, Size),
			FMath::Min(Info.Min.Y + SubChunkSize, Size),
			FMath::Min(Info.Min.Z + SubChunkSize, Size));
		Info.SectionIndex = Section++;
		Info.bCreated = false;
		SubChunks.Add(Info);
	}
}

void AGenerateSurface::RebuildAllSubChunks()
{
	Mesh->ClearAllMeshSections();

	const int32 N = SubChunks.Num();
	if (CachedBuilds.Num() < N) CachedBuilds.SetNum(N);

	ParallelFor(N, [this](int32 i)
	{
		BuildSubChunkData(i, CachedBuilds[i]);
	});

	for (int32 i = 0; i < N; ++i)
	{
		UploadSubChunk(i, CachedBuilds[i]);
	}

	if (Material) Mesh->SetMaterial(0, Material);
	Mesh->SetCastShadow(true);
}

FVector AGenerateSurface::GradientAtCorner(int32 x, int32 y, int32 z) const
{
	const int32 xm = FMath::Max(0, x - 1);
	const int32 xp = FMath::Min(Size, x + 1);
	const int32 ym = FMath::Max(0, y - 1);
	const int32 yp = FMath::Min(Size, y + 1);
	const int32 zm = FMath::Max(0, z - 1);
	const int32 zp = FMath::Min(Size, z + 1);
	return FVector(
		Voxels[GetVoxelIndex(xp, y, z)] - Voxels[GetVoxelIndex(xm, y, z)],
		Voxels[GetVoxelIndex(x, yp, z)] - Voxels[GetVoxelIndex(x, ym, z)],
		Voxels[GetVoxelIndex(x, y, zp)] - Voxels[GetVoxelIndex(x, y, zm)]);
}

void AGenerateSurface::BuildSubChunkData(int32 Idx, FSubChunkBuildData& Out) const
{
	const FSubChunk& Info = SubChunks[Idx];

	Out.Vertices.Reset();
	Out.Triangles.Reset();
	Out.Normals.Reset();
	Out.UV0.Reset();
	Out.Colors.Reset();

	const int32 EstReserve = SubChunkSize * SubChunkSize * SubChunkSize * 3;
	Out.Vertices.Reserve(EstReserve);
	Out.Triangles.Reserve(EstReserve);
	Out.Normals.Reserve(EstReserve);
	Out.UV0.Reserve(EstReserve);
	Out.Colors.Reserve(EstReserve);

	int32 VertexCounter = 0;

	for (int32 z = Info.Min.Z; z < Info.Max.Z; ++z)
	for (int32 y = Info.Min.Y; y < Info.Max.Y; ++y)
	for (int32 x = Info.Min.X; x < Info.Max.X; ++x)
	{
		float CornerValues[8];
		FVector CornerPos[8];
		int32 CubeIndex = 0;
		for (int32 c = 0; c < 8; ++c)
		{
			const int32 ox = x + VertexOffset[c][0];
			const int32 oy = y + VertexOffset[c][1];
			const int32 oz = z + VertexOffset[c][2];
			CornerValues[c] = Voxels[GetVoxelIndex(ox, oy, oz)];
			CornerPos[c] = FVector(ox, oy, oz) * kVoxelScale;
			if (CornerValues[c] < 0.f) CubeIndex |= (1 << c);
		}

		const int32 Edges = CubeEdgeFlags[CubeIndex];
		if (Edges == 0) continue;

		FVector CornerGrads[8];
		for (int32 c = 0; c < 8; ++c)
		{
			const int32 ox = x + VertexOffset[c][0];
			const int32 oy = y + VertexOffset[c][1];
			const int32 oz = z + VertexOffset[c][2];
			CornerGrads[c] = GradientAtCorner(ox, oy, oz);
		}

		FVector EdgeVerts[12];
		FVector EdgeNormals[12];
		for (int32 e = 0; e < 12; ++e)
		{
			if (!(Edges & (1 << e))) continue;
			const int32 a = EdgeConnection[e][0];
			const int32 b = EdgeConnection[e][1];
			const float va = CornerValues[a];
			const float vb = CornerValues[b];
			const float denom = vb - va;
			const float t = FMath::IsNearlyZero(denom) ? 0.5f : (-va / denom);
			EdgeVerts[e] = CornerPos[a] + (CornerPos[b] - CornerPos[a]) * t;
			EdgeNormals[e] = FMath::Lerp(CornerGrads[a], CornerGrads[b], t).GetSafeNormal();
		}

		for (int32 i = 0; i < 16; i += 3)
		{
			const int t0 = TriangleConnectionTable[CubeIndex][i];
			if (t0 == -1) break;
			const int t1 = TriangleConnectionTable[CubeIndex][i + 1];
			const int t2 = TriangleConnectionTable[CubeIndex][i + 2];

			const FVector A = EdgeVerts[t0];
			const FVector B = EdgeVerts[t1];
			const FVector C = EdgeVerts[t2];
			const FVector Na = EdgeNormals[t0];
			const FVector Nb = EdgeNormals[t1];
			const FVector Nc = EdgeNormals[t2];

			const FVector FaceNormal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
			const FColor FaceColor = GetVertexColor(A, FaceNormal);
			const FVector2D UVa = GetUV(A, FaceNormal);
			const FVector2D UVb = GetUV(B, FaceNormal);
			const FVector2D UVc = GetUV(C, FaceNormal);

			Out.Vertices.Add(A);
			Out.Vertices.Add(B);
			Out.Vertices.Add(C);
			Out.Normals.Add(Na);
			Out.Normals.Add(Nb);
			Out.Normals.Add(Nc);
			Out.Triangles.Add(VertexCounter + 0);
			Out.Triangles.Add(VertexCounter + 1);
			Out.Triangles.Add(VertexCounter + 2);
			VertexCounter += 3;

			Out.UV0.Add(UVa);
			Out.UV0.Add(UVb);
			Out.UV0.Add(UVc);
			Out.Colors.Add(FaceColor);
			Out.Colors.Add(FaceColor);
			Out.Colors.Add(FaceColor);
		}
	}
}

void AGenerateSurface::UploadSubChunk(int32 Idx, const FSubChunkBuildData& Data)
{
	FSubChunk& Info = SubChunks[Idx];
	static const TArray<FProcMeshTangent> EmptyTangents;

	if (Data.Vertices.Num() == 0)
	{
		if (Info.bCreated)
		{
			Mesh->ClearMeshSection(Info.SectionIndex);
		}
		return;
	}

	const bool bWithCollision = !(bDeferCollisionDuringEdit && bInEditStroke);
	Mesh->CreateMeshSection(Info.SectionIndex, Data.Vertices, Data.Triangles, Data.Normals, Data.UV0, Data.Colors, EmptyTangents, bWithCollision);
	if (Material) Mesh->SetMaterial(Info.SectionIndex, Material);
	Info.bCreated = true;

	if (bWithCollision)
	{
		CollisionPendingSubChunks.Remove(Idx);
	}
	else
	{
		CollisionPendingSubChunks.Add(Idx);
	}
}

void AGenerateSurface::FlushDirtySubChunks()
{
	const int32 N = DirtySubChunks.Num();
	if (N == 0) return;

	CachedIndices.Reset(N);
	for (int32 Idx : DirtySubChunks) CachedIndices.Add(Idx);
	DirtySubChunks.Reset();

	if (CachedBuilds.Num() < N) CachedBuilds.SetNum(N);

	ParallelFor(N, [this](int32 i)
	{
		BuildSubChunkData(CachedIndices[i], CachedBuilds[i]);
	});

	for (int32 i = 0; i < N; ++i)
	{
		UploadSubChunk(CachedIndices[i], CachedBuilds[i]);
	}
}

void AGenerateSurface::MarkVoxelDirty(int32 x, int32 y, int32 z)
{
	const int32 cx = x / SubChunkSize;
	const int32 cy = y / SubChunkSize;
	const int32 cz = z / SubChunkSize;
	const bool bx = (x % SubChunkSize == 0) && (cx > 0);
	const bool by = (y % SubChunkSize == 0) && (cy > 0);
	const bool bz = (z % SubChunkSize == 0) && (cz > 0);

	auto Add = [&](int32 ax, int32 ay, int32 az)
	{
		if (ax < 0 || ay < 0 || az < 0) return;
		if (ax >= SubChunkCount.X || ay >= SubChunkCount.Y || az >= SubChunkCount.Z) return;
		DirtySubChunks.Add(ax + SubChunkCount.X * (ay + SubChunkCount.Y * az));
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

void AGenerateSurface::Setup()
{
	const int Dim = Size + 1;
	Voxels.SetNumZeroed(Dim * Dim * Dim);
	HumidityNoiseValues.SetNumZeroed(Dim * Dim);
	TemperatureNoiseValues.SetNumZeroed(Dim * Dim);
	float sampleHeight = 0.0f;
	// Example 2D height sampling (fill your own noise sampling)
	for (int x = 0; x < Dim; ++x)
	{
		for (int y = 0; y < Dim; ++y)
		{
			// compute height in world units (use your noise, offset by actor/world position)
			for (int z = 0; z < Dim; ++z)
			{
				// store signed distance relative to SurfaceLevel (marching uses iso = 0)
				float value = z - sampleHeight - SurfaceLevel;
				Voxels[GetVoxelIndex(x, y, z)] = value;
				
			}
		}
	}
}
void AGenerateSurface::Generate2DHeightMap(FVector Position)
{
	UE_LOG(LogTemp, Warning, TEXT("Generating 2D Height Map at Position: %f, %f, %f"), Position.X, Position.Y, Position.Z);
	Voxels.SetNum((Size + 1) * (Size + 1) * (Size + 1));
    
	for (int x = 0; x <= Size; ++x)
	{
		for (int y = 0; y <= Size; ++y)
		{
			if (x ==0 && y == 0)
				UE_LOG( LogTemp, Warning, TEXT("Generating voxel column at x: %f, y: %f"), ((Position.X ) + x), (Position.Y) + y);
			// Get 2D noise height at this x,y position
			float noiseHeight = Noise->GetNoise(
				(Position.X ) + x, 
				(Position.Y ) + y
			);
			HumidityNoiseValues[x + y * (Size + 1)] = Noise->GetNoise(
				(Position.X + HumidityPos) + x, 
				(Position.Y + HumidityPos) + y
			);
			TemperatureNoiseValues[x + y * (Size + 1)] = Noise->GetNoise(
				(Position.X + TemperaturePos) + x, 
				(Position.Y + TemperaturePos) + y
			);
			// Scale noise to useful range (e.g., 0 to Size)
			float terrainHeight = (noiseHeight + 1.0f) * 0.5f * Size * HeightScale + HeightOffset;
            
			for (int z = 0; z <= Size; z++)
			{
				int index = GetVoxelIndex(x, y, z);
				// Voxel value: negative = solid, positive = air
				// Distance from terrain surface
				Voxels[index] = z - terrainHeight;
			}
		}
	}
}

ProceduralGenerationType AGenerateSurface::SetGenerationType()
{
	return ProceduralGenerationType::GT_2D;
}

int AGenerateSurface::GetVoxelIndex(const int X, const int Y, const int Z) const
{
	return X + Y * (Size + 1) + Z * (Size + 1) * (Size + 1);
}

void AGenerateSurface::GenerateMesh()
{
	// Clear previous mesh arrays (Vertices, Triangles, Normals, UVs, etc.)
	MeshData.Clear();
	VertexCount = 0;

	// March over cubes (Size cubes along each axis)
	for (int x = 0; x < Size; ++x)
	{
		for (int y = 0; y < Size; ++y)
		{
			for (int z = 0; z < Size; ++z)
			{
				TArray<float> cubeValues;
				cubeValues.SetNumUninitialized(8);
				for (int i = 0; i < 8; ++i)
				{
					int vx = x + VertexOffset[i][0];
					int vy = y + VertexOffset[i][1];
					int vz = z + VertexOffset[i][2];
					cubeValues[i] = Voxels[GetVoxelIndex(vx, vy, vz)];
				}
				March(x, y, z, cubeValues);
			}
		}
	}

	// After building vertices/triangles update procedural mesh component (ApplyMesh)
}


void AGenerateSurface::March(const int X, const int Y, const int Z, TArray<float> Cube)
{
	// Compute cubeIndex using iso=0
	int cubeIndex = 0;
	for (int i = 0; i < 8; ++i)
	{
		if (Cube[i] < 0.0f) // inside
			cubeIndex |= (1 << i);
	}

	int edgeFlags = CubeEdgeFlags[cubeIndex];
	if (edgeFlags == 0) return;

	// For each edge, compute interpolated vertex
	FVector edgeVertex[12];
	const float VoxelScale = 100.0f; // match your chunk/world scale
	for (int i = 0; i < 12; ++i)
	{
		if (edgeFlags & (1 << i))
		{
			int v1 = EdgeConnection[i][0];
			int v2 = EdgeConnection[i][1];

			FVector p1 = FVector(X + VertexOffset[v1][0], Y + VertexOffset[v1][1], Z + VertexOffset[v1][2]);
			FVector p2 = FVector(X + VertexOffset[v2][0], Y + VertexOffset[v2][1], Z + VertexOffset[v2][2]);

			float t = GetInterpolationOffset(Cube[v1], Cube[v2]);
			FVector p = FMath::Lerp(p1, p2, t) * VoxelScale;
			edgeVertex[i] = p;
			
		}
	}

	// Build triangles from TriangleConnectionTable
	for (int t = 0; t < 16; t += 3)
	{
		int idx0 = TriangleConnectionTable[cubeIndex][t + 0];
		if (idx0 < 0) break;
		int idx1 = TriangleConnectionTable[cubeIndex][t + 1];
		int idx2 = TriangleConnectionTable[cubeIndex][t + 2];

		FVector a = edgeVertex[idx0];
		FVector b = edgeVertex[idx1];
		FVector c = edgeVertex[idx2];

		MeshData.Vertices.Add(a);
		MeshData.Vertices.Add(b);
		MeshData.Vertices.Add(c);

		MeshData.Triangles.Add(VertexCount + 0);
		MeshData.Triangles.Add(VertexCount + 1);
		MeshData.Triangles.Add(VertexCount + 2);
		FVector VectorAB = b-a;
		FVector VectorAC = c-a;
		FVector normal = FVector::CrossProduct(VectorAB, VectorAC).GetSafeNormal();
		MeshData.Normals.Add(normal);
		MeshData.Normals.Add(normal);
		MeshData.Normals.Add(normal); 
		VertexCount += 3;

		MeshData.UV0.Add(GetUV(a, normal));
		MeshData.UV0.Add(GetUV(b, normal));
		MeshData.UV0.Add(GetUV(c, normal));

		MeshData.Colors.Add(GetVertexColor(a, normal));
		MeshData.Colors.Add(GetVertexColor(b, normal));
		MeshData.Colors.Add(GetVertexColor(c, normal));
	}
}
FVector2D AGenerateSurface::GetUV(FVector Position, FVector Normal) const
{
	// Scale for texture tiling (smaller = more repetition/detail)
	const float UVScale = 0.005f; // Adjust to match texture resolution

	// Triplanar projection for seamless mapping on any surface angle
	FVector absNormal = Normal.GetAbs();

	if (absNormal.Z >= absNormal.X && absNormal.Z >= absNormal.Y)
	{
		// Top/bottom face - use X,Y
		return FVector2D(Position.X, Position.Y) * UVScale;
	}
	else if (absNormal.X >= absNormal.Y)
	{
		// Side face - use Y,Z
		return FVector2D(Position.Y, Position.Z) * UVScale;
	}
	else
	{
		// Front/back face - use X,Z
		return FVector2D(Position.X, Position.Z) * UVScale;
	}
}

FColor AGenerateSurface::GetVertexColor(FVector Position, FVector Normal) const
{
	const float VoxelHeight = Position.Z / 100.0f;
	if (VoxelHeight <= SeaLevel)
	{
		return FColor(32, 96, 160);
	}

	const float Slope = 1.0f - FMath::Clamp(FVector::DotProduct(Normal, FVector::UpVector), 0.0f, 1.0f);
	if (Slope > 0.45f)
	{
		return FColor(96, 96, 96);
	}

	return FColor(70, 130, 60);
}

float AGenerateSurface::GetInterpolationOffset(const float V1, const float V2) const
{
	float PV1 = V1;
	const float PV2 = V2;
	const float Delta = PV2 - PV1;
	if (FMath::IsNearlyZero(Delta))
	{
		return 0.5f;
	}
	return - PV1 / Delta;
}

void AGenerateSurface::ModifyVoxelData(FVector Position)
{
	const int Index = GetVoxelIndex(
		static_cast<int>(Position.X),
		static_cast<int>(Position.Y),
		static_cast<int>(Position.Z)
	);

	if (Index >= 0 && Index < Voxels.Num())
	{
		// Toggle voxel - flip above/below surface threshold
		Voxels[Index] = Voxels[Index] > SurfaceLevel ? SurfaceLevel - 1.0f : SurfaceLevel + 1.0f;
	}
}

FBox AGenerateSurface::GetWorldAABB() const
{
	const FVector Origin = GetActorLocation();
	const float WorldSize = Size * kVoxelScale;
	return FBox(Origin, Origin + FVector(WorldSize, WorldSize, WorldSize));
}

void AGenerateSurface::BeginEditStroke()
{
	bInEditStroke = true;
}

void AGenerateSurface::EndEditStroke()
{
	bInEditStroke = false;
	bDirtyDuringStroke = false;

	if (CollisionPendingSubChunks.Num() == 0) return;

	const int32 N = CollisionPendingSubChunks.Num();
	CachedIndices.Reset(N);
	for (int32 Idx : CollisionPendingSubChunks) CachedIndices.Add(Idx);
	CollisionPendingSubChunks.Reset();

	if (CachedBuilds.Num() < N) CachedBuilds.SetNum(N);

	ParallelFor(N, [this](int32 i)
	{
		BuildSubChunkData(CachedIndices[i], CachedBuilds[i]);
	});

	for (int32 i = 0; i < N; ++i)
	{
		UploadSubChunk(CachedIndices[i], CachedBuilds[i]);
	}
}

void AGenerateSurface::ResetToOriginal()
{
	if (OriginalVoxels.Num() != Voxels.Num()) return;
	Voxels = OriginalVoxels;
	DirtySubChunks.Reset();
	RebuildAllSubChunks();
}

float AGenerateSurface::SampleDensityTrilinear(float lx, float ly, float lz) const
{
	const int32 x0 = FMath::FloorToInt(lx);
	const int32 y0 = FMath::FloorToInt(ly);
	const int32 z0 = FMath::FloorToInt(lz);
	if (x0 < 0 || y0 < 0 || z0 < 0) return 1.f;
	if (x0 >= Size || y0 >= Size || z0 >= Size) return 1.f;

	const float tx = lx - x0;
	const float ty = ly - y0;
	const float tz = lz - z0;

	const float c000 = Voxels[GetVoxelIndex(x0,     y0,     z0)];
	const float c100 = Voxels[GetVoxelIndex(x0 + 1, y0,     z0)];
	const float c010 = Voxels[GetVoxelIndex(x0,     y0 + 1, z0)];
	const float c110 = Voxels[GetVoxelIndex(x0 + 1, y0 + 1, z0)];
	const float c001 = Voxels[GetVoxelIndex(x0,     y0,     z0 + 1)];
	const float c101 = Voxels[GetVoxelIndex(x0 + 1, y0,     z0 + 1)];
	const float c011 = Voxels[GetVoxelIndex(x0,     y0 + 1, z0 + 1)];
	const float c111 = Voxels[GetVoxelIndex(x0 + 1, y0 + 1, z0 + 1)];

	const float c00 = FMath::Lerp(c000, c100, tx);
	const float c10 = FMath::Lerp(c010, c110, tx);
	const float c01 = FMath::Lerp(c001, c101, tx);
	const float c11 = FMath::Lerp(c011, c111, tx);

	const float c0 = FMath::Lerp(c00, c10, ty);
	const float c1 = FMath::Lerp(c01, c11, ty);

	return FMath::Lerp(c0, c1, tz);
}

bool AGenerateSurface::TraceDensityField(const FVector& WorldStart, const FVector& WorldDir, float MaxDist, FVector& OutHitPoint, FVector& OutNormal) const
{
	const FVector LocalStart = (WorldStart - GetActorLocation()) / kVoxelScale;
	const FVector LocalDir = WorldDir.GetSafeNormal();

	const float StepGrid = 0.5f;
	const float StepWorld = StepGrid * kVoxelScale;
	const int32 MaxSteps = FMath::CeilToInt(MaxDist / StepWorld);

	float PrevD = SampleDensityTrilinear(LocalStart.X, LocalStart.Y, LocalStart.Z);
	FVector PrevL = LocalStart;

	for (int32 i = 1; i <= MaxSteps; ++i)
	{
		const FVector L = LocalStart + LocalDir * (StepGrid * i);
		const float D = SampleDensityTrilinear(L.X, L.Y, L.Z);

		if (PrevD > 0.f && D <= 0.f)
		{
			const float denom = PrevD - D;
			const float t = FMath::IsNearlyZero(denom) ? 0.5f : PrevD / denom;
			const FVector HitL = FMath::Lerp(PrevL, L, t);
			OutHitPoint = GetActorLocation() + HitL * kVoxelScale;

			const float h = 0.5f;
			OutNormal = FVector(
				SampleDensityTrilinear(HitL.X + h, HitL.Y, HitL.Z) - SampleDensityTrilinear(HitL.X - h, HitL.Y, HitL.Z),
				SampleDensityTrilinear(HitL.X, HitL.Y + h, HitL.Z) - SampleDensityTrilinear(HitL.X, HitL.Y - h, HitL.Z),
				SampleDensityTrilinear(HitL.X, HitL.Y, HitL.Z + h) - SampleDensityTrilinear(HitL.X, HitL.Y, HitL.Z - h)
			).GetSafeNormal();
			return true;
		}
		PrevD = D;
		PrevL = L;
	}
	return false;
}

void AGenerateSurface::ApplyBrush(const FVector& WorldCenter, float Radius, float Delta)
{
	if (OriginalVoxels.Num() == 0)
	{
		OriginalVoxels = Voxels;
	}

	const FVector LocalCenter = (WorldCenter - GetActorLocation()) / kVoxelScale;
	const float LocalRadius = Radius / kVoxelScale;
	const float R2 = LocalRadius * LocalRadius;
	const float InvR = 1.f / FMath::Max(LocalRadius, KINDA_SMALL_NUMBER);

	const int32 MinX = FMath::Max(0, FMath::FloorToInt(LocalCenter.X - LocalRadius));
	const int32 MinY = FMath::Max(0, FMath::FloorToInt(LocalCenter.Y - LocalRadius));
	const int32 MinZ = FMath::Max(0, FMath::FloorToInt(LocalCenter.Z - LocalRadius));
	const int32 MaxX = FMath::Min(Size, FMath::CeilToInt(LocalCenter.X + LocalRadius));
	const int32 MaxY = FMath::Min(Size, FMath::CeilToInt(LocalCenter.Y + LocalRadius));
	const int32 MaxZ = FMath::Min(Size, FMath::CeilToInt(LocalCenter.Z + LocalRadius));

	for (int32 z = MinZ; z <= MaxZ; ++z)
	for (int32 y = MinY; y <= MaxY; ++y)
	for (int32 x = MinX; x <= MaxX; ++x)
	{
		const float dx = x - LocalCenter.X;
		const float dy = y - LocalCenter.Y;
		const float dz = z - LocalCenter.Z;
		const float D2 = dx*dx + dy*dy + dz*dz;
		if (D2 > R2) continue;
		const float Falloff = 1.f - FMath::Sqrt(D2) * InvR;
		float& V = Voxels[GetVoxelIndex(x, y, z)];
		const float NewV = FMath::Clamp(V + Delta * Falloff, DensityClampMin, DensityClampMax);
		if (NewV == V) continue;
		V = NewV;
		MarkVoxelDirty(x, y, z);
	}
}

void AGenerateSurface::ApplyFlatten(const FVector& WorldCenter, float Radius, float TargetWorldZ, float Strength)
{
	if (OriginalVoxels.Num() == 0)
	{
		OriginalVoxels = Voxels;
	}

	const FVector LocalCenter = (WorldCenter - GetActorLocation()) / kVoxelScale;
	const float LocalTargetZ = (TargetWorldZ - GetActorLocation().Z) / kVoxelScale;
	const float LocalRadius = Radius / kVoxelScale;
	const float R2 = LocalRadius * LocalRadius;
	const float InvR = 1.f / FMath::Max(LocalRadius, KINDA_SMALL_NUMBER);

	const int32 MinX = FMath::Max(0, FMath::FloorToInt(LocalCenter.X - LocalRadius));
	const int32 MinY = FMath::Max(0, FMath::FloorToInt(LocalCenter.Y - LocalRadius));
	const int32 MinZ = FMath::Max(0, FMath::FloorToInt(LocalCenter.Z - LocalRadius));
	const int32 MaxX = FMath::Min(Size, FMath::CeilToInt(LocalCenter.X + LocalRadius));
	const int32 MaxY = FMath::Min(Size, FMath::CeilToInt(LocalCenter.Y + LocalRadius));
	const int32 MaxZ = FMath::Min(Size, FMath::CeilToInt(LocalCenter.Z + LocalRadius));

	for (int32 z = MinZ; z <= MaxZ; ++z)
	for (int32 y = MinY; y <= MaxY; ++y)
	for (int32 x = MinX; x <= MaxX; ++x)
	{
		const float dx = x - LocalCenter.X;
		const float dy = y - LocalCenter.Y;
		const float D2 = dx*dx + dy*dy;
		if (D2 > R2) continue;
		const float Falloff = 1.f - FMath::Sqrt(D2) * InvR;
		const float Target = (float)z - LocalTargetZ;
		float& V = Voxels[GetVoxelIndex(x, y, z)];
		const float NewV = FMath::Clamp(FMath::Lerp(V, Target, FMath::Clamp(Strength * Falloff, 0.f, 1.f)), DensityClampMin, DensityClampMax);
		if (NewV == V) continue;
		V = NewV;
		MarkVoxelDirty(x, y, z);
	}
}

void AGenerateSurface::ApplySmooth(const FVector& WorldCenter, float Radius, float Strength)
{
	if (OriginalVoxels.Num() == 0)
	{
		OriginalVoxels = Voxels;
	}

	const FVector LocalCenter = (WorldCenter - GetActorLocation()) / kVoxelScale;
	const float LocalRadius = Radius / kVoxelScale;
	const float R2 = LocalRadius * LocalRadius;
	const float InvR = 1.f / FMath::Max(LocalRadius, KINDA_SMALL_NUMBER);

	const int32 MinX = FMath::Max(1, FMath::FloorToInt(LocalCenter.X - LocalRadius));
	const int32 MinY = FMath::Max(1, FMath::FloorToInt(LocalCenter.Y - LocalRadius));
	const int32 MinZ = FMath::Max(1, FMath::FloorToInt(LocalCenter.Z - LocalRadius));
	const int32 MaxX = FMath::Min(Size - 1, FMath::CeilToInt(LocalCenter.X + LocalRadius));
	const int32 MaxY = FMath::Min(Size - 1, FMath::CeilToInt(LocalCenter.Y + LocalRadius));
	const int32 MaxZ = FMath::Min(Size - 1, FMath::CeilToInt(LocalCenter.Z + LocalRadius));

	for (int32 z = MinZ; z <= MaxZ; ++z)
	for (int32 y = MinY; y <= MaxY; ++y)
	for (int32 x = MinX; x <= MaxX; ++x)
	{
		const float dx = x - LocalCenter.X;
		const float dy = y - LocalCenter.Y;
		const float dz = z - LocalCenter.Z;
		const float D2 = dx*dx + dy*dy + dz*dz;
		if (D2 > R2) continue;
		const float Falloff = 1.f - FMath::Sqrt(D2) * InvR;

		const float Avg = (
			Voxels[GetVoxelIndex(x - 1, y, z)] + Voxels[GetVoxelIndex(x + 1, y, z)] +
			Voxels[GetVoxelIndex(x, y - 1, z)] + Voxels[GetVoxelIndex(x, y + 1, z)] +
			Voxels[GetVoxelIndex(x, y, z - 1)] + Voxels[GetVoxelIndex(x, y, z + 1)]) / 6.f;

		float& V = Voxels[GetVoxelIndex(x, y, z)];
		const float NewV = FMath::Clamp(FMath::Lerp(V, Avg, FMath::Clamp(Strength * Falloff, 0.f, 1.f)), DensityClampMin, DensityClampMax);
		if (NewV == V) continue;
		V = NewV;
		MarkVoxelDirty(x, y, z);
	}
}
