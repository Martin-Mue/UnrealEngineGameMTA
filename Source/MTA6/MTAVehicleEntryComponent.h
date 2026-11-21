#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "MTAInteractionInterface.h"
#include "MTAVehicleEntryComponent.generated.h"

class ACharacter;
class APawn;
class APlayerController;
class AWheeledVehiclePawn;
class UInputComponent;
class UMTAChaosSubsystem;

/** Ablauf des Ein-/Aussteigens (AnimBP/Montagen haengen sich an OnPhaseChanged). */
UENUM(BlueprintType)
enum class EMTAVehicleEntryPhase : uint8
{
	None             UMETA(DisplayName = "Zu Fuss"),
	Approaching      UMETA(DisplayName = "Laeuft zur Tuer"),
	BreakingIn       UMETA(DisplayName = "Bricht auf"),
	OpeningDoor      UMETA(DisplayName = "Oeffnet Tuer"),
	PullingOutDriver UMETA(DisplayName = "Zieht Fahrer heraus"),
	EnteringSeat     UMETA(DisplayName = "Steigt ein"),
	Seated           UMETA(DisplayName = "Faehrt"),
	ExitingSeat      UMETA(DisplayName = "Steigt aus"),
	BailingOut       UMETA(DisplayName = "Springt ab")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAEntryPhaseSignature, EMTAVehicleEntryPhase, NewPhase, AWheeledVehiclePawn*, Vehicle);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAEntryVehicleSignature, AWheeledVehiclePawn*, Vehicle);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTADriverEjectedSignature, AWheeledVehiclePawn*, Vehicle, APawn*, EjectedDriver);

/**
 * Ein-/Aussteigen fuer AMTACharacter.
 * Spricht Fahrzeuge nur ueber die Engine-Basisklasse AWheeledVehiclePawn an (keine Abhaengigkeit
 * zu AMTAVehiclePawn). Wechselt den Possess des PlayerControllers, haengt die Figur an den
 * Fahrersitz (Sockel oder Versatz), legt eine eigene Eingabe-Komponente fuer "Aussteigen" auf den
 * Controller-Stack und meldet Diebstahl/Fahrzeugraub an UMTAChaosSubsystem (das Heat ueber
 * UMTAHeatSubsystem::AddHeat bucht).
 *
 * Fahrzeug-Tags (am Fahrzeug-Aktor): "PlayerOwned" = eigenes Auto, "Locked" = abgeschlossen.
 * Sockel am Fahrzeug-Mesh (optional): DriverSeatSocket, DriverDoorSocket.
 */
