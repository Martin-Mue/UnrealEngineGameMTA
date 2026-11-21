#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "MTAProgressionTypes.h"
#include "MTAVehicleCatalog.generated.h"

/** Fahrzeugmodell aus Content/Data/vehicles.json (alle Marken/Modelle frei erfunden). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAVehicleModel
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") FName Brand;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") FString BrandName;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") FString ModelName;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") EMTAVehicleClass Class = EMTAVehicleClass::Sedan;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") EMTARarity Rarity = EMTARarity::Common;
	/** Listenpreis (vor Bezirks-/Knappheitsfaktor). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") int64 Price = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") FMTAVehicleStats BaseStats;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") int32 Seats = 4;
	/** Bezirke mit Haendler fuer dieses Modell. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") TArray<FName> Dealers;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") bool bPurchasable = true;
	/** Alternativer Erwerb: stolen_only, contract, reward, auction. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") FString Acquire;
	/** Mindest-Fraktionsstand fuer Kauf/Verleihung. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") TMap<FName, float> FactionReq;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") TArray<FName> TuningSlots;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") FString Description;
};

/** Tuning-Teil (vehicles.json "tuning"). */
USTRUCT(BlueprintType)
struct MTA6_API FMTATuningPart
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") FName Id;
	/** engine, transmission, tires, brakes, suspension, armor, paint, underglow, module */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") FName Category;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") int32 Level = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") int64 Price = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") TMap<FName, float> StandingReq;
	/** In diesen Bezirken illegal: Kamera-Treffer erhoeht Aegis, KCG kann Bussgeld "illegalTuning" verhaengen. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") TArray<FName> IllegalIn;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") int64 MaintenancePerDay = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") TArray<FMTAStatModifier> Effects;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Vehicle") TArray<FName> Tags;
};

/**
 * Fahrzeugkatalog: Modelle, Klassen-Regeln, Raritaeten und Tuning-Teile aus vehicles.json.
 * Reine Daten, kein Spielzustand (Besitz liegt in UMTAGarageSubsystem).
 */
UCLASS()
class MTA6_API UMTAVehicleCatalog : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Vehicle")
	bool LoadCatalog(const FString& RelativePath = TEXT("Data/vehicles.json"));

	UFUNCTION(BlueprintPure, Category = "MTA6|Vehicle")
	bool GetModel(FName ModelId, FMTAVehicleModel& OutModel) const;

	const FMTAVehicleModel* FindModel(FName ModelId) const { return Models.Find(ModelId); }

	UFUNCTION(BlueprintPure, Category = "MTA6|Vehicle")
	TArray<FMTAVehicleModel> GetAllModels() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Vehicle")
	TArray<FMTAVehicleModel> GetModelsByClass(EMTAVehicleClass Class) const;

	/** Kaeufliche Modelle beim Haendler eines Bezirks. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Vehicle")
	TArray<FMTAVehicleModel> GetDealerModels(FName District) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Vehicle")
	FMTAVehicleStats GetBaseStats(FName ModelId) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Vehicle")
	bool GetTuningPart(FName PartId, FMTATuningPart& OutPart) const;

	const FMTATuningPart* FindTuningPart(FName PartId) const { return Parts.Find(PartId); }

	UFUNCTION(BlueprintPure, Category = "MTA6|Vehicle")
	TArray<FMTATuningPart> GetTuningParts(FName Category) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Vehicle")
	bool IsTuningSlotAllowed(FName ModelId, FName Category) const;

	/** Wiederverkaufsfaktor der Raritaet (rarityRules.resale). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Vehicle")
	float GetRarityResale(EMTARarity Rarity) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Vehicle") int32 GetNumModels() const { return Models.Num(); }
	UFUNCTION(BlueprintPure, Category = "MTA6|Vehicle") int32 GetNumTuningParts() const { return Parts.Num(); }

	/** Vergleichstabelle (CSV) aller Modelle mit Basiswerten und Leistungsindex fuer das Balancing (12.10). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Vehicle")
	FString BuildComparisonTable() const;

private:
	TSharedPtr<FJsonObject> Data;
	TMap<FName, FMTAVehicleModel> Models;
	TArray<FName> ModelOrder;
	TMap<FName, FMTATuningPart> Parts;
	TArray<FName> PartOrder;
};
