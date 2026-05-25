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

ACaveMarchingCube::~ACaveMarchingCube()
{
	delete(noise);
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

void ACaveMarchingCube::CarveRoom(FVector Center, float BaseRadius)
{
	// 1. Determine bounding box for this room
	// We add some buffer for the noise distortion
	int32 R = FMath::CeilToInt(BaseRadius + 5.0f);

	// 2. Configure Noise for wall roughness
	// High frequency = rugged, rocky walls
	FastNoiseLite WallNoise; 
	WallNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	WallNoise.SetFrequency(0.1f); 

	for (int32 z = -R; z <= R; z++)
	{
		for (int32 y = -R; y <= R; y++)
		{
			for (int32 x = -R; x <= R; x++)
			{
				FVector VoxelOffset(x, y, z);
				FVector TargetPos = Center + VoxelOffset;

				// Bounds Check
				if (TargetPos.X <= 0 || TargetPos.X >= gridSize - 1 ||
					TargetPos.Y <= 0 || TargetPos.Y >= gridSize - 1 ||
					TargetPos.Z <= 0 || TargetPos.Z >= gridSize - 1)
					continue;

				// 3. Calculate Distance
				float Dist = VoxelOffset.Size();

				// 4. Distort the Radius
				// We sample noise based on the direction from the center
				float NoiseValue = WallNoise.GetNoise((float)x, (float)y, (float)z);
                
				// The "Real" radius for this specific voxel fluctuates
				// e.g. Radius 15 becomes 13 to 17 depending on noise
				float NoisyRadius = BaseRadius + (NoiseValue * 5.0f); 

				if (Dist < NoisyRadius)
				{
					int32 Idx = GetGlobalIndex((int32)TargetPos.X, (int32)TargetPos.Y, (int32)TargetPos.Z);
                    
					// Carve Air (-1.0)
					densityGrid[Idx] = FMath::Min(densityGrid[Idx], -1.0f);
				}
			}
		}
	}
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
						int32 Idx = GetGlobalIndex((int32)TargetPos.X, (int32)TargetPos.Y, (int32)TargetPos.Z);
						// Set density to negative (Air)
						// Using FMath::Min ensures we don't accidentally fill a hole we already dug
						densityGrid[Idx] = FMath::Min(densityGrid[Idx], -1.0f);
					}
				}
			}
		}
	}
}

int32 ACaveMarchingCube::GetGlobalIndex(int32 X, int32 Y, int32 Z)
{
	// Safety Clamp
	X = FMath::Clamp(X, 0, globalSize - 1);
	Y = FMath::Clamp(Y, 0, globalSize - 1);
	Z = FMath::Clamp(Z, 0, globalSize - 1);
    
	return X + globalSize * (Y + globalSize * Z);
}

