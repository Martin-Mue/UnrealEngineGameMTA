#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "MTAInteractionInterface.h"
#include "MTAInteractionComponent.generated.h"

class UMTAVehicleEntryComponent;
class AWheeledVehiclePawn;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAInteractionFocusSignature, AActor*, FocusedActor, const FMTAInteractionInfo&, Info);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAInteractionHoldSignature, AActor*, FocusedActor, float, Alpha);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAInteractionDoneSignature, AActor*, InteractedActor);

/**
 * Sucht interaktive Objekte vor der Figur (Kamera-Trace + Naehe-Overlap), haelt das Fokusobjekt,
 * liefert den Prompt-Text fuers HUD und fuehrt Druecken- oder Halten-Interaktionen aus.
 * Fahrzeuge (AWheeledVehiclePawn) werden an UMTAVehicleEntryComponent weitergereicht.
 */
UCLASS(ClassGroup = (MTA6), meta = (BlueprintSpawnableComponent))
class MTA6_API UMTAInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMTAInteractionComponent();

	/** Reichweite des Blick-Traces ab der Figur (cm, die Kamera-Distanz wird addiert). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	float TraceDistance = 300.f;

	/** Radius der Naehe-Suche um die Figur (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	float ProximityRadius = 220.f;

	/** Objekte ausserhalb dieses Abstands (cm, zur Figur) werden nie fokussiert. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	float MaxInteractDistance = 350.f;

	/** Mindest-Ausrichtung (Kosinus) der Blickrichtung zum Objekt bei der Naehe-Suche. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	float MinFacingDot = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	float ScanInterval = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	/** Tastenname fuer den Prompt (HUD ersetzt ihn bei Gamepad). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Interaction")
	FText KeyLabel = NSLOCTEXT("MTAInteraction", "Key", "E");

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Interaction")
	FMTAInteractionFocusSignature OnFocusChanged;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Interaction")
	FMTAInteractionHoldSignature OnHoldProgress;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Interaction")
	FMTAInteractionDoneSignature OnInteracted;

	/** Taste gedrueckt. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interaction")
	void BeginInteract();

	/** Taste losgelassen (bricht Halten ab). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interaction")
	void EndInteract();

	UFUNCTION(BlueprintPure, Category = "MTA6|Interaction")
	AActor* GetFocusedActor() const { return FocusedActor.Get(); }

	UFUNCTION(BlueprintPure, Category = "MTA6|Interaction")
	FMTAInteractionInfo GetFocusInfo() const { return FocusInfo; }

	/** Fertiger HUD-Text, z. B. "[E] Tuer oeffnen" oder "[E halten] Aufbrechen". Leer ohne Fokus. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Interaction")
	FText GetPromptText() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Interaction")
	float GetHoldAlpha() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Interaction")
	bool IsHolding() const { return bHolding; }

	/** Suche an/aus (z. B. im Fahrzeug oder in Zwischensequenzen). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interaction")
	void SetInteractionEnabled(bool bEnabled);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void Scan();
	void SetFocus(AActor* NewFocus, const FMTAInteractionInfo& NewInfo);
	bool IsCandidate(AActor* Actor) const;
	bool QueryInfo(AActor* Actor, FMTAInteractionInfo& OutInfo) const;
	void Execute(AActor* Target);
	void GetView(FVector& OutLocation, FVector& OutDirection) const;
	UMTAVehicleEntryComponent* GetEntry() const;

	TWeakObjectPtr<AActor> FocusedActor;
	FMTAInteractionInfo FocusInfo;
	float ScanTimer = 0.f;
	float HoldElapsed = 0.f;
	bool bHolding = false;
	bool bEnabledScan = true;
};
