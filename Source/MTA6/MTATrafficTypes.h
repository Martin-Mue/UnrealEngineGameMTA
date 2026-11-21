#pragma once

#include "CoreMinimal.h"
#include "MTATrafficTypes.generated.h"

/**
 * Gemeinsame Typen fuer Verkehr, Fussgaenger und Polizei (Module M5/M6, docs/WELTKONZEPT.md).
 * Diese Datei ist die oeffentliche Schnittstelle der Welt-KI: GameMode, HUD und Fahrzeug-Pawn
 * binden ausschliesslich die hier definierten Delegates und lesen FMTAMinimapAgent.
 *
 * Einheiten: cm, cm/s, Sekunden. Stadtraster wie im Godot-Prototyp (godot/main.gd), skaliert x100.
 */

/** Stadtraster-Konstanten (8x8 Bloecke, Periode 80 m, Mitte am Ursprung). */
namespace MTACity
{
	/** Abstand zweier Strassenmitten (cm). */
	constexpr float Period = 8000.f;
	/** Anzahl Bloecke je Achse; es gibt Blocks+1 Strassenlinien (Index 0..Blocks). */
	constexpr int32 Blocks = 8;
	/** Halbe Stadtgroesse (cm). */
	constexpr float Half = Period * Blocks * 0.5f;
	/** Seitlicher Versatz der Fahrspur zur Strassenmitte (Rechtsverkehr). */
	constexpr float LaneOffset = 400.f;
	/** Seitlicher Versatz des Buergersteigs zur Strassenmitte. */
	constexpr float SidewalkOffset = 950.f;
	/** Halbe befahrbare Strassenbreite (fuer "ist auf der Strasse"-Tests). */
	constexpr float RoadHalfWidth = 800.f;

	/** Strassenmitte der Linie K: R(k) = k*8000 - 32000. */
	FORCEINLINE float RoadCenter(int32 K) { return K * Period - Half; }

	/** Index der naechstgelegenen Strassenlinie zu einer Koordinate (geklemmt auf 0..Blocks). */
	FORCEINLINE int32 NearestLine(float Coord)
	{
		return FMath::Clamp(FMath::RoundToInt((Coord + Half) / Period), 0, Blocks);
	}
}

/** Art eines simulierten Agenten. */
UENUM(BlueprintType)
enum class EMTAAgentKind : uint8
{
	Traffic     UMETA(DisplayName = "Verkehr"),
	Police      UMETA(DisplayName = "Polizei"),
	Pedestrian  UMETA(DisplayName = "Fussgaenger")
};

/**
 * Ein Eintrag fuer die Minimap (reine Daten, wird pro Frame vom Manager gefuellt).
 * Der HUD-Agent hat eigene Typen in MTAMinimapTypes.h; die Umrechnung erfolgt ueber eine
 * duenne Adapterfunktion im HUD (siehe Abschlussbericht).
 */
USTRUCT(BlueprintType)
struct MTA6_API FMTAMinimapAgent
{
	GENERATED_BODY()

	/** Weltposition (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|AI")
	FVector Location = FVector::ZeroVector;

	/** Fahrtrichtung als Gierwinkel in Grad (UE-Konvention, 0 = +X). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|AI")
	float YawDegrees = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|AI")
	EMTAAgentKind Kind = EMTAAgentKind::Traffic;

	/** Blinkzustand der Sirenenlichter: true = rote Phase, false = blaue Phase. Nur Polizei. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|AI")
	bool bLightPhaseRed = false;

	/** Sirene aktiv (Fahndung > 0). Bei false soll die Minimap nicht blinken. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|AI")
	bool bSirenActive = false;
};

/**
 * Feinwerte fuer Verkehr und Fussgaenger. Standardwerte = Godot-Prototyp x100 (m -> cm).
 */
USTRUCT(BlueprintType)
struct MTA6_API FMTATrafficTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Counts", meta = (ClampMin = "0", ClampMax = "2000"))
	int32 NumTrafficVehicles = 60;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Counts", meta = (ClampMin = "0", ClampMax = "4000"))
	int32 NumPedestrians = 120;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed")
	float TrafficSpeedMin = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed")
	float TrafficSpeedMax = 1700.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed")
	float PedestrianSpeedMin = 160.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed")
	float PedestrianSpeedMax = 260.f;

