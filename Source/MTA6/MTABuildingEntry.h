#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MTAInteriorTypes.h"
#include "MTAInteriorSubsystem.h"
#include "MTABuildingEntry.generated.h"

class USphereComponent;

/** Ein Eingang am Aussengebaeude (Vordertuer, Hinterhof, Dach, Notausgang, Garage). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAEntrance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entrance")
	EMTAEntranceKind Kind = EMTAEntranceKind::Front;

	/** Stelle vor der Tuer (relativ zum Actor); X-Achse zeigt vom Gebaeude weg (Blickrichtung beim Herauskommen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entrance", meta = (MakeEditWidget = true))
	FTransform Exterior;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entrance", meta = (ClampMin = "50"))
	float UseRadius = 160.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entrance")
	bool bAllowsVehicles = false;

	/** Eigene Regel statt der aus der Vorlage abgeleiteten. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entrance")
	bool bOverrideAccess = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entrance", meta = (EditCondition = "bOverrideAccess"))
	FMTAAccessRule Access;

	/** Laufzeit: per Schluessel/Hack dauerhaft entriegelt. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Entrance")
	bool bUnlocked = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAOnEntranceUsed, APawn*, Pawn, int32, EntranceIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMTAOnEntranceDenied, APawn*, Pawn, int32, EntranceIndex, EMTALockState, Reason);

/**
 * Betretbares Gebaeude. Wird vom Welt-Agenten je Parzelle gesetzt (SpawnForLot) oder im
 * Editor platziert. Der Innenraum entsteht erst bei Bedarf (UMTAInteriorSubsystem).
 */
UCLASS(Blueprintable)
class MTA6_API AMTABuildingEntry : public AActor, public IMTAEnterable, public IMTAHackable
{
	GENERATED_BODY()

public:
	AMTABuildingEntry();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// IMTAEnterable
	virtual bool CanEnter(APawn* Pawn, FText& OutReason) const override;
	virtual FText GetEnterPrompt(APawn* Pawn) const override;
	virtual bool Enter(APawn* Pawn) override;
	virtual int32 FindEntranceFor(const APawn* Pawn) const override;

	// IMTAHackable
	virtual void OnHackResolved(bool bSuccess, FName ContextId) override;

	/** Eindeutiger Schluessel; leer = aus der Position erzeugt ("bld_X_Y"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building") FName BuildingKey;
	/** Feste Vorlage; leer = automatisch nach Bezirk/Raumtyp/Seed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building") FName TemplateId;
	/** Raumtyp fuer die automatische Wahl (z. B. bar_club); leer = beliebig. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building") FName RoomType;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building") EMTADistrict District = EMTADistrict::Core;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building") int32 Seed = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building") FName StyleOverride;
	/** Grundflaeche der Parzelle (cm) fuer Vorlagenwahl und automatische Eingaenge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building") FVector2D LotSize = FVector2D(2600.f, 2600.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building") float BuildingHeight = 1200.f;
	/** Innenraum im Voraus bauen, wenn der Spieler naeher kommt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building") bool bPrewarm = true;
	/** Leer = automatisch aus der Vorlage (Vorder-/Hintertuer, Dach, Notausgang, Garage). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building") TArray<FMTAEntrance> Entrances;

	UPROPERTY(BlueprintAssignable, Category = "Building") FMTAOnEntranceUsed OnEntered;
	UPROPERTY(BlueprintAssignable, Category = "Building") FMTAOnEntranceDenied OnDenied;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Building")
	EMTALockState EvaluateAccess(int32 EntranceIndex, APawn* Pawn, FText& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Building")
	bool EnterVia(APawn* Pawn, int32 EntranceIndex);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Building")
	void UnlockEntrance(int32 EntranceIndex);

	UFUNCTION(BlueprintPure, Category = "MTA6|Building")
	int32 FindEntranceIndexByKind(EMTAEntranceKind Kind) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Building")
	FTransform GetEntranceExteriorTransform(int32 EntranceIndex) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Building")
	FMTAInteriorRequest MakeInteriorRequest() const;

	/** Anzeigename aus der Vorlage (z. B. "Kneipe Zum Ankerkettenriss"). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Building")
	FText GetDisplayName() const;

	/** Legt Eingaenge passend zur Vorlage an; StreetDir = Richtung zur Strasse (Weltraum). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Building")
	void AutoLayoutEntrances(FVector StreetDir);

	/**
	 * Fuer den Welt-Agenten: betretbares Gebaeude auf einer Parzelle erzeugen.
	 * LotCenter = Bodenmitte, LotSizeCm = Parzellengroesse, StreetDir = Richtung zur naechsten Strasse.
	 */
	static AMTABuildingEntry* SpawnForLot(UWorld* World, const FVector& LotCenter, const FVector2D& LotSizeCm, float HeightCm,
		const FVector& StreetDir, EMTADistrict InDistrict, int32 InSeed, FName InRoomType = NAME_None);

private:
	UPROPERTY(VisibleAnywhere, Category = "Building")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USphereComponent>> Triggers;

	TWeakObjectPtr<APawn> PendingHackPawn;
	int32 PendingHackEntrance = INDEX_NONE;
	FVector StreetDirection = FVector::ForwardVector;

	void ResolveTemplate();
	void CreateTriggers();
	FMTAAccessRule GetEffectiveAccess(int32 EntranceIndex) const;
	const FMTAInteriorTemplate* GetTemplate() const;
	FName UnlockKey(int32 EntranceIndex) const;

	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
