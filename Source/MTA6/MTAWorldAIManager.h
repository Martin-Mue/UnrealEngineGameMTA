#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MTATrafficTypes.h"
#include "MTAPoliceTypes.h"
#include "MTAWorldAITypes.h"
#include "MTAWorldAIManager.generated.h"

class APawn;
class UStaticMesh;
class UMaterialInterface;
class UInstancedStaticMeshComponent;
class UMTAHeatSubsystem;

/**
 * Welt-KI (Module M5 Verkehr/Fussgaenger und M6 Polizei): EIN Actor simuliert alle Agenten
 * datenorientiert (Struct-of-Arrays) und zeichnet sie ueber wenige UInstancedStaticMeshComponents.
 * Keine Actors, keine Physikkoerper pro Agent; Kontakt mit dem Spieler ist rein abstandsbasiert.
 *
 * =============================================================================================
 * VERHALTENSPROTOKOLL (compilerunabhaengig; Referenz: godot/main.gd, Werte x100 in cm)
 * =============================================================================================
 * Takt: Tick sammelt dt; Simulationsschritte mit fester Rate (SimulationHz, Standard 30 Hz,
 *       hoechstens MaxStepsPerFrame je Frame). Darstellung jedes Frame: Glaettung zum Sim-Ziel
 *       (Faktor 1 - exp(-dt*12)), danach ein Batch-Update je Instanzkomponente.
 * Sim-LOD: Agenten weiter als LodDistance werden nur jeden LodStepDivider-ten Schritt (versetzt
 *       nach Index) mit aufsummiertem dt bewegt und ohne Glaettung dargestellt.
 * Simulationsblase: Agenten weiter als BubbleRadius werden (max. BubbleRespawnsPerStep je Schritt)
 *       auf einen zufaelligen Spurpunkt im Ring [BubbleRespawnMinDistance, BubbleRadius] ausserhalb
 *       des Kamerablicks versetzt (nicht fuer aktive Polizei).
 *
 * ZIVILFAHRZEUG (Kind=Traffic), Zustaende ueber Zeitgeber:
 *   Fahren   : Zieltempo = Cruise (1000..1700). Tempo naehert sich mit +1400 / -3000 cm/s^2.
 *   Blockiert: im Korridor 0 < vorn <= 1100, |seitlich| < 260 steht ein Fahrzeug (inkl. Polizei)
 *              oder der Spieler -> Zieltempo 0, Wait += dt.
 *   Geist    : Wait > 4 s -> Ghost = 3 s, Wait = 0; waehrenddessen wird die Abstandsregel ignoriert
 *              (Deadlock-Aufloesung). Nicht blockiert -> Wait = 0.
 *   Betaeubt : Stun > 0 nach Spielertreffer (1,5 s): Tempo -> 0 mit 4000 cm/s^2, bewegt sich weiter.
 *   Kreuzung : beim Ueberfahren einer Strassenlinie m (Wechsel von floor((Pos+Half)/Period)):
 *              Optionen geradeaus (Gewicht 0,6; nicht am Stadtrand) und je Abbiegung (0,2; nicht
 *              aus der Stadt hinaus); gewichteter Zufall. Abbiegen: Achse wechselt, Line = m,
 *              Sign = Abbiegerichtung, Pos = R(alte Linie) + 100*Sign, Off neu.
 *   Treffer  : Abstand Spieler < 350 und Treffersperre abgelaufen (0,5 s) -> Impuls auf Spieler
 *              (Richtung KI->Spieler), Spielertempo * 0,75, OnPlayerHitByAI(Schaden, Impuls, Dir);
 *              Fahndung +0,35, wenn Spieler > 600 cm/s.
 * FUSSGAENGER (Kind=Pedestrian):
 *   Gehen    : Buergersteig (Off = Side*950), Tempo 160..260, keine Abstandsregel.
 *   Kreuzung : wie Fahrzeuge; nach dem Abbiegen zufaellige Strassenseite.
 *   Umgefallen: Spieler < 220 und Spielertempo > 400 und nicht bereits umgefallen -> Stun = 5 s,
 *              liegend dargestellt, Fahndung +0,7, OnPedestrianHit. Danach steht er wieder auf.
 * POLIZEI: siehe MTAPoliceTypes.h (EMTAPoliceState) und MTAPoliceLogic.cpp.
 * =============================================================================================
 *
 * Darstellung: ohne Assets lauffaehig (/Engine/BasicShapes/Cube und Cylinder mit
 * BasicShapeMaterial-Instanzen je Farbgruppe). Per-Instance-Custom-Data: [0..2] = Farbe RGB,
 * [3] = Emissiv (Sirene). Ein eigenes Material mit PerInstanceCustomData kann ueber
 * AgentMaterial gesetzt werden; Meshes ueber die Mesh-UPROPERTYs (spaeter Skeletal/Mass).
 */
