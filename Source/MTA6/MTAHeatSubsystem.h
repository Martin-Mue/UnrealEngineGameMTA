#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MTAHeatSubsystem.generated.h"

/**
 * Bezirks-Ruf: Auffaelligkeit wird pro Bezirk gefuehrt, nicht global.
 * Wer in einem Bezirk auffaellt, ist dort bekannt (siehe docs/WELTKONZEPT.md).
 */
UCLASS()
class MTA6_API UMTAHeatSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Erhoeht die Fahndung in einem Bezirk (0..5 Sterne). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Law")
	void AddHeat(FName DistrictId, float Amount);

	UFUNCTION(BlueprintPure, Category = "MTA6|Law")
	float GetHeat(FName DistrictId) const;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }

private:
	UPROPERTY()
	TMap<FName, float> DistrictHeat;
};
