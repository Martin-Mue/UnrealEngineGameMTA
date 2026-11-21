#include "MTAVehicleWheels.h"

UMTAWheelFront::UMTAWheelFront()
{
	// Maße in cm; entsprechen dem Reifen des Template-Sportwagens
	WheelRadius = 35.f;
	WheelWidth = 25.f;
	WheelMass = 20.f;
	FrictionForceMultiplier = 3.f;

	bAffectedBySteering = true;
	MaxSteerAngle = 40.f;
	bAffectedByBrake = true;
	bAffectedByHandbrake = false;
	bAffectedByEngine = false;
	MaxBrakeTorque = 4500.f;

	SuspensionMaxRaise = 8.f;
	SuspensionMaxDrop = 12.f;
	SuspensionDampingRatio = 1.f;
	SpringRate = 250.f;
	SpringPreload = 50.f;
}

UMTAWheelRear::UMTAWheelRear()
{
	WheelRadius = 35.f;
	WheelWidth = 25.f;
	WheelMass = 20.f;
	FrictionForceMultiplier = 3.f;

	bAffectedBySteering = false;
	bAffectedByBrake = true;
	bAffectedByHandbrake = true;
	bAffectedByEngine = true;
	MaxBrakeTorque = 4500.f;
	MaxHandBrakeTorque = 6000.f;

	SuspensionMaxRaise = 8.f;
	SuspensionMaxDrop = 12.f;
	SuspensionDampingRatio = 1.f;
	SpringRate = 250.f;
	SpringPreload = 50.f;
}
