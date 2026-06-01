#include "GenerateSurface.h"
#include"ProceduralGeneration/Utils/FastNoiseLite.h"
#include "ProceduralMeshComponent.h"
#include "Async/ParallelFor.h"
#include "Async/Async.h"
#include "ProceduralGeneration/Utils/TransvoxelTables.h"

static const float kVoxelScale = 100.f;

AGenerateSurface::AGenerateSurface()
{
	PrimaryActorTick.bCanEverTick = true;
	if (Mesh)
	{
		Mesh->bUseAsyncCooking = true;
		Mesh->bUseComplexAsSimpleCollision = true;
		Mesh->SetCanEverAffectNavigation(false);
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

	if (!BiomeNoise.IsValid()) BiomeNoise = MakeUnique<FastNoiseLite>();
	BiomeNoise->SetSeed(Seed + 777);
	BiomeNoise->SetFrequency(BiomeFrequency);
	BiomeNoise->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	BiomeNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
	BiomeNoise->SetFractalOctaves(1);

	if (!ContinentNoise.IsValid()) ContinentNoise = MakeUnique<FastNoiseLite>();
	ContinentNoise->SetSeed(Seed + 4242);
	ContinentNoise->SetFrequency(ContinentFrequency);
	ContinentNoise->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	ContinentNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
	ContinentNoise->SetFractalOctaves(2);

	if (!RiverNoise.IsValid()) RiverNoise = MakeUnique<FastNoiseLite>();
	RiverNoise->SetSeed(Seed + 9001);
	RiverNoise->SetFrequency(RiverFrequency);
	RiverNoise->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	RiverNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
	RiverNoise->SetFractalOctaves(2);

	GenerationType = SetGenerationType();
	Setup();

	const double T0 = FPlatformTime::Seconds();
	Generate2DHeightMap(GetActorLocation() / kVoxelScale);
	const double T1 = FPlatformTime::Seconds();

	if (Material) Mesh->SetMaterial(0, Material);

	BuildSubChunks();
	RebuildAllSubChunks();
	const double T2 = FPlatformTime::Seconds();

	UE_LOG(LogTemp, Display, TEXT("[PERF CHUNK] voxels=%.2fms mesh=%.2fms total=%.2fms collision=%d"),
		(T1 - T0) * 1000.0, (T2 - T1) * 1000.0, (T2 - T0) * 1000.0, bCollisionEnabled ? 1 : 0);
}

void AGenerateSurface::SetPendingCaveData(TArray<FCaveCarveOp>&& Ops, float MinRoof)
{
	PendingCaveOps = MoveTemp(Ops);
	PendingCaveMinRoof = MinRoof;
}

void AGenerateSurface::StartGenerationAsync()
{
	Noise->SetSeed(Seed);
	Noise->SetFrequency(Frequency);
	Noise->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	Noise->SetFractalType(FastNoiseLite::FractalType_FBm);
	Noise->SetFractalOctaves(FractalOctaves);
	Noise->SetFractalLacunarity(FractalLacunarity);
	Noise->SetFractalGain(FractalGain);

	if (!BiomeNoise.IsValid()) BiomeNoise = MakeUnique<FastNoiseLite>();
	BiomeNoise->SetSeed(Seed + 777);
	BiomeNoise->SetFrequency(BiomeFrequency);
	BiomeNoise->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	BiomeNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
	BiomeNoise->SetFractalOctaves(1);

	if (!ContinentNoise.IsValid()) ContinentNoise = MakeUnique<FastNoiseLite>();
	ContinentNoise->SetSeed(Seed + 4242);
	ContinentNoise->SetFrequency(ContinentFrequency);
	ContinentNoise->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	ContinentNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
	ContinentNoise->SetFractalOctaves(2);

	if (!RiverNoise.IsValid()) RiverNoise = MakeUnique<FastNoiseLite>();
	RiverNoise->SetSeed(Seed + 9001);
	RiverNoise->SetFrequency(RiverFrequency);
	RiverNoise->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	RiverNoise->SetFractalType(FastNoiseLite::FractalType_FBm);
	RiverNoise->SetFractalOctaves(2);

	GenerationType = SetGenerationType();
	if (Material) Mesh->SetMaterial(0, Material);

	KickAsyncBuild(true);
}

void AGenerateSurface::KickAsyncBuild(bool bGenVoxels)
{
	if (bGenerating) return;
	bGenerating = true;
	GenChunkOrigin = GetActorLocation();

	TWeakObjectPtr<AGenerateSurface> WeakThis(this);
	const bool bGen = bGenVoxels;
	Async(EAsyncExecution::ThreadPool, [WeakThis, bGen]()
	{
		AGenerateSurface* Self = WeakThis.Get();
		if (!Self) return;
		Self->BuildChunkDataAsync(bGen);
		AsyncTask(ENamedThreads::GameThread, [WeakThis]()
		{
			if (AGenerateSurface* S = WeakThis.Get())
			{
				S->FinishGenerationGameThread();
			}
		});
	});
}

void AGenerateSurface::BuildChunkDataAsync(bool bGenVoxels)
{
	if (bGenVoxels)
	{
		Setup();
		Generate2DHeightMap(GenChunkOrigin / kVoxelScale);
		ApplyPendingCavesGenTime();
		ApplyPersistedEditsGenTime();
		BuildSubChunks();
	}

	const int32 N = SubChunks.Num();
	if (N == 0) return;
	if (CachedBuilds.Num() < N) CachedBuilds.SetNum(N);
	ParallelFor(N, [this](int32 i)
	{
		BuildSubChunkData(i, CachedBuilds[i]);
	});
}

void AGenerateSurface::ApplyPendingCavesGenTime()
{
	for (const FCaveCarveOp& Op : PendingCaveOps)
	{
		CarveSphereImpl(GenChunkOrigin, Op.WorldCenter, Op.WorldRadius, Op.bDistorted, Op.MinRoof, false);
	}
}

void AGenerateSurface::ApplyPendingCavesSync()
{
	for (const FCaveCarveOp& Op : PendingCaveOps)
	{
		CarveCaveSphere(Op.WorldCenter, Op.WorldRadius, Op.bDistorted, Op.MinRoof);
	}
}

void AGenerateSurface::SetPersistedEdits(const TMap<int32, float>& InEdits)
{
	PendingEdits = InEdits;
}

void AGenerateSurface::ExtractEditedVoxels(TMap<int32, float>& OutEdits) const
{
	OutEdits.Reset();
	if (OriginalVoxels.Num() != Voxels.Num()) return;
	OutEdits.Reserve(64);
	for (int32 i = 0; i < Voxels.Num(); ++i)
	{
		if (Voxels[i] != OriginalVoxels[i])
		{
			OutEdits.Add(i, Voxels[i]);
		}
	}
}

void AGenerateSurface::ApplyPersistedEditsGenTime()
{
	if (PendingEdits.Num() == 0) return;
	if (Voxels.Num() == 0) return;
	OriginalVoxels = Voxels;
	for (const TPair<int32, float>& E : PendingEdits)
	{
		if (Voxels.IsValidIndex(E.Key))
		{
			Voxels[E.Key] = E.Value;
		}
	}
	PendingEdits.Empty();
}

void AGenerateSurface::ApplyPersistedEditsSync()
{
	if (PendingEdits.Num() == 0) return;
	if (Voxels.Num() == 0) return;
	OriginalVoxels = Voxels;
	bool bAny = false;
	for (const TPair<int32, float>& E : PendingEdits)
	{
		if (Voxels.IsValidIndex(E.Key) && Voxels[E.Key] != E.Value)
		{
			Voxels[E.Key] = E.Value;
			bAny = true;
		}
	}
	PendingEdits.Empty();
	if (bAny) RebuildAllSubChunks();
}

void AGenerateSurface::FinishGenerationGameThread()
{
	const double T0 = FPlatformTime::Seconds();

	Mesh->ClearAllMeshSections();

	const int32 N = SubChunks.Num();
	for (int32 i = 0; i < N; ++i)
	{
		UploadSubChunk(i, CachedBuilds[i]);
	}

	if (Material) Mesh->SetMaterial(0, Material);
	Mesh->SetCastShadow(bCastShadows);

	DirtySubChunks.Reset();
	bGenerating = false;

	const double UploadMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	UE_LOG(LogTemp, Display, TEXT("[PERF ASYNC] gamethread upload=%.2fms subchunks=%d collision=%d"), UploadMs, N, bCollisionEnabled ? 1 : 0);
}

void AGenerateSurface::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bGenerating) return;
	if (DirtySubChunks.Num() == 0) return;
	RemeshAccumulator += DeltaTime;
	const float Interval = bInEditStroke ? RemeshIntervalDuringStroke : RemeshInterval;
	if (RemeshAccumulator < Interval) return;
	RemeshAccumulator = 0.f;
	FlushDirtySubChunks();
}

