#pragma once

#include "CoreMinimal.h"

struct FCaveCarveOp
{
	FVector WorldCenter;
	float WorldRadius;
	bool bDistorted;
	float MinRoof = 0.f;
};
