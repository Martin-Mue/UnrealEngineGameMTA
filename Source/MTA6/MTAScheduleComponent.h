#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MTANPCTypes.h"
#include "MTAScheduleComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAScheduleChanged, const FMTAScheduleState&, NewState);

/**
 * Tagesablauf am sichtbaren NPC-Actor. Fragt den aufgeloesten Zeitplan beim UMTANPCSubsystem ab
 * (Aktivitaet, Orts-Tag, Verfuegbarkeit fuer Dialog/Handel, Wetter-/Ereignis-Abweichung) und meldet
 * Wechsel an Animation/Bewegung (Delegate + IMTANPCActor::OnActivityChanged).
 * Registriert den Besitzer beim BeginPlay als Near-Actor (Sim-LOD) und meldet ihn beim EndPlay ab.
 * Der Orts-Tag wird von Welt-/Gebaeude-Agenten auf eine Position aufgeloest (z. B. ueber eine Tabelle
 * Tag -> Transform); dieses System kennt keine Koordinaten.
 */
UCLASS(ClassGroup = (MTA6), meta = (BlueprintSpawnableComponent))
class MTA6_API UMTAScheduleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMTAScheduleComponent();

	/** Schluesselfigur, deren Tagesablauf dieser Actor darstellt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Schedule")
	FName NPCId;

	/** Abfrageintervall in Echtsekunden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Schedule", meta = (ClampMin = "0.1"))
	float PollInterval = 1.0f;

	UFUNCTION(BlueprintPure, Category = "MTA6|Schedule")
	FMTAScheduleState GetCurrentState() const { return Current; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Schedule")
	bool IsAvailableForDialogue() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Schedule")
	bool IsTrading() const;

	/** Zeitplan fuer einen ganzen Tag (z. B. Vertrags-Zielprofil, Debug-Overlay): Zustand je volle Stunde. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Schedule")
	TArray<FMTAScheduleState> PreviewDay(int32 Day) const;

	/** Sofort neu abfragen. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Schedule")
	void Refresh();

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Schedule")
	FMTAScheduleChanged OnScheduleChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	class UMTANPCSubsystem* GetNPCSubsystem() const;

	FMTAScheduleState Current;
	bool bHasState = false;
	float Accumulator = 0.f;
};
