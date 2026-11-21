#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MTAInteriorTypes.h"
#include "MTAInteriorData.h"
#include "MTAInteriorBuilder.h"
#include "MTAInteriorSubsystem.generated.h"

class AMTAInteriorInstance;
class AMTABuildingEntry;
class APawn;

/** Anforderung eines Innenraums fuer ein bestimmtes Gebaeude. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAInteriorRequest
{
	GENERATED_BODY()

	/** Eindeutiger Gebaeudeschluessel (AMTABuildingEntry::BuildingKey). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior") FName EntryKey;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior") FName TemplateId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior") EMTADistrict District = EMTADistrict::Core;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior") int32 Seed = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior") FName StyleOverride;
	/** Weltposition des Aussengebaeudes; die Zelle entsteht senkrecht darunter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior") FVector Anchor = FVector::ZeroVector;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAOnInteriorTravel, AMTAInteriorInstance*, Interior, APawn*, Pawn);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAOnInteriorBuilt, AMTAInteriorInstance*, Interior);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAOnInteriorDestroyed, FName, EntryKey);

/**
 * Innenraum-Verwaltung (Variante a: versteckte Zellen im "Interior Grid" unter der Stadt).
 *  - erzeugt Innenraeume bei Bedarf aus interiors.json/furnishing.json
 *  - Vorwaermen in der Naehe von Eingaengen, Aufraeumen nach Radius/Budget (LRU)
 *  - Uebergaenge mit Ueberblende und Teleport, Rueckkehr zum passenden Eingang
 *  - Tageszeit-Schnittstelle (IMTATimeProvider oder SetHourOfDay), Hack-/Entdeckungs-Hooks
 * Konfiguration in Config/DefaultGame.ini unter [/Script/MTA6.MTAInteriorSubsystem].
 */
