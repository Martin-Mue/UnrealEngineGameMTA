#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "MTADiscoveryTypes.h"
#include "MTADiscoverySubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDiscoverySecretFound, const FMTADiscoveryDef&, Discovery, const FMTADistrictProgress&, DistrictProgress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSecretHinted, FName, DiscoveryId, const FMTADiscoveryHint&, Hint);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChainProgress, const FMTAChainProgress&, Progress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCollectionProgress, const FMTACollectionProgress&, Progress);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDiscoveryReward, FName, SourceId, const FMTADiscoveryReward&, Reward);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRegionStateChanged, FName, Key, int32, Value);

/** Laufzeitzustand je Entdeckung (nicht reflektiert). */
struct FMTADiscoveryRuntime
{
	TArray<TSharedPtr<FJsonValue>> VisibleWhen;
	TArray<TSharedPtr<FJsonValue>> UnlockWhen;
	EMTADiscoveryState State = EMTADiscoveryState::Hidden;
	int32 HintsRevealed = 0;
	float DwellTime = 0.f;
	bool bAnchorResolved = false;
	bool bHasAnchor = false;
	FVector AnchorLocation = FVector::ZeroVector;
};

/**
 * Entdeckungssystem (Paket K): handgeschriebene Geheimnisse je Bezirk (Content/Data/secrets.json)
 * und in der Wildnis (Content/Data/wilderness.json), Raetselketten, Sammelobjekte, Landmarken.
 *
 * VERHALTENSPROTOKOLL:
 *  - Alle TickInterval s: Spielerposition (Pawn 0) holen, Story-Zustand als Schnappschuss lesen
 *    (UMTAStorySubsystem::StateToJson), jede nicht gefundene Entdeckung pruefen:
 *    sichtbar = visibleWhen erfuellt UND Vorgaenger der Kette gefunden;
 *    freigeschaltet = sichtbar UND unlockWhen erfuellt.
 *    Sound-Hinweise im Hoerradius -> Zustand Hinted (OnSecretHinted).
 *    Proximity: im Radius + freigeschaltet -> Fund. Dwell: DwellSeconds im Radius -> Fund.
 *    Interact/Photo: nur ueber TryInteract/ReportPhoto.
 *  - Fund: Belohnungen anwenden (Geld/Skillpunkte/Story-Effekte werden gepuffert und vom Spiel per
 *    Consume* abgeholt), Kette fortschreiben, naechsten Kettenschritt auf Hinted setzen,
 *    bei Kettenende Abschlussbelohnung. Delegates: OnSecretFound, OnChainProgress, OnDiscoveryReward.
 *  - Bedingungen: Story-Schema (standing, npcTrust, flag, aegis, done, tag, any, all, not) und
 *    Erweiterungen hour [von,bis] (ueber Mitternacht moeglich), weekday [0=Mo..6], weather [..],
 *    item, discovered, event (Weltereignis aktiv), heat [bezirk, op, wert], collection [id, op, n],
 *    regionState [key, op, wert].
 *  - Speichern: ToJson/FromJson (SaveManager legt den String neben StoryJson ab).
 */
