#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "MTAVehiclePawn.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UChaosVehicleMovementComponent;
class AMTA6GameMode;
class UMTAVehicleAudioComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAVehicleDamagedSignature, float, NewHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMTAVehicleWreckedSignature);

/**
 * Spielerfahrzeug (Port des VehicleBody3D aus godot/main.gd).
 * Raeder, Skeletal Mesh, Physik-Asset und Motor-/Lenkkurven werden im Blueprint
 * BP_MTAVehicle gesetzt; dieser Code kompiliert und laeuft ohne Assets.
 */
UCLASS(Blueprintable)
class MTA6_API AMTAVehiclePawn : public AWheeledVehiclePawn
{
	GENERATED_BODY()

public:
	AMTAVehiclePawn(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved,
		FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

	// --- Komponenten ---------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** Motor-, Reifen-, Wind-, Hupen- und Aufprallklang; hoerbar nur, solange der Spieler faehrt. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Audio")
	TObjectPtr<UMTAVehicleAudioComponent> VehicleAudio;

	// --- Fahrzeugdaten -------------------------------------------------------------

	/** Hoechstgeschwindigkeit; darueber wird kein Gas mehr gegeben (Godot: ca. 150 km/h). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle", meta = (ClampMin = "10"))
	float MaxSpeedKmh = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle", meta = (ClampMin = "1"))
	float MaxHealth = 100.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Vehicle")
	float Health = 100.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Vehicle")
	bool bWrecked = false;

	/** Aufprallgeschwindigkeit (cm/s entlang der Trefferrichtung), ab der Schaden entsteht. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle|Damage")
	float DamageSpeedThreshold = 600.f;

	/** Schaden pro m/s ueber der Schwelle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle|Damage")
	float DamagePerMps = 2.5f;

	/** Mindestabstand zwischen zwei Treffern (s), wie hit_cool im Prototyp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle|Damage")
	float HitCooldown = 0.5f;

	/** Maximaler Lenkfaktor bei hoher Geschwindigkeit wird bis auf (1 - SteerSpeedReduction) gesenkt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle|Input", meta = (ClampMin = "0", ClampMax = "1"))
	float SteerSpeedReduction = 0.7f;

	/** Geschwindigkeit (km/h), bei der die Lenkreduktion voll greift. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle|Input")
	float SteerReductionSpeedKmh = 216.f;

	/** Glaettung der Lenkeingabe (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle|Input")
	float SteerInterpSpeed = 8.f;

	/** Seitlicher Abstand zur Strassenachse beim Zuruecksetzen (rechte Spur, cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle")
	float ResetLaneOffset = 400.f;

	// --- Klang ----------------------------------------------------------------------

	/** Rutschgeschwindigkeit eines Rades (Chaos SkidMagnitude, cm/s), bei der der Schlupf als voll gilt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Audio")
	float AudioSkidFullCms = 1000.f;

	/** Querbeschleunigung (in g), ab der die Reifen zu quietschen beginnen bzw. voll quietschen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Audio")
	float AudioLateralGStart = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Audio")
	float AudioLateralGFull = 1.3f;

	// --- Kamera ---------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Camera")
	float ArmLengthMin = 750.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Camera")
	float ArmLengthMax = 1050.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Camera")
	float FOVMin = 72.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Camera")
	float FOVMax = 88.f;

	/** Geschwindigkeit (km/h), bei der Abstand/FOV ihr Maximum erreichen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Camera")
	float CameraMaxSpeedKmh = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Camera")
	float CameraInterpSpeed = 3.f;

	/** Maximale Verschiebung (cm) bzw. Drehung (Grad) beim Kamera-Schuetteln. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Camera")
	float ShakeMaxOffset = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Camera")
	float ShakeMaxAngle = 2.5f;

	/** Abbau des Schuettelns pro Sekunde (0..1-Skala). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Camera")
	float ShakeDecay = 1.6f;

	// --- Ereignisse -----------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Vehicle")
	FMTAVehicleDamagedSignature OnDamaged;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Vehicle")
	FMTAVehicleWreckedSignature OnWrecked;

	// --- API ------------------------------------------------------------------------

	/** Fuegt Schaden zu (z. B. von der KI bei Rammstoessen). Loest Kamera-Schuetteln aus. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Vehicle")
	void ApplyVehicleDamage(float Amount);

	/** Kamera-Schuetteln (0..1 wird addiert). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Camera")
	void AddCameraShake(float Intensity);

	/** Taste R: aufrichten und auf die naechste Strasse setzen. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Vehicle")
	void ResetVehicle();

	/** Teleport (Laden, Festnahme, Wrack) inklusive Physik-Stopp. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Vehicle")
	void TeleportVehicle(const FVector& Location, const FRotator& Rotation);

	/** Volle Gesundheit, Wrack-Status aufheben. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Vehicle")
	void Repair();

	UFUNCTION(BlueprintPure, Category = "MTA6|Vehicle")
	float GetSpeedKmh() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Vehicle")
	float GetHealthFraction() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }

protected:
	virtual void BeginPlay() override;

private:
	// Eingaben (Legacy-Input, Namen aus Config/DefaultInput.ini)
	void OnThrottle(float Value);
	void OnBrake(float Value);
	void OnSteer(float Value);
	void OnHandbrakePressed();
	void OnHandbrakeReleased();
	void OnHornPressed();
	void OnHornReleased();
	void OnResetPressed();
	void OnSavePressed();
	void OnLoadPressed();
	void OnChoice1();
	void OnChoice2();
	void OnChoice3();
	void OnChoice4();
	void OnConfirm();
	void SelectChoice(int32 Index);

	void UpdateDriving(float DeltaSeconds);
	void UpdateCamera(float DeltaSeconds);
	void UpdateVehicleAudio(float DeltaSeconds);
	AMTA6GameMode* GetMTAGameMode() const;
	bool IsInputBlocked() const;

	float ThrottleInput = 0.f;
	float BrakeInput = 0.f;
	float SteerInput = 0.f;
	float SmoothedSteer = 0.f;
	bool bHandbrake = false;
	bool bHornHeld = false;

	float ShakeTrauma = 0.f;
	float HitCooldownLeft = 0.f;
	FVector LastVelocity = FVector::ZeroVector;
	FVector CameraBaseLocation = FVector::ZeroVector;
};