UCLASS(ClassGroup = (MTA6), meta = (BlueprintSpawnableComponent))
class MTA6_API UMTAVehicleEntryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMTAVehicleEntryComponent();

	// --- Suche ---------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|VehicleEntry")
	float SearchRadius = 450.f;

	/** Weiter als das (cm) von der Tuer entfernt wird kein Einstieg begonnen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|VehicleEntry")
	float MaxEnterDistance = 400.f;

	// --- Sitz und Tuer -------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|VehicleEntry")
	FName DriverSeatSocket = TEXT("DriverSeat");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|VehicleEntry")
	FName DriverDoorSocket = TEXT("DriverDoor");

	/** Ersatz, wenn der Sockel fehlt: Sitzposition relativ zum Fahrzeug (Linkslenker: -Y). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|VehicleEntry")
	FVector FallbackSeatOffset = FVector(-10.f, -40.f, 60.f);

	/** Ersatz-Tuerpunkt relativ zum Fahrzeug (dort steht die Figur beim Einsteigen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|VehicleEntry")
	FVector FallbackDoorOffset = FVector(10.f, -170.f, 0.f);

	/** Figur waehrend der Fahrt ausblenden (bis eine Sitz-Animation existiert). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|VehicleEntry")
	bool bHideCharacterWhileDriving = true;

	// --- Zeiten (Platzhalter fuer Animationen) -------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|VehicleEntry|Timing")
	float ApproachTimeout = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|VehicleEntry|Timing")
	float DoorOpenTime = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|VehicleEntry|Timing")
	float BreakInTime = 2.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|VehicleEntry|Timing")
	float PullOutTime = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|VehicleEntry|Timing")
	float EnterSeatTime = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|VehicleEntry|Timing")
	float ExitTime = 0.5f;

	// --- Aussteigen ----------------------------------------------------------------------

	/** Bis zu dieser Geschwindigkeit normal aussteigen, darueber abspringen (Rolle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|VehicleEntry")
	float MaxCalmExitSpeedKmh = 18.f;

	/** Action-Mapping fuer Ein-/Aussteigen (Config/DefaultInputOnFoot.ini). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|VehicleEntry")
	FName EnterExitActionName = TEXT("EnterExitVehicle");

	// --- NPC-Fahrer ----------------------------------------------------------------------

	/** Figur, die beim Herausziehen eines KI-Fahrers erscheint (leer = nur Controller entfernen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|VehicleEntry")
	TSubclassOf<ACharacter> EjectedDriverClass;

	// --- Zustand -------------------------------------------------------------------------

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|VehicleEntry")
	EMTAVehicleEntryPhase Phase = EMTAVehicleEntryPhase::None;

	/** Tuer-Oeffnungsgrad 0..1 fuer Fahrzeug-AnimBP/Tuer-Platzhalter. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|VehicleEntry")
	float DoorOpenAlpha = 0.f;

	// --- Ereignisse ----------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "MTA6|VehicleEntry")
	FMTAEntryPhaseSignature OnPhaseChanged;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|VehicleEntry")
	FMTAEntryVehicleSignature OnEnteredVehicle;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|VehicleEntry")
	FMTAEntryVehicleSignature OnExitedVehicle;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|VehicleEntry")
	FMTADriverEjectedSignature OnDriverEjected;

	// --- API -----------------------------------------------------------------------------

	/** Eingabe "EnterExitVehicle": im Auto aussteigen, sonst naechstes Fahrzeug betreten. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|VehicleEntry")
	void ToggleEnterExit();

	UFUNCTION(BlueprintCallable, Category = "MTA6|VehicleEntry")
	bool RequestEnter(AWheeledVehiclePawn* Vehicle);

	UFUNCTION(BlueprintCallable, Category = "MTA6|VehicleEntry")
	bool RequestExit();

	/** Bricht einen laufenden Einstieg ab (Treffer, Bewegungseingabe). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|VehicleEntry")
	void CancelEntry();

	/**
	 * Setzt die Figur direkt in ein Fahrzeug (Spielstart im Auto, Missionsskript, Laden).
	 * Kein Verbrechen, keine Animation.
	 */
	UFUNCTION(BlueprintCallable, Category = "MTA6|VehicleEntry")
	void PlaceInVehicleImmediately(AWheeledVehiclePawn* Vehicle);

	UFUNCTION(BlueprintPure, Category = "MTA6|VehicleEntry")
	AWheeledVehiclePawn* FindNearestVehicle() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|VehicleEntry")
	FMTAInteractionInfo GetVehicleInteractionInfo(AWheeledVehiclePawn* Vehicle) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|VehicleEntry")
	bool IsInVehicle() const { return Phase == EMTAVehicleEntryPhase::Seated && CurrentVehicle != nullptr; }

	/** Laeuft gerade ein Ein-/Ausstieg (Figur nimmt keine Bewegungseingabe an)? */
	UFUNCTION(BlueprintPure, Category = "MTA6|VehicleEntry")
	bool IsBusy() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|VehicleEntry")
	AWheeledVehiclePawn* GetCurrentVehicle() const { return CurrentVehicle; }

	UFUNCTION(BlueprintPure, Category = "MTA6|VehicleEntry")
	AWheeledVehiclePawn* GetTargetVehicle() const { return TargetVehicle; }

	/** Hat das Fahrzeug einen Fahrer, der nicht der Spieler ist? */
	static bool HasForeignDriver(const AWheeledVehiclePawn* Vehicle);

	/** Weltposition der Fahrertuer (Sockel oder Versatz). */
	FVector GetDoorLocation(const AWheeledVehiclePawn* Vehicle) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void SetPhase(EMTAVehicleEntryPhase NewPhase, float Duration);
	void AdvancePhase();
	void EjectDriver(AWheeledVehiclePawn* Vehicle);
	void CompleteEnter(bool bSilent);
	void CompleteExit(bool bBailOut);
	bool FindExitLocation(const AWheeledVehiclePawn* Vehicle, FVector& OutLocation) const;
	void PushExitInput(APlayerController* PC);
	void PopExitInput();
	void HandleExitPressed();
	void StopWatchingVehicle();
	void ReportTheftIfNeeded(AWheeledVehiclePawn* Vehicle);

	ACharacter* GetCharacter() const;
	APlayerController* GetPlayerController() const;
	UMTAChaosSubsystem* GetChaos() const;

	UPROPERTY(Transient)
	TObjectPtr<AWheeledVehiclePawn> TargetVehicle;

	UPROPERTY(Transient)
	TObjectPtr<AWheeledVehiclePawn> CurrentVehicle;

	/** PlayerController, der die Figur vor dem Einsteigen besessen hat. */
	UPROPERTY(Transient)
	TObjectPtr<APlayerController> DrivingController;

	UPROPERTY(Transient)
	TObjectPtr<UInputComponent> ExitInput;

	UPROPERTY(Transient)
	TObjectPtr<APawn> EjectedDriver;

	float PhaseTimeLeft = 0.f;
	float PhaseDuration = 0.f;
	bool bTargetWasLocked = false;
	bool bTargetHadDriver = false;
	TWeakObjectPtr<AWheeledVehiclePawn> LastWatchedVehicle;
	FTimerHandle UnwatchTimer;
};