void AGenerateSurface::BuildSubChunks()
{
	SubChunks.Reset();
	SubChunkCount = FIntVector(
		FMath::DivideAndRoundUp(Size, SubChunkSize),
		FMath::DivideAndRoundUp(Size, SubChunkSize),
		FMath::DivideAndRoundUp(SizeZ, SubChunkSize));

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
			FMath::Min(Info.Min.Z + SubChunkSize, SizeZ));
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
	Mesh->SetCastShadow(bCastShadows);
}

FVector AGenerateSurface::GradientAtCorner(int32 x, int32 y, int32 z) const
{
	const int32 xm = FMath::Max(0, x - 1);
	const int32 xp = FMath::Min(Size, x + 1);
	const int32 ym = FMath::Max(0, y - 1);
	const int32 yp = FMath::Min(Size, y + 1);
	const int32 zm = FMath::Max(0, z - 1);
	const int32 zp = FMath::Min(SizeZ, z + 1);
	return FVector(
		Voxels[GetVoxelIndex(xp, y, z)] - Voxels[GetVoxelIndex(xm, y, z)],
		Voxels[GetVoxelIndex(x, yp, z)] - Voxels[GetVoxelIndex(x, ym, z)],
		Voxels[GetVoxelIndex(x, y, zp)] - Voxels[GetVoxelIndex(x, y, zm)]);
}

float AGenerateSurface::SampleTransitionCorner(int32 ox, int32 oy, int32 oz, int32 Step) const
{
	if (TransitionFaceMask != 0)
	{
		const int32 Coarse = Step * 2;
		int32 FixedAxis = -1;
		if (((TransitionFaceMask & 1) && ox == Size) || ((TransitionFaceMask & 2) && ox == 0)) FixedAxis = 0;
		else if (((TransitionFaceMask & 4) && oy == Size) || ((TransitionFaceMask & 8) && oy == 0)) FixedAxis = 1;

		if (FixedAxis >= 0 && Coarse > 1)
		{
			int32 P[3] = { ox, oy, oz };
			const int32 A = (FixedAxis + 1) % 3;
			const int32 B = (FixedAxis + 2) % 3;
			const int32 a0 = (P[A] / Coarse) * Coarse;
			const int32 b0 = (P[B] / Coarse) * Coarse;
			const int32 a1 = FMath::Min(a0 + Coarse, A == 2 ? SizeZ : Size);
			const int32 b1 = FMath::Min(b0 + Coarse, B == 2 ? SizeZ : Size);
			const float fa = (a1 > a0) ? (float)(P[A] - a0) / (float)(a1 - a0) : 0.f;
			const float fb = (b1 > b0) ? (float)(P[B] - b0) / (float)(b1 - b0) : 0.f;
			int32 Q[3];
			Q[FixedAxis] = P[FixedAxis];
			auto S = [&](int32 av, int32 bv) -> float
			{
				Q[A] = av;
				Q[B] = bv;
				return Voxels[GetVoxelIndex(Q[0], Q[1], Q[2])];
			};
			const float v0 = FMath::Lerp(S(a0, b0), S(a1, b0), fa);
			const float v1 = FMath::Lerp(S(a0, b1), S(a1, b1), fa);
			return FMath::Lerp(v0, v1, fb);
		}
	}
	return Voxels[GetVoxelIndex(ox, oy, oz)];
}

