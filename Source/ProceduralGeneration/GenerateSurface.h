// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include  "ProceduralMeshComponent.h"
#include "GenerateSurface.generated.h"

UCLASS()
class PROCEDURALGENERATION_API AGenerateSurface : public AActor
{
	GENERATED_BODY()

public:
	AGenerateSurface();

protected:

	virtual void BeginPlay() override;


};