UCLASS(Blueprintable)
class MTA6_API AMTAWorldAIManager : public AActor
{
	GENERATED_BODY()

public:
	AMTAWorldAIManager();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// --- Schnittstellen -----------------------------------------------------------------------

	/** KI-Fahrzeug hat den Spieler getroffen (Schaden, Impuls in kg*cm/s, Richtung KI->Spieler). */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|AI")
	FOnPlayerHitByAI OnPlayerHitByAI;

	/** Festnahme erfolgt (Fahndung bereits geloescht, Polizei entfernt). */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|AI")
	FOnArrested OnArrested;

	/** Spieler hat einen Fussgaenger umgefahren. */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|AI")
	FOnPedestrianHit OnPedestrianHit;

	/** Minimap-Export: alle aktiven Fahrzeuge (optional auch Fussgaenger). Leert Out zuerst. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|AI")
	void GetMinimapAgents(TArray<FMTAMinimapAgent>& Out, bool bIncludePedestrians = false) const;

	/** Straftat am Ort melden (Bezirk wird aus dem Ort bestimmt). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|AI")
	void ReportCrime(float HeatAmount, FVector Location);

	/** Alle Polizeieinheiten sofort entfernen (z. B. Missionsneustart). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|AI")
	void DespawnAllPolice();

	UFUNCTION(BlueprintPure, Category = "MTA6|AI")
	int32 GetActivePoliceCount() const;

	/** Festnahmefortschritt 0..1 (fuer eine HUD-Leiste). */
	UFUNCTION(BlueprintPure, Category = "MTA6|AI")
	float GetArrestProgress() const;

	/** Bezirk an einer Weltposition (DistrictResolver oder Standardraster 3x2). */
	UFUNCTION(BlueprintPure, Category = "MTA6|AI")
	FName GetDistrictAt(const FVector& Location) const;

	/** Optional: C++-Rueckruf fuer die echte Bezirkskarte (vom CityGenerator/GameMode gesetzt). */
	TFunction<FName(const FVector&)> DistrictResolver;

	// --- Konfiguration ------------------------------------------------------------------------

	/** Spieler-Pawn. Leer -> erster PlayerController. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI")
	TObjectPtr<APawn> PlayerPawn;

	/** Spielerindex fuer UMTAHeatSubsystem. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI")
	int32 PlayerIndex = 0;

	/** Impuls direkt auf den simulierenden Root des Pawns anwenden (sonst nur Delegate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI")
	bool bApplyImpulseToPlayer = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI")
	int32 RandomSeed = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI")
	FMTATrafficTuning Traffic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI")
	FMTAPoliceTuning Police;

	/** Standard-Bezirksnamen fuer das Ersatzraster (3 Spalten x 2 Reihen, Reihe Y<0 zuerst). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI")
	TArray<FName> FallbackDistrictNames;

	// --- Darstellung (vor BeginPlay setzen) ----------------------------------------------------

	/** Karosserie (Standard: Engine-Wuerfel 100 cm, auf Fahrzeugmasse skaliert). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	TObjectPtr<UStaticMesh> VehicleBodyMesh;

	/** Kabine/Dach (Standard: Wuerfel). Leer = keine Kabine. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	TObjectPtr<UStaticMesh> VehicleCabinMesh;

	/** Fussgaenger (Standard: Engine-Zylinder als Kapselersatz). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	TObjectPtr<UStaticMesh> PedestrianMesh;

	/** Sirenenbalken-Element (Standard: Wuerfel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	TObjectPtr<UStaticMesh> SirenMesh;

	/** Material mit PerInstanceCustomData (0..2 Farbe, 3 Emissiv). Leer -> BasicShapeMaterial-Instanzen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	TObjectPtr<UMaterialInterface> AgentMaterial;

	/** Basis fuer die Farbinstanzen (Vektorparameter "Color"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	TObjectPtr<UMaterialInterface> FallbackColorMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	TArray<FLinearColor> TrafficColors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	TArray<FLinearColor> PedestrianColors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	FLinearColor PoliceColor = FLinearColor(0.06f, 0.06f, 0.09f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	FLinearColor CabinColor = FLinearColor(0.05f, 0.07f, 0.1f);

	/** Fahrzeugmasse (cm): Laenge, Breite, Hoehe der Karosserie. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	FVector VehicleBodySize = FVector(440.f, 190.f, 90.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	FVector VehicleCabinSize = FVector(230.f, 170.f, 60.f);

	/** Fussgaenger: Durchmesser, Hoehe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	FVector2D PedestrianSize = FVector2D(52.0, 175.0);

	/** Bodenhoehe der Agenten (Z, cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	float GroundZ = 0.f;

	/** Schatten der Agenten (Kosten!). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	bool bAgentsCastShadows = true;

protected:
	// --- Simulation (MTAWorldAIManager.cpp) -----------------------------------------------------
	void InitAgents();
	void InitVisuals();
	void SimStep(float Dt);
	void RefreshPlayer();
	void BuildGrid();
	void UpdateVehicles(float Dt);
	void UpdatePedestrians(float Dt);
	void UpdateBubble();
	void UpdateVisuals(float FrameDt);

	void SpawnVehicleAt(int32 I, EMTAAgentKind InKind);
	void RandomizeLanePlacement(uint8& OutAxis, int8& OutSign, int32& OutLine, float& OutPos);
	void SetVehicleOffset(int32 I);
	void ComputeVehicleSim(int32 I);
	void ComputePedestrianSim(int32 I);
	void MoveVehicleAlongLane(int32 I, float Dt);
	/** Tempo-Regelung im Spurmodus inkl. Betaeubung, Abstandsregel, Wait/Ghost. */
	void UpdateLaneSpeed(int32 I, float Dt, float TargetSpeed);
	void PlacePedestrianRandom(int32 I);
	/** Kreuzungslogik (Godot _agent_intersection). Liefert true bei Abbiegen. */
	bool HandleIntersection(uint8& Axis, int8& Sign, int32& Line, float& Pos, int32 M, bool bGreedy);
	bool IsVehicleBlocked(int32 I) const;
	static bool IsAhead(const FVector2f& P, const FVector2f& Fwd, const FVector2f& Q, float Dist, float HalfWidth);
	void HandleVehiclePlayerContact(int32 I, float Dist, const FVector2f& Delta);
	bool IsOutOfView(const FVector2f& Loc) const;

