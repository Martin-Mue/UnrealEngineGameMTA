#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MTADistrict.h"
#include "MTAWorldDetail.generated.h"

class AMTACityGenerator;
class AMTABuildingEntry;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UTextRenderComponent;

/**
 * Instanz-Ebenen der Welt-Details. Jede Ebene ist eine HISM-Komponente mit festem Mesh, Material,
 * Kollision, Schatten und Sichtweite (siehe AMTAWorldDetail::GetLayerSpec). Reihenfolge = Komponentenname.
 */
UENUM(BlueprintType)
enum class EMTADetailLayer : uint8
{
	Water,          // Meeresflaeche (Ebenen-Kacheln, Wellen per WPO)
	Seabed,         // Meeresgrund (Kollision)
	Quay,           // Kaimauern, Molen, Promenade, Strand, Salzebene
	Piles,          // Pfaehle unter dem Pier
	Roads,          // Kuestenstrasse, Route 9
	Mountains,      // Kegel (Berge)
	Hills,          // abgeflachte Kugeln (Ruecken, Klippen)
	StructConcrete, // Landmarken: Beton/Putz
	StructMetal,    // Landmarken: Stahltraeger, Kraene, Tore
	StructMetalCyl, // Landmarken: Tanks, Schornsteine, Wasserturm
	StructCone,     // Turmspitzen, Tankdaecher
	Containers,     // Containerstapel
	NeonLandmark,   // weit sichtbare Leuchtakzente (Kuben)
	NeonGlobe,      // Warnlichter, Fackel, Leuchtkugeln
	NeonSmall,      // Ampel-/Kameralinsen, Leuchtschilder (nah)
	Poles,          // Masten (Ampeln, Schilder, Kameras, Werbetafeln)
	FurnitureBox,   // Bank, Schildplatten, Ampelgehaeuse, Leitplanken (nur Abfrage-Kollision)
	FurnitureCyl,   // Hydranten, Muelleimer, Poller (nur Abfrage-Kollision)
	RoofAntennas,   // Antennenwald (Lantern Hill)
	Lines,          // Waescheleinen, Kabel, Gelaender
	Cloth,          // Waesche
	Signage,        // Plakate, Graffiti, Werbetafeln, Schilder (nah)
	SignageFar,     // Aegis-Logos an Tuermen (weit)
	Scrap,          // Schrotthaufen
	PalmTrunks,     // Promenaden-Palmen
	PalmCrowns,
	WheelFrame,     // Riesenrad (beweglich, dreht sich)
	WheelNeon,
	WheelCabins,
	DoorSigns,      // Eingangsmarkierungen der begehbaren Gebaeude (Laufzeit)
	Count UMETA(Hidden)
};

/** Aegis-Kameramast (Datenbasis fuer die spaetere Kamera-Funktion, Weltkonzept 8.2a). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAAegisCameraPoint
{
	GENERATED_BODY()

	/** Position der Linse (Welt). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aegis")
	FVector Location = FVector::ZeroVector;

	/** Blickrichtung der Kamera (auf die Kreuzungsmitte bzw. Torzufahrt). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aegis")
	FRotator Facing = FRotator::ZeroRotator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aegis")
	EMTADistrict District = EMTADistrict::Core;

	/** Kreuzung (Strassenindex X/Y); (-1,-1) = keine Kreuzung (Tor, Hafen, Landmarke). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aegis")
	FIntPoint Intersection = FIntPoint(-1, -1);

	/** Vom Collective gehackt/verdeckt (Lantern Hill): Linse dunkel, erfasst nichts. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aegis")
	bool bHacked = false;

	/** Erfassungsreichweite (cm). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aegis")
	float Range = 4500.f;

	/** Art: "crossing", "gate", "harbor", "landmark". */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aegis")
	FName Kind;
};

