#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MTAChaseTarget.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Bewegungsart eines Missionsfahrzeugs. */
UENUM(BlueprintType)
enum class EMTAChaseTargetMode : uint8
{
	/** Steht still (vor dem Start, nach dem Stellen). */
	Idle   UMETA(DisplayName = "Steht"),
	/** Faehrt auf dem Strassenraster und flieht vor dem Spieler. */
	Flee   UMETA(DisplayName = "Flucht"),
	/** Folgt einer festen Punktkette (Renngegner). */
	Route  UMETA(DisplayName = "Strecke"),
	/** Folgt der Spur eines Anfuehrers mit Zeitversatz (Begleitfahrzeug). */
	Trail  UMETA(DisplayName = "Kolonne")
};

/** Spielerzustand, den der Runner pro Schritt uebergibt (keine eigene Spielersuche im Fahrzeug). */
struct FMTAChasePlayerContext
{
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	bool bValid = false;
	/** Wird bei der Bodensuche ignoriert. */
	TWeakObjectPtr<AActor> PlayerActor;
};

/**
 * KI-Zielfahrzeug fuer Verfolgungen, Auftragsjagden und Rennen.
 * Kein Physikfahrzeug: kinematische Bewegung auf dem Stadtraster (MTACity, Rechtsverkehr)
 * wie die Welt-KI. Tickt NICHT selbst; UMTAMissionRunner ruft StepTarget() mit festem
 * Spielerkontext auf (deterministisch, auch im Selbsttest ohne Frames nutzbar).
 *
 * VERHALTEN (Flee):
 *   Tempo   : Ziel = CruiseSpeed * (1 + 0,4 * EscapeSkill); Spieler < PanicDistance -> MaxSpeed;
 *             Spieler > CatchUpDistance -> * CatchUpFactor (Gummiband, damit man aufholen kann).
 *   Kreuzung: geradeaus / links / rechts (nicht aus der Stadt). Bewertung =
 *             Zufall * (1,2 - Skill) + Skill * 1,5 * cos(Winkel zur Fluchtrichtung) + 0,3 (geradeaus).
 *             Nach dem Abbiegen Tempo * 0,65. Sackgasse am Stadtrand -> Wende.
 *   Blockade: Spieler im Korridor vor dem Ziel (BlockLookAhead x BlockHalfWidth) und langsamer
 *             als 600 cm/s -> Ziel bremst auf 0, BlockedTime waechst (Stellen durch Blockieren).
 *   Rammen  : Abstand < ContactRadius und Annaeherung > RamMinClosingSpeed -> Schaden
 *             8 + 1,6 je m/s Annaeherung, Tempo * 0,5, Sperre RamCooldown.
 */
UCLASS(Blueprintable)
class MTA6_API AMTAChaseTarget : public AActor
{
	GENERATED_BODY()

public:
	AMTAChaseTarget();

	// --- Komponenten -----------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Chase")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Chase")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Chase")
	TObjectPtr<UStaticMeshComponent> CabinMesh;

	/** Signalleuchte auf dem Dach (macht das Ziel im Verkehr erkennbar). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Chase")
	TObjectPtr<UStaticMeshComponent> BeaconMesh;

	// --- Einstellungen ---------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase")
	float CruiseSpeed = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase")
	float MaxSpeed = 2600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase")
	float Acceleration = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase")
	float Braking = 2500.f;

	/** 0..1, siehe Klassenbeschreibung. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase", meta = (ClampMin = "0", ClampMax = "1"))
	float EscapeSkill = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase")
	float MaxHitPoints = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase")
	float PanicDistance = 4000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase")
	float CatchUpDistance = 15000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase")
	float CatchUpFactor = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase")
	float ContactRadius = 520.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase")
	float RamMinClosingSpeed = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase")
	float RamCooldown = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase")
	float BlockLookAhead = 1300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase")
	float BlockHalfWidth = 380.f;

	/** Seitlicher Versatz zur Strassenmitte (Rechtsverkehr, wie MTACity::LaneOffset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase")
	float LaneOffset = 400.f;

	/** Karosserie blockiert Fahrzeuge physisch (kinematisch bewegt). Aus = nur abstandsbasierter Kontakt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase")
	bool bPhysicalCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase|Visual")
	FVector BodySize = FVector(440.f, 190.f, 90.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase|Visual")
	FVector CabinSize = FVector(230.f, 170.f, 60.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase|Visual")
	FLinearColor BodyColor = FLinearColor(0.75f, 0.08f, 0.06f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase|Visual")
	FLinearColor BeaconColor = FLinearColor(1.f, 0.35f, 0.05f);

	/** Glaettung der Darstellung zur Simulationsposition (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chase|Visual")
	float VisualSmoothing = 10.f;

	// --- Steuerung durch den Runner ----------------------------------------------------------

	/** Flucht auf dem Raster: rastet auf die naechste Strasse bei Near ein, faehrt weg von AwayFrom. */
	void InitFlee(const FVector& Near, const FVector& AwayFrom, int32 Seed);

