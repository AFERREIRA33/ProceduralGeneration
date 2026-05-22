// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BiomeData.generated.h"

/**
 * 
 */
UCLASS()
class PROCEDURALGENERATION_API UBiomeData : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BiomeData")
	TArray<FString> Biomes;
};
