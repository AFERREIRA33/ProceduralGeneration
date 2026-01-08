// Fill out your copyright notice in the Description page of Project Settings.
#include "GenerateSurface.h"
#include"ProceduralGeneration/Utils/FastNoiseLite.h"

#pragma optimize("", off)

void AGenerateSurface::Setup()
{
	const int Dim = Size + 1;
	Voxels.SetNumZeroed(Dim * Dim * Dim);

	// Example 2D height sampling (fill your own noise sampling)
	for (int x = 0; x < Dim; ++x)
	{
		for (int y = 0; y < Dim; ++y)
		{
			// compute height in world units (use your noise, offset by actor/world position)
			float sampleHeight = /* Noise sampling here returning world z */ 0.0f;
			for (int z = 0; z < Dim; ++z)
			{
				// store signed distance relative to SurfaceLevel (marching uses iso = 0)
				float value = (float)z - sampleHeight - (float)SurfaceLevel;
				Voxels[GetVoxelIndex(x, y, z)] = value;
			}
		}
	}
}
void AGenerateSurface::Generate2DHeightMap(const FVector Position)
{
	UE_LOG(LogTemp, Warning, TEXT("Generating 2D Height Map at Position: %s"), *Position.ToString());
	Voxels.SetNum((Size + 1) * (Size + 1) * (Size + 1));
    
	for (int x = 0; x <= Size; ++x)
	{
		for (int y = 0; y <= Size; ++y)
		{
			if (x ==0 && y == 0)
				UE_LOG( LogTemp, Warning, TEXT("Generating voxel column at x: %f, y: %f"), ((Position.X / 100.0f) + x), (Position.Y / 100.0f) + y);
			// Get 2D noise height at this x,y position
			float noiseHeight = Noise->GetNoise(
				(Position.X ) + x, 
				(Position.Y ) + y
			);
            
			// Scale noise to useful range (e.g., 0 to Size)
			float terrainHeight = (noiseHeight + 1.0f) * 0.5f * Size;
            
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

		VertexCount += 3;
		
	}
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

#pragma optimize("", on)