UCLASS(Config = Game)
class MTA6_API UMTAInteriorSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UMTAInteriorSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// --- Konfiguration ------------------------------------------------------------
	/** Datenpfade relativ zu Content/. */
	UPROPERTY(Config, EditAnywhere, Category = "Interior|Data") FString InteriorsDataPath = TEXT("Data/interiors.json");
	UPROPERTY(Config, EditAnywhere, Category = "Interior|Data") FString FurnishingDataPath = TEXT("Data/furnishing.json");
	/** Klasse der Innenraum-Actors (BP-Ableitung mit Materialien). */
	UPROPERTY(Config, EditAnywhere, Category = "Interior|Data") TSoftClassPtr<AMTAInteriorInstance> InstanceClass;

	/** Z-Hoehe der obersten Zellenebene (cm, unter Gelaende und Wasser). */
	UPROPERTY(Config, EditAnywhere, Category = "Interior|Grid") float InteriorGridDepth = -60000.f;
	/** Abstand der Ebenen; groesser als der groesste Lichtradius, damit nichts durchleuchtet. */
	UPROPERTY(Config, EditAnywhere, Category = "Interior|Grid") float LayerSpacing = 5000.f;
	UPROPERTY(Config, EditAnywhere, Category = "Interior|Grid") int32 MaxLayers = 24;

	/** Vorwaermen: Eingaenge naeher als dieser Radius werden im Hintergrund gebaut. */
	UPROPERTY(Config, EditAnywhere, Category = "Interior|Streaming") float PrewarmRadius = 3500.f;
	/** Schlafende Innenraeume weiter weg als dieser Radius werden zerstoert. */
	UPROPERTY(Config, EditAnywhere, Category = "Interior|Streaming") float KeepAliveRadius = 15000.f;
	UPROPERTY(Config, EditAnywhere, Category = "Interior|Streaming") int32 MaxCachedInteriors = 6;
	/** Geschaetztes Speicherbudget aller Innenraeume (KB, siehe FMTAInteriorBuildResult::EstimateCostKB). */
	UPROPERTY(Config, EditAnywhere, Category = "Interior|Streaming") float MemoryBudgetKB = 12288.f;
	UPROPERTY(Config, EditAnywhere, Category = "Interior|Streaming") float UpdateInterval = 0.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Interior|Travel") float FadeOutTime = 0.35f;
	UPROPERTY(Config, EditAnywhere, Category = "Interior|Travel") float FadeInTime = 0.5f;
	/** Sperrzeit nach einem Uebergang (verhindert Hin-und-her bei Overlap-Triggern). */
	UPROPERTY(Config, EditAnywhere, Category = "Interior|Travel") float TravelCooldown = 2.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Interior|Build") int32 MaxLightsPerInterior = 24;
	UPROPERTY(Config, EditAnywhere, Category = "Interior|Build") int32 MaxShadowLightsPerInterior = 4;
	UPROPERTY(Config, EditAnywhere, Category = "Interior|Build") int32 MaxWindowLightsPerInterior = 8;

	// --- Delegates ------------------------------------------------------------------
	/** Hook fuer das Entdeckungssystem. */
	UPROPERTY(BlueprintAssignable, Category = "Interior") FOnSecretFound OnSecretFound;
	/** Hack-Minispiel starten; Ergebnis per ResolveHack zurueckmelden. */
	UPROPERTY(BlueprintAssignable, Category = "Interior") FMTAOnHackRequested OnHackRequested;
	UPROPERTY(BlueprintAssignable, Category = "Interior") FMTAOnHidingChanged OnHidingChanged;
	UPROPERTY(BlueprintAssignable, Category = "Interior") FMTAOnInteriorTravel OnInteriorEntered;
	UPROPERTY(BlueprintAssignable, Category = "Interior") FMTAOnInteriorTravel OnInteriorExited;
	/** NPC-Agent: hier GetNPCSlots() abholen und Bewohner spawnen. */
	UPROPERTY(BlueprintAssignable, Category = "Interior") FMTAOnInteriorBuilt OnInteriorBuilt;
	UPROPERTY(BlueprintAssignable, Category = "Interior") FMTAOnInteriorDestroyed OnInteriorDestroyed;

	// --- Daten ----------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	bool LoadData();

	UFUNCTION(BlueprintPure, Category = "MTA6|Interior")
	bool IsDataLoaded() const { return Database.IsLoaded(); }

	const FMTAInteriorDatabase& GetDatabase() const { return Database; }

	/** Vorlage fuer Bezirk/Raumtyp (Raumtyp None = beliebig), deterministisch ueber Seed. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	FName PickTemplateId(EMTADistrict District, FName RoomType, int32 Seed, FVector2D MaxFootprintCm) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	TArray<FName> GetTemplateIds(EMTADistrict District, FName RoomType) const;

	// --- Instanzen ------------------------------------------------------------------
	/** Liefert (und baut bei Bedarf) den Innenraum; neue Instanzen starten schlafend. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	AMTAInteriorInstance* AcquireInterior(const FMTAInteriorRequest& Request);

	UFUNCTION(BlueprintPure, Category = "MTA6|Interior")
	AMTAInteriorInstance* FindInterior(FName EntryKey) const;

	/** Zerstoert einen Innenraum sofort (nicht, solange jemand drin ist). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	bool ReleaseInterior(FName EntryKey);

	void RegisterEntry(AMTABuildingEntry* Entry);
	void UnregisterEntry(AMTABuildingEntry* Entry);

	// --- Uebergaenge ----------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	bool EnterInterior(APawn* Pawn, AMTABuildingEntry* Entry, int32 EntranceIndex);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	bool ExitInterior(APawn* Pawn, AMTAInteriorInstance* Interior, EMTAEntranceKind Via);

	/** Innenraum, in dem Pawn gerade ist (oder nullptr). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Interior")
	AMTAInteriorInstance* GetPawnInterior(const APawn* Pawn) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Interior")
	bool IsTravelCoolingDown(const APawn* Pawn) const;

	// --- Tageszeit ------------------------------------------------------------------
	/** Manuelle Uhrzeit (ueberschreibt einen gefundenen IMTATimeProvider). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	void SetHourOfDay(float Hour);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	void SetTimeProvider(UObject* Provider);

	UFUNCTION(BlueprintPure, Category = "MTA6|Interior")
	float GetHourOfDay() const { return CurrentHour; }

	/** Wetter: 0 = klar, 1 = Unwetter (dunkelt Fensterlicht ab). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	void SetWeatherDim(float Dim);

	// --- Wirtschaft -----------------------------------------------------------------
	/** Objekt muss IMTAPriceProvider implementieren. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	void SetPriceProvider(UObject* Provider);

	UObject* GetPriceProvider() const { return PriceProvider.Get(); }

	// --- Hacking, Geheimnisse, Verstecke, Schluessel ----------------------------------
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	void RequestHack(UObject* Target, int32 Difficulty, FName ContextId);

	/** Vom Hack-Minispiel aufzurufen. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	void ResolveHack(UObject* Target, bool bSuccess);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	void ReportSecretFound(FName SecretId);

	UFUNCTION(BlueprintPure, Category = "MTA6|Interior")
	bool IsSecretFound(FName SecretId) const { return FoundSecrets.Contains(SecretId); }

	UFUNCTION(BlueprintPure, Category = "MTA6|Interior")
	int32 GetNumSecretsFound() const { return FoundSecrets.Num(); }

	void NotifyHidingChanged(APawn* Pawn, bool bHidden, AActor* Spot);

	void MarkUnlocked(FName Key) { Unlocked.Add(Key); }
	bool IsUnlocked(FName Key) const { return Unlocked.Contains(Key); }

	/** Sucht IMTAKeyHolder auf Pawn, Controller und PlayerState. */
	static bool PawnHasKey(const APawn* Pawn, FName KeyId);

	// --- Speichern ------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	FString StateToJson() const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Interior")
	bool StateFromJson(const FString& Json);

	// --- Diagnose -------------------------------------------------------------------
	UFUNCTION(BlueprintPure, Category = "MTA6|Interior")
	int32 GetNumInteriors() const { return Instances.Num(); }

	UFUNCTION(BlueprintPure, Category = "MTA6|Interior")
	float GetUsedBudgetKB() const;