void ACaveMarchingCube::GenerateChunkMesh(int32 ChunkX, int32 ChunkY, int32 SectionIndex)
{
	// 1. Data Structures
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FProcMeshTangent> Tangents;

    // 2. Calculate Offsets
    int32 StartX = ChunkX * ChunkSize;
    int32 StartY = ChunkY * ChunkSize;

    // 3. Marching Cubes Tables (Standard Paul Bourke)
    // Ensure you have these defined in your header or a static library
    const int32 EdgeConnection[12][2] = { 
        {0,1}, {1,2}, {2,3}, {3,0}, {4,5}, {5,6}, {6,7}, {7,4}, 
        {0,4}, {1,5}, {2,6}, {3,7} 
    };

    const FVector CornerOffsets[8] = {
        FVector(0, 0, 0), FVector(1, 0, 0), FVector(1, 1, 0), FVector(0, 1, 0),
        FVector(0, 0, 1), FVector(1, 0, 1), FVector(1, 1, 1), FVector(0, 1, 1)
    };

    // 4. Main Loop
    // We iterate Z normally. 
    // For X and Y, we iterate up to ChunkSize. 
    // Note: We need data at (x+1), so the grid must support it.
    for (int32 z = 0; z < globalSize - 1; z++)
    {
        for (int32 y = 0; y < ChunkSize; y++)
        {
            for (int32 x = 0; x < ChunkSize; x++)
            {
                // Global Coordinates
                int32 GlobalX = StartX + x;
                int32 GlobalY = StartY + y;

                // Stop if we are at the very edge of the total world
                if (GlobalX >= globalSize - 1 || GlobalY >= globalSize - 1) continue;

                // 5. Calculate Cube Index
                int32 CubeIndex = 0;
                float CornerValues[8];
                FVector GridPos(GlobalX, GlobalY, z);

                for (int i = 0; i < 8; i++)
                {
                    // Calculate global position of this corner
                    FVector CornerGlobalPos = GridPos + CornerOffsets[i];
                    
                    // Sample Density
                    float Val = GetDensitySafe(CornerGlobalPos.X, CornerGlobalPos.Y, CornerGlobalPos.Z);
                    CornerValues[i] = Val;

                    // Determine if inside or outside surface (Assume SurfaceLevel = 0.0f)
                    if (Val < surfaceLevel) 
                        CubeIndex |= (1 << i);
                }

                // Skip completely empty or completely full cubes
                if (edgeTable[CubeIndex] == 0) continue;

                // 6. Interpolate Vertices
                FVector IntersectVerts[12];
                FVector IntersectNormals[12]; // We will interpolate normals too!

                for (int i = 0; i < 12; i++)
                {
                    if (edgeTable[CubeIndex] & (1 << i))
                    {
                        int32 Corner1 = EdgeConnection[i][0];
                        int32 Corner2 = EdgeConnection[i][1];

                        FVector P1 = GridPos + CornerOffsets[Corner1];
                        FVector P2 = GridPos + CornerOffsets[Corner2];
                        float V1 = CornerValues[Corner1];
                        float V2 = CornerValues[Corner2];

                        // Position Interpolation
                        IntersectVerts[i] = VertexInterpolation(P1, P2, V1, V2);

                        // Normal Interpolation (High Quality Lighting)
                        // Get the gradient normal at the two corners
                        FVector N1 = CalculateGradientNormal(P1.X, P1.Y, P1.Z);
                        FVector N2 = CalculateGradientNormal(P2.X, P2.Y, P2.Z);
                        
                        // Linear interpolate the normal based on exactly where the surface cuts
                        float Mu = (surfaceLevel - V1) / (V2 - V1);
                        IntersectNormals[i] = FMath::Lerp(N1, N2, Mu).GetSafeNormal();
                    }
                }

                // 7. Build Triangles
                for (int i = 0; triTable[CubeIndex][i] != -1; i += 3)
                {
                    int32 EdgeIndex1 = triTable[CubeIndex][i];
                    int32 EdgeIndex2 = triTable[CubeIndex][i + 1];
                    int32 EdgeIndex3 = triTable[CubeIndex][i + 2];

                    // Add Vertices (Scaled by VoxelSize)
                    int32 VIndex = Vertices.Num();
                    Vertices.Add(IntersectVerts[EdgeIndex1] * voxelSize);
                    Vertices.Add(IntersectVerts[EdgeIndex2] * voxelSize);
                    Vertices.Add(IntersectVerts[EdgeIndex3] * voxelSize);

                    // Add Normals (Already calculated!)
                    Normals.Add(IntersectNormals[EdgeIndex1]);
                    Normals.Add(IntersectNormals[EdgeIndex2]);
                    Normals.Add(IntersectNormals[EdgeIndex3]);

                    // Add Triangles
                	Triangles.Add(VIndex + 2);
                	Triangles.Add(VIndex + 1);
                    Triangles.Add(VIndex);
                    
                    

                    // Add Simple Planar UVs (Top-down projection)
                    // Divide by 512 or 1024 to spread texture out
                    UVs.Add(FVector2D(Vertices[VIndex].X, Vertices[VIndex].Y) / 512.0f);
                    UVs.Add(FVector2D(Vertices[VIndex+1].X, Vertices[VIndex+1].Y) / 512.0f);
                    UVs.Add(FVector2D(Vertices[VIndex+2].X, Vertices[VIndex+2].Y) / 512.0f);
                    
                    // Add dummy tangent (Procedural Mesh can auto-calc this later if needed)
                    Tangents.Add(FProcMeshTangent(1, 0, 0));
                    Tangents.Add(FProcMeshTangent(1, 0, 0));
                    Tangents.Add(FProcMeshTangent(1, 0, 0));
                }
            }
        }
    }

    // 8. Submit to GPU
    // Note: We do NOT use KismetLibrary::CalculateTangents here because we did manual normals.
    mesh->CreateMeshSection_LinearColor(SectionIndex, Vertices, Triangles, Normals, UVs, TArray<FLinearColor>(), Tangents, false);
}

