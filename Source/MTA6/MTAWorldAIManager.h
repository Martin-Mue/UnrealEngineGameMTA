#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MTATrafficTypes.h"
#include "MTAPoliceTypes.h"
#include "MTAWorldAITypes.h"
#include "UObject/SoftObjectPtr.h"
#include "MTAWorldAIManager.generated.h"

class APawn;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UInstancedStaticMeshComponent;
class UMTAHeatSubsystem;

/** Karosserie-Sorte eines Verkehrsfahrzeugs (intern, ohne Reflection). Box = Platzhalter-Wuerfel. */
enum class EMTATrafficBodyModel : uint8
{
	Sports = 0,
	Offroad = 1,
	Concept = 2,
	Box = 3,
	Count = 4
};

/**
 * Aus GetBoundingBox() berechnete Einpassung eines Vorlagen-Fahrzeugs auf Zielmasse (intern).
 * Alle Versaetze sind fahrzeuglokal (X = Fahrtrichtung, Z = Boden), Einheiten cm.
 */
struct FMTATrafficModelFit
{
	bool bValid = false;
	/** Skalierung der Karosserie (X Laenge, Y Breite, Z Mittelwert). */
	FVector BodyScale = FVector::OneVector;
	/** Versatz des Mesh-Pivots, damit die Karosserie mittig und auf Bodenhoehe steht. */
	FVector BodyOffset = FVector::ZeroVector;
	/** Masse nach Skalierung. */
	float Length = 0.f;
	float Width = 0.f;
	float RoofZ = 0.f;
	/** Rohlaenge des Meshes (unskaliert) fuer den Selbsttest. */
	float RawLength = 0.f;
	/** Materialslot des Lacks und der gefundene Farbparameter (NAME_None = keiner gefunden). */
	int32 PaintSlot = INDEX_NONE;
	FName PaintParam;

