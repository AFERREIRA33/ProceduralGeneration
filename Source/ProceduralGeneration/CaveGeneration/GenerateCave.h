#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GenerateCave.generated.h"

class ACaveMarchingCube;
UCLASS()
class PROCEDURALGENERATION_API AGenerateCave : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AGenerateCave();

	UPROPERTY(EditInstanceOnly, Category="Generation")
	float surfaceLevel = 0;
	
	UPROPERTY(EditInstanceOnly, Category="Generation")
	int drawDistance = 5;
	
	UPROPERTY(EditInstanceOnly, Category="Generation")
	float frequency = 0.03f;
	
	UPROPERTY(EditInstanceOnly, Category="Generation")
	int size = 16;

	UPROPERTY(EditInstanceOnly, Category="Generation")
	TObjectPtr<UMaterialInterface> material;

	TMap<FIntVector, ACaveMarchingCube*> LoadedChunks;

	UPROPERTY(EditAnywhere)
	int32 ChunkLoadPerFrame = 4;  // How many chunks to spawn per frame

	TQueue<FIntVector> PendingChunks; 

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	// Called every frame


	void GenerateCave();
	FIntVector GetPlayerChunk() const;
	void SpawnChunkAt(const FIntVector& chunkCoords);
};
