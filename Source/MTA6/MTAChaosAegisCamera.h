#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/TimerHandle.h"
#include "UObject/ObjectKey.h"
#include "MTAChaosTypes.h"
#include "MTAChaosAegisCamera.generated.h"

class UMTAChaosSubsystem;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAAegisCaptureSignature, AActor*, Subject, bool, bFace);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAAegisStateSignature, bool, bOnline);

/**
 * Aegis-Kamera (Konzept 8.2a): Sichtkegel in Blickrichtung (+X) der Komponente, scannt die
 * Spieler-Subjekte des UMTAChaosSubsystem, wuerfelt die Erfassung abhaengig vom Aegis-Profil
 * und meldet Treffer an das Subsystem (Kennzeichen, Gesicht, Tempo). Hackbar und abschaltbar.
 */
UCLASS(ClassGroup = (MTA6), meta = (BlueprintSpawnableComponent))
class MTA6_API UMTAAegisCameraComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMTAAegisCameraComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Aegis")
	EMTAAegisTier Tier = EMTAAegisTier::Street;

	/** true = CaptureProfile beim Spielstart aus den Tier-Standardwerten fuellen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Aegis")
	bool bUseTierDefaults = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Aegis")
	FMTAAegisCaptureProfile CaptureProfile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Aegis")
	float Range = 3500.f;

	/** Halber Oeffnungswinkel des Sichtkegels (Grad). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Aegis", meta = (ClampMin = "1", ClampMax = "89"))
	float ConeHalfAngle = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Aegis", meta = (ClampMin = "0.05"))
	float ScanInterval = 0.5f;

	/** Schwenk der Kamera (Grad links/rechts, 0 = starr) und Dauer eines Schwenks (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Aegis")
	float SweepAmplitude = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Aegis")
	float SweepPeriod = 8.f;

	/** Nach einer Erfassung wird dasselbe Subjekt so lange ignoriert (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Aegis")
	float SubjectCooldown = 3.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Aegis")
	bool bOnline = true;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Aegis")
	FMTAAegisCaptureSignature OnCaptured;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Aegis")
	FMTAAegisStateSignature OnOnlineChanged;

	/** Standardprofil eines Tiers (Konzept 8.2: Core dicht, Rustline fast blind, ...). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Aegis")
	static FMTAAegisCaptureProfile GetTierDefaults(EMTAAegisTier InTier);

	/** Hack: Kamera fuer Duration Sekunden offline. Instigator != null meldet Aegis-Sabotage. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Aegis")
	void Hack(float Duration, AActor* InstigatorActor);

	/** Dauerhaft zerstoert/ueberklebt (Aktivitaet "Kamera ueberkleben"). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Aegis")
	void Disable(AActor* InstigatorActor);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Aegis")
	void SetOnline(bool bNewOnline);

	/** Liegt Point im Sichtkegel (ohne Sichtlinien-Test)? */
	UFUNCTION(BlueprintPure, Category = "MTA6|Aegis")
	bool IsInCone(const FVector& Point) const;

	/** Erfassungschance (0..1) fuer ein Subjekt bei aktuellem Aegis-Profil - fuer HUD/Debug. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Aegis")
	float ComputeCaptureChance(AActor* Subject, bool bAsVehicle) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void Scan();
	void RestoreFromHack();
	UMTAChaosSubsystem* GetChaos() const;

	FTimerHandle ScanTimer;
	FTimerHandle HackTimer;
	TMap<FObjectKey, float> Cooldowns;
	FRotator BaseRelativeRotation = FRotator::ZeroRotator;
	float SweepTime = 0.f;
	bool bPermanentlyDisabled = false;
};

/** Platzierbarer Kamera-Aktor (Mast + Kamera-Kopf als Platzhalter-Mesh, im Blueprint setzen). */
UCLASS(Blueprintable)
class MTA6_API AMTAAegisCameraActor : public AActor
{
	GENERATED_BODY()

public:
	AMTAAegisCameraActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Aegis")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Aegis")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Aegis")
	TObjectPtr<UMTAAegisCameraComponent> Camera;
};
