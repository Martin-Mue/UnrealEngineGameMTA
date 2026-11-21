#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MTACharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UMTAInteractionComponent;
class UMTAVehicleEntryComponent;
class UMTAStealthComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAStaminaSignature, float, Stamina, float, MaxStamina);

/** Gangart zu Fuss. */
UENUM(BlueprintType)
enum class EMTAGait : uint8
{
	Walk   UMETA(DisplayName = "Gehen"),
	Jog    UMETA(DisplayName = "Laufen"),
	Sprint UMETA(DisplayName = "Rennen")
};

/**
 * Spielerfigur zu Fuss (Konzept Kapitel 12/13: "Zu-Fuss-Paket").
 * Third-Person-Bewegung mit Gehen/Laufen/Rennen, Springen, Ducken, Ueberklettern niedriger
 * Hindernisse (Trace-basiert), Ausweichrolle, Stamina und Schulterkamera.
 * Animationsanbindung ausschliesslich ueber die "Anim"-Variablen (AnimBP liest sie per
 * Property Access). Kompiliert und laeuft ohne Assets (Kapsel + Kamera).
 * Eingaben: Legacy-Mappings aus Config/DefaultInputOnFoot.ini.
 */
UCLASS(Blueprintable)
class MTA6_API AMTACharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AMTACharacter(const FObjectInitializer& ObjectInitializer);

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	// --- Komponenten ---------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Components")
	TObjectPtr<UMTAInteractionComponent> Interaction;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Components")
	TObjectPtr<UMTAVehicleEntryComponent> VehicleEntry;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Components")
	TObjectPtr<UMTAStealthComponent> Stealth;

	// --- Bewegung --------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Movement")
	float WalkSpeed = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Movement")
	float JogSpeed = 420.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Movement")
	float SprintSpeed = 680.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Movement")
	float CrouchSpeed = 170.f;

	/** Gamepad-Drehrate (Grad/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Input")
	float TurnRateGamepad = 120.f;

	/** Toggle statt Halten fuer Ducken (Barrierefreiheit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Input")
	bool bToggleCrouch = true;

	// --- Stamina -------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stamina")
	float MaxStamina = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stamina")
	float SprintDrainPerSecond = 16.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stamina")
	float StaminaRegenPerSecond = 22.f;

	/** Verzoegerung (s) nach Verbrauch, bevor Stamina regeneriert. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stamina")
	float StaminaRegenDelay = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stamina")
	float JumpStaminaCost = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stamina")
	float VaultStaminaCost = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stamina")
	float DodgeStaminaCost = 25.f;

	/** Nach Erschoepfung erst ab diesem Anteil wieder rennen (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stamina", meta = (ClampMin = "0", ClampMax = "1"))
	float ExhaustionRecoverFraction = 0.3f;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Stamina")
	FMTAStaminaSignature OnStaminaChanged;

	// --- Klettern (Vault/Mantle) -----------------------------------------------------------

	/** Hindernishoehe ueber den Fuessen (cm), die ueberklettert werden kann. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vault")
	float VaultMinHeight = 35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vault")
	float VaultMaxHeight = 130.f;

	/** Bis zu dieser Tiefe (cm) wird darueber gesprungen, sonst hinaufgestiegen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vault")
	float VaultMaxDepth = 90.f;

	/** Suchweite nach vorn (cm ab Kapselrand). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vault")
	float VaultReach = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vault")
	float VaultDuration = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vault")
	float MantleDuration = 0.6f;

	// --- Ausweichrolle -------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Dodge")
	float DodgeSpeed = 850.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Dodge")
	float DodgeDuration = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Dodge")
	float DodgeCooldown = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Dodge")
	float BailOutDuration = 1.1f;

	// --- Kamera ----------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Camera")
	float ArmLength = 320.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Camera")
	float CrouchArmLength = 260.f;

	/** Seitlicher Schulterversatz (cm, rechts positiv). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Camera")
	float ShoulderOffset = 55.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Camera")
	float ShoulderHeight = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Camera")
	float BaseFOV = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Camera")
	float SprintFOV = 88.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Camera")
	float CameraInterpSpeed = 8.f;

	// --- Animationsvariablen (nur lesen; AnimBP) ------------------------------------------

	/** Horizontale Geschwindigkeit (cm/s). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Anim")
	float Speed = 0.f;

	/** Bewegungsrichtung relativ zur Blickrichtung der Figur (-180..180 Grad). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Anim")
	float Direction = 0.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Anim")
	bool bIsInAir = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Anim")
	bool bIsCrouching = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Anim")
	bool bIsSprinting = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Anim")
	bool bIsDodging = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Anim")
	bool bIsVaulting = false;

	/** true = Hinaufsteigen (Mantle), false = Darueberspringen (Vault). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Anim")
	bool bIsMantling = false;

	/** Fortschritt 0..1 des aktuellen Vault/Mantle. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Anim")
	float VaultAlpha = 0.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Anim")
	bool bIsBailingOut = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Anim")
	bool bIsInVehicle = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Anim")
	bool bIsExhausted = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Anim")
	EMTAGait Gait = EMTAGait::Jog;

	/** Blick-Neigung relativ zur Figur (Aim-Offset). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Anim")
	float AimPitch = 0.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Stamina")
	float Stamina = 100.f;

	// --- API -------------------------------------------------------------------------------

	/** Prueft per Trace, ob vor der Figur ein niedriges Hindernis ueberklettert werden kann. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Vault")
	bool TryVault();

	UFUNCTION(BlueprintCallable, Category = "MTA6|Dodge")
	bool TryDodge();

	/** Abspringen aus dem fahrenden Auto (von UMTAVehicleEntryComponent aufgerufen). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Dodge")
	void StartBailOutRoll(FVector BailDirection);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Camera")
	void SwapShoulder();

	UFUNCTION(BlueprintPure, Category = "MTA6|Stamina")
	float GetStaminaFraction() const { return MaxStamina > 0.f ? Stamina / MaxStamina : 0.f; }

	/** Kann die Figur gerade Bewegungseingaben annehmen? */
	UFUNCTION(BlueprintPure, Category = "MTA6|Movement")
	bool CanMove() const;

	/** Verbraucht Stamina; false, wenn nicht genug vorhanden. */
	bool ConsumeStamina(float Amount);