	/** Raeder als eigene Instanzen (4 je Fahrzeug) oder als ein Satz (1 je Fahrzeug, Pivot wie Karosserie). */
	bool bSeparateWheels = false;
	bool bWheelSet = false;
	FVector WheelScale = FVector::OneVector;
	/** Mesh-Mittelpunkt des Rads (unskaliert) fuer die Pivot-Korrektur. */
	FVector WheelMeshCenter = FVector::ZeroVector;
	/** Achse des Rads liegt im Mesh entlang X -> 90 Grad gieren. */
	bool bWheelAxleAlongX = false;
	float WheelRadius = 30.f;
	/** Radmitten fahrzeuglokal: VL, VR, HL, HR. */
	FVector WheelCenters[4] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
	/** Mitte des linken Scheinwerfers / Ruecklichts fahrzeuglokal (rechts = Y gespiegelt). */
	FVector HeadLightLocal = FVector::ZeroVector;
	FVector TailLightLocal = FVector::ZeroVector;
};

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
 * Darstellung Fahrzeuge: echte Modelle aus den Epic-Vorlagen (tools/setup_template_assets.ps1):
 *   Sportwagen  /Game/Vehicles/SportsCar/SM_SportsCar (+ _Glass, + _Wheel als 4 Rad-Instanzen)
 *   Gelaende    /Game/Vehicles/OffroadCar/SM_Offroad_Body (+ SM_Offroad_Tire als 4 Rad-Instanzen)
 *   Concept Car /Game/ConceptCar/Car/SM_AutomotiveTP_Car (Raeder im Mesh)
 * Einpassung aus GetBoundingBox() auf TargetSize je Sorte (Vorlagen schauen in +X), Pivot mittig,
 * Unterkante auf Bodenfreiheit. Lack: eine UMaterialInstanceDynamic je (Sorte, Farbe) auf dem
 * Lack-Slot (Parameter "Paint Tint" bzw. "PaintColor", wird zur Laufzeit im Material gesucht),
 * daher eine Instanzkomponente je (Sorte, Farbe). Polizei: Sportwagen mit dunklem Lack, weisser
 * Seitenstreifen (zweite Materialinstanz), Sirenenbalken auf dem Dach. Fehlt ein Asset, faellt
 * die Sorte weg; fehlen alle, wird wie frueher mit Engine-Wuerfeln gezeichnet (Sorte Box).
 * Darstellung Fussgaenger: schlanker Zylinder (Koerper, je Kleidungsfarbe) + Kugel (Kopf, je
 * Hautton), Groesse/Breite deterministisch je Index variiert.
 * Per-Instance-Custom-Data (nur Grundformen): [0..2] = Farbe RGB, [3] = Emissiv (Sirene).
 * Fahrzeuglichter: je Fahrzeug 2 Scheinwerfer + 2 Ruecklichter als abgeflachte Kugeln (keine
 * Lichtquellen), Material M_MTA_LampEmissive (Custom-Data [0..2] = LampColor, nachts emissiv ueber
 * MPC "Night"); Instanzindex = Fahrzeugindex * 2 + Seite (0 links, 1 rechts). Bremslicht heller beim
 * Verzoegern/Stehen, Polizei: weisse Frontlichter, bei Sirene wechselseitig blinkend.
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

	/**
	 * Modelltest ohne Eingabe: Fahrzeug-Meshes und Materialien laden, Lackparameter vorhanden,
	 * Instanzen aller Sorten vorhanden, Laengen plausibel (380-520 cm), Polizei-Materialien gesetzt,
	 * Lackfarben nicht zu dunkel, Scheinwerfer + Ruecklichter = 4 x Fahrzeuge (Lage vorn/hinten).
	 * Letzte Zeile: "MODELLTEST ERGEBNIS: n Fehler". Erst nach BeginPlay aussagekraeftig.
	 */
	UFUNCTION(BlueprintCallable, Category = "MTA6|AI")
	FString RunModelSelfTest();

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

	/** Lack der Polizei: dunkles Blau (Selbsttest erwartet Luminanz < 0.25), nachts per Streifen/Lichtern erkennbar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	FLinearColor PoliceColor = FLinearColor(0.07f, 0.1f, 0.24f);

	/**
	 * Multiplikator auf TrafficColors fuer den Lack. "Paint Tint" multipliziert die Lacktextur, daher sind
	 * Werte > 1 sinnvoll, damit die Autos nachts im Laternenlicht hell bleiben (Grundfarbe klemmt bei 1).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float PaintBrightness = 1.5f;

	// --- Fahrzeuglichter (instanziert, keine Lichtquellen) --------------------------------------------

	/** Form der Scheinwerfer/Ruecklichter (Standard: Engine-Kugel, abgeflacht). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Lights")
	TObjectPtr<UStaticMesh> VehicleLightMesh;

	/** Emissives Material mit Custom-Data [0..2] = Farbe (Standard: M_MTA_LampEmissive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Lights")
	TSoftObjectPtr<UMaterialInterface> VehicleLightMaterialAsset;

	/** Skalarparameter "LampLuminance" der Licht-Materialinstanz (Laternen nutzen 600). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Lights")
	float VehicleLightLuminance = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Lights")
	FLinearColor HeadLightColor = FLinearColor(1.f, 0.9f, 0.68f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Lights")
	FLinearColor PoliceHeadLightColor = FLinearColor(0.88f, 0.94f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Lights")
	FLinearColor TailLightColor = FLinearColor(0.4f, 0.012f, 0.008f);

	/** Bremslicht (heller als TailLightColor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Lights")
	FLinearColor BrakeLightColor = FLinearColor(1.f, 0.04f, 0.025f);

	/** Scheinwerfer: Einzug ab Fahrzeugende (cm), Anteil der halben Breite, Anteil der Karosseriehoehe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Lights")
	FVector HeadLightPlacement = FVector(16.0, 0.66, 0.42);

	/** Ruecklicht: Einzug ab Fahrzeugende (cm), Anteil der halben Breite, Anteil der Karosseriehoehe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Lights")
	FVector TailLightPlacement = FVector(12.0, 0.7, 0.55);

	/** Groesse (cm) Tiefe x Breite x Hoehe eines Scheinwerfers bzw. Ruecklichts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Lights")
	FVector HeadLightSize = FVector(16.0, 28.0, 11.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Lights")
	FVector TailLightSize = FVector(14.0, 30.0, 9.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	FLinearColor CabinColor = FLinearColor(0.05f, 0.07f, 0.1f);

	/** Fahrzeugmasse (cm): Laenge, Breite, Hoehe der Karosserie. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	FVector VehicleBodySize = FVector(440.f, 190.f, 90.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	FVector VehicleCabinSize = FVector(230.f, 170.f, 60.f);

	/** Fussgaenger: Koerperdurchmesser, Gesamthoehe inkl. Kopf (Mittelwerte, je Index variiert). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	FVector2D PedestrianSize = FVector2D(38.0, 174.0);

	/** Kopf der Fussgaenger (Standard: Engine-Kugel). Leer = kein Kopf. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	TObjectPtr<UStaticMesh> PedestrianHeadMesh;

	/** Kopfdurchmesser (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	float PedestrianHeadSize = 23.f;

	/** Hauttoene der Koepfe (je Ton eine Instanzkomponente). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	TArray<FLinearColor> PedestrianSkinColors;

	/** Bodenhoehe der Agenten (Z, cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	float GroundZ = 0.f;

	/** Schatten der Agenten (Kosten!). Fahrzeuge werfen nur nahe Schatten (VehicleShadowDistance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Visual")
	bool bAgentsCastShadows = true;

	// --- Fahrzeugmodelle (Epic-Vorlagen) ---------------------------------------------------------

	/** Obergrenze aller Fahrzeuge (Verkehr + Polizei); der Verkehr wird ggf. gekuerzt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	int32 MaxTotalVehicles = 60;

	/** Echte Modelle verwenden (aus = Platzhalter-Wuerfel wie frueher). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	bool bUseVehicleModels = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	TSoftObjectPtr<UStaticMesh> SportsBodyAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	TSoftObjectPtr<UStaticMesh> SportsGlassAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	TSoftObjectPtr<UStaticMesh> SportsWheelAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	TSoftObjectPtr<UStaticMesh> OffroadBodyAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	TSoftObjectPtr<UStaticMesh> OffroadWheelAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	TSoftObjectPtr<UStaticMesh> ConceptBodyAsset;

	/** Zielmasse (cm) Laenge x Breite je Sorte: Sportwagen, Gelaendewagen, Concept Car. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	FVector2D SportsTargetSize = FVector2D(440.0, 190.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	FVector2D OffroadTargetSize = FVector2D(450.0, 195.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	FVector2D ConceptTargetSize = FVector2D(440.0, 190.0);

	/** Bodenfreiheit der Karosserie-Unterkante bei separaten Raedern (cm): Sportwagen, Gelaende. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	FVector2D BodyClearance = FVector2D(12.0, 26.0);

	/** Raddurchmesser (cm): Sportwagen, Gelaende. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	FVector2D WheelDiameter = FVector2D(66.0, 78.0);

	/** Achsposition als Anteil der Laenge ab Mitte (vorn, hinten), z. B. 0.31 = 31 % nach vorn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	FVector2D SportsAxleFactor = FVector2D(0.315, 0.300);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	FVector2D OffroadAxleFactor = FVector2D(0.300, 0.290);

	/** Rad-Mesh ist fuer die rechte statt die linke Seite modelliert (Felge zeigt sonst nach innen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	bool bFlipWheelSides = false;

	/** Gewicht der Sorten im Verkehr: Sportwagen, Gelaende, Concept Car. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	FVector ModelWeights = FVector(0.3, 0.3, 0.4);

	/** Lack der Polizei und Farbe des Seitenstreifens. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	FLinearColor PoliceStripeColor = FLinearColor(0.95f, 0.95f, 0.95f);

	/** Streifen: Hoehe ueber Boden als Anteil der Dachhoehe, Breitenfaktor (Abstand zur Flanke). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	FVector2D PoliceStripePlacement = FVector2D(0.42, 0.985);

	/** Dachmitte (Sirenenbalken) als Anteil der Laenge ab Mitte (negativ = hinten). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	float PoliceRoofXFactor = -0.07f;

	/** Instanz-Cull-Distanz (cm) Fahrzeuge: Beginn/Ende der Ausblendung. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	FIntPoint VehicleCullDistance = FIntPoint(16000, 20000);

	/** Instanz-Cull-Distanz (cm) Fussgaenger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	FIntPoint PedestrianCullDistance = FIntPoint(7000, 9000);

	/** Fahrzeugschatten nur, wenn ein Fahrzeug der Komponente naeher als dieser Abstand ist (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|AI|Models")
	float VehicleShadowDistance = 4000.f;

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
	/** Karosserie je Gruppe G = Sorte * NumPaintColors + Farbe (leer, wenn die Gruppe ungenutzt ist). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> TrafficBodyISMs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> PedISMs;           // Koerper je Kleidungsfarbe

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> PedHeadISMs;       // Kopf je Hautton

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> CabinISM;                  // nur Sorte Box, Index = Fahrzeugindex

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> PoliceBodyISM;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> PoliceStripeISM;           // weisser Streifen, Index = Polizei-Slot

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> SirenRedISM;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> SirenBlueISM;

	/** Raeder je Sorte (Sportwagen inkl. Polizei, Gelaende); Concept Car hat die Raeder im Mesh. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> WheelISMs;

	/** Scheiben/Lichter des Sportwagens (Verkehr + Polizei). */
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> SportsGlassISM;

	/** Geladene Meshes je Sorte (Index EMTATrafficBodyModel), null = Sorte nicht verfuegbar. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> ModelBodyMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> ModelWheelMeshes;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> LoadedSportsGlass;

	/** Engine-Wuerfel fuer den Polizeistreifen (unabhaengig von den Mesh-UPROPERTYs). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeShapeMesh;

	/** Lack-Instanzen der Polizei (fuer den Selbsttest gemerkt). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PolicePaintMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PoliceStripeMID;

	/** Scheinwerfer und Ruecklichter, Index = Fahrzeugindex * 2 + Seite (Verkehr + Polizei). */
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> HeadLightISM;

	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> TailLightISM;

	/** Materialinstanz der Lichter (null = Lampenmaterial fehlt, Rueckfall auf Grundform-Material). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> VehicleLightMID;

	/** Einpassung je Sorte. */
	FMTATrafficModelFit ModelFits[static_cast<int32>(EMTATrafficBodyModel::Count)];

	/** Je Fahrzeug-Slot: Sorte, erster Rad-Instanzindex, Glas-Instanzindex, Raddrehung (Grad). */
	TArray<uint8> VehicleModel;
	TArray<int32> WheelInstanceBase;
	TArray<int32> GlassInstance;
	TArray<float> WheelSpin;

	/** Je Fussgaenger: Kopf-Gruppe und Index darin. */
	TArray<uint8> PedHeadGroup;
	TArray<int32> PedHeadInstance;

	int32 NumPaintColors = 1;
	bool bVisualsReady = false;

	// Wiederverwendete Puffer (keine Allokation pro Frame)
	TArray<TArray<FTransform>> TrafficXfBuffers;
	TArray<TArray<FTransform>> PedXfBuffers;
	TArray<TArray<FTransform>> PedHeadXfBuffers;
	TArray<TArray<FTransform>> WheelXfBuffers;
	TArray<FTransform> GlassXfBuffer;
	TArray<FTransform> CabinXfBuffer;
	TArray<FTransform> PoliceXfBuffer;
	TArray<FTransform> StripeXfBuffer;
	TArray<FTransform> SirenRedXfBuffer;
	TArray<FTransform> SirenBlueXfBuffer;
	TArray<FTransform> HeadLightXfBuffer;
	TArray<FTransform> TailLightXfBuffer;
	/** Je Fahrzeug: Tempo im letzten Frame, Restzeit Bremslicht, zuletzt geschriebener Lichtzustand. */
	TArray<float> LightPrevSpeed;
	TArray<float> BrakeHold;
	TArray<uint8> TailLightState;
	TArray<uint8> HeadLightState;
	/** Je Karosserie-Komponente: naechster Abstand^2 zum Spieler in diesem Frame (Schatten nur nah). */
	TArray<float> BodyNearestDistSq;

	UInstancedStaticMeshComponent* CreateInstancer(const FName& InName, UStaticMesh* Mesh, const FLinearColor& Color, int32 Count, bool bShadows);
	/** Instanzer fuer Vorlagen-Meshes: Materialien des Meshes, optional Lack-MID auf PaintSlot. */
	UInstancedStaticMeshComponent* CreateModelInstancer(const FName& InName, UStaticMesh* Mesh, int32 Count, bool bShadows, const FIntPoint& Cull);
	/** Laedt die Vorlagen-Meshes und berechnet die Einpassung je Sorte. */
	void LoadVehicleModels();
	bool ComputeModelFit(EMTATrafficBodyModel Model, UStaticMesh* Body, UStaticMesh* Wheel, FMTATrafficModelFit& Out) const;
	/** Lack-Slot und Farbparameter eines Karosserie-Meshes suchen. */
	static bool FindPaintSlot(const UStaticMesh* Mesh, int32& OutSlot, FName& OutParam);
	UMaterialInstanceDynamic* MakePaintMID(EMTATrafficBodyModel Model, const FLinearColor& Color);
	/** Verfuegbare Sorte fuer einen Verkehrs-Slot (deterministisch aus dem Index, gewichtet). */
	EMTATrafficBodyModel PickModelForSlot(int32 Slot) const;
	void WriteWheelTransforms(int32 I, const FQuat& Rot, const FVector& Base, float FrameDt);
	/** Licht-Lagen einer Sorte aus Laenge/Breite, Unterkante und Hoehe der Karosserie. */
	void ComputeLightPlacement(FMTATrafficModelFit& Fit, double BottomZ, double Height, bool bFlatEnds) const;
	/** Lichter erzeugen (nach den Karosserien, Vehicles.Num() bekannt). */
	void InitVehicleLights();
	/** Transformationen und Zustand (Bremse, Polizei-Blinken) der Lichter eines Fahrzeugs. */
	void WriteLightTransforms(int32 I, const FQuat& Rot, const FVector& Base, const FMTATrafficModelFit& Fit);
	void UpdateLightState(int32 I, float FrameDt, bool bPolice, bool bSirenOn, bool bRedPhase);
	void HideVehicleLights(int32 I);
	static uint32 HashIndex(uint32 X);
	static FTransform HiddenTransform();
};
