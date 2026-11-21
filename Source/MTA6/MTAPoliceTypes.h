#pragma once

#include "CoreMinimal.h"
#include "MTAPoliceTypes.generated.h"

/**
 * Zustaende einer Polizeieinheit (Modul M6).
 *
 * VERHALTENSPROTOKOLL POLIZEI (compilerunabhaengig, massgeblich fuer Tests):
 *
 *   [inaktiv] --(Fahndung>0, Anzahl < Soll, Zufall dt*SpawnRate, Spawnpunkt 90..200 m)--> Chase
 *   Chase  : faehrt auf der Strassenmitte (Versatz 0) mit 2400 + Heat*250 cm/s; an jeder Kreuzung
 *            mit Wahrscheinlichkeit GreedyChance die Option, deren Zielpunkt (0,7 Bloecke voraus)
 *            dem Spieler am naechsten ist (gierig), sonst gewichteter Zufall wie Zivilverkehr.
 *            Abstandsregel + Geist/Wartezeit wie Verkehr (der Spieler blockiert Polizei NICHT).
 *     --(Fahndung>0, Spieler < RamEnterDistance, Spieler und Einheit auf Strassenflaeche)--> Ram
 *     --(Fahndung==0)--> Calm
 *   Ram    : verlaesst das Spurmodell, steuert mit begrenzter Drehrate direkt auf den Spieler
 *            (RamSpeedFactor * Chase-Tempo). Treffer (< VehicleHitRadius) => Impuls/Schaden am Spieler,
 *            Einheit betaeubt (StunTime), bleibt in Ram.
 *     --(Spieler > RamExitDistance ODER Einheit nicht mehr auf Strasse ODER Fahndung==0)--> Chase
 *       (Einrasten auf naechste Strassenlinie: Achse nach Hauptrichtung, Linie = gerundet)
 *   Calm   : wie Zivilverkehr (zufaellige Kreuzungswahl, keine Sirene);
 *     --(Entfernung > DespawnDistanceWhenCalm)--> [inaktiv]
 *     --(Fahndung>0)--> Chase
 *   Festnahme (global, nicht je Einheit): Fahndung>0 UND irgendeine Einheit < ArrestRadius UND
 *     Spielertempo < ArrestMaxPlayerSpeed -> Zaehler += dt, sonst Zaehler = 0.
 *     Zaehler > ArrestTime -> Fahndung loeschen, alle Einheiten -> [inaktiv], OnArrested.
 *   Abkuehlung: laeuft im UMTAHeatSubsystem; blockiert, solange eine Einheit < NearPoliceRadius ist.
 *   Sirene: Phase rot, wenn floor(Zeit * SirenTogglesPerSecond) gerade, sonst blau; nur bei Fahndung>0.
 */
UENUM(BlueprintType)
enum class EMTAPoliceState : uint8
{
	Inactive  UMETA(DisplayName = "Inaktiv"),
	Chase     UMETA(DisplayName = "Verfolgung (Spur)"),
	Ram       UMETA(DisplayName = "Rammen (frei)"),
	Calm      UMETA(DisplayName = "Ruhig (Abzug)")
};

/** Feinwerte der Polizei. Standardwerte = Godot-Prototyp x100. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAPoliceTuning
{
	GENERATED_BODY()

	/** Hoechstzahl gleichzeitiger Einheiten (Pool-Groesse, nur beim BeginPlay wirksam). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dispatch", meta = (ClampMin = "0", ClampMax = "64"))
	int32 MaxPolice = 8;

	/** Soll-Anzahl = min(MaxPolice, BaseUnits + Sterne * UnitsPerStar) bei Fahndung > 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dispatch")
	int32 BaseUnits = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dispatch")
	int32 UnitsPerStar = 1;

	/** Spawnwahrscheinlichkeit pro Sekunde, solange Ist < Soll. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dispatch")
	float SpawnRatePerSecond = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dispatch")
	float SpawnMinDistance = 9000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dispatch")
	float SpawnMaxDistance = 20000.f;

	/** Versuche je Spawn (zufaellige Spurpunkte, Godot: 30). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dispatch")
	int32 SpawnAttempts = 30;

	/** Bevorzugt Spawnpunkte ausserhalb des Sichtkegels (Kamera-Vorwaertsrichtung). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dispatch")
	bool bPreferSpawnOutOfView = true;

	/** Bei Fahndung 0 werden Einheiten weiter als dies entfernt (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dispatch")
	float DespawnDistanceWhenCalm = 15000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driving")
	float InitialSpeed = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driving")
	float CalmSpeed = 1400.f;

	/** Verfolgungstempo = ChaseSpeedBase + Heat * ChaseSpeedPerHeat (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driving")
	float ChaseSpeedBase = 2400.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driving")
	float ChaseSpeedPerHeat = 250.f;

	/** Wahrscheinlichkeit der gierigen Kreuzungswahl (Godot: 0,9). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Driving", meta = (ClampMin = "0", ClampMax = "1"))
	float GreedyChance = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ram")
	bool bEnableRam = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ram")
	float RamEnterDistance = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ram")
	float RamExitDistance = 4000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ram")
	float RamSpeedFactor = 1.1f;

	/** Maximale Drehrate im Rammmodus (Grad/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ram")
	float RamTurnRateDeg = 140.f;

	/** Schadensfaktor bei Polizeitreffern gegenueber Zivilverkehr. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ram")
	float RamDamageMultiplier = 1.5f;

	/** Betaeubung der Einheit nach eigenem Treffer (s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ram")
	float StunTime = 0.6f;

	/** Festnahme: maximale Spielergeschwindigkeit (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arrest")
	float ArrestMaxPlayerSpeed = 300.f;

	/** Festnahme: Naehe einer Einheit (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arrest")
	float ArrestRadius = 700.f;

	/** Festnahme: benoetigte Dauer (s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arrest")
	float ArrestTime = 2.5f;

	/** Abkuehlung ist blockiert, solange eine Einheit naeher als dies ist (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arrest")
	float NearPoliceRadius = 6000.f;

	/** Blinkwechsel pro Sekunde (Godot: int(t*8)%2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Lights")
	float SirenTogglesPerSecond = 8.f;
};