private:
	FMTAInteriorDatabase Database;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<AMTAInteriorInstance>> Instances;

	TArray<TWeakObjectPtr<AMTABuildingEntry>> Entries;
	TMap<TWeakObjectPtr<APawn>, FName> PawnInside;
	TMap<TWeakObjectPtr<APawn>, double> LastTravel;
	TSet<TWeakObjectPtr<APawn>> PawnsInTransit;
	TMap<TWeakObjectPtr<UObject>, int32> PendingHacks;
	TSet<FName> FoundSecrets;
	TSet<FName> Unlocked;

	TWeakObjectPtr<UObject> PriceProvider;
	TWeakObjectPtr<UObject> TimeProvider;
	bool bManualHour = false;
	float CurrentHour = 12.f;
	float AppliedHour = -100.f;
	float WeatherDim = 0.f;
	float UpdateAccumulator = 0.f;
	float ProviderSearchCooldown = 0.f;

	TSubclassOf<AMTAInteriorInstance> ResolveInstanceClass() const;
	int32 AllocateLayer(const FBox2D& XYBounds) const;
	void UpdateHour(float DeltaTime);
	void ApplyDaylightToAll(bool bForce);
	void UpdateStreaming();
	void DestroyInstance(FName EntryKey);
	bool IsOccupied(FName EntryKey) const;
	APawn* GetPlayerPawn() const;
	void TravelPawn(APawn* Pawn, const FTransform& Dest, TFunction<void()> OnArrive);
};
