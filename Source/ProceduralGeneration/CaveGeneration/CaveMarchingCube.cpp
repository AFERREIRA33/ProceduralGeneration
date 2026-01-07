#include "CaveMarchingCube.h"
#include "ProceduralMeshComponent.h"




// Sets default values
ACaveMarchingCube::ACaveMarchingCube()
{
	PrimaryActorTick.bCanEverTick = false;
	mesh = CreateDefaultSubobject<UProceduralMeshComponent>("Mesh");
	SetRootComponent(mesh);
	noise = new FastNoiseLite();
	noise->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	
}


// Called when the game starts or when spawned
void ACaveMarchingCube::BeginPlay()
{
	Super::BeginPlay();
	densityGrid.Init(1.0f, gridSize * gridSize * gridSize);
	CaveWorm();
	GenerateMesh();
}

// Called every frame
void ACaveMarchingCube::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
}

int ACaveMarchingCube::GetIndex(int x, int y, int z)
{
	return x + gridSize * (y + gridSize * z);
}

FVector ACaveMarchingCube::VertexInterpolation(FVector p1, FVector p2, float valp1, float valp2)
{
	if (FMath::Abs(surfaceLevel - valp1) < 0.00001f) return p1;
	if (FMath::Abs(surfaceLevel - valp2) < 0.00001f) return p1;
	if (FMath::Abs(valp1 - valp2) < 0.00001f) return p1;

	float Mu = (surfaceLevel - valp1) / (valp2 - valp1);
    
	// Linear interpolation between P1 and P2
	return p1 + (p2 - p1) * Mu;
}



void ACaveMarchingCube::CaveWorm()
{
	FVector currentPos = FVector(gridSize / 2, gridSize / 2, gridSize - 5); 
    
  
    noise->SetFrequency(wormNoiseFrequency);

    for (int step = 0; step < wormSteps; step++)
    {
  
        float dirX = noise->GetNoise((float)step, 0.0f);
        float dirY = noise->GetNoise((float)step, 100.0f);
        float dirZ = noise->GetNoise((float)step, 200.0f) - 0.5f; 

        FVector direction = FVector(dirX, dirY, dirZ).GetSafeNormal();
        currentPos += direction * (wormRadius * 0.5f); 

    
        int RadiusInt = FMath::CeilToInt(wormRadius + 2);
        

        for (int z = -RadiusInt; z <= RadiusInt; z++)
        {
            for (int y = -RadiusInt; y <= RadiusInt; y++)
            {
                for (int x = -RadiusInt; x <= RadiusInt; x++)
                {
                    FVector VoxelOffset(x, y, z);
                    FVector targetVoxel = currentPos + VoxelOffset;


                    if (targetVoxel.X > 0 && targetVoxel.X < gridSize - 1 &&
                        targetVoxel.Y > 0 && targetVoxel.Y < gridSize - 1 &&
                        targetVoxel.Z > 0 && targetVoxel.Z < gridSize - 1)
                    {
                        float Dist = FVector::Dist(currentPos, targetVoxel);
                        

                        if (Dist < wormRadius)
                        {
                            int index = GetIndex(targetVoxel.X, targetVoxel.Y, targetVoxel.Z);

                            densityGrid[index] = FMath::Min(densityGrid[index], -1.0f); 
                        }
                    }
                }
            }
        }
    }
}

