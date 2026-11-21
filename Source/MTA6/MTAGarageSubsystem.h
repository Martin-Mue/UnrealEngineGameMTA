#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "Math/RandomStream.h"
#include "MTAProgressionTypes.h"
#include "MTAGarageSubsystem.generated.h"

class UMTAEconomySubsystem;
class UMTAVehicleCatalog;
class UMTAProgressionSubsystem;

/** Ein Fahrzeug im Besitz des Spielers. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAOwnedVehicle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") FName InstanceId;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") FName ModelId;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") FString Plate;
	/** Wie bekannt das Kennzeichen bei Aegis ist (0..100). Kennzeichenwechsel setzt es auf 0. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") float PlateHeat = 0.f;
	/** Gestohlen / zur Fahndung ausgeschrieben ("heiss"). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") bool bStolen = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") bool bHot = false;
	/** Schaden 0..1 (1 = Wrack). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") float Damage = 0.f;
	/** Tankfuellung 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") float Fuel = 1.f;
	/** Ein Teil je Kategorie. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") TArray<FName> InstalledParts;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") FLinearColor PaintColor = FLinearColor(0.5f, 0.5f, 0.5f);
	/** -1 = unversichert, 0..2 = Basis/Komfort/Voll. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") int32 InsuranceTier = -1;
	/** Bezirk, in dem das Fahrzeug steht (Garage oder Strasse). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") FName District;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") bool bInGarage = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") FVector ParkedLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") float ParkedYaw = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") bool bImpounded = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") int32 ImpoundDays = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") float OdometerKm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") int64 PurchasePrice = 0;
};

/** Garage eines Bezirks (Ausbaustufen aus economy.json "garages.levels"). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAGarageInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") FName District;
	/** -1 = keine Garage. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") int32 Level = -1;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") int32 Capacity = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") int32 Used = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") int64 RentPerDay = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Garage") int64 NextLevelPrice = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMTAGarageChangedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAVehicleStatsChangedSignature, FName, InstanceId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAVehicleEventSignature, FName, InstanceId, const FString&, Message);

/**
 * Fahrzeugbesitz, Garagen je Bezirk, Kennzeichenwechsel (senkt Aegis-Profil), Tuning,
 * Reparatur, Versicherung, Abschleppen/Verwahrstelle. Liefert FMTAVehicleStats fuer den Pawn.
 */
