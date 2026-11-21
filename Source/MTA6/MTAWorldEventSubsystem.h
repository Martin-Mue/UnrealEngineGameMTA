#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Dom/JsonValue.h"
#include "MTADiscoveryTypes.h"
#include "MTAWorldEventSubsystem.generated.h"

/** Wirkung eines Ereignisses auf einen Bezirk. Multiplikatoren: 1 = neutral. */
USTRUCT(BlueprintType)
struct MTA6_API FMTADistrictModifiers
{
	GENERATED_BODY()

	/** Verkehrsdichte (M5). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	float Traffic = 1.f;

	/** Fussgaengerdichte (M5). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	float Pedestrians = 1.f;

	/** Polizeipraesenz (M6). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	float Police = 1.f;

	/** Faktor auf Fahndungszuwachs bei Straftaten (UMTAHeatSubsystem::AddHeat-Aufrufer). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	float HeatGain = 1.f;

	/** Aegis-Kameraempfindlichkeit (0 = Kameras aus). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	float Cameras = 1.f;

	/** Strassen-/Fensterlicht (0 = Blackout). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	float Lights = 1.f;

	/** Sichtweite (Nebel, Rauch). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	float Visibility = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	bool bTrafficLightsOff = false;

	/** Preisfaktoren je Warengruppe (Wirtschaft, M8). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	TMap<FName, float> Prices;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	TArray<FString> RoadClosures;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	TArray<FName> WorldTags;
};

USTRUCT(BlueprintType)
struct MTA6_API FMTAWorldEventOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	FString Text;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	TArray<FMTADiscoveryReward> Rewards;
};

/** Definition aus Content/Data/world_events.json. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAWorldEventDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	FString Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	FName Kind;

	/** Bezirks-Ids; "any" = zufaelliger Bezirk. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	TArray<FName> Districts;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	TArray<int32> Weekdays;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	float HourFrom = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	float HourTo = 24.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	TArray<FName> Weather;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	float ChancePerHour = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	float CooldownHours = 24.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	float DurationMinHours = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	float DurationMaxHours = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	FMTADistrictModifiers Effects;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	TArray<FMTAWorldEventOption> Options;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	TArray<FMTADiscoveryHint> Hints;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Events")
	FString TriggerWhenJson;
};

/** Laufendes Ereignis. Zeiten in absoluten Spielstunden seit Spielbeginn. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAActiveWorldEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Events")
	FName EventId;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Events")
	FName DistrictId;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Events")
	FString Title;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Events")
	double StartHours = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Events")
	double EndHours = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Events")
	FName ChosenOption;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWorldEventStarted, const FMTAActiveWorldEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWorldEventEnded, const FMTAActiveWorldEvent&, Event);

/** Geparste Ausloesebedingungen (nicht reflektiert). */
struct FMTAWorldEventRuntime
{
	TArray<TSharedPtr<FJsonValue>> When;
	TArray<TSharedPtr<FJsonValue>> StoryEffects;
};

/**
 * Dynamische Weltereignisse (Paket K): Strassenfest, Razzia, Blackout, Rennen, Streik, Sturm ...
 *
 * VERHALTENSPROTOKOLL:
 *  - Jede Sekunde (Echtzeit) wird die Spielzeit von UMTADiscoverySubsystem gelesen und als absolute
 *    Spielstunden fortgeschrieben (Tageswechsel: negativer Sprung + 24).
 *  - Beendet werden Ereignisse mit EndHours <= jetzt (OnWorldEventEnded, Story-Effekte gepuffert).
 *  - Gestartet wird je Definition hoechstens eine Instanz, wenn: kein Cooldown, Wochentag/Stunde/Wetter
 *    passen, trigger.when erfuellt, Zufall < 1 - (1 - ChancePerHour)^(vergangene Spielstunden).
 *    Hoechstens MaxConcurrentPerDistrict Ereignisse je Bezirk. Zufall: FRandomStream(Seed), deterministisch.
 *  - Wirkung: GetDistrictModifiers multipliziert die Faktoren aller aktiven Ereignisse im Bezirk.
 *    KI, Polizei, Wirtschaft und Licht fragen das ab (Pull), das Subsystem schreibt nichts in fremde Systeme.
 */
UCLASS()
class MTA6_API UMTAWorldEventSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Events")
	bool LoadEvents(const FString& Path = TEXT(""));

	/** Planungsschritt mit expliziter Stunde (0..24); wird automatisch vom Ticker aufgerufen. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Events")
	void UpdateSchedule(float CurrentHour);

	UFUNCTION(BlueprintPure, Category = "MTA6|Events")
	bool IsEventActive(FName EventId) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Events")
	TArray<FMTAActiveWorldEvent> GetActiveEvents() const { return Active; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Events")
	TArray<FMTAActiveWorldEvent> GetActiveEventsInDistrict(FName DistrictId) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Events")
	FMTADistrictModifiers GetDistrictModifiers(FName DistrictId) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Events")
	float GetPriceMultiplier(FName DistrictId, FName Category) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Events")
	bool GetEventDef(FName EventId, FMTAWorldEventDef& OutDef) const;

	/** Debug/Story: Ereignis sofort starten. District None = erster Bezirk der Definition. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Events")
	bool StartEvent(FName EventId, FName DistrictId = NAME_None, float DurationHours = -1.f);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Events")
	bool EndEvent(FName EventId);

	/** Spieleroption waehlen (einmal je Ereignis); Belohnungen laufen ueber UMTADiscoverySubsystem::ApplyReward. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Events")
	bool ChooseOption(FName EventId, FName OptionId);

	/** Story-Effekte beendeter Ereignisse als JSON-Array fuer UMTAStorySubsystem. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Events")
	FString ConsumeStoryEffectsJson();

	UFUNCTION(BlueprintCallable, Category = "MTA6|Events")
	FString ToJson() const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Events")
	bool FromJson(const FString& Json);

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Events")
	FOnWorldEventStarted OnWorldEventStarted;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Events")
	FOnWorldEventEnded OnWorldEventEnded;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Events")
	bool bAutoSchedule = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Events")
	int32 MaxConcurrentPerDistrict = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Events")
	int32 Seed = 1337;

private:
	bool HandleTick(float DeltaSeconds);
	void StartEventInternal(int32 DefIndex, FName DistrictId, float DurationHours);
	void EndEventAt(int32 ActiveIndex);
	bool CanTrigger(int32 DefIndex, float Hour) const;
	int32 CountInDistrict(FName DistrictId) const;
	int32 FindDef(FName Id) const;

	UPROPERTY()
	TArray<FMTAWorldEventDef> Defs;

	TArray<FMTAWorldEventRuntime> Runtime;

	UPROPERTY()
	TArray<FMTAActiveWorldEvent> Active;

	/** Absolute Spielstunde, ab der eine Definition wieder starten darf. */
	TMap<FName, double> CooldownUntil;
	TArray<TSharedPtr<FJsonValue>> PendingStoryEffects;

	double AbsHours = 0.0;
	float LastHour = -1.f;
	FRandomStream Rng;
	FTSTicker::FDelegateHandle TickHandle;
};
