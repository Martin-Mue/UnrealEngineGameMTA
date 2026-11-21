#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MTAInteractionInterface.h"
#include "MTAStealthHideout.generated.h"

class UBoxComponent;
class UMTAStealthComponent;

UENUM(BlueprintType)
enum class EMTAHideoutType : uint8
{
	Garage    UMETA(DisplayName = "Garage"),
	Tunnel    UMETA(DisplayName = "Tunnel/Unterfuehrung"),
	Safehouse UMETA(DisplayName = "Unterschlupf"),
	Alley     UMETA(DisplayName = "Hinterhof"),
	Water     UMETA(DisplayName = "Wasser/Steg")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMTAHideoutServiceSignature, AActor*, Interactor, FName, ServiceId, FName, NewValue);

/**
 * Versteck-Volumen (Konzept M6: "Unterschlupf (Garage, Tunnel, Wasser) zum Abschuetteln").
 * Wer ohne Sichtung darin wartet, taucht unter: Aegis-Profil und Bezirks-Heat sinken.
 * Optional Dienste per Interaktion: Kleidung wechseln, Kennzeichen tauschen (Datenhooks).
 */
UCLASS(Blueprintable)
class MTA6_API AMTAStealthHideout : public AActor, public IMTAInteractable
{
	GENERATED_BODY()

public:
	AMTAStealthHideout();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Hideout")
	TObjectPtr<UBoxComponent> Volume;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Hideout")
	EMTAHideoutType HideoutType = EMTAHideoutType::Garage;

	/** Aegis-Profil-Abbau pro Sekunde beim Untertauchen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Hideout")
	float AegisDecayPerSecond = 0.4f;

	/** Heat-Abbau pro Sekunde (0..5-Skala) beim Untertauchen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Hideout")
	float HeatDecayPerSecond = 0.08f;

	/** Verdeckt die Sicht vollstaendig (Sichtbarkeit 0). Sonst nur halbiert (Hinterhof). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Hideout")
	bool bBlocksSight = true;

	/** Fahrzeuge koennen mit hinein (Garage, Tunnel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Hideout")
	bool bAllowsVehicles = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Hideout|Services")
	bool bOffersOutfitChange = false;

	/** Wechsel-Outfits (zyklisch); IDs kommen aus dem Inventar/Schrank-Datenblatt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Hideout|Services")
	TArray<FName> OutfitOptions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Hideout|Services")
	bool bOffersPlateSwap = false;

	/** Preis-Datenhook (Wirtschaft bucht ueber OnServiceUsed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Hideout|Services")
	int32 PlateSwapCost = 250;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Hideout|Services")
	float ServiceHoldDuration = 2.f;

	/** ServiceId: "outfit" oder "plate"; NewValue: neue Outfit-ID bzw. neues Kennzeichen. */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Hideout")
	FMTAHideoutServiceSignature OnServiceUsed;

	// IMTAInteractable
	virtual FMTAInteractionInfo GetInteractionInfo_Implementation(AActor* Interactor) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;

	/** Erstes Fahrzeug im Volumen (fuer den Kennzeichentausch). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Hideout")
	AActor* FindVehicleInside() const;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
};