UCLASS()
class MTA6_API UMTAGarageSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 SchemaVersion = 1;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	void ResetGarage();

	// --- Besitz -----------------------------------------------------------------------------

	/** Fuegt ein Fahrzeug ohne Bezahlung hinzu (Herkunft, Belohnung, Diebstahl). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	FName AddVehicle(FName ModelId, FName District, bool bStolen = false);

	/** Kauf beim Haendler: Preis ueber Wirtschaft (Bezirk, Knappheit "vehicles"), Fraktionsanforderung, freier Stellplatz oder Strasse. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	FName BuyVehicle(FName ModelId, FName District, FString& OutReason);

	/** Verkauf: legal beim Haendler, gestohlen nur auf dem Schwarzmarkt (stolenResalePct). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	int64 SellVehicle(FName InstanceId, FName District);

	UFUNCTION(BlueprintPure, Category = "MTA6|Garage")
	TArray<FMTAOwnedVehicle> GetVehicles() const { return Vehicles; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Garage")
	bool GetVehicle(FName InstanceId, FMTAOwnedVehicle& Out) const;

	/** Zeitwert (Listenpreis * Raritaet * Abnutzung * Schaden + halber Tuningwert). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Garage")
	int64 GetVehicleValue(FName InstanceId) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	bool SetActiveVehicle(FName InstanceId);

	UFUNCTION(BlueprintPure, Category = "MTA6|Garage")
	FName GetActiveVehicleId() const { return ActiveVehicle; }

	// --- Garagen ----------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	bool BuyOrUpgradeGarage(FName District, FString& OutReason);

	/** Kostenlose Garage (Herkunft/Story). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	void GrantGarage(FName District, int32 Level);

	UFUNCTION(BlueprintPure, Category = "MTA6|Garage")
	FMTAGarageInfo GetGarage(FName District) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Garage")
	TArray<FMTAGarageInfo> GetAllGarages() const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	bool StoreVehicle(FName InstanceId, FName District);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	void ParkOnStreet(FName InstanceId, FName District, FVector Location, float Yaw);

	// --- Kennzeichen ------------------------------------------------------------------------

	/**
	 * Kennzeichenwechsel. Legal (Werkstatt, teuer, kleine Wirkung) oder Schwarzmarkt (nur Bezirke mit
	 * Schwarzmarkt, groessere Wirkung). Ein Kennzeichen-Rohling im Inventar ersetzt die Teilkosten.
	 * Senkt das globale Aegis-Profil (Story) und setzt PlateHeat/bHot zurueck.
	 */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	bool ChangePlate(FName InstanceId, bool bBlackMarket, FName District, FString& OutReason);

	/** Kamera-Treffer auf das Kennzeichen (vom Aegis-/Kamerasystem aufzurufen). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	void RegisterPlateSighting(FName InstanceId, float Amount, bool bDuringCrime);

	// --- Tuning -----------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|Garage")
	bool CanInstallPart(FName InstanceId, FName PartId, FString& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	bool InstallPart(FName InstanceId, FName PartId, FName District, FString& OutReason);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	bool RemovePart(FName InstanceId, FName Category);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	void SetPaintColor(FName InstanceId, FLinearColor Color);

	/** Fahrzeug faehrt mit Teilen, die im Bezirk illegal sind, oder mit heissem Kennzeichen. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Garage")
	bool IsVehicleIllegalIn(FName InstanceId, FName District) const;

	// --- Zustand, Reparatur, Versicherung, Abschleppen -------------------------------------

	/** Schaden vom Pawn melden (HealthFraction 0..1 aus AMTAVehiclePawn::GetHealthFraction). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	void SyncFromPawn(FName InstanceId, float HealthFraction, float FuelFraction, float DrivenKm);

	UFUNCTION(BlueprintPure, Category = "MTA6|Garage")
	int64 GetRepairCost(FName InstanceId, FName District) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	bool RepairVehicle(FName InstanceId, FName District);

	/** Reparatur um einen Anteil (Reparaturset aus dem Inventar). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	void RepairByFraction(FName InstanceId, float Fraction);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	void Refuel(FName InstanceId, float Fraction, FName District, bool bFree);

	UFUNCTION(BlueprintPure, Category = "MTA6|Garage")
	int64 GetInsurancePremium(FName InstanceId, int32 Tier) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	bool SetInsurance(FName InstanceId, int32 Tier);

	/** Totalschaden: Versicherung zahlt Anteil der Reparatur, Rest + Selbstbehalt traegt der Spieler. Gibt Spielerkosten zurueck. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	int64 ReportWrecked(FName InstanceId, FName District);

	/** Abschleppen zur naechsten eigenen Garage; ohne Garage in die Verwahrstelle. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	int64 TowVehicle(FName InstanceId, float DistanceKm, FName District);

	/** Festnahme/Falschparken: Fahrzeug in die Verwahrstelle. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	void ImpoundVehicle(FName InstanceId);

	UFUNCTION(BlueprintPure, Category = "MTA6|Garage")
	int64 GetImpoundFee(FName InstanceId) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	bool ReleaseFromImpound(FName InstanceId);

	// --- Werte ------------------------------------------------------------------------------

	/** Basiswerte + Tuning + Skills/Herkunft (vehicle.*) + Schadensabzug. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Garage")
	FMTAVehicleStats ComputeStats(FName InstanceId) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Garage")
	FMTAVehicleStats GetActiveVehicleStats() const { return ComputeStats(ActiveVehicle); }

	// --- Persistenz -------------------------------------------------------------------------

	TSharedRef<FJsonObject> ToJson() const;
	bool FromJson(const TSharedPtr<FJsonObject>& Json);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	FString SaveStateToString() const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Garage")
	bool LoadStateFromString(const FString& Json);

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Garage") FMTAGarageChangedSignature OnGarageChanged;
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Garage") FMTAVehicleStatsChangedSignature OnVehicleStatsChanged;
	/** Meldungen fuer HUD-Toasts (gestohlen, abgeschleppt, Versicherung gekuendigt ...). */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Garage") FMTAVehicleEventSignature OnVehicleEvent;

private:
	UPROPERTY() TArray<FMTAOwnedVehicle> Vehicles;
	TMap<FName, int32> GarageLevels;
	FName ActiveVehicle;
	int32 NextInstance = 1;
	FRandomStream Rng;
	FDelegateHandle DayHandle;
	FDelegateHandle ModsHandle;

	FMTAOwnedVehicle* Find(FName InstanceId);
	const FMTAOwnedVehicle* Find(FName InstanceId) const;
	UMTAEconomySubsystem* Economy() const;
	UMTAVehicleCatalog* Catalog() const;
	UMTAProgressionSubsystem* Progression() const;
	TSharedPtr<FJsonObject> GarageLevelData(int32 Level) const;
	int32 CountInGarage(FName District) const;
	FString MakePlate();
	void HandleDayPassed(int32 Day);
	void HandleModifiersChanged();
	void Changed(FName InstanceId);
};
