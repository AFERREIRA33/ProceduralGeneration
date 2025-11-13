#include "GenerateCave.h"
#include "CaveMarchingCube.h"
#include "Kismet/GameplayStatics.h"


// Sets default values
AGenerateCave::AGenerateCave()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AGenerateCave::BeginPlay()
{
	Super::BeginPlay();
	Generate();
}

// Called every frame
void AGenerateCave::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AGenerateCave::Generate()
{
	for (int x = -drawDistance; x <= drawDistance; x++)
	{
		for (int y = -drawDistance; y <= drawDistance; ++y)
		{
			for (int z = -drawDistance; z <= drawDistance; ++z)
			{
				auto transform = FTransform(
					FRotator::ZeroRotator,
					FVector(x * size * 100, y * size * 100, z * size * 100),
					FVector::OneVector
				);

				const auto chunk = GetWorld()->SpawnActorDeferred<ACaveMarchingCube>(ACaveMarchingCube::StaticClass(), transform,this);
				chunk->frequency = frequency;
				chunk->material = material;
				chunk->size = size;
				UGameplayStatics::FinishSpawningActor(chunk, transform);
			}
		}
	}
}

