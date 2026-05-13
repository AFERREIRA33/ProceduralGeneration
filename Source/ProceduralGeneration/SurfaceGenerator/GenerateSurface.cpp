#include "GenerateSurface.h"
#include"ProceduralGeneration/Utils/FastNoiseLite.h"

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
