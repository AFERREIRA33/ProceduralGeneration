#include "GenerateCave.h"
#include "CaveMarchingCube.h"
#include "Kismet/GameplayStatics.h"


// Sets default values
AGenerateCave::AGenerateCave()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

void AGenerateCave::BeginPlay()
{
	Super::BeginPlay();

	GenerateCave();

}


// Called every frame to update the game
void AGenerateCave::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);



	for (int i = 0; i < ChunkLoadPerFrame && !PendingChunks.IsEmpty(); ++i)
	{
		FIntVector chunkCoords;
		PendingChunks.Dequeue(chunkCoords);
		SpawnChunkAt(chunkCoords);
	}

	// Optionally unload distant chunks
	FVector PlayerPos = GetWorld()->GetFirstPlayerController()->GetPawn()->GetActorLocation();
	for (auto it = LoadedChunks.CreateIterator(); it; ++it)
	{
		if (it.Value() && FVector::Dist(it.Value()->GetActorLocation(), PlayerPos) > drawDistance * size * 100)
		{
			it.Value()->Destroy();
			it.RemoveCurrent();
		}
	}

	// Enqueue new chunks around player
	GenerateCave();
}

// Generate all chunks around the player within a radius of drawDistance
void AGenerateCave::GenerateCave()
{
	FIntVector PlayerChunk = GetPlayerChunk();

	for (int x = -drawDistance; x <= drawDistance; x++)
	{
		for (int y = -drawDistance; y <= drawDistance; y++)
		{
			for (int z = -drawDistance; z <= drawDistance; z++)
			{
				FIntVector chunkCoords = PlayerChunk + FIntVector(x, y, z);
				if (!LoadedChunks.Contains(chunkCoords))
				{
					PendingChunks.Enqueue(chunkCoords);
					LoadedChunks.Add(chunkCoords, nullptr); // reserve spot
				}
			}
		}
	}
}

// Returns the coordinates of the chunk in which the player is located
FIntVector AGenerateCave::GetPlayerChunk() const
{

	FVector PlayerPos = GetWorld()->GetFirstPlayerController()->GetPawn()->GetActorLocation();
	int32 cx = FMath::FloorToInt(PlayerPos.X / (size * 100));
	int32 cy = FMath::FloorToInt(PlayerPos.Y / (size * 100));
	int32 cz = FMath::FloorToInt(PlayerPos.Z / (size * 100));
	return FIntVector(cx, cy, cz);
}

// Creates and initializes a chunk at the specified coordinates
void AGenerateCave::SpawnChunkAt(const FIntVector& chunkCoords)
{

	FVector WorldPos = FVector(chunkCoords.X * size * 100, chunkCoords.Y * size * 100, chunkCoords.Z * size * 100);
	FTransform transform(FRotator::ZeroRotator, WorldPos, FVector::OneVector);

	ACaveMarchingCube* chunk = GetWorld()->SpawnActorDeferred<ACaveMarchingCube>(
		ACaveMarchingCube::StaticClass(),
		transform,
		this
	);

	chunk->frequency = frequency;
	chunk->material = material;
	chunk->size = size;

	UGameplayStatics::FinishSpawningActor(chunk, transform);

	// Assign chunk to map
	LoadedChunks[chunkCoords] = chunk;
}
