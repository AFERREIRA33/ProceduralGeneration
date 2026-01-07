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
	noise->SetFrequency(0.02f);
}




// Called when the game starts or when spawned
void ACaveMarchingCube::BeginPlay()
{
	Super::BeginPlay();
	densityGrid.Init(1.0f, gridSize * gridSize * gridSize);
	GenerateCaveSystem();
	//CaveWorm();
	//GenerateMesh();
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

void ACaveMarchingCube::CarveSphere(FVector Center, float Radius)
{
	int32 R = FMath::CeilToInt(Radius + 1);

	for (int32 z = -R; z <= R; z++)
	{
		for (int32 y = -R; y <= R; y++)
		{
			for (int32 x = -R; x <= R; x++)
			{
				FVector VoxelOffset(x, y, z);
				FVector TargetPos = Center + VoxelOffset;
                
				// Bounds Check
				if (TargetPos.X >= 1 && TargetPos.X < gridSize - 1 &&
					TargetPos.Y >= 1 && TargetPos.Y < gridSize - 1 &&
					TargetPos.Z >= 1 && TargetPos.Z < gridSize - 1)
				{
					float Dist = VoxelOffset.Size();
					if (Dist < Radius)
					{
						int32 Idx = GetIndex((int32)TargetPos.X, (int32)TargetPos.Y, (int32)TargetPos.Z);
						// Set density to negative (Air)
						// Using FMath::Min ensures we don't accidentally fill a hole we already dug
						densityGrid[Idx] = FMath::Min(densityGrid[Idx], -1.0f);
					}
				}
			}
		}
	}
}

void ACaveMarchingCube::GenerateCaveSystem()
{
	densityGrid.Init(1.0f, gridSize * gridSize * gridSize);

    // 2. Setup Worm Management
    TArray<FWorm> ActiveWorms;
    int32 TotalWormsSpawned = 0;

    // --- CREATE THE MOTHER WORM ---
    // Start at Top Center (Surface)
    FWorm MotherWorm;
    MotherWorm.Position = FVector(gridSize / 2, gridSize / 2, gridSize - 5);
    MotherWorm.Direction = FVector(0, 0, -1); // Initial push DOWN
    MotherWorm.RemainingSteps = 400; // Long life for main tunnel
    MotherWorm.Radius = 5.0f; // Big tunnel
    MotherWorm.NoiseOffset = FMath::RandRange(0.0f, 1000.0f);
    
    ActiveWorms.Add(MotherWorm);
    TotalWormsSpawned++;

    // 3. Simulation Loop
    // Continue until all worms have died
    while (ActiveWorms.Num() > 0)
    {
        // Iterate backwards so we can remove items safely
        for (int32 i = ActiveWorms.Num() - 1; i >= 0; i--)
        {
            FWorm& CurrentWorm = ActiveWorms[i];

            // --- MOVEMENT LOGIC ---
            // Use Noise + Momentum to calculate new direction
            // We use (OriginalSteps - Remaining) as the time value for noise
            float Time = (400 - CurrentWorm.RemainingSteps) * 0.1f; 
            
            float NoiseX = noise->GetNoise(Time + CurrentWorm.NoiseOffset, 0.0f);
            float NoiseY = noise->GetNoise(Time + CurrentWorm.NoiseOffset, 100.0f);
            float NoiseZ = noise->GetNoise(Time + CurrentWorm.NoiseOffset, 200.0f);

        	float HorizontalScale = 2.5f; 

        	// Weaken Z so it doesn't wiggle up/down as violently (flattens the floor/ceiling)
        	float VerticalScale = 1.0f; //initial 0.3f

        	// Reduce the downward bias significantly. 
        	// -0.6f was a "Slide"; -0.05f is a "Slight Slope"
        	float DownwardBias = -0.05f;
        	
            // Add strong downward bias to Z noise (-0.6) to keep caves going deep
            //FVector NoiseDir = FVector(NoiseX, NoiseY, NoiseZ - 0.6f); 
        	FVector NoiseDir = FVector(
				NoiseX * HorizontalScale, 
				NoiseY * HorizontalScale, 
				(NoiseZ * VerticalScale) + DownwardBias
			);
            // Blend new noise with old direction (Inertia) to prevent jagged turns
        	FVector NewDirection = (CurrentWorm.Direction * 0.7f + NoiseDir * 0.3f).GetSafeNormal();
            CurrentWorm.Direction = NewDirection;

            // Move
            CurrentWorm.Position += NewDirection * (CurrentWorm.Radius * 0.6f); // Move roughly half a radius per step

            // --- CARVE ---
            CarveSphere(CurrentWorm.Position, CurrentWorm.Radius);

            // --- BRANCHING LOGIC ---
            // Only branch if we haven't hit the limit AND rolled the dice
            if (TotalWormsSpawned < MaxWormsTotal && FMath::FRand() < BranchProbability)
            {
                FWorm ChildWorm;
                ChildWorm.Position = CurrentWorm.Position; // Start where parent is
                
                // Shoot child off in a random direction (slightly different from parent)
                ChildWorm.Direction = FMath::VRand(); 
                
                ChildWorm.RemainingSteps = CurrentWorm.RemainingSteps / 2; // Child lives half as long
                ChildWorm.Radius = FMath::Max(2.0f, CurrentWorm.Radius * 0.8f); // Child is smaller
                ChildWorm.NoiseOffset = FMath::RandRange(0.0f, 1000.0f); // Unique path

                ActiveWorms.Add(ChildWorm); // Add to end of list (processed next frame)
                TotalWormsSpawned++;
            }

            // --- DEATH ---
            CurrentWorm.RemainingSteps--;
            
            // Kill if steps done OR if it wandered out of bounds
            bool bOutOfBounds = (CurrentWorm.Position.X <= 2 || CurrentWorm.Position.X >= gridSize - 2 ||
                                 CurrentWorm.Position.Y <= 2 || CurrentWorm.Position.Y >= gridSize - 2 ||
                                 CurrentWorm.Position.Z <= 2 || CurrentWorm.Position.Z >= gridSize - 2);

            if (CurrentWorm.RemainingSteps <= 0 || bOutOfBounds)
            {
                ActiveWorms.RemoveAt(i);
            }
        }
    }

    // 4. Generate the final mesh
    GenerateMesh();
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






