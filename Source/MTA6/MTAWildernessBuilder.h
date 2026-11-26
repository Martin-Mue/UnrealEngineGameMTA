#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPtr.h"
#include "MTAInteractionInterface.h"
#include "MTAWildernessBuilder.generated.h"

class AMTACityGenerator;
class UAudioComponent;
class UBoxComponent;
class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UPointLightComponent;
class UPrimitiveComponent;
class UProceduralMeshComponent;
class USoundBase;
class UStaticMesh;
class UTextRenderComponent;

/** Meldung der Wildnis an HUD/Blueprints: Entdeckung (Id) und kurzer Text (Titel oder Fundtext). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAWildernessEventSignature, FName, DiscoveryId, const FString&, Message);

/**
 * Unsichtbarer Anker einer Wildnis-Entdeckung: Tag = Entdeckungs-Id. UMTADiscoverySubsystem nimmt die
 * Position des ersten Actors mit diesem Tag (siehe ResolveLocation) statt der Planungs-Koordinate localM.
 */
UCLASS(NotBlueprintable, meta = (DisplayName = "MTA Wilderness Marker"))
class MTA6_API AMTAWildernessMarker : public AActor
{
	GENERATED_BODY()

public:
	AMTAWildernessMarker();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Wilderness")
	TObjectPtr<USceneComponent> SceneRoot;
};

/**
 * Interaktionspunkt in der Wildnis (Kiste, Notiz, Fernglas, Stahltuer). Implementiert IMTAInteractable,
 * damit UMTAInteractionComponent ihn per Blick-Trace und Naehe-Overlap findet. Interact meldet die
 * Entdeckung DiscoveryId an UMTADiscoverySubsystem (ForceDiscover mit Bedingungspruefung).
 */
UCLASS(Blueprintable, meta = (DisplayName = "MTA Wilderness Interactable"))
class MTA6_API AMTAWildernessInteractable : public AActor, public IMTAInteractable
{
	GENERATED_BODY()

public:
	AMTAWildernessInteractable();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Wilderness")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Abfrage-Volumen (nur Query, blockt Visibility fuer den Blick-Trace, sonst Overlap). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Wilderness")
	TObjectPtr<UBoxComponent> InteractVolume;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness")
	FName DiscoveryId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness")
	FText PromptText;

	/** Wird nach der Benutzung als ausgegrauter Grund angezeigt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness")
	FText UsedText;

	/** Verschlossen: Prompt ausgegraut mit LockedReason (z. B. Stahltuer ohne Zugangskarte). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness")
	bool bLocked = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Wilderness")
	FText LockedReason;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Wilderness")
	bool bUsed = false;

	/** Nach erfolgreicher Benutzung: Id und Fundtext der Entdeckung. */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Wilderness")
	FMTAWildernessEventSignature OnUsed;

	/** Einrichtung nach dem Spawnen (Tag = DiscoveryId, Volumen-Halbmasse in cm). */
	void Configure(FName InDiscoveryId, const FText& InPrompt, const FText& InUsedText, const FVector& HalfExtentCm);

	virtual FMTAInteractionInfo GetInteractionInfo_Implementation(AActor* Interactor) const override;
	virtual bool CanInteract_Implementation(AActor* Interactor) const override;
	virtual void Interact_Implementation(AActor* Interactor) override;

protected:
	virtual void BeginPlay() override;
};

/**
 * Wildnis "Verdanta": erzeugt zur Laufzeit rein aus C++ ein begehbares Waldgebiet (ca. 600 x 640 m) neben
 * der Stadtkante. Gelaende = UProceduralMeshComponent-Kacheln (Hoehenfeld aus mehrschichtigem Rauschen,
 * Talboden, Bergruecken mit Felswand, Flusstal, Weg mit Vertex-Farben, Kollision Complex-as-Simple),
 * Vegetation/Felsen/Bauten = HISM aus /Engine/BasicShapes, Wasserfall mit animierten Wasserbaendern
 * und Gischt (ISM, Tick), Becken, Bach, Teich, Hoehle hinter dem Wasserfall (geheimer Eingang),
 * Ueberraschungen (Wrack, Haengebruecke, Aussichtsfels mit Fernglas, Lagerfeuer mit Zelt, Tierspuren,
 * Wachturm) als Entdeckungen ueber UMTADiscoverySubsystem (Daten: Content/Data/wilderness.json).
 *
 * Koordinaten: lokales Regionssystem in Metern, U = weg von der Stadt (Actor-X), V = entlang der
 * Stadtkante (Actor-Y), Z = Hoehe ueber Stadtboden. Der Actor steht in der Suedwest-Ecke (U = V = 0).
 * Platzierung: FindOrSpawn prueft mehrere Kandidaten (Nord, Nord-Ost, Ost) gegen Stadtflaeche,
 * Strassen/Wasser von AMTAWorldDetail, Kueste und Berge und nimmt den ersten freien.
 */
