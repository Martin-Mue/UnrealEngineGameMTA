#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MTADistrict.h"
#include "MTAProgressionTypes.generated.h"

class UGameInstance;

/**
 * Gemeinsame Typen des Fortschrittssystems (Wirtschaft, Skills, Garage, Inventar, Herkunft).
 * Alle Inhalte kommen aus Content/Data/*.json; dieser Header enthaelt nur Datenstrukturen und
 * kleine Hilfsfunktionen, damit die Subsysteme ohne Editor-Assets kompilieren.
 * Doku: docs/PROGRESSION.md
 */

/** Kategorie einer Buchung im Transaktionsprotokoll (UI-Filter, Statistik, Simulation). */
UENUM(BlueprintType)
enum class EMTATransactionCategory : uint8
{
	Other        UMETA(DisplayName = "Sonstiges"),
	Mission      UMETA(DisplayName = "Mission"),
	Gig          UMETA(DisplayName = "Gig"),
	Activity     UMETA(DisplayName = "Aktivitaet"),
	Sale         UMETA(DisplayName = "Verkauf"),
	Purchase     UMETA(DisplayName = "Einkauf"),
	Vehicle      UMETA(DisplayName = "Fahrzeug"),
	Tuning       UMETA(DisplayName = "Tuning"),
	Repair       UMETA(DisplayName = "Reparatur"),
	Fuel         UMETA(DisplayName = "Treibstoff"),
	Fine         UMETA(DisplayName = "Bussgeld"),
	Bribe        UMETA(DisplayName = "Bestechung"),
	Loan         UMETA(DisplayName = "Kredit"),
	Interest     UMETA(DisplayName = "Zinsen"),
	Insurance    UMETA(DisplayName = "Versicherung"),
	Garage       UMETA(DisplayName = "Garage"),
	BlackMarket  UMETA(DisplayName = "Schwarzmarkt"),
	Plate        UMETA(DisplayName = "Kennzeichen"),
	Tow          UMETA(DisplayName = "Abschleppen"),
	Origin       UMETA(DisplayName = "Herkunft"),
	Reward       UMETA(DisplayName = "Belohnung"),
	Confiscation UMETA(DisplayName = "Beschlagnahmung")
};

/** Fahrzeugklassen (vehicles.json, Feld "class"). */
UENUM(BlueprintType)
enum class EMTAVehicleClass : uint8
{
	Compact    UMETA(DisplayName = "Kompakt"),
	Sedan      UMETA(DisplayName = "Limousine"),
	SUV        UMETA(DisplayName = "SUV"),
	Sports     UMETA(DisplayName = "Sportwagen"),
	Muscle     UMETA(DisplayName = "Muscle"),
	Van        UMETA(DisplayName = "Van"),
	Truck      UMETA(DisplayName = "Truck"),
	Motorcycle UMETA(DisplayName = "Motorrad"),
	Patrol     UMETA(DisplayName = "Streife")
};

/** Seltenheit eines Modells (Haendlerverfuegbarkeit, Wiederverkauf, Auffaelligkeit). */
UENUM(BlueprintType)
enum class EMTARarity : uint8
{
	Common   UMETA(DisplayName = "Gewoehnlich"),
	Uncommon UMETA(DisplayName = "Ungewoehnlich"),
	Rare     UMETA(DisplayName = "Selten"),
	Exotic   UMETA(DisplayName = "Exotisch"),
	Unique   UMETA(DisplayName = "Einzelstueck")
};

/**
 * Ein Modifikator auf einen benannten Wert ("vehicle.grip", "econ.repairCost", ...).
 * Endwert = (Basis + Add) * Mul. Mehrere Modifikatoren: Add wird summiert, Mul multipliziert.
 * Die vollstaendige Liste der Stat-Namen steht in docs/PROGRESSION.md.
 */
USTRUCT(BlueprintType)
struct MTA6_API FMTAStatModifier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Progression")
	FName Stat;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Progression")
	float Add = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Progression")
	float Mul = 1.f;

	float Apply(float Base) const { return (Base + Add) * Mul; }

	/** Fasst einen weiteren Modifikator auf denselben Stat zusammen. */
	void Combine(const FMTAStatModifier& Other) { Add += Other.Add; Mul *= Other.Mul; }
};

/**
 * Effektive Fahrzeugwerte (Katalog-Basis + Tuning + Skills + Schaden).
 * Der Pawn bildet sie auf Chaos-Parameter ab (Tabelle in docs/PROGRESSION.md, Abschnitt 6).
 */
USTRUCT(BlueprintType)
struct MTA6_API FMTAVehicleStats
{
	GENERATED_BODY()

