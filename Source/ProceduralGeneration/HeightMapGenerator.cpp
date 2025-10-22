// Fill out your copyright notice in the Description page of Project Settings.


#include "HeightMapGenerator.h"


// Sets default values
AHeightMapGenerator::AHeightMapGenerator()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AHeightMapGenerator::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AHeightMapGenerator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