void AGenerateSurface::BuildSubChunkData(int32 Idx, FSubChunkBuildData& Out) const
{
	if (bUseTransvoxelMesher)
	{
		BuildSubChunkDataTransvoxel(Idx, Out);
		return;
	}

	const FSubChunk& Info = SubChunks[Idx];

	Out.Vertices.Reset();
	Out.Triangles.Reset();
	Out.Normals.Reset();
	Out.UV0.Reset();
	Out.Colors.Reset();

	const int32 EstReserve = SubChunkSize * SubChunkSize * 4;
	Out.Vertices.Reserve(EstReserve);
	Out.Triangles.Reserve(EstReserve * 3);
	Out.Normals.Reserve(EstReserve);
	Out.UV0.Reserve(EstReserve);
	Out.Colors.Reserve(EstReserve);

	static const int8 EdgeOffsetTable[12][4] =
	{
		{0,0,0,0}, {1,0,0,1}, {0,1,0,0}, {0,0,0,1},
		{0,0,1,0}, {1,0,1,1}, {0,1,1,0}, {0,0,1,1},
		{0,0,0,2}, {1,0,0,2}, {1,1,0,2}, {0,1,0,2}
	};

	const int32 Step = FMath::Clamp(1 << LODLevel, 1, SubChunkSize);
	const int32 EdgeGridSize = (SubChunkSize / Step) + 1;
	const int32 NumEdgeIds = EdgeGridSize * EdgeGridSize * EdgeGridSize * 3;
	TArray<int32>& EdgeToVertex = Out.EdgeToVertex;
	EdgeToVertex.SetNumUninitialized(NumEdgeIds);
	FMemory::Memset(EdgeToVertex.GetData(), 0xFF, NumEdgeIds * sizeof(int32));

	for (int32 z = Info.Min.Z; z < Info.Max.Z; z += Step)
	for (int32 y = Info.Min.Y; y < Info.Max.Y; y += Step)
	for (int32 x = Info.Min.X; x < Info.Max.X; x += Step)
	{
		float CornerValues[8];
		FVector CornerPos[8];
		int32 CubeIndex = 0;
		for (int32 c = 0; c < 8; ++c)
		{
			const int32 ox = FMath::Min(x + VertexOffset[c][0] * Step, Size);
			const int32 oy = FMath::Min(y + VertexOffset[c][1] * Step, Size);
			const int32 oz = FMath::Min(z + VertexOffset[c][2] * Step, SizeZ);
			CornerValues[c] = SampleTransitionCorner(ox, oy, oz, Step);
			CornerPos[c] = FVector(ox, oy, oz) * kVoxelScale;
			if (CornerValues[c] < 0.f) CubeIndex |= (1 << c);
		}

		const int32 Edges = CubeEdgeFlags[CubeIndex];
		if (Edges == 0) continue;

		FVector CornerGrads[8];
		bool bGradsComputed = false;

		const int32 lcx = (x - Info.Min.X) / Step;
		const int32 lcy = (y - Info.Min.Y) / Step;
		const int32 lcz = (z - Info.Min.Z) / Step;

		int32 EdgeVertIndex[12];
		for (int32 e = 0; e < 12; ++e)
		{
			if (!(Edges & (1 << e))) { EdgeVertIndex[e] = -1; continue; }

			const int32 ex = lcx + EdgeOffsetTable[e][0];
			const int32 ey = lcy + EdgeOffsetTable[e][1];
			const int32 ez = lcz + EdgeOffsetTable[e][2];
			const int32 axis = EdgeOffsetTable[e][3];
			const int32 Key = ((ex * EdgeGridSize + ey) * EdgeGridSize + ez) * 3 + axis;

			const int32 ExistingIdx = EdgeToVertex[Key];
			if (ExistingIdx != -1)
			{
				EdgeVertIndex[e] = ExistingIdx;
				continue;
			}

			if (!bGradsComputed)
			{
				for (int32 c = 0; c < 8; ++c)
				{
					const int32 ox = FMath::Min(x + VertexOffset[c][0] * Step, Size);
					const int32 oy = FMath::Min(y + VertexOffset[c][1] * Step, Size);
					const int32 oz = FMath::Min(z + VertexOffset[c][2] * Step, SizeZ);
					CornerGrads[c] = GradientAtCorner(ox, oy, oz);
				}
				bGradsComputed = true;
			}

			const int32 a = EdgeConnection[e][0];
			const int32 b = EdgeConnection[e][1];
			const float va = CornerValues[a];
			const float vb = CornerValues[b];
			const float denom = vb - va;
			const float t = FMath::IsNearlyZero(denom) ? 0.5f : (-va / denom);

			const FVector VPos = CornerPos[a] + (CornerPos[b] - CornerPos[a]) * t;
			const FVector VNorm = FMath::Lerp(CornerGrads[a], CornerGrads[b], t).GetSafeNormal();

			const int32 NewIdx = Out.Vertices.Num();
			Out.Vertices.Add(VPos);
			Out.Normals.Add(VNorm);
			Out.UV0.Add(GetUV(VPos, VNorm));
			Out.Colors.Add(GetVertexColor(VPos, VNorm));
			EdgeToVertex[Key] = NewIdx;
			EdgeVertIndex[e] = NewIdx;
		}

		for (int32 i = 0; i < 16; i += 3)
		{
			const int t0 = TriangleConnectionTable[CubeIndex][i];
			if (t0 == -1) break;
			const int t1 = TriangleConnectionTable[CubeIndex][i + 1];
			const int t2 = TriangleConnectionTable[CubeIndex][i + 2];

			Out.Triangles.Add(EdgeVertIndex[t0]);
			Out.Triangles.Add(EdgeVertIndex[t1]);
			Out.Triangles.Add(EdgeVertIndex[t2]);
		}
	}
}

int32 AGenerateSurface::AddOrReuseVertex(FSubChunkBuildData& Out, TMap<FIntVector, int32>& Dedup, const FVector& Pos, const FVector& Norm) const
{
	const FIntVector Key(FMath::RoundToInt(Pos.X * 16.f), FMath::RoundToInt(Pos.Y * 16.f), FMath::RoundToInt(Pos.Z * 16.f));
	if (const int32* Found = Dedup.Find(Key)) return *Found;

	const int32 NewIdx = Out.Vertices.Num();
	Out.Vertices.Add(Pos);
	Out.Normals.Add(Norm);
	Out.UV0.Add(GetUV(Pos, Norm));
	Out.Colors.Add(GetVertexColor(Pos, Norm));
	Dedup.Add(Key, NewIdx);
	return NewIdx;
}