void ACaveMarchingCube::GenerateMesh()
{
	TArray<FVector> vertices;
	TArray<int32> triangles;
	TArray<FVector> normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> tangents;


	const FVector CornerOffsets[8] = {
        FVector(0, 0, 0), FVector(1, 0, 0), FVector(1, 1, 0), FVector(0, 1, 0),
        FVector(0, 0, 1), FVector(1, 0, 1), FVector(1, 1, 1), FVector(0, 1, 1)
    };

    // Main Grid Loop
    for (int32 z = 0; z < gridSize - 1; z++)
    {
        for (int32 y = 0; y < gridSize - 1; y++)
        {
            for (int32 x = 0; x < gridSize - 1; x++)
            {
                // 1. Calculate the Cube Index
                // This determines which of the 8 corners are inside the "ground" vs "air"
                int32 CubeIndex = 0;
                float CubeValues[8];
                FVector GridPos(x, y, z);

                for (int i = 0; i < 8; i++)
                {
                    FVector CornerPos = GridPos + CornerOffsets[i];
                    // Get density from your grid array
                    float Density = densityGrid[GetIndex(CornerPos.X, CornerPos.Y, CornerPos.Z)];
                    CubeValues[i] = Density;

                    // If density is below surface level (solid), toggle the bit
                    // Note: Depending on your logic, < Surface might be solid or air. 
                    // Usually: Density > SurfaceLevel = Solid.
                    if (Density < surfaceLevel) 
                        CubeIndex |= (1 << i);
                }

                // 2. Look up Edge Table
                // If the cube is entirely inside or entirely outside, CubeIndex is 0 or 255.
                // The edge table tells us which edges intersect the surface.
                if (edgeTable[CubeIndex] == 0) continue;

                // 3. Calculate Intersection Points
                // There are 12 possible edges on a cube. We compute the vertex on the required edges.
                FVector intersectVerts[12];

                // Check standard MC Edge list (0->1, 1->2, etc.)
                if (edgeTable[CubeIndex] & 1)    intersectVerts[0]  = VertexInterpolation(GridPos + CornerOffsets[0], GridPos + CornerOffsets[1], CubeValues[0], CubeValues[1]);
                if (edgeTable[CubeIndex] & 2)    intersectVerts[1]  = VertexInterpolation(GridPos + CornerOffsets[1], GridPos + CornerOffsets[2], CubeValues[1], CubeValues[2]);
                if (edgeTable[CubeIndex] & 4)    intersectVerts[2]  = VertexInterpolation(GridPos + CornerOffsets[2], GridPos + CornerOffsets[3], CubeValues[2], CubeValues[3]);
                if (edgeTable[CubeIndex] & 8)    intersectVerts[3]  = VertexInterpolation(GridPos + CornerOffsets[3], GridPos + CornerOffsets[0], CubeValues[3], CubeValues[0]);
                if (edgeTable[CubeIndex] & 16)   intersectVerts[4]  = VertexInterpolation(GridPos + CornerOffsets[4], GridPos + CornerOffsets[5], CubeValues[4], CubeValues[5]);
                if (edgeTable[CubeIndex] & 32)   intersectVerts[5]  = VertexInterpolation(GridPos + CornerOffsets[5], GridPos + CornerOffsets[6], CubeValues[5], CubeValues[6]);
                if (edgeTable[CubeIndex] & 64)   intersectVerts[6]  = VertexInterpolation(GridPos + CornerOffsets[6], GridPos + CornerOffsets[7], CubeValues[6], CubeValues[7]);
                if (edgeTable[CubeIndex] & 128)  intersectVerts[7]  = VertexInterpolation(GridPos + CornerOffsets[7], GridPos + CornerOffsets[4], CubeValues[7], CubeValues[4]);
                if (edgeTable[CubeIndex] & 256)  intersectVerts[8]  = VertexInterpolation(GridPos + CornerOffsets[0], GridPos + CornerOffsets[4], CubeValues[0], CubeValues[4]);
                if (edgeTable[CubeIndex] & 512)  intersectVerts[9]  = VertexInterpolation(GridPos + CornerOffsets[1], GridPos + CornerOffsets[5], CubeValues[1], CubeValues[5]);
                if (edgeTable[CubeIndex] & 1024) intersectVerts[10] = VertexInterpolation(GridPos + CornerOffsets[2], GridPos + CornerOffsets[6], CubeValues[2], CubeValues[6]);
                if (edgeTable[CubeIndex] & 2048) intersectVerts[11] = VertexInterpolation(GridPos + CornerOffsets[3], GridPos + CornerOffsets[7], CubeValues[3], CubeValues[7]);

                // 4. Create Triangles from TriTable
                // The TriTable gives us indices (0-15) to look up in our IntersectVerts array
                // The table is terminated by -1
                for (int i = 0; triTable[CubeIndex][i] != -1; i += 3)
                {
                    // Scale vertex by VoxelSize to fit world space
                    FVector V1 = intersectVerts[triTable[CubeIndex][i]]     * voxelSize;
                    FVector V2 = intersectVerts[triTable[CubeIndex][i + 1]] * voxelSize;
                    FVector V3 = intersectVerts[triTable[CubeIndex][i + 2]] * voxelSize;

                    // Add to Vertex Array
                    int Index1 = vertices.Add(V1);
                    int Index2 = vertices.Add(V2);
                    int Index3 = vertices.Add(V3);

                    // Add Triangle Indices (winding order matters for visibility)
                	triangles.Add(Index3);
                	triangles.Add(Index2);
                    triangles.Add(Index1);

                    
                    // Simple UV Mapping (planar projection from top)
                    UVs.Add(FVector2D(V1.X, V1.Y) / 512.0f);
                    UVs.Add(FVector2D(V2.X, V2.Y) / 512.0f);
                    UVs.Add(FVector2D(V3.X, V3.Y) / 512.0f);
                }
            }
        }
    }
	// Upload to GPU
	mesh->CreateMeshSection_LinearColor(0, vertices, triangles, normals, UVs, TArray<FLinearColor>(), tangents, true);
}






