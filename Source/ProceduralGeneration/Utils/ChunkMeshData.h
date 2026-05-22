#pragma once

#include "CoreMinimal.h"
#include "ChunkMeshData.generated.h"

USTRUCT()
struct FChunkMeshData
{
	GENERATED_BODY();

public:
	TArray<FVector> Vertices;
	TArray<int> Triangles;
	TArray<FVector> Normals;
	TArray<FColor> Colors;
	TArray<FVector2D> UV0;

	void Clear();
};

inline void FChunkMeshData::Clear()
{
	Vertices.Empty();
	Triangles.Empty();
	Normals.Empty();
	Colors.Empty();
	UV0.Empty();
}

USTRUCT()
struct FTriangle
{
	GENERATED_BODY();
public:
	TArray<FVector> position[3];
};

USTRUCT()
struct FGridCell
{
	GENERATED_BODY();
public:
	TArray<FVector> point[8];
	TArray<double> value[8];
};