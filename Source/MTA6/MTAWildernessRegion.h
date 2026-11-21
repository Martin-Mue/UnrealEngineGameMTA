#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPtr.h"
#include "MTAWildernessRegion.generated.h"

class UBoxComponent;
class USceneComponent;
class UMTADiscoverySubsystem;

/** Zustandsautomat des Geheimeingangs hinter dem Wasserfall. Werte = regionState in wilderness.json. */
UENUM(BlueprintType)
enum class EMTASecretEntranceState : uint8
{
	Hidden = 0  UMETA(DisplayName = "Verborgen"),
	Hinted = 1  UMETA(DisplayName = "Angedeutet"),
	Open = 2    UMETA(DisplayName = "Offen")
};

/** Landscape-Vorgaben (UE-Empfehlung: Sektionen 63 Quads, 2x2 Sektionen je Komponente). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAWildernessLandscapeParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landscape")
	FIntPoint OverallResolution = FIntPoint(2017, 2017);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landscape")
	int32 SectionSizeQuads = 63;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landscape")
	int32 SectionsPerComponent = 2;

	/** Kantenlaenge eines Quads in cm (Landscape-Scale X/Y). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landscape", meta = (ClampMin = "10"))
	float QuadSizeCm = 120.f;

	/** Landscape-Scale Z (100 = +-256 m Hoehenbereich). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landscape", meta = (ClampMin = "1"))
	float ZScaleCm = 100.f;

	/** Hoehenprofil-Stuetzpunkte (m, relativ zum Regionsursprung); Quelle: wilderness.json heightProfile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landscape")
	TArray<FVector> HeightControlPointsM;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landscape")
	TSoftObjectPtr<UObject> LandscapeMaterial;
};

/** PCG-/Foliage-Dichte eines Bioms. */
USTRUCT(BlueprintType)
struct MTA6_API FMTABiomeFoliageParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage")
	FName BiomeId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage", meta = (ClampMin = "0"))
	float TreesPerHectare = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage", meta = (ClampMin = "0", ClampMax = "1"))
	float Undergrowth = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage", meta = (ClampMin = "0", ClampMax = "1"))
	float Rocks = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage", meta = (ClampMin = "0", ClampMax = "1"))
	float Lianas = 0.f;

	/** Mindestabstand der Vegetation zu Pfaden (cm), damit Wege lesbar bleiben. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foliage", meta = (ClampMin = "0"))
	float PathClearanceCm = 250.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSecretEntranceStateChanged, EMTASecretEntranceState, OldState, EMTASecretEntranceState, NewState);

/**
 * Wildnisregion Verdanta (Paket K, Gerüst): Wald/Dschungel am Stadtrand mit Fluss, Wasserfall,
 * Hoehle und Geheimeingang. Kompiliert ohne Editor-Assets und ohne Niagara-/PCG-Modulabhaengigkeit:
 * alle Assets sind TSoftObjectPtr<UObject>, das Spawnen von Effekten uebernimmt ein Blueprint
 * (OnSpawnWaterfallFX).
 *
 * Zustandsautomat Geheimeingang (persistiert als regionState "<RegionId>.entrance" im UMTADiscoverySubsystem):
 *   Hidden  -> Hinted : automatisch, wenn der Spieler im HintRadius ist und HiddenToHintedWhen erfuellt ist.
 *   Hinted  -> Open   : TryOpenEntrance() (Interaktion) im Eingangstrigger und HintedToOpenWhen erfuellt.
 *   Open ist endgueltig. Bedingungen im secrets-Schema (JSON-Array), Standard aus wilderness.json.
 * Schleuse: SetSluiceClosed setzt "<RegionId>.sluice" und meldet WaterflowScale an das Blueprint.
 */
UCLASS(Blueprintable, meta = (DisplayName = "MTA Wilderness Region"))
class MTA6_API AMTAWildernessRegion : public AActor
{
	GENERATED_BODY()

public:
	AMTAWildernessRegion();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	// --- Komponenten -----------------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Wilderness")
	TObjectPtr<USceneComponent> Root;