FVector AGenerateSurface::ComputeTransitionInset(const FVector& P, const FVector& N) const
{
	if (FinerNeighbourMask == 0) return FVector::ZeroVector;

	const float Step = (float)FMath::Clamp(1 << LODLevel, 1, SubChunkSize);
	const float Ext = (float)Size * kVoxelScale;
	const float R = Step * kVoxelScale;
	const float M = Step * kVoxelScale;

	float W = TransitionWidthScale * (Step * 0.5f) * kVoxelScale;
	W = FMath::Min(W, 0.85f * R);
	if (W <= 0.f) return FVector::ZeroVector;

	auto Taper = [Ext, M](float c) -> float
	{
		return FMath::Clamp(FMath::Min(c, Ext - c) / M, 0.f, 1.f);
	};

	FVector Off = FVector::ZeroVector;

	if (FinerNeighbourMask & 1)
	{
		const float rp = FMath::Clamp(1.f - (Ext - P.X) / R, 0.f, 1.f);
		Off.X -= W * rp * Taper(P.Y);
	}
	if (FinerNeighbourMask & 2)
	{
		const float rp = FMath::Clamp(1.f - P.X / R, 0.f, 1.f);
		Off.X += W * rp * Taper(P.Y);
	}
	if (FinerNeighbourMask & 4)
	{
		const float rp = FMath::Clamp(1.f - (Ext - P.Y) / R, 0.f, 1.f);
		Off.Y -= W * rp * Taper(P.X);
	}
	if (FinerNeighbourMask & 8)
	{
		const float rp = FMath::Clamp(1.f - P.Y / R, 0.f, 1.f);
		Off.Y += W * rp * Taper(P.X);
	}

	return Off;
}

void AGenerateSurface::BuildSubChunkDataTransvoxel(int32 Idx, FSubChunkBuildData& Out) const
{
	using namespace Transvoxel;

	const FSubChunk& Info = SubChunks[Idx];

	Out.Vertices.Reset();
	Out.Triangles.Reset();
	Out.Normals.Reset();
	Out.UV0.Reset();
	Out.Colors.Reset();

	const int32 EstReserve = SubChunkSize * SubChunkSize * 4;
	Out.Vertices.Reserve(EstReserve);
	Out.Triangles.Reserve(EstReserve * 3);
	Out.Normals.Reserve(EstReserve);
	Out.UV0.Reserve(EstReserve);
	Out.Colors.Reserve(EstReserve);

	TMap<FIntVector, int32> Dedup;
	Dedup.Reserve(EstReserve);

	static const int32 LCorner[8][3] =
	{
		{0,0,0}, {1,0,0}, {0,1,0}, {1,1,0},
		{0,0,1}, {1,0,1}, {0,1,1}, {1,1,1}
	};

	const int32 Step = FMath::Clamp(1 << LODLevel, 1, SubChunkSize);

	for (int32 z = Info.Min.Z; z < Info.Max.Z; z += Step)
	for (int32 y = Info.Min.Y; y < Info.Max.Y; y += Step)
	for (int32 x = Info.Min.X; x < Info.Max.X; x += Step)
	{
		float CornerValues[8];
		FVector CornerPos[8];
		int32 CaseCode = 0;
		for (int32 c = 0; c < 8; ++c)
		{
			const int32 ox = FMath::Min(x + LCorner[c][0] * Step, Size);
			const int32 oy = FMath::Min(y + LCorner[c][1] * Step, Size);
			const int32 oz = FMath::Min(z + LCorner[c][2] * Step, SizeZ);
			CornerValues[c] = Voxels[GetVoxelIndex(ox, oy, oz)];
			CornerPos[c] = FVector(ox, oy, oz) * kVoxelScale;
			if (CornerValues[c] < 0.f) CaseCode |= (1 << c);
		}

		if (CaseCode == 0 || CaseCode == 0xFF) continue;

		const uint8 CellClass = regularCellClass[CaseCode];
		const RegularCellData& Cell = regularCellData[CellClass & 0x0F];
		const unsigned short* VertData = regularVertexData[CaseCode];
		const int32 VertCount = Cell.GetVertexCount();
		const int32 TriCount = Cell.GetTriangleCount();

		FVector CornerGrads[8];
		bool bGradsComputed = false;

		int32 LocalVtx[12];
		for (int32 v = 0; v < VertCount; ++v)
		{
			const uint8 Edge = VertData[v] & 0xFF;
			const int32 a = (Edge >> 4) & 0x0F;
			const int32 b = Edge & 0x0F;

			const float va = CornerValues[a];
			const float vb = CornerValues[b];
			const float denom = vb - va;
			const float t = FMath::IsNearlyZero(denom) ? 0.5f : (-va / denom);

			const FVector VPos = CornerPos[a] + (CornerPos[b] - CornerPos[a]) * t;

			if (!bGradsComputed)
			{
				for (int32 c = 0; c < 8; ++c)
				{
					const int32 ox = FMath::Min(x + LCorner[c][0] * Step, Size);
					const int32 oy = FMath::Min(y + LCorner[c][1] * Step, Size);
					const int32 oz = FMath::Min(z + LCorner[c][2] * Step, SizeZ);
					CornerGrads[c] = GradientAtCorner(ox, oy, oz);
				}
				bGradsComputed = true;
			}
			const FVector VNorm = FMath::Lerp(CornerGrads[a], CornerGrads[b], t).GetSafeNormal();
			const FVector FinalPos = VPos + ComputeTransitionInset(VPos, VNorm);
			LocalVtx[v] = AddOrReuseVertex(Out, Dedup, FinalPos, VNorm);
		}

		for (int32 ti = 0; ti < TriCount * 3; ti += 3)
		{
			Out.Triangles.Add(LocalVtx[Cell.vertexIndex[ti + 0]]);
			Out.Triangles.Add(LocalVtx[Cell.vertexIndex[ti + 2]]);
			Out.Triangles.Add(LocalVtx[Cell.vertexIndex[ti + 1]]);
		}
	}

	if (FinerNeighbourMask != 0)
	{
		if ((FinerNeighbourMask & 1) && Info.Max.X == Size) BuildTransitionFace(Info, 0, Out, Dedup);
		if ((FinerNeighbourMask & 2) && Info.Min.X == 0)    BuildTransitionFace(Info, 1, Out, Dedup);
		if ((FinerNeighbourMask & 4) && Info.Max.Y == Size) BuildTransitionFace(Info, 2, Out, Dedup);
		if ((FinerNeighbourMask & 8) && Info.Min.Y == 0)    BuildTransitionFace(Info, 3, Out, Dedup);
	}
}