UCLASS(Blueprintable, meta = (DisplayName = "MTA Wilderness Builder"))
class MTA6_API AMTAWildernessBuilder : public AActor
{
	GENERATED_BODY()

public:
	AMTAWildernessBuilder();

	virtual void Tick(float DeltaSeconds) override;

	// ---------------------------------------------------------------- Erzeugung

	/** Vorhandenen Builder liefern oder (nur Spielwelt) an freier Stelle neben der Stadt erzeugen und bauen. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Wilderness", meta = (WorldContext = "WorldContext"))
	static AMTAWildernessBuilder* FindOrSpawn(UObject* WorldContext);

	/** Ersten Builder der Welt finden (nullptr, falls keiner). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Wilderness", meta = (WorldContext = "WorldContext"))
	static AMTAWildernessBuilder* FindWildernessBuilder(const UObject* WorldContext);

	/** Alles (neu) erzeugen. Deterministisch aus Seed und Actor-Transformation. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Wilderness")
	void Build();

	/** Alle erzeugten Komponenten und gespawnten Actors entfernen. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Wilderness")
	void ClearWilderness();

	// ---------------------------------------------------------------- Abfragen (Missionen, Marker, Minimap)

	/** Mitte des Gebiets (Welt, Z = Gelaende). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Wilderness")
	FVector GetWildernessCenter() const;

	/** Zufahrt: Beginn der Forststrasse an der Stadtkante (Welt, Z = Fahrbahn). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Wilderness")
	FVector GetEntranceLocation() const;

	/** Wanderparkplatz am Waldrand (Welt). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Wilderness")
	FVector GetParkingLocation() const;

	/** Geheimer Eingang hinter dem Wasserfall (Hoehlenmund, Welt). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Wilderness")
	FVector GetSecretEntranceLocation() const;

	/** Fusspunkt des Wasserfalls am Becken (Welt). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Wilderness")
	FVector GetWaterfallLocation() const;

	/** Anzeigename der Region (wilderness.json region.name, sonst "Verdanta"). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Wilderness")
	FText GetRegionName() const;

	/** true, wenn die Welt-Position (X/Y) im Gebiet liegt. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Wilderness")
	bool IsInsideWilderness(const FVector& WorldLocation) const;

	/** Statuszeile/HUD: Regionsname, wenn Location in einer Wildnis liegt, sonst leer. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Wilderness", meta = (WorldContext = "WorldContext"))
	static FText GetRegionNameAt(const UObject* WorldContext, const FVector& WorldLocation);

	/** Gelaendehoehe (Welt-Z) aus dem Hoehenfeld; false ausserhalb. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Wilderness")
	bool GetTerrainHeightAt(const FVector& WorldLocation, float& OutWorldZ) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Wilderness")
	int32 GetTotalInstanceCount() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Wilderness")
	bool IsBuilt() const { return bBuilt; }

	/** Selbsttest (auch unter -nullrhi): Gelaende, Kollision, Budget, Stadtabstand, Steigung, Wasserfall, Hoehle, Entdeckungen, Assets. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Wilderness")
	FString RunWildernessSelfTest();

	/** Entdeckung in der Wildnis gemacht (Vorhang durchschritten, Kiste, Notiz ...). */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Wilderness")
	FMTAWildernessEventSignature OnWildernessDiscovery;

	// ---------------------------------------------------------------- Parameter

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness")
	FName RegionId = TEXT("verdanta");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness")
	int32 Seed = 4711;