	/** Ausdehnung der Region (Streaming/Debug). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Wilderness")
	TObjectPtr<UBoxComponent> RegionBounds;

	/** Oberkante des Wasserfalls; hier haengt der Blueprint die Niagara-Effekte an. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Wilderness|Waterfall")
	TObjectPtr<USceneComponent> WaterfallAnchor;

	/** Geheimtuer hinter dem Wasservorhang. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Wilderness|Secret")
	TObjectPtr<UBoxComponent> SecretEntranceTrigger;

	// --- Parameter -------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness")
	FName RegionId = TEXT("verdanta");

	/** Groesse in m (X = Nord, Y = Ost). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness")
	FVector2D SizeM = FVector2D(2400.0, 1800.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness")
	int32 Seed = 4711;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness|Landscape")
	FMTAWildernessLandscapeParams Landscape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness|Foliage")
	TArray<FMTABiomeFoliageParams> Biomes;

	/** Globaler Faktor auf alle Dichten (Skalierbarkeit/Performance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness|Foliage", meta = (ClampMin = "0", ClampMax = "2"))
	float FoliageDensityScale = 1.f;

	/** PCG-Graph (optional, Soft-Referenz: kein PCG-Modul noetig). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness|Foliage")
	TSoftObjectPtr<UObject> PCGGraph;

	/** Wasserfall-Position relativ zur Region in m (Oberkante). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness|Waterfall")
	FVector WaterfallTopM = FVector(1530.0, 1110.0, 128.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness|Waterfall")
	float WaterfallHeightM = 38.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness|Waterfall")
	float WaterfallWidthM = 14.f;

	/** Niagara-System des Falls (Soft, Typ UObject: keine Niagara-Abhaengigkeit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness|Waterfall")
	TSoftObjectPtr<UObject> WaterfallSystem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness|Waterfall")
	TSoftObjectPtr<UObject> MistSystem;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness|Waterfall")
	TSoftObjectPtr<UObject> WaterfallSound;

	/** Geheimtuer relativ zur Region in m. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness|Secret")
	FVector SecretDoorM = FVector(1512.0, 1086.0, 93.0);

	/** Ab dieser Entfernung (m) kann der Eingang angedeutet werden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness|Secret")
	float HintRadiusM = 60.f;

	/** Bedingungen Hidden -> Hinted (JSON-Array). Leer + bLoadRulesFromJson = aus wilderness.json. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness|Secret", meta = (MultiLine = "true"))
	FString HiddenToHintedWhen;

	/** Bedingungen Hinted -> Open (JSON-Array). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness|Secret", meta = (MultiLine = "true"))
	FString HintedToOpenWhen;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness|Secret")
	bool bLoadRulesFromJson = true;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Wilderness|Secret")
	FOnSecretEntranceStateChanged OnEntranceStateChangedEvent;

	// --- Steuerung -------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|Wilderness|Secret")
	EMTASecretEntranceState GetEntranceState() const { return EntranceState; }

	/** Interaktion am Eingang. true = jetzt offen. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Wilderness|Secret")
	bool TryOpenEntrance();

	/** Debug/Story: Zustand setzen (wird gespeichert). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Wilderness|Secret")
	void SetEntranceState(EMTASecretEntranceState NewState);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Wilderness|Waterfall")
	void SetSluiceClosed(bool bClosed);

	/** 1 = normaler Fall, 0.5 = Schleuse zu, 2 = Hochwasser (Regen). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Wilderness|Waterfall")
	float GetWaterflowScale() const;

	/** Weltposition eines Punkts in Regionsmetern. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Wilderness")
	FVector RegionMetersToWorld(const FVector& LocalM) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Wilderness|Foliage")
	FMTABiomeFoliageParams GetBiomeParams(FName BiomeId) const;

	// --- Blueprint-Hooks (Assets/Effekte) ---------------------------------------------------------

	/** Nach BeginPlay mit geladenen Soft-Assets (nullptr, wenn nicht gesetzt). */
	UFUNCTION(BlueprintImplementableEvent, Category = "MTA6|Wilderness|Waterfall")
	void OnSpawnWaterfallFX(UObject* LoadedWaterfallSystem, UObject* LoadedMistSystem, UObject* LoadedSound, USceneComponent* Anchor);

	UFUNCTION(BlueprintImplementableEvent, Category = "MTA6|Wilderness|Waterfall")
	void OnWaterflowChanged(float WaterflowScale);

	/** Optik des Eingangs (Lichtkante, Tueranimation, Server-Glimmen). */
	UFUNCTION(BlueprintImplementableEvent, Category = "MTA6|Wilderness|Secret")
	void OnEntranceStateChanged(EMTASecretEntranceState OldState, EMTASecretEntranceState NewState);

private:
	void LoadRulesFromJson();
	void ApplyLayout();
	FName StateKey() const;
	FName SluiceKey() const;
	UMTADiscoverySubsystem* GetDiscovery() const;
	bool IsPlayerNear(const FVector& WorldLocation, float RadiusCm, FVector* OutPlayerLocation = nullptr) const;

	EMTASecretEntranceState EntranceState = EMTASecretEntranceState::Hidden;
};
