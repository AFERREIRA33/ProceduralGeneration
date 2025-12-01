// Fill out your copyright notice in the Description page of Project Settings.


#include "ChunkBase.h"


// Sets default values
AChunkBase::AChunkBase()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AChunkBase::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AChunkBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