protected:
	virtual void BeginPlay() override;

private:
	// Eingaben
	void MoveForward(float Value);
	void MoveRight(float Value);
	void Turn(float Value);
	void LookUp(float Value);
	void TurnRate(float Value);
	void LookUpRate(float Value);
	void OnJumpPressed();
	void OnJumpReleased();
	void OnSprintPressed();
	void OnSprintReleased();
	void OnCrouchPressed();
	void OnCrouchReleased();
	void OnWalkToggle();
	void OnDodgePressed();
	void OnInteractPressed();
	void OnInteractReleased();
	void OnEnterExitPressed();
	void OnShoulderSwapPressed();

	void UpdateStamina(float DeltaSeconds);
	void UpdateGait();
	void UpdateVault(float DeltaSeconds);
	void UpdateDodge(float DeltaSeconds);
	void UpdateCamera(float DeltaSeconds);
	void UpdateAnimVariables();
	void EndDodge();

	bool bWantsSprint = false;
	bool bWalkToggled = false;
	float ShoulderSign = 1.f;
	float CurrentShoulderY = 55.f;
	float StaminaRegenDelayLeft = 0.f;
	float DodgeTimeLeft = 0.f;
	float DodgeCooldownLeft = 0.f;
	float BailOutTimeLeft = 0.f;

	// Dodge: gesicherte Bewegungswerte
	float SavedGroundFriction = 8.f;
	float SavedBrakingDecel = 2048.f;

	// Vault-Verlauf (Start -> Scheitel -> Ziel)
	FVector VaultStart = FVector::ZeroVector;
	FVector VaultApex = FVector::ZeroVector;
	FVector VaultEnd = FVector::ZeroVector;
	float VaultElapsed = 0.f;
	float VaultTotal = 0.f;
};