float ACaveMarchingCube::GetDensitySafe(int32 X, int32 Y, int32 Z)
{
	if (X < 0 || X >= globalSize || Y < 0 || Y >= globalSize || Z < 0 || Z >= globalSize)
	{
		return 1.0f; // Treat outside world as solid ground (or -1.0f for air, your choice)
	}
	return densityGrid[GetGlobalIndex(X, Y, Z)];
}

FVector ACaveMarchingCube::CalculateGradientNormal(int32 X, int32 Y, int32 Z)
{
	float D_X = GetDensitySafe(X - 1, Y, Z) - GetDensitySafe(X + 1, Y, Z);
	float D_Y = GetDensitySafe(X, Y - 1, Z) - GetDensitySafe(X, Y + 1, Z);
	float D_Z = GetDensitySafe(X, Y, Z - 1) - GetDensitySafe(X, Y, Z + 1);

	return FVector(D_X, D_Y, D_Z).GetSafeNormal();
}

void ACaveMarchingCube::GenerateCaveSystem()
{
    // 1. Calculate Global Resolution based on your Chunk Settings
    // e.g. 32 * 4 = 128
    globalSize = ChunkSize * WorldWidthInChunks;

    // 2. Run Generation on a Background Thread
    Async(EAsyncExecution::Thread, [this]()
    {
        // --- THREAD SAFE ZONE START ---
        
        // Create a local grid so we don't touch the main one while playing
        TArray<float> LocalGrid;
        LocalGrid.Init(1.0f, globalSize * globalSize * globalSize);

        // Local Noise Instance (Thread Safe)
        FastNoiseLite ThreadNoise;
        ThreadNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
        ThreadNoise.SetFrequency(0.02f);

        // Local Wall Noise for Rooms
        FastNoiseLite WallNoise;
        WallNoise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
        WallNoise.SetFrequency(0.1f);

        // --- HELPER LAMBDAS (To modify LocalGrid safely) ---
        
        // Lambda: Carve Sphere
        auto CarveSphereLocal = [&](FVector Center, float Radius) 
        {
            int32 R = FMath::CeilToInt(Radius + 1);
            for (int32 z = -R; z <= R; z++) {
                for (int32 y = -R; y <= R; y++) {
                    for (int32 x = -R; x <= R; x++) {
                        FVector Offset(x, y, z);
                        FVector Target = Center + Offset;
                        // Global Bounds Check
                        if (Target.X > 0 && Target.X < globalSize - 1 &&
                            Target.Y > 0 && Target.Y < globalSize - 1 &&
                            Target.Z > 0 && Target.Z < globalSize - 1) 
                        {
                            if (Offset.Size() < Radius) {
                                int32 Idx = GetGlobalIndex(Target.X, Target.Y, Target.Z);
                                LocalGrid[Idx] = FMath::Min(LocalGrid[Idx], -1.0f);
                            }
                        }
                    }
                }
            }
        };

        // Lambda: Carve Room (Distorted)
        auto CarveRoomLocal = [&](FVector Center, float BaseRadius)
        {
            int32 R = FMath::CeilToInt(BaseRadius + 5.0f);
            for (int32 z = -R; z <= R; z++) {
                for (int32 y = -R; y <= R; y++) {
                    for (int32 x = -R; x <= R; x++) {
                        FVector Offset(x, y, z);
                        FVector Target = Center + Offset;
                        if (Target.X > 0 && Target.X < globalSize - 1 &&
                            Target.Y > 0 && Target.Y < globalSize - 1 &&
                            Target.Z > 0 && Target.Z < globalSize - 1)
                        {
                            float NoiseVal = WallNoise.GetNoise((float)x, (float)y, (float)z);
                            float NoisyRadius = BaseRadius + (NoiseVal * 5.0f);
                            if (Offset.Size() < NoisyRadius) {
                                int32 Idx = GetGlobalIndex(Target.X, Target.Y, Target.Z);
                                LocalGrid[Idx] = FMath::Min(LocalGrid[Idx], -1.0f);
                            }
                        }
                    }
                }
            }
        };

        // --- WORM SIMULATION ---

        TArray<FWorm> ActiveWorms;
        int32 TotalWormsSpawned = 0;

        // MOTHER WORM SETUP
        FWorm Mother;
        // Start Top Center of the entire world
        Mother.Position = FVector(globalSize / 2, globalSize / 2, globalSize - 5); 
        Mother.Direction = FVector(0, 0, -1);
        Mother.RemainingSteps = 1000; // Long life for big world
        Mother.Radius = 6.0f; 
        Mother.NoiseOffset = FMath::RandRange(0.0f, 1000.0f);

        ActiveWorms.Add(Mother);
        TotalWormsSpawned++;

        while (ActiveWorms.Num() > 0)
        {
            for (int32 i = ActiveWorms.Num() - 1; i >= 0; i--)
            {
                FWorm& Worm = ActiveWorms[i];
                float Time = (1000 - Worm.RemainingSteps) * 0.1f;

                // 1. HORIZONTAL BIASED MOVEMENT
                float NX = ThreadNoise.GetNoise(Time + Worm.NoiseOffset, 0.0f);
                float NY = ThreadNoise.GetNoise(Time + Worm.NoiseOffset, 100.0f);
                float NZ = ThreadNoise.GetNoise(Time + Worm.NoiseOffset * 0.5f, 200.0f);

                // Horizontal Strength 2.5, Vertical 0.3, Gravity -0.05
                FVector NoiseDir = FVector(NX * 2.5f, NY * 2.5f, (NZ * 0.3f) - 0.05f);
                
                // Inertia Blend
                Worm.Direction = (Worm.Direction * 0.6f + NoiseDir.GetSafeNormal() * 0.4f).GetSafeNormal();
                Worm.Position += Worm.Direction * (Worm.Radius * 0.5f);

                // 2. CHECK BOUNDS (Global Size)
                if (Worm.Position.X <= 2 || Worm.Position.X >= globalSize - 2 ||
                    Worm.Position.Y <= 2 || Worm.Position.Y >= globalSize - 2 ||
                    Worm.Position.Z <= 2 || Worm.Position.Z >= globalSize - 2)
                {
                    ActiveWorms.RemoveAt(i);
                    continue;
                }

                // 3. CARVE
                CarveSphereLocal(Worm.Position, Worm.Radius);

                // 4. ROOM GENERATION
                // Only if old enough (don't break entrance)
                if ((1000 - Worm.RemainingSteps) > 20 && FMath::FRand() < RoomProbability) 
                {
                    CarveRoomLocal(Worm.Position, 15.0f); 
                }

                // 5. BRANCHING
                if (TotalWormsSpawned < MaxWormsTotal && FMath::FRand() < BranchProbability)
                {
                    FWorm Child = Worm;
                    Child.Direction = FMath::VRand(); 
                    Child.RemainingSteps = Worm.RemainingSteps / 2;
                    Child.Radius = FMath::Max(2.5f, Worm.Radius * 0.8f);
                    Child.NoiseOffset = FMath::RandRange(0.0f, 1000.0f);
                    ActiveWorms.Add(Child);
                    TotalWormsSpawned++;
                }

                // 6. DEATH
                Worm.RemainingSteps--;
                if (Worm.RemainingSteps <= 0) ActiveWorms.RemoveAt(i);
            }
        }

        // --- RETURN TO GAME THREAD ---
        AsyncTask(ENamedThreads::GameThread, [this, LocalGrid]()
        {
            // 1. Apply Data
            this->densityGrid = LocalGrid;

            // 2. Loop Chunks and Generate Meshes
            int32 SectionIdx = 0;
            for (int32 Cy = 0; Cy < WorldWidthInChunks; Cy++)
            {
                for (int32 Cx = 0; Cx < WorldWidthInChunks; Cx++)
                {
                    // Call the Mesh Generator (using Gradient Normals)
                    GenerateChunkMesh(Cx, Cy, SectionIdx);
                    SectionIdx++;
                }
            }
        });
    });
}