UCLASS()
class MTA6_API UMTADiscoverySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// --- Daten ---------------------------------------------------------------------------------

	/** Laedt secrets.json und wilderness.json (leere Pfade = Content/Data/...). Zustand bleibt erhalten. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	bool LoadDefinitions(const FString& SecretsPath = TEXT(""), const FString& WildernessPath = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	void ResetProgress();

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	TArray<FMTADiscoveryDef> GetAllDiscoveries() const { return Defs; }

	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	bool GetDiscovery(FName Id, FMTADiscoveryDef& OutDef) const;

	// --- Umgebung ------------------------------------------------------------------------------

	/** Tageszeit/Tag/Wetter setzen (Wetter-/Zeitsystem). Ohne Aufruf: Stunde vom AMTA6GameMode, Wetter "clear". */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	void SetEnvironment(float Hour, int32 DayIndex, FName Weather);

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	float GetHour() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	int32 GetWeekday() const { return ((DayIndex % 7) + 7) % 7; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	FName GetWeather() const { return Weather; }

	/** Wildnisregion meldet ihre Transformation (Ursprung fuer LocalM). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	void RegisterRegion(FName RegionId, const FTransform& RegionTransform);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	void UnregisterRegion(FName RegionId);

	/** Anker-Actors (Tag = Id) neu suchen, z. B. nachdem zur Laufzeit Anker gespawnt wurden (Wildnis-Builder). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	void RefreshAnchors();

	// --- Spielerereignisse -----------------------------------------------------------------------

	/** Pruefdurchlauf mit expliziter Position (wird bei bAutoTrackPlayer automatisch aufgerufen). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	void UpdatePlayer(const FVector& PlayerLocation, float DeltaSeconds);

	/** Interaktionstaste: naechste Interact-Entdeckung im Radius finden. Liefert Id oder None. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	FName TryInteract(const FVector& PlayerLocation);

	/** Fotomodus: alle Photo-Entdeckungen im Radius und im Blickkegel werden gefunden. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	TArray<FName> ReportPhoto(const FVector& CameraLocation, const FVector& ViewDirection, float MaxAngleDeg = 15.f);

	/** Fund erzwingen (Script-Modus, Debug, Sammelkomponente). bCheckConditions = Bedingungen beachten. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	bool ForceDiscover(FName Id, bool bCheckConditions = false);

	/** NPC hat ein Geruecht erzaehlt / Radio lief: Hinweis gilt als gehoert. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	void MarkHintHeard(FName Id);

	// --- Abfragen ----------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	bool IsDiscovered(FName Id) const { return FoundIds.Contains(Id); }

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	EMTADiscoveryState GetState(FName Id) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	bool IsVisible(FName Id);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	bool IsUnlocked(FName Id);

	/** Weltposition (Anker-Actor, Region oder Parzellenraster). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	bool GetDiscoveryLocation(FName Id, FVector& OutLocation);

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	FMTADistrictProgress GetDistrictProgress(FName DistrictId) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	TArray<FMTADistrictProgress> GetAllDistrictProgress() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	FMTAChainProgress GetChainProgress(FName ChainId) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	TArray<FMTALandmark> GetLandmarks(FName DistrictId) const;

	/** NPC-Hook: Geruechte zu sichtbaren, noch nicht gefundenen Orten im Bezirk (max. MaxCount). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	TArray<FMTARumorHook> GetRumors(FName DistrictId, int32 MaxCount = 3);

	/** Audio-Hook: hoerbare Orte im Umkreis (Sound-Hinweise noch nicht gefundener, sichtbarer Orte). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	TArray<FMTASoundHook> GetSoundHooksNear(const FVector& Location, float RadiusCm);

	/** Bedingungen (JSON-Array im secrets-Schema) gegen den aktuellen Zustand pruefen. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	bool EvaluateConditionsJson(const FString& ConditionsJson);

	/** C++-Variante fuer bereits geparste Bedingungen (WorldEvents, Wildnis). Schnappschuss vorher aktualisieren. */
	bool EvaluateConditions(const TArray<TSharedPtr<FJsonValue>>& Conditions) const;
	void RefreshStorySnapshot();

	// --- Sammelobjekte -------------------------------------------------------------------------

	/** Item Index (0..Count-1) einer Sammlung einsammeln. false = unbekannt oder schon gesammelt. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	bool CollectItem(FName CollectionId, int32 Index);

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	bool IsItemCollected(FName CollectionId, int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	FMTACollectionProgress GetCollectionProgress(FName CollectionId) const;

	// --- Inventar-Schluessel, Regionszustaende -------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	void AddItem(FName ItemId);

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	bool HasItem(FName ItemId) const { return Items.Contains(ItemId); }

	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	void SetRegionState(FName Key, int32 Value);

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	int32 GetRegionState(FName Key) const;

	// --- Belohnungen abholen (Spiel/Wirtschaft/Story wenden sie an) ----------------------------

	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	int32 ConsumeMoney();

	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	TMap<FName, int32> ConsumeSkillPoints();

	/** JSON-Array von Story-Effekten ({"standing":[f,n]}, {"aegis":n}) fuer UMTAStorySubsystem. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	FString ConsumeStoryEffectsJson();

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	TArray<FName> GetUnlockedVehicles() const { return UnlockedVehicles; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	TArray<FName> GetUnlockedCosmetics() const { return UnlockedCosmetics; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Discovery")
	TArray<FName> GetUnlockedLore() const { return UnlockedLore; }

	/** Belohnung anwenden (auch fuer WorldEvents nutzbar). */
	void ApplyReward(FName SourceId, const FMTADiscoveryReward& Reward);

	// --- Speichern -------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	FString ToJson() const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Discovery")
	bool FromJson(const FString& Json);

	// --- Delegates -------------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Discovery")
	FOnDiscoverySecretFound OnSecretFound;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Discovery")
	FOnSecretHinted OnSecretHinted;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Discovery")
	FOnChainProgress OnChainProgress;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Discovery")
	FOnCollectionProgress OnCollectionProgress;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Discovery")
	FOnDiscoveryReward OnDiscoveryReward;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Discovery")
	FOnRegionStateChanged OnRegionStateChanged;

	// --- Einstellungen ---------------------------------------------------------------------------

	/** Spieler 0 automatisch verfolgen (sonst UpdatePlayer selbst aufrufen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Discovery")
	bool bAutoTrackPlayer = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Discovery")
	float TickInterval = 0.25f;

	/** Rasterursprung, falls kein AMTACityGenerator in der Welt steht (Stadtmitte, cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Discovery")
	FVector FallbackGridOrigin = FVector::ZeroVector;

private:
	bool HandleTick(float DeltaSeconds);
	void ParseDiscoveryArray(const TArray<TSharedPtr<FJsonValue>>& Arr, FName RegionId);
	void ParseChains(const TArray<TSharedPtr<FJsonValue>>& Arr);
	void ParseCollections(const TArray<TSharedPtr<FJsonValue>>& Arr);
	void ParseLandmarks(const TArray<TSharedPtr<FJsonValue>>& Arr);
	static TArray<FMTADiscoveryReward> ParseRewards(const TArray<TSharedPtr<FJsonValue>>* Arr);
	void RebuildRuntimeStates();

	bool CheckCond(const TSharedPtr<FJsonObject>& C) const;
	bool IsVisibleIndex(int32 Index) const;
	bool IsUnlockedIndex(int32 Index) const;
	bool ResolveLocation(int32 Index, FVector& OutLocation);
	void Discover(int32 Index);
	void RevealHint(int32 Index);
	void ApplyRewards(FName SourceId, const TArray<FMTADiscoveryReward>& Rewards);
	int32 FindIndex(FName Id) const;
	int32 FindChainStepIndex(FName ChainId, int32 Step) const;
	UWorld* GetGameWorld() const;
	FText GetDistrictDisplayName(FName DistrictId) const;

	UPROPERTY()
	TArray<FMTADiscoveryDef> Defs;

	TArray<FMTADiscoveryRuntime> Runtime;
	TMap<FName, int32> IndexById;

	UPROPERTY()
	TMap<FName, FMTAChainDef> Chains;

	UPROPERTY()
	TMap<FName, FMTACollectionDef> Collections;

	UPROPERTY()
	TArray<FMTALandmark> Landmarks;

	TMap<FName, FString> DistrictNames;
	TMap<FName, FTransform> Regions;

	// Gespeicherter Zustand
	TSet<FName> FoundIds;
	TMap<FName, int32> HintCounts;
	TSet<FName> Items;
	TMap<FName, TSet<int32>> CollectedItems;
	TMap<FName, int32> RegionStates;
	TSet<FName> CompletedChains;
	TArray<FName> UnlockedVehicles;
	TArray<FName> UnlockedCosmetics;
	TArray<FName> UnlockedLore;
	int32 PendingMoney = 0;
	TMap<FName, int32> PendingSkillPoints;
	TArray<TSharedPtr<FJsonValue>> PendingStoryEffects;

	// Umgebung
	float HourOverride = -1.f;
	int32 DayIndex = 0;
	FName Weather = TEXT("clear");
	TSharedPtr<FJsonObject> StorySnapshot;

	TWeakObjectPtr<UWorld> AnchorWorld;
	TWeakObjectPtr<AActor> CachedCity;
	bool bCityLookupDone = false;
	FTSTicker::FDelegateHandle TickHandle;
};
