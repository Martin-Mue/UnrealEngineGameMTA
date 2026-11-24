#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MTAInteractionInterface.h"
#include "MTAMissionPickup.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class USphereComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Darstellung/Bedeutung eines Missionspunkts. */
UENUM(BlueprintType)
enum class EMTAPickupKind : uint8
{
	Meeting     UMETA(DisplayName = "Treffpunkt"),
	Pickup      UMETA(DisplayName = "Abholpunkt"),
	Dropoff     UMETA(DisplayName = "Abgabepunkt"),
	Checkpoint  UMETA(DisplayName = "Checkpoint"),
	Terminal    UMETA(DisplayName = "Terminal"),
	Zone        UMETA(DisplayName = "Bereich"),
	SafeZone    UMETA(DisplayName = "Versteck")
};

/**
 * Abholpunkt / Checkpoint / Terminal einer Mission.
 * Sichtbar als Leuchtsaeule (Engine-Zylinder) plus Bodenscheibe in Radiusgroesse.
 * Die Erfuellung prueft UMTAMissionRunner abstandsbasiert; zusaetzlich kann der Spieler zu Fuss
 * ueber IMTAInteractable (Taste E, Halten) interagieren, z. B. "Hack starten" oder "Paket nehmen".
 * Kein eigener Tick.
 */
UCLASS(Blueprintable)
class MTA6_API AMTAMissionPickup : public AActor, public IMTAInteractable
{
	GENERATED_BODY()

public:
	AMTAMissionPickup();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Mission")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Leuchtsaeule. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Mission")
	TObjectPtr<UStaticMeshComponent> BeaconMesh;

	/** Flache Scheibe am Boden (Radius des Zielbereichs). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Mission")
	TObjectPtr<UStaticMeshComponent> GroundDisc;

	/** Kleines Ueberlappvolumen, damit die Interaktionskomponente den Punkt findet. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Mission")
	TObjectPtr<USphereComponent> InteractVolume;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	EMTAPickupKind Kind = EMTAPickupKind::Meeting;

	/** Zielradius (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float Radius = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float BeaconHeight = 4000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float BeaconDiameter = 300.f;

	/** Interaktionstext ("Hack starten"); leer = keine Interaktion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	FString PromptText;

	/** Haltezeit der Interaktion (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float InteractHoldDuration = 1.5f;

	/** Konfiguration durch den Runner. */
	void Setup(EMTAPickupKind InKind, float InRadius, const FString& InPrompt);

	/** Aktuelles Ziel (hell), Vorschau (dunkel, ohne Interaktion) oder versteckt. */
	void SetMarkerState(bool bInCurrent, bool bInPreview);

	/** Fortschritt 0..1 (Hack-Upload, Halten): Saeule waechst, Farbe geht nach Weiss. */
	void SetProgress(float Alpha);

	/** true, wenn seit dem letzten Aufruf interagiert wurde (setzt das Flag zurueck). */
	bool ConsumeInteraction();

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	bool IsCurrent() const { return bCurrent; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	static FLinearColor GetKindColor(EMTAPickupKind InKind);

	// --- IMTAInteractable ------------------------------------------------------------------
	virtual FMTAInteractionInfo GetInteractionInfo_Implementation(AActor* Interactor) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual void OnInteractionHoldProgress_Implementation(AActor* Interactor, float Alpha) override;

protected:
	virtual void BeginPlay() override;

private:
	void ApplyLook();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BeaconMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DiscMID;

	bool bCurrent = false;
	bool bPreview = false;
	bool bInteracted = false;
	float Progress = 0.f;
};