	/** Ausdehnung U x V in m (Layout ist auf 600 x 640 m ausgelegt). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wilderness")
	FVector2D SizeM = FVector2D(600.0, 640.0);

	/** Rasterweite des Hoehenfelds (m). Kleiner = feiner, aber mehr Dreiecke und laengeres Kollisions-Cooking. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness|Terrain", meta = (ClampMin = "1.5", ClampMax = "5"))
	float GridSpacingM = 2.5f;

	/** Gelaende-Kacheln je Achse (Culling, kleinere Kollisionskoerper). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness|Terrain", meta = (ClampMin = "1", ClampMax = "8"))
	int32 ChunksPerAxis = 4;

	/** Obergrenze aller Instanzen (Vegetation wird zuletzt verteilt und vorher gekappt). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness|Foliage", meta = (ClampMin = "1500", ClampMax = "6000"))
	int32 MaxInstances = 5800;

	/** Faktor auf Baum- und Unterholzdichte. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness|Foliage", meta = (ClampMin = "0.1", ClampMax = "2"))
	float FoliageDensity = 1.f;

	/** Faktor auf alle Sichtweiten (Qualitaetsstufen). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness|Rendering", meta = (ClampMin = "0.25", ClampMax = "4"))
	float CullDistanceScale = 1.f;

	/** Wasserfall/Gischt/Lagerfeuer werden nur animiert, wenn der Spieler naeher ist (m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness|Rendering", meta = (ClampMin = "50"))
	float AnimationRadiusM = 350.f;

	/** Gelaende-Material (Vertex-Farben). Rueckfall: TerrainFallbackMaterial. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness|Assets")
	TSoftObjectPtr<UMaterialInterface> TerrainMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness|Assets")
	TSoftObjectPtr<UMaterialInterface> TerrainFallbackMaterial;

	/** Wasserfall-Material (tools/ue/build_materials.py: M_MTA_Waterfall). Rueckfall: WaterfallFallbackMaterial. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness|Assets")
	TSoftObjectPtr<UMaterialInterface> WaterfallMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness|Assets")
	TSoftObjectPtr<UMaterialInterface> WaterfallFallbackMaterial;

	/** Wasserfall-Loop (tools/ue_audio/import_wilderness_sounds.py). Rueckfall: WaterfallFallbackSound. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness|Assets")
	TSoftObjectPtr<USoundBase> WaterfallSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness|Assets")
	TSoftObjectPtr<USoundBase> WaterfallFallbackSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness|Assets") TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness|Assets") TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness|Assets") TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wilderness|Assets") TObjectPtr<UStaticMesh> ConeMesh;

	// ---------------------------------------------------------------- Ergebnis

	/** Name des gewaehlten Platzierungs-Kandidaten (Nord, Nord-Ost, Ost, manuell). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wilderness|Result")
	FString PlacementName;

	/** Zusammenfassung der Platzierungspruefung (Selbsttest). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wilderness|Result")
	FString PlacementReport;

	/** false = gewaehlter Platz kollidiert mit Stadt, Strasse, Wasser oder Kueste (Selbsttest meldet Fehler). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wilderness|Result")
	bool bPlacementFree = true;

	/** V-Position (m) der Zufahrt an der Stadtkante (fluchtet mit einer Stadtstrasse, wenn moeglich). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wilderness|Result")
	float EntryV = 220.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wilderness|Result")
	bool bEntryAlignedWithCityRoad = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wilderness|Result")
	FString ResolvedTerrainMaterial;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wilderness|Result")
	FString ResolvedWaterfallMaterial;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Wilderness|Result")
	FString ResolvedSound;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, Category = "Wilderness|Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> TerrainChunks;

	/** Eine HISM je Instanz-Ebene (siehe Ebenentabelle in der .cpp). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> InstanceLayers;

	/** Wasserbaender (animiert). */
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Ribbons;

	/** Gischt-Kugeln (animiert, nur mit M_MTA_Waterfall). */
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Mist;

	/** Durchgang hinter dem Wasservorhang (Hoehlenmund). */
	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> VeilTrigger;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> WaterfallAudio;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> CaveLight;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> CampLight;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> SignTexts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnedActors;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> LoadedMaterials;

private:
	/** Sammelpuffer je Ebene (lokale Transformationen in cm, 7 Custom-Data-Werte je Instanz). */
	struct FWildBatch
	{
		TArray<FTransform> Transforms;
		TArray<float> Data;
	};

	/** Polylinie (Weg/Pfad) in lokalen Metern; Z wird beim Einebnen gesetzt. */
	struct FWildTrail
	{
		TArray<FVector2D> Points;
		float HalfWidth = 1.f;
		float Shoulder = 2.f;
		float MaxSlope = 0.3f;
		bool bVehicle = false;
		/** Ebener Abschnitt (U-Bereich in m, z. B. Parkplatz); FlatToU <= FlatFromU = keiner. */
		float FlatFromU = -1.f;
		float FlatToU = -1.f;
		TArray<FVector> Samples; // U, V, Z (m) im Abstand von ca. 2 m
	};

