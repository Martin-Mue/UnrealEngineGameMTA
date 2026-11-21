#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MTAInteractionInterface.generated.h"

/** Art eines interaktiven Objekts (HUD-Symbol, Eingabehinweis, Telemetrie). */
UENUM(BlueprintType)
enum class EMTAInteractionKind : uint8
{
	Generic  UMETA(DisplayName = "Allgemein"),
	Door     UMETA(DisplayName = "Tuer"),
	Vendor   UMETA(DisplayName = "Haendler"),
	Garage   UMETA(DisplayName = "Garage"),
	Terminal UMETA(DisplayName = "Terminal"),
	Evidence UMETA(DisplayName = "Beweisstueck"),
	Vehicle  UMETA(DisplayName = "Fahrzeug"),
	Hideout  UMETA(DisplayName = "Versteck")
};

/** Was das HUD zum fokussierten Objekt anzeigt und wie interagiert wird. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAInteractionInfo
{
	GENERATED_BODY()

	/** Kurzer Handlungstext, z. B. "Tuer oeffnen", "Fahrzeug stehlen". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	FText Prompt;

	/** Zusatzzeile (Preis, Risiko-Hinweis "Zeugen in der Naehe"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	FText SubText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	EMTAInteractionKind Kind = EMTAInteractionKind::Generic;

	/** 0 = sofort beim Druecken; > 0 = Taste so lange (s) halten. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction", meta = (ClampMin = "0"))
	float HoldDuration = 0.f;

	/** false = Prompt wird ausgegraut mit DisabledReason angezeigt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	FText DisabledReason;

	/** Hoeher gewinnt bei gleich guter Ausrichtung (Beweis vor Tuer). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	int32 Priority = 0;

	/** true = Handlung ist eine Straftat (HUD faerbt den Prompt rot). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	bool bIsIllegal = false;
};

UINTERFACE(MinimalAPI, Blueprintable)
class UMTAInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interaktives Objekt (Tueren, Haendler, Garagen, Terminals, Beweise ...).
 * Aufruf immer ueber IMTAInteractable::Execute_*(Object, ...), damit auch Blueprint-
 * Implementierungen greifen. Fahrzeuge (AWheeledVehiclePawn) werden ohne dieses Interface
 * ueber UMTAVehicleEntryComponent behandelt.
 */
class MTA6_API IMTAInteractable
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MTA6|Interaction")
	FMTAInteractionInfo GetInteractionInfo(AActor* Interactor) const;
	virtual FMTAInteractionInfo GetInteractionInfo_Implementation(AActor* Interactor) const { return FMTAInteractionInfo(); }

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MTA6|Interaction")
	bool CanInteract(AActor* Interactor) const;
	virtual bool CanInteract_Implementation(AActor* Interactor) const { return true; }

	/** Wird nach Druecken (bzw. vollstaendigem Halten) ausgefuehrt. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MTA6|Interaction")
	void Interact(AActor* Interactor);
	virtual void Interact_Implementation(AActor* Interactor) {}

	/** Fokus erhalten/verloren (Umriss-Highlight, Tuerschild beleuchten). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MTA6|Interaction")
	void OnInteractionFocus(AActor* Interactor, bool bFocused);
	virtual void OnInteractionFocus_Implementation(AActor* Interactor, bool bFocused) {}

	/** Halte-Fortschritt 0..1 (Aufbrechen-Geraeusch, Hack-Animation). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MTA6|Interaction")
	void OnInteractionHoldProgress(AActor* Interactor, float Alpha);
	virtual void OnInteractionHoldProgress_Implementation(AActor* Interactor, float Alpha) {}
};