void AGenerateSurface::BuildTransitionFace(const FSubChunk& Info, int32 FaceDir, FSubChunkBuildData& Out, TMap<FIntVector, int32>& Dedup) const
{
	using namespace Transvoxel;

	const int32 Sc = FMath::Clamp(1 << LODLevel, 2, SubChunkSize);
	const int32 Sf = Sc / 2;
	if (Sf < 1) return;

	FIntVector Fixed, Uvec, Vvec;
	int32 FixedVal;
	switch (FaceDir)
	{
		case 0:  Fixed = FIntVector(1,0,0); FixedVal = Size; Uvec = FIntVector(0,1,0); Vvec = FIntVector(0,0,1); break;
		case 1:  Fixed = FIntVector(1,0,0); FixedVal = 0;    Uvec = FIntVector(0,0,1); Vvec = FIntVector(0,1,0); break;
		case 2:  Fixed = FIntVector(0,1,0); FixedVal = Size; Uvec = FIntVector(0,0,1); Vvec = FIntVector(1,0,0); break;
		default: Fixed = FIntVector(0,1,0); FixedVal = 0;    Uvec = FIntVector(1,0,0); Vvec = FIntVector(0,0,1); break;
	}

	const int32 UMaxClamp = (Uvec.Z != 0) ? SizeZ : Size;
	const int32 VMaxClamp = (Vvec.Z != 0) ? SizeZ : Size;

	auto Dot = [](const FIntVector& A, const FIntVector& B) { return A.X*B.X + A.Y*B.Y + A.Z*B.Z; };
	const int32 uMin = Dot(Info.Min, Uvec);
	const int32 uMax = Dot(Info.Max, Uvec);
	const int32 vMin = Dot(Info.Min, Vvec);
	const int32 vMax = Dot(Info.Max, Vvec);

	static const int32 Contrib[9]   = {0x001, 0x002, 0x004, 0x080, 0x100, 0x008, 0x040, 0x020, 0x010};
	static const int32 CornerAlias[4] = {0, 2, 6, 8};

	for (int32 vo = vMin; vo < vMax; vo += Sc)
	for (int32 uo = uMin; uo < uMax; uo += Sc)
	{
		FVector    SamplePos[13];
		FIntVector SampleVox[13];
		float      SampleVal[13];

		for (int32 s = 0; s < 9; ++s)
		{
			const int32 col = s % 3;
			const int32 row = s / 3;
			const int32 uu = FMath::Min(uo + col * Sf, UMaxClamp);
			const int32 vv = FMath::Min(vo + row * Sf, VMaxClamp);
			const FIntVector Vx = Fixed * FixedVal + Uvec * uu + Vvec * vv;
			SampleVox[s] = Vx;
			SampleVal[s] = Voxels[GetVoxelIndex(Vx.X, Vx.Y, Vx.Z)];
			SamplePos[s] = FVector(Vx.X, Vx.Y, Vx.Z) * kVoxelScale;
		}
		for (int32 k = 0; k < 4; ++k)
		{
			const int32 a = CornerAlias[k];
			SampleVox[9 + k] = SampleVox[a];
			SampleVal[9 + k] = SampleVal[a];
			SamplePos[9 + k] = SamplePos[a];
		}

		int32 CaseCode = 0;
		for (int32 s = 0; s < 9; ++s)
			if (SampleVal[s] < 0.f) CaseCode |= Contrib[s];

		if (CaseCode == 0 || CaseCode == 0x1FF) continue;

		const uint8 Raw = transitionCellClass[CaseCode];
		const TransitionCellData& Cell = transitionCellData[Raw & 0x7F];
		const unsigned short* VertData = transitionVertexData[CaseCode];
		const int32 VertCount = Cell.GetVertexCount();
		const int32 TriCount = Cell.GetTriangleCount();

		int32 LocalVtx[12];
		for (int32 i = 0; i < VertCount; ++i)
		{
			const uint8 Edge = VertData[i] & 0xFF;
			const int32 a = (Edge >> 4) & 0x0F;
			const int32 b = Edge & 0x0F;

			const float va = SampleVal[a];
			const float vb = SampleVal[b];
			const float denom = vb - va;
			const float t = FMath::IsNearlyZero(denom) ? 0.5f : (-va / denom);

			const FVector VPos = SamplePos[a] + (SamplePos[b] - SamplePos[a]) * t;
			const FVector Ga = GradientAtCorner(SampleVox[a].X, SampleVox[a].Y, SampleVox[a].Z);
			const FVector Gb = GradientAtCorner(SampleVox[b].X, SampleVox[b].Y, SampleVox[b].Z);
			const FVector VNorm = FMath::Lerp(Ga, Gb, t).GetSafeNormal();

			const bool bHalfRes = (a >= 9 && b >= 9);
			const FVector FinalPos = bHalfRes ? (VPos + ComputeTransitionInset(VPos, VNorm)) : VPos;
			LocalVtx[i] = AddOrReuseVertex(Out, Dedup, FinalPos, VNorm);
			if (bDebugTransitionColor && Out.Colors.IsValidIndex(LocalVtx[i])) Out.Colors[LocalVtx[i]] = FColor(255, 0, 0, 255);
		}

		for (int32 ti = 0; ti < TriCount * 3; ti += 3)
		{
			const int32 i0 = LocalVtx[Cell.vertexIndex[ti + 0]];
			const int32 i1 = LocalVtx[Cell.vertexIndex[ti + 1]];
			const int32 i2 = LocalVtx[Cell.vertexIndex[ti + 2]];
			const FVector GeoN = FVector::CrossProduct(Out.Vertices[i1] - Out.Vertices[i0], Out.Vertices[i2] - Out.Vertices[i0]);
			const FVector AvgN = Out.Normals[i0] + Out.Normals[i1] + Out.Normals[i2];
			Out.Triangles.Add(i0);
			if (FVector::DotProduct(GeoN, AvgN) > 0.f)
			{
				Out.Triangles.Add(i1);
				Out.Triangles.Add(i2);
			}
			else
			{
				Out.Triangles.Add(i2);
				Out.Triangles.Add(i1);
			}
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

	const bool bWithCollision = bCollisionEnabled && !(bDeferCollisionDuringEdit && bInEditStroke);
	Mesh->CreateMeshSection(Info.SectionIndex, Data.Vertices, Data.Triangles, Data.Normals, Data.UV0, Data.Colors, EmptyTangents, bWithCollision);
	if (Material && !Info.bCreated) Mesh->SetMaterial(Info.SectionIndex, Material);
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

	const double T0 = FPlatformTime::Seconds();

	CachedIndices.Reset(N);
	for (int32 Idx : DirtySubChunks) CachedIndices.Add(Idx);
	DirtySubChunks.Reset();

	if (CachedBuilds.Num() < N) CachedBuilds.SetNum(N);

	ParallelFor(N, [this](int32 i)
	{
		BuildSubChunkData(CachedIndices[i], CachedBuilds[i]);
	});

	const double T1 = FPlatformTime::Seconds();

	for (int32 i = 0; i < N; ++i)
	{
		UploadSubChunk(CachedIndices[i], CachedBuilds[i]);
	}

	const double T2 = FPlatformTime::Seconds();
	const double TotalMs = (T2 - T0) * 1000.0;
	if (TotalMs > 3.0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[FLUSH] subchunks=%d build=%.2fms upload=%.2fms total=%.2fms"),
			N, (T1 - T0) * 1000.0, (T2 - T1) * 1000.0, TotalMs);
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
	SizeZ = Size + UndergroundDepth;
	const int Dim = Size + 1;
	Voxels.SetNumUninitialized(Dim * Dim * (SizeZ + 1));
	HumidityNoiseValues.SetNumUninitialized(Dim * Dim);
	TemperatureNoiseValues.SetNumUninitialized(Dim * Dim);
}
void AGenerateSurface::Generate2DHeightMap(FVector Position)
{
	UE_LOG(LogTemp, Verbose, TEXT("Generating 2D Height Map at Position: %f, %f, %f"), Position.X, Position.Y, Position.Z);
	const int Dim = Size + 1;
	const int DimZ = SizeZ + 1;
	Voxels.SetNumUninitialized(Dim * Dim * DimZ);
	HumidityNoiseValues.SetNumUninitialized(Dim * Dim);
	TemperatureNoiseValues.SetNumUninitialized(Dim * Dim);
	SurfaceHeightVoxel.SetNumUninitialized(Dim * Dim);

	const float MaxTerrainHeight = (float)Size * HeightScale * MountainBoost + HeightOffset;
	if (MaxTerrainHeight >= (float)Size)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[HEIGHTMAP] terrain may clip top of chunk: maxHeight=%.1f, Size=%d. Lower MountainBoost or HeightScale."), MaxTerrainHeight, Size);
	}

	const float MaxAllowed = (float)SizeZ - 1.0f;
	const float PX = Position.X;
	const float PY = Position.Y;
	BiomeOriginYVoxel = PY;
	const int32 HPos = HumidityPos;
	const int32 TPos = TemperaturePos;
	FastNoiseLite* LocalNoise = Noise.Get();
	FastNoiseLite* LocalBiomeNoise = BiomeNoise.IsValid() ? BiomeNoise.Get() : LocalNoise;
	FastNoiseLite* LocalContinentNoise = ContinentNoise.IsValid() ? ContinentNoise.Get() : LocalNoise;
	FastNoiseLite* LocalRiverNoise = RiverNoise.IsValid() ? RiverNoise.Get() : LocalNoise;

	ParallelFor(Dim, [&](int32 x)
	{
		for (int y = 0; y < Dim; ++y)
		{
			float noiseHeight = LocalNoise->GetNoise(PX + (float)x, PY + (float)y);
			HumidityNoiseValues[x + y * Dim] = LocalBiomeNoise->GetNoise(PX + HPos + (float)x, PY + HPos + (float)y);
			TemperatureNoiseValues[x + y * Dim] = LocalBiomeNoise->GetNoise(PX + TPos + (float)x, PY + TPos + (float)y);

			float h01 = FMath::Clamp((noiseHeight + 1.0f) * 0.5f + MountainBias, 0.0f, 1.0f);
			h01 = FMath::Pow(h01, HeightRedistribution);
			float terrainHeight = h01 * (float)Size * HeightScale * MountainBoost + HeightOffset;

			if (bEnableOceans)
			{
				const float cont01 = (LocalContinentNoise->GetNoise(PX + (float)x, PY + (float)y) + 1.0f) * 0.5f;
				const float landT = FMath::Clamp((cont01 - OceanThreshold) / FMath::Max(0.0001f, CoastWidth), 0.0f, 1.0f);
				const float landFactor = landT * landT * (3.0f - 2.0f * landT);
				terrainHeight = FMath::Lerp(OceanFloorVoxel, terrainHeight, landFactor);
			}

			if (bEnableRivers && terrainHeight < RiverMaxTerrain)
			{
				const float rv = LocalRiverNoise->GetNoise(PX + (float)x, PY + (float)y);
				const float rEdge = FMath::Clamp(FMath::Abs(rv) / FMath::Max(0.0001f, RiverWidth), 0.0f, 1.0f);
				const float riverMask = 1.0f - (rEdge * rEdge * (3.0f - 2.0f * rEdge));
				if (riverMask > 0.0f)
				{
					terrainHeight = FMath::Lerp(terrainHeight, RiverBedVoxel, riverMask * RiverStrength);
				}
			}

			terrainHeight += (float)UndergroundDepth;
			if (terrainHeight < 0.2f) terrainHeight = 0.2f;
			if (terrainHeight > MaxAllowed) terrainHeight = MaxAllowed;

			SurfaceHeightVoxel[x + y * Dim] = terrainHeight;

			for (int z = 0; z < DimZ; ++z)
			{
				Voxels[GetVoxelIndex(x, y, z)] = (float)z - terrainHeight;
			}
		}
	});

	float TMin = FLT_MAX, TMax = -FLT_MAX, TSum = 0.f;
	float HMin = FLT_MAX, HMax = -FLT_MAX, HSum = 0.f;
	const int32 Count = Dim * Dim;
	for (int32 i = 0; i < Count; ++i)
	{
		const float Tv = TemperatureNoiseValues[i] * 0.5f + 0.5f;
		const float Hv = HumidityNoiseValues[i] * 0.5f + 0.5f;
		TMin = FMath::Min(TMin, Tv); TMax = FMath::Max(TMax, Tv); TSum += Tv;
		HMin = FMath::Min(HMin, Hv); HMax = FMath::Max(HMax, Hv); HSum += Hv;
	}
	UE_LOG(LogTemp, Verbose, TEXT("[BIOME] Temp [%.3f..%.3f] avg %.3f | Humid [%.3f..%.3f] avg %.3f | freq=%.4f chunk(%.0f,%.0f)"),
		TMin, TMax, TSum / Count, HMin, HMax, HSum / Count, BiomeFrequency, PX, PY);
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
	const float VoxelHeight = Position.Z / kVoxelScale - (float)UndergroundDepth;

	const int32 Dim = Size + 1;
	const int32 vx = FMath::Clamp(FMath::RoundToInt(Position.X / kVoxelScale), 0, Size);
	const int32 vy = FMath::Clamp(FMath::RoundToInt(Position.Y / kVoxelScale), 0, Size);
	const int32 NIdx = vx + vy * Dim;

	float NoiseTemp = 0.5f;
	float NoiseHumid = 0.5f;
	const float BiomeContrast = 2.8f;
	if (TemperatureNoiseValues.IsValidIndex(NIdx)) NoiseTemp = FMath::Clamp(TemperatureNoiseValues[NIdx] * BiomeContrast * 0.5f + 0.5f, 0.0f, 1.0f);
	if (HumidityNoiseValues.IsValidIndex(NIdx)) NoiseHumid = FMath::Clamp(HumidityNoiseValues[NIdx] * BiomeContrast * 0.5f + 0.5f, 0.0f, 1.0f);

	const float Denom = FMath::Max(1.0f, (float)Size - SeaLevel);
	const float NormalizedAltitude = FMath::Clamp((VoxelHeight - SeaLevel) / Denom, 0.0f, 1.0f);

	const float LatitudePeriod = 2000.0f;
	const float WorldVoxelY = BiomeOriginYVoxel + Position.Y / kVoxelScale;
	const float LatitudeClimate = 0.5f + 0.5f * FMath::Sin(WorldVoxelY * (2.0f * PI / LatitudePeriod));

	float Temperature = NoiseTemp * 0.8f + LatitudeClimate * 0.2f;
	Temperature = FMath::Clamp(Temperature - NormalizedAltitude * BiomeHeightCooling, 0.0f, 1.0f);
	const float Humidity = FMath::Clamp(NoiseHumid + (0.5f - NormalizedAltitude) * BiomeHeightDrying, 0.0f, 1.0f);

	if (BiomeDebugView == 1)
	{
		const uint8 c = (uint8)(Temperature * 255.0f);
		return FColor(c, 0, 255 - c);
	}
	if (BiomeDebugView == 2)
	{
		const uint8 c = (uint8)(Humidity * 255.0f);
		return FColor(255 - c, 255 - c, 255);
	}
	if (BiomeDebugView == 3)
	{
		return GetBiomeColor(Temperature, Humidity);
	}
	if (BiomeDebugView == 4)
	{
		const uint8 c = (uint8)(FMath::Clamp(VoxelHeight / FMath::Max(1.0f, (float)Size), 0.0f, 1.0f) * 255.0f);
		return FColor(c, c, c);
	}

	const float SurfaceVox = SurfaceHeightVoxel.IsValidIndex(NIdx) ? SurfaceHeightVoxel[NIdx] : 0.f;
	if (Position.Z / kVoxelScale < SurfaceVox - 2.0f)
	{
		return FColor(82, 70, 55);
	}

	if (VoxelHeight <= SeaLevel)
	{
		return FColor(32, 96, 160);
	}
	if (VoxelHeight <= SeaLevel + 2.0f)
	{
		return FColor(214, 203, 156);
	}
	if (VoxelHeight >= SnowLevel)
	{
		return FColor(236, 240, 245);
	}

	const float Slope = 1.0f - FMath::Clamp(FVector::DotProduct(Normal, FVector::UpVector), 0.0f, 1.0f);
	if (Slope > 0.45f)
	{
		return FColor(96, 96, 96);
	}

	return GetBiomeColor(Temperature, Humidity);
}

FColor AGenerateSurface::GetBiomeColor(float Temperature01, float Humidity01) const
{
	const FColor Palette[3][3] = {
		{ FColor(150, 152, 138), FColor(104, 144, 120), FColor(66, 112, 92)  },
		{ FColor(206, 200, 120), FColor(108, 166, 78),  FColor(56, 118, 62)  },
		{ FColor(224, 168, 84),  FColor(198, 192, 86),  FColor(52, 142, 66)  }
	};

	const float tt = FMath::Clamp(Temperature01, 0.0f, 1.0f) * 2.0f;
	const float hh = FMath::Clamp(Humidity01, 0.0f, 1.0f) * 2.0f;
	const int32 t0 = FMath::Clamp(FMath::FloorToInt(tt), 0, 2);
	const int32 h0 = FMath::Clamp(FMath::FloorToInt(hh), 0, 2);
	const int32 t1 = FMath::Min(t0 + 1, 2);
	const int32 h1 = FMath::Min(h0 + 1, 2);
	const float tf = tt - (float)t0;
	const float hf = hh - (float)h0;

	const FColor& c00 = Palette[t0][h0];
	const FColor& c01 = Palette[t0][h1];
	const FColor& c10 = Palette[t1][h0];
	const FColor& c11 = Palette[t1][h1];

	const float r0 = FMath::Lerp((float)c00.R, (float)c01.R, hf);
	const float g0 = FMath::Lerp((float)c00.G, (float)c01.G, hf);
	const float b0 = FMath::Lerp((float)c00.B, (float)c01.B, hf);
	const float r1 = FMath::Lerp((float)c10.R, (float)c11.R, hf);
	const float g1 = FMath::Lerp((float)c10.G, (float)c11.G, hf);
	const float b1 = FMath::Lerp((float)c10.B, (float)c11.B, hf);

	return FColor(
		(uint8)FMath::RoundToInt(FMath::Lerp(r0, r1, tf)),
		(uint8)FMath::RoundToInt(FMath::Lerp(g0, g1, tf)),
		(uint8)FMath::RoundToInt(FMath::Lerp(b0, b1, tf)),
		255);
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
	const float WorldSizeZ = (Size + UndergroundDepth) * kVoxelScale;
	return FBox(Origin, Origin + FVector(WorldSize, WorldSize, WorldSizeZ));
}

void AGenerateSurface::CarveCaveSphere(const FVector& WorldCenter, float WorldRadius, bool bDistorted, float MinRoofVoxels)
{
	if (OriginalVoxels.Num() == 0) OriginalVoxels = Voxels;
	CarveSphereImpl(GetActorLocation(), WorldCenter, WorldRadius, bDistorted, MinRoofVoxels, true);
}

void AGenerateSurface::CarveSphereImpl(const FVector& ChunkOrigin, const FVector& WorldCenter, float WorldRadius, bool bDistorted, float MinRoofVoxels, bool bMarkDirty)
{
	const FVector LocalCenter = (WorldCenter - ChunkOrigin) / kVoxelScale;
	const float LocalRadius = WorldRadius / kVoxelScale;
	const float Padding = bDistorted ? 5.f : 1.f;
	const float OuterRadius = LocalRadius + Padding;
	const float R2 = LocalRadius * LocalRadius;

	const int32 MinX = FMath::Max(0, FMath::FloorToInt(LocalCenter.X - OuterRadius));
	const int32 MinY = FMath::Max(0, FMath::FloorToInt(LocalCenter.Y - OuterRadius));
	const int32 MinZ = FMath::Max(2, FMath::FloorToInt(LocalCenter.Z - OuterRadius));
	const int32 MaxX = FMath::Min(Size, FMath::CeilToInt(LocalCenter.X + OuterRadius));
	const int32 MaxY = FMath::Min(Size, FMath::CeilToInt(LocalCenter.Y + OuterRadius));
	const int32 MaxZ = FMath::Min(SizeZ - 2, FMath::CeilToInt(LocalCenter.Z + OuterRadius));

	static thread_local FastNoiseLite WallNoise;
	static thread_local bool bWallNoiseInit = false;
	if (bDistorted && !bWallNoiseInit)
	{
		WallNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
		WallNoise.SetFrequency(0.1f);
		bWallNoiseInit = true;
	}

	for (int32 z = MinZ; z <= MaxZ; ++z)
	for (int32 y = MinY; y <= MaxY; ++y)
	for (int32 x = MinX; x <= MaxX; ++x)
	{
		const float dx = x - LocalCenter.X;
		const float dy = y - LocalCenter.Y;
		const float dz = z - LocalCenter.Z;
		const float D2 = dx*dx + dy*dy + dz*dz;

		float EffectiveR2 = R2;
		if (bDistorted)
		{
			const float NoiseVal = WallNoise.GetNoise((float)x, (float)y, (float)z);
			const float NoisyR = LocalRadius + (NoiseVal * 5.f);
			EffectiveR2 = NoisyR * NoisyR;
		}

		if (D2 > EffectiveR2) continue;

		float& V = Voxels[GetVoxelIndex(x, y, z)];
		if (V >= DensityClampMax) continue;
		if (MinRoofVoxels > 0.f && V > -MinRoofVoxels) continue;
		V = DensityClampMax;
		if (bMarkDirty) MarkVoxelDirty(x, y, z);
	}
}

void AGenerateSurface::SetChunkCollisionEnabled(bool bEnable)
{
	if (bCollisionEnabled == bEnable) return;
	if (bGenerating) return;
	bCollisionEnabled = bEnable;
	KickAsyncBuild(false);
}

void AGenerateSurface::SetLODLevel(int32 NewLOD)
{
	NewLOD = FMath::Clamp(NewLOD, 0, 5);
	if (NewLOD == LODLevel) return;
	if (bGenerating) return;
	LODLevel = NewLOD;
	KickAsyncBuild(false);
}

void AGenerateSurface::SetLODAndTransitions(int32 NewLOD, int32 NewMask, int32 NewFinerMask)
{
	NewLOD = FMath::Clamp(NewLOD, 0, 5);
	if (NewLOD == LODLevel && NewMask == TransitionFaceMask && NewFinerMask == FinerNeighbourMask) return;
	if (bGenerating) return;
	LODLevel = NewLOD;
	TransitionFaceMask = NewMask;
	FinerNeighbourMask = NewFinerMask;
	KickAsyncBuild(false);
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
	const float LocalMaxDist = MaxDist / kVoxelScale;

	const float BoxMin = 0.f;
	const float BoxMax = (float)Size;
	float tEnter = 0.f;
	float tExit = LocalMaxDist;
	for (int32 axis = 0; axis < 3; ++axis)
	{
		const float O = LocalStart[axis];
		const float D = LocalDir[axis];
		if (FMath::Abs(D) < KINDA_SMALL_NUMBER)
		{
			if (O < BoxMin || O > BoxMax) return false;
			continue;
		}
		const float InvD = 1.f / D;
		float t1 = (BoxMin - O) * InvD;
		float t2 = (BoxMax - O) * InvD;
		if (t1 > t2) Swap(t1, t2);
		tEnter = FMath::Max(tEnter, t1);
		tExit = FMath::Min(tExit, t2);
		if (tEnter > tExit) return false;
	}
	if (tExit <= 0.f) return false;

	const FVector EntryL = LocalStart + LocalDir * tEnter;
	const int32 MaxSteps = FMath::CeilToInt((tExit - tEnter) / StepGrid);

	float PrevD = SampleDensityTrilinear(EntryL.X, EntryL.Y, EntryL.Z);
	FVector PrevL = EntryL;

	for (int32 i = 1; i <= MaxSteps; ++i)
	{
		const FVector L = EntryL + LocalDir * (StepGrid * i);
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
	const int32 MinZ = FMath::Max(1, FMath::FloorToInt(LocalCenter.Z - LocalRadius));
	const int32 MaxX = FMath::Min(Size, FMath::CeilToInt(LocalCenter.X + LocalRadius));
	const int32 MaxY = FMath::Min(Size, FMath::CeilToInt(LocalCenter.Y + LocalRadius));
	const int32 MaxZ = FMath::Min(SizeZ - 1, FMath::CeilToInt(LocalCenter.Z + LocalRadius));

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
	const int32 MinZ = FMath::Max(1, FMath::FloorToInt(LocalCenter.Z - LocalRadius));
	const int32 MaxX = FMath::Min(Size, FMath::CeilToInt(LocalCenter.X + LocalRadius));
	const int32 MaxY = FMath::Min(Size, FMath::CeilToInt(LocalCenter.Y + LocalRadius));
	const int32 MaxZ = FMath::Min(SizeZ - 1, FMath::CeilToInt(LocalCenter.Z + LocalRadius));

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
	const int32 MaxZ = FMath::Min(SizeZ - 1, FMath::CeilToInt(LocalCenter.Z + LocalRadius));

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
