#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MTAInteriorTypes.h"
#include "MTAInteriorBuilder.h"
#include "MTAInteriorInteractive.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class AMTAInteriorInstance;

/**
 * Interaktives Objekt im Innenraum: Tuer, Schalter, Terminal (Hack-Hook), Versteck,
 * Geheimnis/Sammelobjekt, Ausgang. Der Zu-Fuss-Agent findet es ueber UseVolume (Overlap mit
 * Pawn) und ruft IMTAInteriorUsable::Use. Fahrzeuge loesen Garagen-Ausgaenge per Overlap aus.
 */
UCLASS(Blueprintable)
class MTA6_API AMTAInteriorInteractive : public AActor, public IMTAInteriorUsable, public IMTAHackable
{
	GENERATED_BODY()

public:
	AMTAInteriorInteractive();

	virtual void Tick(float DeltaSeconds) override;
	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;

	// IMTAInteriorUsable
	virtual FText GetUsePrompt(APawn* Pawn) const override;
	virtual bool CanUse(APawn* Pawn) const override;
	virtual bool Use(APawn* Pawn) override;

	// IMTAHackable
	virtual void OnHackResolved(bool bSuccess, FName ContextId) override;

	/** Wird vom Innenraum direkt nach dem Spawn aufgerufen. */
	void Setup(const FMTAInteractiveSpec& Spec, AMTAInteriorInstance* InInterior);
	void SetDormant(bool bDormant);
	/** Ausloesung durch ein verknuepftes Terminal: Tuer entriegeln bzw. Geheimnis aufdecken. */
	void ActivateFromLink();

	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	void SetLocked(bool bInLocked) { bLocked = bInLocked; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interior") TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interior") TObjectPtr<USceneComponent> Hinge;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interior") TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Interior") TObjectPtr<UBoxComponent> UseVolume;

	UPROPERTY(BlueprintReadOnly, Category = "Interior") EMTAInteractiveKind Kind = EMTAInteractiveKind::Terminal;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") FName InteractiveId;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") FName Links;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") FName KeyId;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") int32 HackDifficulty = 0;
	/** ID aus der Vorlage und globale ID (Vorlage@Gebaeude) fuer das Entdeckungssystem. */
	UPROPERTY(BlueprintReadOnly, Category = "Interior") FName LocalSecretId;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") FName GlobalSecretId;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") FName SecretKind;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") EMTAEntranceKind EntranceKind = EMTAEntranceKind::Front;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") int32 RoomIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") bool bLocked = false;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") bool bOpen = false;
	UPROPERTY(BlueprintReadOnly, Category = "Interior") bool bHacked = false;
	/** Verdecktes Geheimnis (erst nach Terminal-Hack sichtbar). */
	UPROPERTY(BlueprintReadOnly, Category = "Interior") bool bConcealed = false;

private:
	TWeakObjectPtr<AMTAInteriorInstance> Interior;
	TWeakObjectPtr<APawn> HiddenPawn;
	TWeakObjectPtr<APawn> PendingHackPawn;
	float DoorAngle = 0.f;
	float DoorTarget = 0.f;
	bool bDormantState = false;

	bool PawnHasKey(const APawn* Pawn) const;
	/** Schluessel fuer den gespeicherten Entriegelt-Zustand (Gebaeude#Tuer). */
	FName GlobalUnlockKey() const;
	void ApplyVisual(const FVector& Size, const FVector& Offset, const FLinearColor& Color, float Emissive, int32 ShapeIndex);
	void RefreshVisibility();
};