	// Erzeugung
	static bool ComputePlacement(UWorld* World, FTransform& OutTransform, FString& OutName, FString& OutReport);
	void ResolveAssets();
	void ComputeEntry();
	void BuildHeightfield();
	void ApplyTrail(FWildTrail& Trail, bool bUseCurrentHeights);
	void CarveWater();
	void BuildTerrainMesh();
	void CreateLayers();
	void BuildStructures();
	void BuildCave();
	void BuildWaterfall();
	void BuildSurprises();
	void BuildVegetation();
	void ApplyBatches();
	void SpawnDiscoveryActors();

	// Bausteine (lokale Meter)
	void AddInst(int32 Layer, const FTransform& LocalCm, const float* Data);
	void AddBoxM(int32 Layer, const FVector& CenterM, const FVector& SizeM3, const FRotator& Rot, const float* Data);
	void AddBoxMinMax(int32 Layer, const FVector& MinM, const FVector& MaxM, const float* Data);
	void AddColumnM(int32 Layer, const FVector& BaseM, float DiameterM, float HeightM, const float* Data);
	void AddRodM(int32 Layer, const FVector& AM, const FVector& BM, float DiameterM, const float* Data);
	void AddBeamM(int32 Layer, const FVector& AM, const FVector& BM, float ThickM, float WidthM, const float* Data);
	int32 PlannedInstances() const;

	// Hoehenfeld
	float NaturalHeight(float U, float V) const;
	float HeightAt(float U, float V) const;
	float SampleGrid(const TArray<float>& Field, float U, float V) const;
	FVector2D GroundSlopeAt(float U, float V) const;
	FVector LocalM(float U, float V, float ZOffset = 0.f) const;
	FVector ToWorld(const FVector& LocalMeters) const;
	bool InCaveHole(float U, float V) const;

	// Laufzeit
	void AnimateWaterfall(float Time);
	void SpawnMarker(FName Id, const FVector& LocalMeters);
	AMTAWildernessInteractable* SpawnInteractable(FName Id, const FVector& LocalMeters, const FText& Prompt, const FText& Used, const FVector& HalfExtentM);
	FString DiscoveryTitle(FName Id) const;

	// Selbsttest-Hilfen
	bool TraceGround(const FVector& LocalTopM, float DepthM, FVector& OutHitLocalM, UPrimitiveComponent** OutComp = nullptr) const;

	bool bBuilt = false;
	TArray<FWildBatch> Batches;
	TArray<float> Heights;   // m, NU x NV
	TArray<float> PathDist;  // m bis zum naechsten Weg-/Pfadrand (negativ = auf dem Weg)
	TArray<float> PathKind;  // 1 = Fahrweg (Schotter), 0.5 = Pfad (Erde), 0 = kein Weg
	TArray<float> RiverDist; // m bis zum Ufer (negativ = im Bachbett)
	int32 NU = 0;
	int32 NV = 0;
	float Grid = 2.5f;

	TArray<FWildTrail> Trails;   // [0] = Fahrweg
	TArray<FVector> RiverPts;    // U, V, Wasserspiegel (m), Abstand ca. 2 m
	TArray<FVector> StreamPts;   // Bach auf dem Plateau
	TArray<FVector4> Clearings;  // U, V, Radius, Ebnungsstaerke
	FVector2D Parking = FVector2D(80.0, 220.0);
	FVector2D BridgeA = FVector2D::ZeroVector;
	FVector2D BridgeB = FVector2D::ZeroVector;
	float PondWater = 2.f;
	float BridgeDeckZ = 0.f;
	FVector TowerPlatformM = FVector::ZeroVector;

	// Wasserfall-Animation
	TArray<float> RibbonV;      // Mitte je Band (m)
	TArray<float> RibbonWidth;  // m
	TArray<float> RibbonSpeed;  // m/s
	TArray<float> RibbonPhase;  // m
	int32 SegmentsPerRibbon = 5;
	float FallTopZ = 0.f;
	float FallBottomZ = 0.f;
	float FallU = 0.f;
	bool bWaterfallMaterialIsFallback = false;
	TArray<FVector> MistBase;

	// Laufzeitstatus
	FString RegionTitle = TEXT("Verdanta");
	bool bVeilFound = false;
	float NextCampFlicker = 0.f;
	TMap<FName, FString> TitleCache;
};
