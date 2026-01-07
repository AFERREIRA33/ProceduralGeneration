// Fill out your copyright notice in the Description page of Project Settings.
#include "GenerateSurface.h"
#include"ProceduralGeneration/Utils/FastNoiseLite.h"

#pragma optimize("", off)

void AGenerateSurface::Setup()
{
	// Initialize Voxels
	Voxels.SetNum((Size + 1) * (Size + 1) * (Size + 1));
}

void AGenerateSurface::Generate2DHeightMap(const FVector Position)
{
	for (int x = 0; x <= Size; x++)
	{
		for (int y = 0; y <= Size; y++)
		{
			const float Xpos = x + Position.X;
			const float ypos = y + Position.Y;
			const int Height = FMath::Clamp(FMath::RoundToInt((Noise->GetNoise(Xpos, ypos) + 1) * Size / 2), 0, Size);

			for (int z = 0; z <= Size; z++)
			{
				Voxels[GetVoxelIndex(x, y, z)] = Height - z;
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
	return Z * (Size + 1) * (Size + 1) + Y * (Size + 1) + X;
}

void AGenerateSurface::GenerateMesh()
{
	// Triangulation order
	if (SurfaceLevel > 0.0f)
	{
		TriangleOrder[0] = 0;
		TriangleOrder[1] = 1;
		TriangleOrder[2] = 2;
	}
	else
	{
		TriangleOrder[0] = 2;
		TriangleOrder[1] = 1;
		TriangleOrder[2] = 0;
	}

	TArray<float> Cube = TArray<float>();
	Cube.Init(0, 8);


	for (int X = 0; X < Size; ++X)
	{
		for (int Y = 0; Y < Size; ++Y)
		{
			for (int Z = 0; Z < Size; ++Z)
			{
				for (int i = 0; i < 8; ++i)
				{
					Cube[i] = Voxels[GetVoxelIndex(X + VertexOffset[i][0],Y + VertexOffset[i][1],Z + VertexOffset[i][2])];
				}

				March(X,Y,Z, Cube);
			}
		}
	}
	UE_LOG(LogTemp, Display, TEXT("Min Value: %f"), Min);
	UE_LOG(LogTemp, Display, TEXT("Max Value: %f"), Max);
	

}


void AGenerateSurface::March(const int X, const int Y, const int Z, TArray<float> Cube)
{
	//Find which vertices are inside of the surface and which are outside
	int VertexMask = 0;
	for (int i = 0; i < 8; ++i)
	{
		if (Cube[i] < SurfaceLevel) VertexMask |= 1 << i;
	}

	const int EdgeMask = CubeEdgeFlags[VertexMask];
	if (EdgeMask == 0) return;
	
	FVector EdgeVertex[12];
	for (int i = 0; i < 12; ++i)
	{
		if ((EdgeMask & 1 << i) != 0)
		{
			int point = EdgeConnection[i][0];
			int connectionPoint = EdgeConnection[i][1];
			const int vertexOffP1[3] = {VertexOffset[point][0], VertexOffset[point][1], VertexOffset[point][2]};
			const int vertexOffP2[3] = {VertexOffset[connectionPoint][0], VertexOffset[connectionPoint][1], VertexOffset[connectionPoint][2]};
			FVector P1 = FVector(X + vertexOffP1[0],Y + vertexOffP1[1],Z + vertexOffP1[2]);
			FVector P2 =  FVector(X + vertexOffP2[0],Y + vertexOffP2[1],Z + vertexOffP2[2]);
			if (Min > Cube[EdgeConnection[i][0]])
			{
				Min = Cube[EdgeConnection[i][0]];
			}
			if (Min > Cube[EdgeConnection[i][1]])
			{
				Min = Cube[EdgeConnection[i][1]];
			}
			if (Max < Cube[EdgeConnection[i][0]])
			{
				Max = Cube[EdgeConnection[i][0]];
			}
			if (Max < Cube[EdgeConnection[i][1]])
			{
				Max = Cube[EdgeConnection[i][1]];
			}
			const float Delta = GetInterpolationOffset(Cube[EdgeConnection[i][0]], Cube[EdgeConnection[i][1]]);
			EdgeVertex[i] = P1+ (P2 - P1)*Delta;
		}
	}

	for (int i = 0; i < 5; ++i)
	{
		if (TriangleConnectionTable[VertexMask][3 * i] < 0) break;

		auto V1 = EdgeVertex[TriangleConnectionTable[VertexMask][3 * i]] * 100;
		auto V2 = EdgeVertex[TriangleConnectionTable[VertexMask][3 * i + 1]] * 100;
		auto V3 = EdgeVertex[TriangleConnectionTable[VertexMask][3 * i + 2]] * 100;

		auto Normal = FVector::CrossProduct(V2 - V1, V3 - V1);
		auto Color = FColor::MakeRandomColor();
		
		Normal.Normalize();

		MeshData.Vertices.Append({V1, V2, V3});
		
		MeshData.Triangles.Append({
			VertexCount + TriangleOrder[0],
			VertexCount + TriangleOrder[1],
			VertexCount + TriangleOrder[2]
		});

		MeshData.Normals.Append({
			Normal,
			Normal,
			Normal
		});

		MeshData.Colors.Append({
			Color,
			Color,
			Color
		});

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