	/** Beschleunigung (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed")
	float Acceleration = 1400.f;

	/** Bremsverzoegerung (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed")
	float Braking = 3000.f;

	/** Verzoegerung waehrend Betaeubung nach einem Treffer (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Speed")
	float StunBraking = 4000.f;

	/** Abstandsregel: Laenge des Vorausschau-Korridors (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blocking")
	float LookAheadDistance = 1100.f;

	/** Abstandsregel: halbe Breite des Vorausschau-Korridors (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blocking")
	float LookAheadHalfWidth = 260.f;

	/** Deadlock-Aufloesung: nach so vielen Sekunden Blockade wird das Fahrzeug zum Geist. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blocking")
	float WaitBeforeGhost = 4.f;

	/** Dauer des Geist-Zustands (ignoriert die Abstandsregel). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blocking")
	float GhostDuration = 3.f;

	/** Kreuzungsgewicht geradeaus. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turning")
	float TurnWeightStraight = 0.6f;

	/** Kreuzungsgewicht je Abbiegerichtung (links/rechts). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Turning")
	float TurnWeightSide = 0.2f;

	/** Feste Simulationsrate (Hz). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation", meta = (ClampMin = "5", ClampMax = "120"))
	float SimulationHz = 30.f;

	/** Maximal nachgeholte Schritte pro Frame (Schutz vor Spiralen bei Rucklern). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxStepsPerFrame = 4;

	/** Sim-LOD: ab dieser Entfernung zum Spieler werden Agenten seltener aktualisiert (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation")
	float LodDistance = 15000.f;

	/** Sim-LOD: ferne Agenten nur jeden N-ten Schritt (mit aufsummiertem dt). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation", meta = (ClampMin = "1", ClampMax = "16"))
	int32 LodStepDivider = 4;

	/** Glaettung der Darstellung zum Sim-Ziel (1/s, wie Godot exp(-dt*12)). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation")
	float VisualSmoothing = 12.f;

	/** Simulationsblase: Agenten weiter als dies vom Spieler werden ausser Sicht neu platziert. 0 = aus. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation")
	float BubbleRadius = 30000.f;

	/** Simulationsblase: Mindestabstand neu platzierter Agenten zum Spieler (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation")
	float BubbleRespawnMinDistance = 12000.f;

	/** Simulationsblase: maximal neu platzierte Agenten pro Schritt (verteilt die Kosten). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Simulation")
	int32 BubbleRespawnsPerStep = 2;

	/** Fussgaenger umfahren: Radius (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerContact")
	float PedestrianHitRadius = 220.f;

	/** Fussgaenger umfahren: Mindestgeschwindigkeit des Spielers (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerContact")
	float PedestrianHitMinSpeed = 400.f;

	/** Dauer "umgefallen" (s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerContact")
	float PedestrianDownTime = 5.f;

	/** Fahndungszuwachs je umgefahrenem Fussgaenger. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerContact")
	float PedestrianHitHeat = 0.7f;

	/** Fahrzeugkontakt: Radius (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerContact")
	float VehicleHitRadius = 350.f;

	/** Fahrzeugkontakt mit Zivilfahrzeug: Fahndungszuwachs, wenn der Spieler schneller als VehicleHitMinSpeedForHeat ist. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerContact")
	float VehicleHitHeat = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerContact")
	float VehicleHitMinSpeedForHeat = 600.f;

	/** Sperrzeit zwischen zwei Fahrzeugtreffern (s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerContact")
	float HitCooldown = 0.5f;

	/** Impuls auf den Spieler (kg*cm/s; Godot 1500 N*s = 150000). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerContact")
	float HitImpulse = 150000.f;

	/** Anteil der Spielergeschwindigkeit, der nach einem Treffer bleibt. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerContact", meta = (ClampMin = "0", ClampMax = "1"))
	float HitVelocityKeep = 0.75f;

	/** Schaden = Basis + je m/s Relativgeschwindigkeit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerContact")
	float HitBaseDamage = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerContact")
	float HitDamagePerMps = 0.8f;

	/** Betaeubung eines Zivilfahrzeugs nach Treffer (s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PlayerContact")
	float TrafficStunTime = 1.5f;
};

// ---------------------------------------------------------------------------------------------
// Delegates (Schnittstelle zu GameMode / Pawn / HUD). Dynamisch, damit auch Blueprints binden koennen.
// ---------------------------------------------------------------------------------------------

/** Ein KI-Fahrzeug hat den Spieler getroffen. Dir = Einheitsvektor vom KI-Fahrzeug zum Spieler. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPlayerHitByAI, float, Damage, float, Impulse, FVector, Dir);

/** Der Spieler wurde festgenommen (Fahndung ist bereits geloescht, Polizei entfernt). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnArrested);

/** Globale Fahndung des lokalen Spielers (Index 0) hat sich geaendert (0..5, kontinuierlich). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHeatChanged, float, Heat);

/** Ganzzahlige Fahndungsstufe (Sterne 0..5) des lokalen Spielers hat sich geaendert. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWantedLevelChanged, int32, Stars);

/** Der Spieler hat einen Fussgaenger umgefahren (fuer Sound/Toast). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPedestrianHit, FVector, Location);
