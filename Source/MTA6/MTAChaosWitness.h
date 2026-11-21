#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "MTAChaosTypes.h"
#include "MTAChaosWitness.generated.h"

class UMTAChaosSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAWitnessSawCrimeSignature, const FMTACrimeEvent&, Crime, float, Confidence);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAWitnessRecognizedSignature, AActor*, Subject, int32, CrimeId);

/**
 * Zeuge (fuer NPCs, Passanten, Fahrer, Polizisten).
 * Sieht Verbrechen mit Sichtlinie (Sichtkegel + Trace), hoert laute Taten, merkt sich Taeter,
 * Kleidung und Kennzeichen und meldet nach einer Verzoegerung an UMTAChaosSubsystem.
 * Wer vor der Meldung eingeschuechtert wird oder verschwindet, meldet nicht.
 * Erkennt den Taeter spaeter wieder, solange Erinnerung und Outfit passen.
 */
UCLASS(ClassGroup = (MTA6), meta = (BlueprintSpawnableComponent))
class MTA6_API UMTAWitnessComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMTAWitnessComponent();

	// --- Wahrnehmung -----------------------------------------------------------------

	/** Maximale Sichtweite (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Witness")
	float SightRange = 4000.f;

	/** Halber Sichtwinkel (Grad). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Witness", meta = (ClampMin = "5", ClampMax = "180"))
	float ViewHalfAngle = 75.f;

	/** Mindest-Sicherheit, damit eine Sichtung zaehlt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Witness", meta = (ClampMin = "0", ClampMax = "1"))
	float MinConfidence = 0.15f;

	/** Bis zu dieser Entfernung (cm) kann ein Kennzeichen gelesen werden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Witness")
	float PlateReadRange = 1500.f;

	// --- Meldung -----------------------------------------------------------------------

	/** Darf ueberhaupt melden (Gangmitglieder, Komplizen: false). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Witness")
	bool bCanReport = true;

	/** Wahrscheinlichkeit 0..1, dass eine gesehene Tat gemeldet wird. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Witness", meta = (ClampMin = "0", ClampMax = "1"))
	float Courage = 0.7f;

	/** Multiplikator auf die Meldeverzoegerung der Regel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Witness", meta = (ClampMin = "0"))
	float ReportDelayScale = 1.f;

	/** Polizisten melden sofort (Funk). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Witness")
	bool bIsPolice = false;

	/** Eingeschuechtert: meldet nichts mehr (Datenhook fuer Bedrohen/Bestechen). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Witness")
	bool bIntimidated = false;

	// --- Gedaechtnis -------------------------------------------------------------------

	/** Sekunden, bis eine sichere Erinnerung (Confidence 1) verblasst ist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Witness")
	float MemoryDuration = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Witness")
	int32 MaxMemories = 12;

	/** Intervall (s), in dem der Zeuge nach bekannten Taetern Ausschau haelt (0 = aus). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Witness")
	float RecognitionInterval = 1.f;

	/** Faktor auf die Wiedererkennung, wenn der Taeter die Kleidung gewechselt hat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Witness", meta = (ClampMin = "0", ClampMax = "1"))
	float DisguisedRecognitionFactor = 0.25f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Witness")
	TArray<FMTAWitnessMemory> Memories;

	// --- Ereignisse fuer NPC-KI ----------------------------------------------------------

	/** Hat ein Verbrechen gesehen/gehoert -> NPC-KI: Panik, Flucht, Handy zuecken. */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Witness")
	FMTAWitnessSawCrimeSignature OnSawCrime;

	/** Erkennt einen Taeter wieder -> NPC-KI: zeigen, rufen, Polizei holen. */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Witness")
	FMTAWitnessRecognizedSignature OnRecognizedPerpetrator;

	// --- API -------------------------------------------------------------------------

	/**
	 * Vom Subsystem aufgerufen. true = Tat wahrgenommen. OutReportDelay < 0 = meldet nicht.
	 */
	bool ConsiderCrime(const FMTACrimeEvent& Crime, const FMTACrimeRule& Rule, bool bIsVictim, float& OutReportDelay);

	bool CanReportNow() const;
	void MarkReported(int32 CrimeId);
	bool RemembersPlate(int32 CrimeId) const;
	bool RemembersOutfit(int32 CrimeId) const;

	/** Einschuechtern/Bestechen: offene Meldungen verfallen. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Witness")
	void Intimidate();

	/** Vergisst alles (z. B. Gedaechtnis-Loeschung durch Story, Tod). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Witness")
	void ForgetAll() { Memories.Reset(); }

	/** Sichtlinie von den Augen des Zeugen zu einem Punkt (ignoriert IgnoreActor). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Witness")
	bool HasLineOfSightTo(const FVector& Target, const AActor* IgnoreActor) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void RecognitionTick();
	FMTAWitnessMemory* FindMemory(int32 CrimeId);
	const FMTAWitnessMemory* FindMemory(int32 CrimeId) const;
	void GetEyes(FVector& OutLocation, FRotator& OutRotation) const;
	UMTAChaosSubsystem* GetChaos() const;

	FTimerHandle RecognitionTimer;
	float LastRecognitionTime = 0.f;
};