/** Landmarke (fuer Selbsttest, Karte, Orientierung). */
USTRUCT(BlueprintType)
struct MTA6_API FMTALandmarkInfo
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landmark")
	FName Id;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landmark")
	FString DisplayName;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landmark")
	EMTADistrict District = EMTADistrict::Core;

	/** Fusspunkt (Welt). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landmark")
	FVector Location = FVector::ZeroVector;

	/** Hoehe ueber Grund (cm). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Landmark")
	float Height = 0.f;
};

/** Werbetafel mit Spruch (Text erscheint nur in der Naehe, siehe TextCullDistance). */
USTRUCT(BlueprintType)
struct MTA6_API FMTABillboardSpot
{
	GENERATED_BODY()

	/** Mitte der Bildflaeche (Welt). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Billboard")
	FVector Location = FVector::ZeroVector;

	/** Normale der Bildflaeche (zeigt zum Betrachter). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Billboard")
	FVector Facing = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Billboard")
	FVector2D PanelSize = FVector2D(1200.f, 500.f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Billboard")
	FString Slogan;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Billboard")
	FLinearColor TextColor = FLinearColor::White;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Billboard")
	EMTADistrict District = EMTADistrict::Core;
};

/** Geplanter Standort eines begehbaren Gebaeudes (Parzelle des Generators + Raumtyp). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAEnterableSite
{
	GENERATED_BODY()

	/** Bodenmitte der (Teil-)Parzelle (Welt). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enterable")
	FVector LotCenter = FVector::ZeroVector;

	/** Grundflaeche X/Y in Weltachsen (cm). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enterable")
	FVector2D LotSize = FVector2D(2400.f, 2400.f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enterable")
	float Height = 1200.f;

	/** Richtung zur Strasse (Welt, Einheitsvektor). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enterable")
	FVector StreetDir = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enterable")
	EMTADistrict District = EMTADistrict::Core;

	/** Raumtyp aus Content/Data/interiors.json (z. B. bar_club). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enterable")
	FName RoomType;

	/** Schildtext an der Tuer (KIOSK, BAR, WERKSTATT ...). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enterable")
	FString SignLabel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enterable")
	FLinearColor SignColor = FLinearColor::White;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enterable")
	int32 Seed = 0;
};

/** Zaehler der Moeblierung (Selbsttest, Budget). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAFurnitureStats
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 TrafficLights = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 StopSigns = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 StreetSigns = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 CrosswalkSigns = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 Hydrants = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 TrashBins = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 Benches = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 Bollards = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 GuardrailSegments = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 Billboards = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 AegisCameras = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 PostersAndGraffiti = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 NeonSigns = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 Containers = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 MountainPeaks = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 WaterTiles = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stats") int32 QuayPieces = 0;
};

/** Indizes der Per-Instance-Custom-Data aller Detail-Ebenen (muss zu tools/ue/build_materials.py passen). */
namespace MTADetailData
{
	inline constexpr int32 R = 0;
	inline constexpr int32 G = 1;
	inline constexpr int32 B = 2;
	inline constexpr int32 P0 = 3; // Beton: Verschleiss | Metall: Rost | Neon: Intensitaet | Schild: Art | Wasser: Kueste Y | Berg: Schneegrenze m | Strasse: Laenge
	inline constexpr int32 P1 = 4; // Beton: Muster | Metall: Metallic | Neon: Flackern (2 = Blinken) | Schild: Seed | Wasser: Ursprung X | Berg: Dunst | Strasse: Breite
	inline constexpr int32 P2 = 5; // Beton: -- | Metall: Streifen | Neon: Tagesanteil | Schild: Leuchten | Wasser: Landzungen-Start | Strasse: Art
	inline constexpr int32 P3 = 6; // Wasser: Landzungen-Steigung
	inline constexpr int32 Num = 7;
}

/**
 * Welt-Details fuer Kestrel Bay: Kueste und Meer im Westen, Berge am Horizont, Landmarken je Bezirk,
 * Strassenmoeblierung (HISM), Aegis-Kameramasten, Werbetafeln, begehbare Gebaeude.
 * Liest die Parzellen und Kuestenlinie aus AMTACityGenerator und erzeugt alles deterministisch.
 * Statische Teile entstehen in OnConstruction (Editor) bzw. BeginPlay (falls nicht gespeichert);
 * Texte, Riesenrad-Drehung und begehbare Gebaeude nur zur Laufzeit.
 */
UCLASS(Blueprintable, meta = (DisplayName = "MTA World Detail"))
class MTA6_API AMTAWorldDetail : public AActor
{
	GENERATED_BODY()

public:
	AMTAWorldDetail();