	// --- Polizei (MTAPoliceLogic.cpp) ---------------------------------------------------------
	void UpdatePoliceDispatch(float Dt);
	void UpdatePoliceUnit(int32 I, float Dt, float Heat);
	bool TrySpawnPolice();
	void DespawnPolice(int32 I);
	void SnapPoliceToLane(int32 I);
	void Arrest();
	float GetPoliceTargetSpeed(float Heat) const;
	bool IsSirenPhaseRed() const;
	static bool IsOnRoad(const FVector2f& P);

	// --- Daten -----------------------------------------------------------------------------------
	FMTAVehicleSoA Vehicles;
	FMTAPedestrianSoA Peds;
	FMTAAgentGrid Grid;
	FRandomStream Rng;

	int32 NumTrafficSlots = 0;
	int32 FirstPoliceSlot = 0;
	int32 NumPoliceSlots = 0;

	float Accumulator = 0.f;
	float SimTime = 0.f;
	int32 StepCounter = 0;
	float HitCooldownLeft = 0.f;
	float ArrestTimer = 0.f;
	float CachedHeat = 0.f;

	// Spielerbezug (pro Schritt aktualisiert)
	bool bHasPlayer = false;
	FVector2f PlayerLoc2D = FVector2f::ZeroVector;
	FVector PlayerVelocity = FVector::ZeroVector;
	float PlayerSpeed = 0.f;
	FVector2f ViewLoc2D = FVector2f::ZeroVector;
	FVector2f ViewDir2D = FVector2f(1.f, 0.f);
	TWeakObjectPtr<APawn> ActivePawn;

	UPROPERTY(Transient)
	TObjectPtr<UMTAHeatSubsystem> HeatSubsystem;

	// --- Instanzkomponenten -------------------------------------------------------------------
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> TrafficBodyISMs;   // je Farbgruppe

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> PedISMs;           // je Farbgruppe

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> CabinISM;                  // alle Fahrzeug-Slots

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> PoliceBodyISM;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> SirenRedISM;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> SirenBlueISM;

	// Wiederverwendete Puffer (keine Allokation pro Frame)
	TArray<TArray<FTransform>> TrafficXfBuffers;
	TArray<TArray<FTransform>> PedXfBuffers;
	TArray<FTransform> CabinXfBuffer;
	TArray<FTransform> PoliceXfBuffer;
	TArray<FTransform> SirenRedXfBuffer;
	TArray<FTransform> SirenBlueXfBuffer;

	UInstancedStaticMeshComponent* CreateInstancer(const FName& Name, UStaticMesh* Mesh, const FLinearColor& Color, int32 Count, bool bShadows);
	static FTransform HiddenTransform();
};
