#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MTAInteractionInterface.h"
#include "MTAInteractionActors.generated.h"

class UStaticMeshComponent;
class AMTAAegisCameraActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAInteractedSignature, AActor*, Interactor, FName, InteractionId);

/**
 * Basis fuer platzierbare interaktive Objekte. Mesh ist ein Platzhalter (im Blueprint setzen).
 * InteractionId verknuepft das Objekt mit Daten (Shop-ID, Garagen-ID, Beweis-ID ...).
 */
UCLASS(Abstract, Blueprintable)
class MTA6_API AMTAInteractableActor : public AActor, public IMTAInteractable
{
	GENERATED_BODY()

public:
	AMTAInteractableActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Interaction")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Interaction")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	FName InteractionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	FText Prompt;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction", meta = (ClampMin = "0"))
	float HoldDuration = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	bool bInteractionEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	int32 Priority = 0;

	/** Wird nach jeder erfolgreichen Interaktion gefeuert (HUD, Missionen, Wirtschaft). */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Interaction")
	FMTAInteractedSignature OnInteracted;

	virtual FMTAInteractionInfo GetInteractionInfo_Implementation(AActor* Interactor) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;

protected:
	virtual EMTAInteractionKind GetKind() const { return EMTAInteractionKind::Generic; }
	/** Unterklassen fuehren hier ihre Wirkung aus. */
	virtual void PerformInteraction(AActor* Interactor) {}
	/** Meldet ein Verbrechen an UMTAChaosSubsystem (Taeter = Interactor). */
	void ReportCrime(uint8 CrimeType, AActor* Interactor, float Severity = 1.f) const;
};

/** Tuer mit Scharnier (Platzhalter-Animation per Rotation), abschliessbar, aufbrechbar. */
UCLASS(Blueprintable)
class MTA6_API AMTAInteractDoor : public AMTAInteractableActor
{
	GENERATED_BODY()

public:
	AMTAInteractDoor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Door")
	TObjectPtr<USceneComponent> Hinge;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Door")
	bool bLocked = false;

	/** Abgeschlossene Tuer kann per Halten aufgebrochen werden (Straftat, Laerm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Door")
	bool bCanBreakIn = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Door")
	float BreakInDuration = 3.f;

	/** Privatgrund: Betreten zaehlt als Hausfriedensbruch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Door")
	bool bPrivateProperty = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Door")
	float OpenAngle = 95.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Door")
	float OpenSpeed = 3.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Door")
	bool bOpen = false;

	virtual void Tick(float DeltaSeconds) override;
	virtual FMTAInteractionInfo GetInteractionInfo_Implementation(AActor* Interactor) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;

protected:
	virtual EMTAInteractionKind GetKind() const override { return EMTAInteractionKind::Door; }
	virtual void PerformInteraction(AActor* Interactor) override;

private:
	float OpenAlpha = 0.f;
};

/** Haendler: oeffnet ueber OnInteracted (InteractionId = Shop-ID) das Shop-Menue im HUD. */
UCLASS(Blueprintable)
class MTA6_API AMTAInteractVendor : public AMTAInteractableActor
{
	GENERATED_BODY()

public:
	AMTAInteractVendor();

	/** Unter diesem Bezirks-Ruf verweigert der Haendler das Geschaeft. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vendor")
	float MinDistrictReputation = -50.f;

	/** Bei Fahndung im Bezirk ab dieser Heat schliesst der Laden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vendor")
	float MaxHeat = 2.f;

	virtual FMTAInteractionInfo GetInteractionInfo_Implementation(AActor* Interactor) const override;

protected:
	virtual EMTAInteractionKind GetKind() const override { return EMTAInteractionKind::Vendor; }
};

/** Garage: InteractionId = Garagen-ID; Inhalt/Stellplaetze verwaltet die Wirtschaft/Speicherung. */
UCLASS(Blueprintable)
class MTA6_API AMTAInteractGarage : public AMTAInteractableActor
{
	GENERATED_BODY()

public:
	AMTAInteractGarage();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Garage")
	int32 Capacity = 2;

protected:
	virtual EMTAInteractionKind GetKind() const override { return EMTAInteractionKind::Garage; }
};

/** Terminal: Halten = Hack. Schaltet verknuepfte Aegis-Kameras fuer HackDuration ab. */
UCLASS(Blueprintable)
class MTA6_API AMTAInteractTerminal : public AMTAInteractableActor
{
	GENERATED_BODY()

public:
	AMTAInteractTerminal();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Terminal")
	TArray<TObjectPtr<AMTAAegisCameraActor>> LinkedCameras;

	/** Zusaetzlich alle Kameras in diesem Radius (cm, 0 = nur Liste). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Terminal")
	float CameraRadius = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Terminal")
	float HackDuration = 60.f;

	/** Abklingzeit (s) bis zum naechsten Hack. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Terminal")
	float Cooldown = 120.f;

	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual FMTAInteractionInfo GetInteractionInfo_Implementation(AActor* Interactor) const override;

protected:
	virtual EMTAInteractionKind GetKind() const override { return EMTAInteractionKind::Terminal; }
	virtual void PerformInteraction(AActor* Interactor) override;

private:
	float NextAvailableTime = 0.f;
};

/** Beweisstueck: aufheben entfernt es aus der Welt (InteractionId = Beweis-/Item-ID). */
UCLASS(Blueprintable)
class MTA6_API AMTAInteractEvidence : public AMTAInteractableActor
{
	GENERATED_BODY()

public:
	AMTAInteractEvidence();

	/** Polizeiliches Beweismittel: Aufheben ist Beweismanipulation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Evidence")
	bool bIsPoliceEvidence = false;

protected:
	virtual EMTAInteractionKind GetKind() const override { return EMTAInteractionKind::Evidence; }
	virtual void PerformInteraction(AActor* Interactor) override;
};
