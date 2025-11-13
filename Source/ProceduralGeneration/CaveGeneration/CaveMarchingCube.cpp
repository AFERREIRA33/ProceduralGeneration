#include "CaveMarchingCube.h"
#include "Utils/FastNoiseLite.h"
#include "ProceduralMeshComponent.h"


// Sets default values
ACaveMarchingCube::ACaveMarchingCube()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	mesh = CreateDefaultSubobject<UProceduralMeshComponent>("Mesh");
	noise = new FastNoiseLite();

	mesh->SetCastShadow(false);

	// Set Mesh as root
	SetRootComponent(mesh);
}

ACaveMarchingCube::~ACaveMarchingCube()
{
	delete noise;
}

// Called when the game starts or when spawned
void ACaveMarchingCube::BeginPlay()
{
	Super::BeginPlay();
	noise->SetFrequency(frequency);
	noise->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
	noise->SetFractalType(FastNoiseLite::FractalType_FBm);

	Setup();
	GenerateHeightMap(GetActorLocation() / 100);
	GenerateMesh();

	UE_LOG(LogTemp, Warning, TEXT("Vertex Count : %d"), vertexCount);
	
	ApplyMesh();
}

// Called every frame
void ACaveMarchingCube::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ACaveMarchingCube::Setup()
{
	Voxels.SetNum((size + 1) * (size + 1) * (size + 1));
}

void ACaveMarchingCube::GenerateHeightMap(const FVector position)
{
	for (int x = 0; x <= size; ++x)
	{
		for (int y = 0; y <= size; ++y)
		{
			for (int z = 0; z <= size; ++z)
			{
				Voxels[GetVoxelIndex(x,y,z)] = noise->GetNoise(x + position.X, y + position.Y, z + position.Z);	
			}
		}
	}
}

void ACaveMarchingCube::GenerateMesh()
{
	if (surfaceLevel > 0.0f)
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

	float Cube[8];
	
	for (int X = 0; X < size; ++X)
	{
		for (int Y = 0; Y < size; ++Y)
		{
			for (int Z = 0; Z < size; ++Z)
			{
				for (int i = 0; i < 8; ++i)
				{
					Cube[i] = Voxels[GetVoxelIndex(X + VertexOffset[i][0],Y + VertexOffset[i][1],Z + VertexOffset[i][2])];
				}

				CaveMarch(X,Y,Z, Cube);
			}
		}
	}
}



void ACaveMarchingCube::CaveMarch(int X, int Y, int Z, const float cube[8])
{
	int VertexMask = 0;
	FVector EdgeVertex[12];


	for (int i = 0; i < 8; ++i)
	{
		if (cube[i] <= surfaceLevel) VertexMask |= 1 << i;
	}

	const int EdgeMask = CubeEdgeFlags[VertexMask];
	
	if (EdgeMask == 0) return;
		

	for (int i = 0; i < 12; ++i)
	{
		if ((EdgeMask & 1 << i) != 0)
		{
			float offset = 0;
			offset = Interpolation(cube[EdgeConnection[i][0]], cube[EdgeConnection[i][1]]);

			EdgeVertex[i].X = X + (VertexOffset[EdgeConnection[i][0]][0] + offset * EdgeDirection[i][0]);
			EdgeVertex[i].Y = Y + (VertexOffset[EdgeConnection[i][0]][1] + offset * EdgeDirection[i][1]);
			EdgeVertex[i].Z = Z + (VertexOffset[EdgeConnection[i][0]][2] + offset * EdgeDirection[i][2]);
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

		meshData.Vertices.Append({V1, V2, V3});
		
		meshData.Triangles.Append({
			vertexCount + TriangleOrder[0],
			vertexCount + TriangleOrder[1],
			vertexCount + TriangleOrder[2]
		});

		meshData.Normals.Append({
			Normal,
			Normal,
			Normal
		});

		meshData.Colors.Append({
			Color,
			Color,
			Color
		});

		vertexCount += 3;
	}
}


void ACaveMarchingCube::ApplyMesh() const
{
	mesh->SetMaterial(0, material);
	mesh->CreateMeshSection(
		0,
		meshData.Vertices,
		meshData.Triangles,
		meshData.Normals,
		meshData.UV0,
		meshData.Colors,
		TArray<FProcMeshTangent>(),
		true
	);
}

int ACaveMarchingCube::GetVoxelIndex(int X, int Y, int Z) const
{
	return Z * (size + 1) * (size + 1) + Y * (size + 1) + X;
}

float ACaveMarchingCube::Interpolation(float V1, float V2) const
{
	
	const float delta = V2 - V1;

	if (fabsf(delta) < 1e-6f)
		return 0.5f;


	float t = (surfaceLevel - V1) / delta;
	return FMath::Clamp(t, 0.0f, 1.0f);
}

