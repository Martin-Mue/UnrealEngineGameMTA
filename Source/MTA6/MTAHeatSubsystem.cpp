#include "MTAHeatSubsystem.h"

void UMTAHeatSubsystem::AddHeat(FName DistrictId, float Amount)
{
	float& Value = DistrictHeat.FindOrAdd(DistrictId);
	Value = FMath::Clamp(Value + Amount, 0.f, 5.f);
}

float UMTAHeatSubsystem::GetHeat(FName DistrictId) const
{
	const float* Value = DistrictHeat.Find(DistrictId);
	return Value ? *Value : 0.f;
}