	/** Rennstrecke (Punkte achsparallel auf Strassenlinien), Tempo und seitlicher Spurversatz. */
	void InitRoute(const TArray<FVector>& Points, float Speed, float Lateral);

	/** Startschuss fuer Route-Modus. */
	void StartRoute() { bRouteStarted = true; }

	/** Kolonne hinter einem Anfuehrer (Zeitversatz in s). */
	void InitTrail(AMTAChaseTarget* InLeader, float DelaySeconds);

	/** Ein Simulationsschritt. */
	void StepTarget(float DeltaSeconds, const FMTAChasePlayerContext& Player);

	/** Gestellt / ausser Gefecht: bleibt stehen, Leuchte aus. */
	void SetDisabled(bool bInDisabled);

	/** Gummiband fuer Renngegner (1 = normal). */
	void SetSpeedScale(float InScale) { SpeedScale = FMath::Clamp(InScale, 0.3f, 2.f); }

	UFUNCTION(BlueprintCallable, Category = "MTA6|Chase")
	void SetBodyColor(const FLinearColor& InColor);

	// --- Abfragen --------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|Chase")
	EMTAChaseTargetMode GetMode() const { return Mode; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Chase")
	float GetHealthFraction() const { return MaxHitPoints > 0.f ? FMath::Max(0.f, HitPoints) / MaxHitPoints : 0.f; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Chase")
	float GetBlockedTime() const { return BlockedTime; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Chase")
	float GetSpeed() const { return Speed; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Chase")
	bool IsDisabled() const { return bDisabled; }

	/** Simulationsposition (ohne Glaettung), Z = Boden. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Chase")
	FVector GetSimLocation() const { return SimLocation; }

	/** Fahrtrichtung (Einheitsvektor, XY). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Chase")
	FVector GetSimForward() const { return SimForward; }

	/** Route: gefahrene Strecke und Gesamtlaenge (cm). */
	float GetRouteProgress() const { return RouteDistance; }
	float GetRouteLength() const { return RouteTotal; }
	bool IsRouteFinished() const { return RouteTotal > 0.f && RouteDistance >= RouteTotal - 1.f; }

	/** Treffer im letzten Schritt (Rammen durch den Spieler). */
	bool WasHitLastStep() const { return bHitLastStep; }
	float GetLastHitDamage() const { return LastHitDamage; }

	/** Spurpunkt vor DelaySeconds (Kolonne). false, wenn die Spur noch zu kurz ist. */
	bool GetTrailSample(float DelaySeconds, FVector& OutLocation, FVector& OutForward) const;

protected:
	virtual void BeginPlay() override;

private:
	void StepFlee(float Dt, const FMTAChasePlayerContext& Player);
	void StepRoute(float Dt);
	void StepTrail(float Dt);
	void HandleCrossing(int32 CrossLine, const FMTAChasePlayerContext& Player);
	void UpdateLaneLocation();
	void HandleContact(float Dt, const FMTAChasePlayerContext& Player);
	void RecordTrail(float Dt);
	void UpdateGround(float Dt, const FMTAChasePlayerContext& Player);
	void ApplyVisual(float Dt, bool bSnap);
	void ApproachSpeed(float Desired, float Dt);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CabinMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BeaconMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	EMTAChaseTargetMode Mode = EMTAChaseTargetMode::Idle;

	// Rasterzustand (wie Welt-KI): Axis 0 = faehrt entlang X auf der Y-Linie "Line", Pos = X.
	uint8 Axis = 0;
	int8 Sign = 1;
	int32 Line = 4;
	float Pos = 0.f;

	float Speed = 0.f;
	float SpeedScale = 1.f;
	float HitPoints = 100.f;
	float BlockedTime = 0.f;
	float HitCooldownLeft = 0.f;
	float LastHitDamage = 0.f;
	bool bHitLastStep = false;
	bool bDisabled = false;
	bool bVisualInitialized = false;

	FVector SimLocation = FVector::ZeroVector;
	FVector SimForward = FVector::ForwardVector;
	FVector SimVelocity = FVector::ZeroVector;
	float GroundZ = 0.f;
	float GroundTimer = 0.f;

	FRandomStream Rng;

	// Route
	TArray<FVector> RoutePoints;
	TArray<float> RouteCumulative;
	float RouteTotal = 0.f;
	float RouteDistance = 0.f;
	float RouteSpeed = 1800.f;
	float RouteLateral = 0.f;
	bool bRouteStarted = false;

	// Kolonne
	TWeakObjectPtr<AMTAChaseTarget> Leader;
	float TrailDelay = 1.f;
	TArray<FVector> TrailLocations;
	TArray<FVector> TrailForwards;
	float TrailTimer = 0.f;
};
