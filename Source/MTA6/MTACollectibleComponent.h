#pragma once

#include "CoreMinimal.h"
#include "Components/SphereComponent.h"
#include "MTACollectibleComponent.generated.h"

class UMTADiscoverySubsystem;
class UMTACollectibleComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnMTACollected, UMTACollectibleComponent*, Component, int32, Collected, int32, Total);

/**
 * Sammelobjekt im Level (Kameraaufkleber, Schichtmarken, Kreideglocken, Gartenzwerge, Kraehenschaetze,
 * Flaschenpost, Forschungsmarken). Ueberlappt der Spieler-Pawn die Kugel, wird das Objekt im
 * UMTADiscoverySubsystem eingesammelt (CollectionId + ItemIndex, Sammlungen in secrets.json/wilderness.json).
 * Optional loest es zusaetzlich eine Entdeckung aus (DiscoveryId, z. B. Modus "script").
 * Bereits gesammelte Objekte verstecken sich beim Laden selbst.
 */
UCLASS(ClassGroup = (MTA6), meta = (BlueprintSpawnableComponent))
class MTA6_API UMTACollectibleComponent : public USphereComponent
{
	GENERATED_BODY()

public:
	UMTACollectibleComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Sammlung, z. B. col_core_blind_spots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Collectible")
	FName CollectionId;

	/** Index in der Sammlung (0..Count-1), je Objekt im Level eindeutig. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Collectible", meta = (ClampMin = "0"))
	int32 ItemIndex = 0;

	/** Optional: zusaetzlich diese Entdeckung ausloesen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Collectible")
	FName DiscoveryId;

	/** Besitzer nach dem Einsammeln verstecken und Kollision abschalten. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Collectible")
	bool bHideOwnerWhenCollected = true;

	/** Nur der lokale Spieler-Pawn (Index 0) sammelt ein, nicht KI-Fahrzeuge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Collectible")
	bool bOnlyPlayerPawn = true;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Collectible")
	FOnMTACollected OnCollected;

	/** Manuell einsammeln (z. B. per Interaktion statt Ueberlappung). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Collectible")
	bool Collect();

	UFUNCTION(BlueprintPure, Category = "MTA6|Collectible")
	bool IsCollected() const;

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void ApplyCollectedLook();
	UMTADiscoverySubsystem* GetDiscovery() const;
};