	/** Masse (kg) -> UChaosVehicleMovementComponent::Mass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float MassKg = 1400.f;
	/** Motorleistung (kW), nur Anzeige/Leistungsindex. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float EnginePowerKw = 100.f;
	/** Max. Drehmoment (Nm) -> EngineSetup.MaxTorque / SetMaxEngineTorque. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float MaxTorqueNm = 250.f;
	/** Max. Drehzahl -> EngineSetup.MaxRPM. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float MaxRPM = 6000.f;
	/** Hoechstgeschwindigkeit (km/h) -> AMTAVehiclePawn::MaxSpeedKmh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float TopSpeedKmh = 180.f;
	/** 0-100 km/h (s), Anzeige; wird aus Leistungsgewicht geschaetzt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float Accel0To100s = 9.f;
	/** Schaltzeit (s) -> TransmissionSetup.GearChangeTime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float GearShiftTime = 0.4f;
	/** Reibungsfaktor (1 = Serienreifen) -> UChaosVehicleWheel::FrictionForceMultiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float Grip = 1.f;
	/** Seitensteifigkeit -> UChaosVehicleWheel::CorneringStiffness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float CorneringStiffness = 1000.f;
	/** Bremsmoment je Rad (Nm) -> SetWheelMaxBrakeTorque. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float BrakeTorqueNm = 1500.f;
	/** Federrate (N/cm) -> SetSuspensionParams(Rate, ...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float SpringRate = 250.f;
	/** Daempfung (0..1) -> SetSuspensionParams(..., Damping, ...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float SuspensionDamping = 0.5f;
	/** Max. Lenkwinkel (Grad) -> SetWheelMaxSteerAngle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float SteerAngleDeg = 40.f;
	/** Luftwiderstand -> SetDragCoefficient. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float DragCoefficient = 0.32f;
	/** Abtrieb -> SetDownforceCoefficient. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float Downforce = 0.1f;
	/** Strukturpunkte -> AMTAVehiclePawn::MaxHealth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float Durability = 100.f;
	/** Schadensreduktion 0..0.8 -> DamagePerMps * (1 - DamageResist). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float DamageResist = 0.f;
	/** Auffaelligkeit fuer Aegis-Kameras 0..1 (Erkennungsrate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float AegisVisibility = 0.5f;
	/** Laerm 0..1 (Hoerweite fuer KCG/Passanten). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float Noise = 0.5f;
	/** Kofferraum-Slots und -Last (kg) -> Inventar-Container "trunk". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") int32 CargoSlots = 4;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float CargoKg = 80.f;
	/** Tank (L) und Verbrauch (L/100 km) -> Tankkosten. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float FuelCapacityL = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") float FuelUseL100 = 8.f;
	/** Zusammenfassender Leistungsindex 100..999 (UI-Vergleich). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle") int32 PerformanceIndex = 300;

	/** Wendet einen Modifikator an, falls Stat ein "vehicle.*"-Wert ist. Gibt true zurueck, wenn angewandt. */
	bool ApplyModifier(const FMTAStatModifier& Mod);

	/** Leitet Beschleunigung und Leistungsindex aus den Rohwerten ab und klemmt Wertebereiche. */
	void Finalize();
};

/** Hilfsfunktionen (reines C++) fuer JSON-Laden, Modifikatoren und die Bruecke zur Story-Engine. */
namespace MTAProgression
{
	/** Laedt Content/<RelativePath> als JSON-Objekt. */
	MTA6_API bool LoadJsonFile(const FString& RelativePath, TSharedPtr<FJsonObject>& Out);

	MTA6_API FString JsonToString(const TSharedRef<FJsonObject>& Obj);
	MTA6_API TSharedPtr<FJsonObject> JsonFromString(const FString& Text);

	MTA6_API double Num(const TSharedPtr<FJsonObject>& Obj, FStringView Field, double Default);
	MTA6_API FString Str(const TSharedPtr<FJsonObject>& Obj, FStringView Field, const FString& Default = FString());
	MTA6_API bool Bool(const TSharedPtr<FJsonObject>& Obj, FStringView Field, bool Default);
	MTA6_API TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& Parent, FStringView Field);
	MTA6_API const TArray<TSharedPtr<FJsonValue>>* Arr(const TSharedPtr<FJsonObject>& Parent, FStringView Field);

	/** [{ "stat": "vehicle.grip", "add": 0, "mul": 1.05 }, ...] */
	MTA6_API TArray<FMTAStatModifier> ReadModifiers(const TArray<TSharedPtr<FJsonValue>>* Array);
	MTA6_API TArray<TSharedPtr<FJsonValue>> WriteModifiers(const TArray<FMTAStatModifier>& Mods);
	/** { "union": 20, ... } -> Map */
	MTA6_API TMap<FName, float> ReadNumberMap(const TSharedPtr<FJsonObject>& Obj);
	MTA6_API TSharedRef<FJsonObject> WriteNumberMap(const TMap<FName, float>& Map);
	MTA6_API TArray<FName> ReadNameArray(const TArray<TSharedPtr<FJsonValue>>* Array);
	MTA6_API TArray<TSharedPtr<FJsonValue>> WriteNameArray(const TArray<FName>& Names);

	/** Bezirks-IDs als FName ("core", "saltmarket", "lanternhill", "greenmoor", "rustline", "sunstrip"). */
	MTA6_API FName DistrictToId(EMTADistrict District);
	MTA6_API EMTADistrict DistrictFromId(FName Id);

	MTA6_API EMTAVehicleClass VehicleClassFromString(const FString& S);
	MTA6_API EMTARarity RarityFromString(const FString& S);

	// --- Bruecke zu UMTAStorySubsystem (nur oeffentliche API, die Datei bleibt unveraendert) ---

	/** Aegis-Profil 0..100 aus der Story-Engine (50, falls nicht verfuegbar). */
	MTA6_API float GetStoryAegis(const UGameInstance* GI);
	/** Fraktionsstand -100..100 aus der Story-Engine. */
	MTA6_API float GetStoryStanding(const UGameInstance* GI, const FString& Faction);
	/**
	 * Aendert den Story-Zustand ueber StateToJson/StateFromJson (Rundreise).
	 * Erlaubt Herkunft (Startstaende, Dialogflags) und Kennzeichenwechsel (Aegis) ohne Eingriff in die Story-Dateien.
	 */
	MTA6_API bool PatchStoryState(UGameInstance* GI, TFunctionRef<void(const TSharedPtr<FJsonObject>&)> Patch);
	MTA6_API bool AddStoryAegis(UGameInstance* GI, float Delta);
}