	virtual void Tick(float DeltaSeconds) override;

	// ---------------------------------------------------------------- Aktionen

	/** Alle statischen Details neu erzeugen (braucht einen generierten Stadtgenerator). */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "MTA|WorldDetail")
	void Generate();

	/** Alle Instanzen, Texte und Laufzeit-Daten entfernen (begehbare Gebaeude bleiben). */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "MTA|WorldDetail")
	void Clear();

	/**
	 * Begehbare Gebaeude auf den geplanten Parzellen erzeugen (nur in der Spielwelt), inkl. Tuermarkierung
	 * und Schild. Ersetzt AMTA6GameMode::SpawnEnterableBuildings. Gibt die Anzahl erzeugter Gebaeude zurueck.
	 * Mehrfacher Aufruf entfernt zuvor erzeugte Gebaeude dieses Aktors.
	 */
	UFUNCTION(BlueprintCallable, Category = "MTA|WorldDetail")
	int32 SpawnEnterableBuildings(int32 MaxCount = 72);

	/** Selbsttest ohne Eingabe: Wasser, Berge, Landmarken je Bezirk, Moeblierung, Gebaeude, Budget. */
	UFUNCTION(BlueprintCallable, Category = "MTA|WorldDetail")
	FString RunWorldSelfTest() const;

	/** Ersten AMTAWorldDetail der Welt finden (nullptr, falls keiner). */
	UFUNCTION(BlueprintPure, Category = "MTA|WorldDetail", meta = (WorldContext = "WorldContextObject"))
	static AMTAWorldDetail* FindWorldDetail(const UObject* WorldContextObject);

	/** Vorhandenen AMTAWorldDetail liefern oder (nur Spielwelt) am Stadtgenerator erzeugen. */
	UFUNCTION(BlueprintCallable, Category = "MTA|WorldDetail", meta = (WorldContext = "WorldContextObject"))
	static AMTAWorldDetail* FindOrSpawnWorldDetail(UObject* WorldContextObject);

	// ---------------------------------------------------------------- Abfragen

	/** Alle Aegis-Kameramasten (Welt). */
	UFUNCTION(BlueprintPure, Category = "MTA|WorldDetail|Aegis")
	const TArray<FMTAAegisCameraPoint>& GetAegisCameraPoints() const { return AegisCameras; }

	/** Aegis-Kameras im Umkreis (cm) um Location; bIncludeHacked = auch gehackte. */
	UFUNCTION(BlueprintCallable, Category = "MTA|WorldDetail|Aegis")
	TArray<FMTAAegisCameraPoint> GetAegisCamerasNear(const FVector& Location, float Radius, bool bIncludeHacked = false) const;

	UFUNCTION(BlueprintPure, Category = "MTA|WorldDetail|Landmarks")
	const TArray<FMTALandmarkInfo>& GetLandmarks() const { return Landmarks; }

	/** Anzahl Landmarken eines Bezirks. */
	UFUNCTION(BlueprintPure, Category = "MTA|WorldDetail|Landmarks")
	int32 GetLandmarkCount(EMTADistrict District) const;

	UFUNCTION(BlueprintPure, Category = "MTA|WorldDetail|Enterable")
	const TArray<FMTAEnterableSite>& GetEnterableSites() const { return EnterableSites; }

	/** Anzahl der aktuell lebenden, von diesem Aktor erzeugten begehbaren Gebaeude. */
	UFUNCTION(BlueprintPure, Category = "MTA|WorldDetail|Enterable")
	int32 GetSpawnedEntryCount() const;

	UFUNCTION(BlueprintPure, Category = "MTA|WorldDetail")
	int32 GetLayerInstanceCount(EMTADetailLayer Layer) const;

	/** Summe aller Instanzen (inkl. Wasser). */
	UFUNCTION(BlueprintPure, Category = "MTA|WorldDetail")
	int32 GetTotalInstanceCount() const;

	/** Wasserspiegel (Welt-Z). */
	UFUNCTION(BlueprintPure, Category = "MTA|WorldDetail|Coast")
	float GetWaterLevel() const;

	// ---------------------------------------------------------------- Parameter

	/** Stadtgenerator; leer = erster in der Welt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldDetail")
	TObjectPtr<AMTACityGenerator> City;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail")
	int32 Seed = 6;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Generation")
	bool bGenerateOnConstruction = true;

	/** In BeginPlay neu erzeugen, wenn nichts gespeichert ist oder sich die Stadt geaendert hat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Generation")
	bool bRegenerateIfStale = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Parts") bool bBuildSea = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Parts") bool bBuildMountains = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Parts") bool bBuildLandmarks = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Parts") bool bBuildFurniture = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Parts") bool bBuildFacadeDetails = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Parts") bool bAnimateWheel = true;

	/** Wasserspiegel relativ zum Stadtboden (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Coast")
	float WaterLevel = -150.f;

	/** Meeresgrund relativ zum Stadtboden (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Coast")
	float SeabedLevel = -900.f;

	/** Kachelgroesse der nahen Wasserflaeche (cm); bestimmt die Aufloesung der WPO-Wellen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Coast", meta = (ClampMin = "500"))
	float WaterTileSize = 2000.f;

	/** Bis zu dieser Entfernung westlich der Kueste feine Wasserkacheln, danach grobe (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Coast", meta = (ClampMin = "10000"))
	float FineWaterReach = 120000.f;

	/** Aeussere Grenze der Meeresflaeche (cm vom Stadtzentrum). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Coast", meta = (ClampMin = "100000"))
	float SeaExtent = 600000.f;

	/** Anzahl begehbarer Standorte, die Generate plant (SpawnEnterableBuildings nutzt die ersten MaxCount). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Enterable", meta = (ClampMin = "0", ClampMax = "300"))
	int32 PlannedEnterableSites = 84;

	/** Instanz-Budget aller Ebenen ohne Wasser (Selbsttest). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Budget")
	int32 InstanceBudget = 30000;

	/** Budget fuer Wasserkacheln (Selbsttest). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Budget")
	int32 WaterTileBudget = 20000;

	/** Budget fuer Komponenten dieses Aktors (HISM + Texte + Pivot). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Budget")
	int32 ComponentBudget = 260;

	/** Sichtweite der Werbe-/Schildtexte (cm). Texte sind einzelne Komponenten, daher kurz halten. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Rendering", meta = (ClampMin = "500"))
	float TextCullDistance = 6000.f;

	/** Kleine Moeblierung wirft Schatten (aus = spart VSM-Seiten auf der 5700 XT). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Rendering")
	bool bSmallPropsCastShadow = false;

	/** Multiplikator auf alle Moeblierungs-Sichtweiten (Qualitaetsstufen). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Rendering", meta = (ClampMin = "0.25", ClampMax = "4"))
	float CullDistanceScale = 1.f;

	/** Grad je Sekunde des Riesenrads. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Landmarks")
	float WheelDegreesPerSecond = 1.5f;

	// ---------------------------------------------------------------- Materialien (leer = /Game/MTA6/Materials/...)

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Assets") TObjectPtr<UMaterialInterface> WaterMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Assets") TObjectPtr<UMaterialInterface> ConcreteMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Assets") TObjectPtr<UMaterialInterface> PaintedMetalMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Assets") TObjectPtr<UMaterialInterface> NeonMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Assets") TObjectPtr<UMaterialInterface> SignageMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Assets") TObjectPtr<UMaterialInterface> MountainMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Assets") TObjectPtr<UMaterialInterface> RoadMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Assets") TObjectPtr<UMaterialInterface> TrunkMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Assets") TObjectPtr<UMaterialInterface> FoliageMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Assets") TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Assets") TObjectPtr<UStaticMesh> CylinderMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Assets") TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Assets") TObjectPtr<UStaticMesh> ConeMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldDetail|Assets") TObjectPtr<UStaticMesh> PlaneMesh;

	// ---------------------------------------------------------------- Ergebnisse (gespeichert)

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "WorldDetail|Result")
	TArray<FMTAAegisCameraPoint> AegisCameras;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "WorldDetail|Result")
	TArray<FMTALandmarkInfo> Landmarks;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "WorldDetail|Result", AdvancedDisplay)
	TArray<FMTABillboardSpot> Billboards;

	/** Hinweisschilder mit Text (Tore, Tunnel), Text nur nahe. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "WorldDetail|Result", AdvancedDisplay)
	TArray<FMTABillboardSpot> SignTexts;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "WorldDetail|Result", AdvancedDisplay)
	TArray<FMTAEnterableSite> EnterableSites;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "WorldDetail|Result")
	FMTAFurnitureStats Stats;

	/** Fingerabdruck der Stadt bei der letzten Generierung (Seed, Parzellen) zur Erkennung veralteter Daten. */
	UPROPERTY(VisibleInstanceOnly, Category = "WorldDetail|Result", AdvancedDisplay)
	int32 BuiltForCityHash = 0;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, Category = "WorldDetail|Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Drehpunkt des Riesenrads (beweglich). */
	UPROPERTY(VisibleAnywhere, Category = "WorldDetail|Components")
	TObjectPtr<USceneComponent> WheelPivot;

	/** Eine HISM je EMTADetailLayer (Index = Ebene). */
	UPROPERTY(VisibleAnywhere, Category = "WorldDetail|Components")
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> DetailLayers;

	/** Laufzeit-Texte der Werbetafeln und Hinweisschilder, nicht gespeichert. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> RuntimeTexts;

	/** Laufzeit-Texte der Tuerschilder (gehoeren zu SpawnEnterableBuildings). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> DoorTexts;

	/** Von SpawnEnterableBuildings erzeugte Gebaeude. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AMTABuildingEntry>> SpawnedEntries;

private:
	/** Feste Eigenschaften einer Ebene. */
	struct FLayerSpec
	{
		const TCHAR* Name;
		int32 Mesh;        // 0 Wuerfel, 1 Zylinder, 2 Kugel, 3 Kegel, 4 Ebene
		int32 Material;    // EMaterialSlot
		int32 Collision;   // 0 keine, 1 nur Abfrage (Fussgaenger), 2 voll
		bool bShadow;
		bool bSmallProp;   // Schatten folgt bSmallPropsCastShadow
		int32 CullEnd;     // cm, 0 = unbegrenzt (wird mit CullDistanceScale skaliert)
		bool bDistanceField;
		bool bMovable;     // haengt am WheelPivot
	};
	static const FLayerSpec& GetLayerSpec(int32 Layer);

	/** Sammelpuffer je Ebene. */
	struct FBatch
	{
		TArray<FTransform> Transforms;
		TArray<float> Data;
	};

	/** Laufzeitdaten einer Generierung (alles in Weltkoordinaten ausser den Riesenrad-Ebenen). */
	struct FDetailContext
	{
		FBatch Batches[static_cast<int32>(EMTADetailLayer::Count)];
		FRandomStream Rng;
		AMTACityGenerator* CityGen = nullptr;
		FVector Origin = FVector::ZeroVector;
		float HalfX = 32000.f;
		float HalfY = 32000.f;
		float RoadW = 1600.f;
		float Curb = 15.f;
		float Period = 8000.f;
		int32 BlocksX = 8;
		int32 BlocksY = 8;
		float GroundHalf = 200000.f;
		/** Kuestenabschnitte (lokales X) von Saltmarket und Sunstrip. */
		float SaltX0 = -16000.f;
		float SaltX1 = 8000.f;
		float SunX0 = 8000.f;
		float SunX1 = 32000.f;
		/** Route 9 (lokales X) und Tunnelportal (lokales Y). */
		float Route9X = 0.f;
		float TunnelY = 144000.f;
		TArray<FMTADistrictProfile> Profiles;
	};

	// Generierungsschritte
	void BuildSea(FDetailContext& Ctx);
	void BuildCoastRoads(FDetailContext& Ctx);
	void BuildMountains(FDetailContext& Ctx);
	void BuildHarbor(FDetailContext& Ctx);
	void BuildSunstrip(FDetailContext& Ctx);
	void BuildCoreTowers(FDetailContext& Ctx);
	void BuildLanternHill(FDetailContext& Ctx);
	void BuildGreenmoor(FDetailContext& Ctx);
	void BuildRustline(FDetailContext& Ctx);
	void BuildIntersections(FDetailContext& Ctx);
	void BuildSidewalkFurniture(FDetailContext& Ctx);
	void BuildFacadeDetails(FDetailContext& Ctx);
	void PlanEnterableSites(FDetailContext& Ctx);

	// Bausteine
	void AddInst(FDetailContext& Ctx, EMTADetailLayer Layer, const FTransform& T, const float* Data) const;
	void AddBox(FDetailContext& Ctx, EMTADetailLayer Layer, const FVector& Center, const FVector& Size, const FRotator& Rot, const float* Data) const;
	/** Zylinder/Kegel: Fusspunkt + Durchmesser + Hoehe (stehend). */
	void AddColumn(FDetailContext& Ctx, EMTADetailLayer Layer, const FVector& Base, float Diameter, float Height, const float* Data) const;
	/** Quader zwischen zwei Punkten (Traeger, Ausleger, Speichen). */
	void AddBeam(FDetailContext& Ctx, EMTADetailLayer Layer, const FVector& A, const FVector& B, float Thick, float Width, const float* Data) const;
	/** Zylinder zwischen zwei Punkten (Leinen, Rohre). */
	void AddRod(FDetailContext& Ctx, EMTADetailLayer Layer, const FVector& A, const FVector& B, float Diameter, const float* Data) const;
	void AddLandmark(FName Id, const FString& DisplayName, EMTADistrict District, const FVector& Location, float Height);
	void AddAegisCamera(FDetailContext& Ctx, const FVector& Foot, const FVector& LookAt, EMTADistrict District, const FIntPoint& Crossing, FName Kind, bool bHacked);
	void AddBillboard(FDetailContext& Ctx, const FVector& PanelCenter, const FVector& Facing, float Width, float Height, EMTADistrict District, bool bOnPosts, float PostBaseZ);
	void AddGuardrail(FDetailContext& Ctx, const FVector& A, const FVector& B);
	void AddScrapHeap(FDetailContext& Ctx, const FVector& Center, float Radius, int32 Pieces);
	void AddPalm(FDetailContext& Ctx, const FVector& Foot, float Scale);

	// Hilfen
	FVector CityToWorld(const FDetailContext& Ctx, float LX, float LY, float Z) const;
	float CoastLocal(const FDetailContext& Ctx, float LX, bool bStepped) const;
	EMTADistrict DistrictAtLocal(const FDetailContext& Ctx, float LX, float LY) const;
	EMTADistrict IntersectionDistrict(const FDetailContext& Ctx, int32 IX, int32 IY) const;
	const FMTADistrictProfile& ProfileOf(const FDetailContext& Ctx, EMTADistrict District) const;
	FString PickSlogan(EMTADistrict District, int32 Salt) const;
	UMaterialInterface* ResolveLayerMaterial(int32 Slot) const;
	UStaticMesh* ResolveLayerMesh(int32 Mesh) const;
	void ConfigureLayers();
	void ApplyBatches(FDetailContext& Ctx);
	AMTACityGenerator* ResolveCity() const;
	int32 ComputeCityHash(const AMTACityGenerator* Gen) const;
	bool IsBuilt() const;

	// Laufzeit
	void SetupRuntime();
	void ClearRuntimeTexts();
	UTextRenderComponent* AddRuntimeText(const FString& Text, const FVector& Location, const FRotator& Rotation, float WorldSize, const FColor& Color);
	void DestroySpawnedEntries();

	/** Riesenrad-Nabe (Welt) fuer die Laufzeit-Drehung. */
	UPROPERTY()
	FVector WheelHub = FVector::ZeroVector;
};
