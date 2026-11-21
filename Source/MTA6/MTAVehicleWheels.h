#pragma once

#include "CoreMinimal.h"
#include "ChaosVehicleWheel.h"
#include "MTAVehicleWheels.generated.h"

/** Vorderrad des Standard-Sportwagens (gelenkt, gebremst). Werte passen zum Template-Fahrzeug SKM_SportsCar. */
UCLASS()
class MTA6_API UMTAWheelFront : public UChaosVehicleWheel
{
	GENERATED_BODY()

public:
	UMTAWheelFront();
};

/** Hinterrad des Standard-Sportwagens (angetrieben, Handbremse). */
UCLASS()
class MTA6_API UMTAWheelRear : public UChaosVehicleWheel
{
	GENERATED_BODY()

public:
	UMTAWheelRear();
};
