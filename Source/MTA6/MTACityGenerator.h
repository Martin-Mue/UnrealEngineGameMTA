#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MTADistrict.h"
// Blocks, Period, Half, LaneOffset und RoadCenter kommen aus MTATrafficTypes.h (einzige Quelle).
#include "MTATrafficTypes.h"
#include "MTACityGenerator.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class USpotLightComponent;
class UStaticMesh;
class UMaterialInterface;
class UCurveFloat;

/**
 * Feste Konstanten des Stadtrasters (Standardwerte des Generators, identisch zum Godot-Prototyp
 * mal 100: Periode 80 m, Strasse 16 m, 8x8 Bloecke, Mitte am Ursprung).
 * Die KI-Gruppe kann diese Werte ohne Instanz des Generators verwenden.
 * Achsen: +X = Nord, +Y = Ost (Meer im Westen = -Y). Strassenmitte R(k) = k * 8000 - 32000.
 */
namespace MTACity
{
	inline constexpr float RoadWidth = 1600.f;
	inline constexpr float HalfExtent = Half;                      // 32000 cm
	inline constexpr int32 RoadCount = Blocks + 1;                 // 9 Strassen je Achse

	/** Mittelpunkt-Koordinate des Blocks i (zwischen Strasse i und i+1). */
	FORCEINLINE float BlockCenter(int32 I) { return RoadCenter(I) + Period * 0.5f; }
}

/** Wie Bloecke ihren Bezirk bekommen. */
UENUM(BlueprintType)
enum class EMTADistrictMode : uint8
{
	/** Feste Rastervorgabe nach der Schemakarte (Weltkonzept 8.1). */
	Layout,
	/** Rastervorgabe, deren Grenzen per Perlin-Noise verwischt werden (seedabhaengig). */
	NoisyLayout,
	/** Alle Bloecke bekommen denselben Bezirk (UniformDistrict), z. B. fuer Look-Dev. */
	Uniform
};

/** Indizes der Per-Instance-Custom-Data der Gebaeude (muss zu tools/ue/build_materials.py passen). */
namespace MTABuildingData
{
	inline constexpr int32 TintR = 0;
	inline constexpr int32 TintG = 1;
	inline constexpr int32 TintB = 2;
	inline constexpr int32 HeightMeters = 3;
	inline constexpr int32 WindowSeed = 4;
	inline constexpr int32 Wear = 5;
	inline constexpr int32 LitRatio = 6;
	inline constexpr int32 Style = 7;      // EMTABuildingStyle als float
	inline constexpr int32 District = 8;   // EMTADistrict als float (bezirkstypisches Fassadendetail)
	inline constexpr int32 Neon = 9;       // Staerke des Neon-Ladenbands 0..1 (0 = keins)
	inline constexpr int32 Detail = 10;    // Fassadendetail 0..1 (Balkone, Fugen, Rippen, Laeden)
	inline constexpr int32 AccentR = 11;   // Neon-Akzentfarbe RGB (11..13)
	inline constexpr int32 AccentG = 12;
	inline constexpr int32 AccentB = 13;
	inline constexpr int32 Num = 14;
}

/**
 * Parzelle eines vom Generator erzeugten Gebaeudes (Ergebnis, Weltkoordinaten).
 * Grundlage fuer AMTAWorldDetail (Leuchtschilder, Graffiti, Werbetafeln, Eingaenge).
 */
USTRUCT(BlueprintType)
struct MTA6_API FMTACityLot
{
	GENERATED_BODY()

	/** Mittelpunkt (Welt X/Y). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lot")
	FVector2D Center = FVector2D::ZeroVector;

	/** Grundflaeche X/Y (cm), achsparallel. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lot")
	FVector2D Size = FVector2D::ZeroVector;

	/** Welt-Z des Sockels (Gehweg-Oberkante). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lot")
	float BaseZ = 0.f;

	/** Hoehe des Hauptkoerpers (cm). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lot")
	float Height = 0.f;

	/** Hoechster Punkt inkl. Aufsatz/Dach (Welt-Z). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lot")
	float RoofTopZ = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lot")
	EMTADistrict District = EMTADistrict::Core;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lot")
	EMTABuildingStyle Style = EMTABuildingStyle::Plaster;

	/** Block (X = Index entlang X, Y = entlang Y). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lot")
	FIntPoint Block = FIntPoint::ZeroValue;

	/** Einheitsachse zur Strasse (Fassaden-/Eingangsseite). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lot")
	FVector2D StreetDir = FVector2D(1.0, 0.0);

	/** Zweite Strassenseite bei Eckparzellen (sonst Nullvektor). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lot")
	FVector2D SecondStreetDir = FVector2D::ZeroVector;

	/** false = Innenparzelle ohne Strassenfront (Mitte eines 3x3-Blocks). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lot")
	bool bHasFrontage = true;

	/** Leere Parzelle (Brache, Garten); Size/Center gelten trotzdem. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lot")
	bool bEmpty = false;

	/** Flachdach (Werbetafeln, Antennen moeglich). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lot")
	bool bFlatRoof = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lot")
	bool bIndustrial = false;

	/** Hochhaus mit zurueckgesetztem Obergeschoss. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lot")
	bool bTower = false;

	/** Deterministischer Seed der Parzelle. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Lot")
	int32 Seed = 0;
};

/** Custom-Data fuer Laternenkoepfe, Baumkronen, Parks, Gehwege: RGB-Farbe + Bezirk + Verschleiss. */
namespace MTAPropData
{
	inline constexpr int32 ColorR = 0;
	inline constexpr int32 ColorG = 1;
	inline constexpr int32 ColorB = 2;
	inline constexpr int32 District = 3;
	inline constexpr int32 Wear = 4;
	inline constexpr int32 Num = 5;
}

/**
 * Prozeduraler Stadtgenerator fuer Kestrel Bay.
 * Erzeugt deterministisch aus einem Seed Boden, Gehwege, Gebaeude (mit Kollision),
 * Dachaufbauten, Antennen, Schornsteine, Parks, Baeume und Strassenlaternen als HISM-Instanzen.
 * Die Generierung laeuft in OnConstruction (Editor) und in BeginPlay, falls noch nichts erzeugt ist.
 */
UCLASS(Blueprintable, meta = (DisplayName = "MTA City Generator"))
class MTA6_API AMTACityGenerator : public AActor
{
	GENERATED_BODY()

public:
	AMTACityGenerator();

	// ---------------------------------------------------------------- Aktionen

	/** Stadt komplett neu erzeugen (deterministisch aus Seed und Parametern). */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "MTA|City")
	void Generate();

	/** Alle erzeugten Instanzen und Laternenlichter entfernen. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "MTA|City")
	void Clear();

	/**
	 * Verschiebt den kleinen Pool echter Laternenlichter zu den Laternen, die ViewLocation am
	 * naechsten sind, und setzt ihre Helligkeit (NightAlpha 0 = Tag, 1 = Nacht).
	 * Wird von AMTATimeOfDay periodisch aufgerufen.
	 */
	UFUNCTION(BlueprintCallable, Category = "MTA|City|Lights")
	void UpdateLampLights(const FVector& ViewLocation, float NightAlpha);

	// ---------------------------------------------------------------- Strassennetz (fuer KI)

	/** Strassenmitte k nach den festen Standardkonstanten (MTACity), ohne Instanz nutzbar. */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Roads")
	static float GetRoadCenter(int32 Index);

	/** Standardkonstanten des Rasters (Blockzahl, Periode, Strassenbreite in cm). */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Roads")
	static void GetCityConstants(int32& OutBlocks, float& OutPeriod, float& OutRoadWidth, float& OutLaneOffset);

	/** Ersten Stadtgenerator in der Welt finden (nullptr, falls keiner existiert). */
	UFUNCTION(BlueprintPure, Category = "MTA|City", meta = (WorldContext = "WorldContextObject"))
	static AMTACityGenerator* FindCityGenerator(const UObject* WorldContextObject);

	/** Anzahl der Strassen je Achse (= Bloecke + 1). Axis 0 = X-Richtung (Bloecke entlang X). */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Roads")
	int32 GetRoadCount(int32 Axis = 0) const;

	/**
	 * Lokale Koordinate der Strassenmitte Index auf der angegebenen Achse
	 * (Axis 0: X-Koordinate der Nord-Sued-Strasse, Axis 1: Y-Koordinate der Ost-West-Strasse).
	 */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Roads")
	float GetRoadCoordinate(int32 Axis, int32 Index) const;

	/** Welt-Position der Kreuzung (Strasse IX auf X-Achse, Strasse IY auf Y-Achse), Z = Boden. */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Roads")
	FVector GetIntersectionLocation(int32 IX, int32 IY) const;

	/** Welt-Position der Blockmitte (Block BX, BY), Z = Gehweg-Oberkante. */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Roads")
	FVector GetBlockCenterLocation(int32 BX, int32 BY) const;

	/**
	 * Koordinate der rechten Fahrspur (Rechtsverkehr) quer zur Fahrtrichtung, in Weltkoordinaten.
	 * RunAxis 0 = Strasse verlaeuft entlang X (konstantes Y), RunAxis 1 = entlang Y (konstantes X).
	 * Direction +1/-1 = Fahrt in positive/negative Achsrichtung. LaneIndex 0 = innere Spur.
	 */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Roads")
	float GetLaneCoordinate(int32 RunAxis, int32 RoadIndex, int32 Direction, int32 LaneIndex = 0) const;

	/** Index der naechstgelegenen Strasse fuer eine Welt-Koordinate auf der Achse (0 = X, 1 = Y). */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Roads")
	int32 GetNearestRoadIndex(int32 Axis, float WorldCoordinate) const;

	/** true, wenn der Punkt auf einer Fahrbahn (nicht Gehweg/Block) liegt. */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Roads")
	bool IsOnRoad(const FVector& WorldLocation) const;

	/** Bezirk des Blocks (Clamp an den Rand). */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Districts")
	EMTADistrict GetBlockDistrict(int32 BX, int32 BY) const;

	/** Bezirk an einer Welt-Position (naechster Block). */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Districts")
	EMTADistrict GetDistrictAtLocation(const FVector& WorldLocation) const;

	/** Aktives Profil eines Bezirks (Generator-Einstellungen oder Standard). */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Districts")
	FMTADistrictProfile GetDistrictProfile(EMTADistrict District) const;

	/** Fusspunkte aller Laternen (Welt). */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Lights")
	const TArray<FVector>& GetLampPositions() const { return LampPositions; }

	// ---------------------------------------------------------------- Parzellen, Plaetze, Kueste (fuer AMTAWorldDetail)

	/** Alle Gebaeude-Parzellen der letzten Generierung (Weltkoordinaten). */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Lots")
	const TArray<FMTACityLot>& GetCityLots() const { return CityLots; }

	/** Halbe Ausdehnung des Blockrasters (lokal, cm; ohne Strassenrand). */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Layout")
	FVector2D GetCityHalfExtent() const;

	/** Halbe Kantenlaenge der tatsaechlich belegten Bodenflaeche (ganze Kacheln, cm). */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Ground")
	float GetGroundHalfSize() const;

	/**
	 * Kuestenlinie (lokales Y) an der lokalen X-Position. Meer liegt westlich (kleineres Y).
	 * bStepped = Wert des Bodenstreifens, wie BuildGround ihn abschneidet (Kaimauern folgen dieser Linie).
	 */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Coast")
	float GetCoastLocalY(float LocalX, bool bStepped = false) const;

	/** Wie GetCoastLocalY, aber in Weltkoordinaten. */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Coast")
	float GetCoastWorldY(float WorldX, bool bStepped = false) const;

	/** Mitte und Innenmass (cm) des reservierten Platzes eines Bezirks (Landmarke). false = keiner. */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Districts")
	bool GetDistrictPlaza(EMTADistrict District, FVector& OutCenter, float& OutInnerSize) const;

	/** Fusspunkt des Aurelia Tower (Welt). false, wenn keine Landmarke erzeugt wurde. */
	UFUNCTION(BlueprintPure, Category = "MTA|City|Districts")
	bool GetLandmarkTowerLocation(FVector& OutBase) const;

	// ---------------------------------------------------------------- Parameter: Raster

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout")
	int32 Seed = 6;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (ClampMin = "1", ClampMax = "64"))
	int32 BlocksX = MTACity::Blocks;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (ClampMin = "1", ClampMax = "64"))
	int32 BlocksY = MTACity::Blocks;

	/** Rasterperiode Strassenmitte zu Strassenmitte (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (ClampMin = "2000"))
	float BlockPeriod = MTACity::Period;

	/** Fahrbahnbreite (cm), Bordstein zu Bordstein. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (ClampMin = "400"))
	float RoadWidth = MTACity::RoadWidth;

	/** Bordsteinhoehe = Hoehe der Gehweg-Platten (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (ClampMin = "0", ClampMax = "40"))
	float CurbHeight = 15.f;

	/** Mindestbreite des Gehwegs zwischen Bordstein und Fassade (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (ClampMin = "100"))
	float SidewalkWidth = 500.f;

	/** Globaler Faktor auf die Park-Wahrscheinlichkeit der Bezirke (1 = Bezirkswerte). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (ClampMin = "0", ClampMax = "5"))
	float ParkShare = 1.f;

	/** Baeume je Parkblock. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (ClampMin = "0", ClampMax = "128"))
	int32 TreesPerPark = 16;

	/** Landmarke Aurelia Tower im Core-Block naechst DowntownCenter erzeugen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout")
	bool bSpawnLandmarks = true;

	/** Je Bezirk aus PlazaDistricts einen Block als Platz freihalten (Landmarken von AMTAWorldDetail). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout")
	bool bReserveDistrictPlazas = true;

	/** Bezirke mit reserviertem Platz (Standard: Lantern Hill = Glockenturm, Greenmoor = Wasserturm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (EditCondition = "bReserveDistrictPlazas"))
	TArray<EMTADistrict> PlazaDistricts;

	// ---------------------------------------------------------------- Parameter: Kueste

	/** Boden westlich der Kuestenlinie weglassen (dort liegt das Meer von AMTAWorldDetail). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Coast")
	bool bGenerateCoast = true;

	/** Abstand Blockraster-Westrand -> Kaimauer (cm). Standard 80 m = Promenade/Kai-Streifen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Coast", meta = (ClampMin = "1000"))
	float CoastOffset = 8000.f;

	/** Ab diesem Nord/Sued-Abstand (lokales |X|, cm) biegt die Kueste zu Landzungen nach Westen (Bucht). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Coast", meta = (ClampMin = "0"))
	float BayHeadlandStart = 52000.f;

	/** Steigung der Landzungen (cm West je cm Nord/Sued). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Coast", meta = (ClampMin = "0", ClampMax = "3"))
	float BayHeadlandSlope = 0.55f;

	/** Breite der Bodenstreifen an der Kueste (cm); kleinere Werte = feinere Kuestenlinie. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Coast", meta = (ClampMin = "500"))
	float CoastStripWidth = 2000.f;

	// ---------------------------------------------------------------- Parameter: Hoehenprofil

	/** Downtown-Zentrum (lokal, cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Height")
	FVector2D DowntownCenter = FVector2D::ZeroVector;

	/** Radius, ab dem das Downtown-Profil auf den Aussenwert gefallen ist (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Height", meta = (ClampMin = "1000"))
	float DowntownRadius = 30000.f;

	/** Hoehenfaktor im Zentrum (multipliziert die Bezirkshoehe). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Height", meta = (ClampMin = "0.1", ClampMax = "4"))
	float DowntownPeakScale = 1.25f;

	/** Hoehenfaktor am Rand des Downtown-Radius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Height", meta = (ClampMin = "0.1", ClampMax = "4"))
	float OutskirtsScale = 0.6f;

	/** Exponent der Abnahme (1 = linear, 2 = Zentrum ausgepraegter). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Height", meta = (ClampMin = "0.1", ClampMax = "8"))
	float HeightFalloffExponent = 1.5f;

	/** Optional: Kurve X = normierter Abstand 0..1, Y = Profil 0..1 (ersetzt den Exponenten). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Height")
	TObjectPtr<UCurveFloat> HeightProfileCurve;

	/** Geschosshoehe (cm); Gebaeudehoehen werden darauf gerundet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Height", meta = (ClampMin = "250"))
	float FloorHeight = 350.f;

	/** Obergrenze aller Gebaeudehoehen (ohne Landmarke), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Height", meta = (ClampMin = "500"))
	float MaxBuildingHeight = 20000.f;

	// ---------------------------------------------------------------- Parameter: Bezirke

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Districts")
	EMTADistrictMode DistrictMode = EMTADistrictMode::Layout;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Districts", meta = (EditCondition = "DistrictMode == EMTADistrictMode::Uniform"))
	EMTADistrict UniformDistrict = EMTADistrict::Core;

	/** Staerke der Grenzverwischung im NoisyLayout-Modus (Anteil der Kartenbreite). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Districts", meta = (ClampMin = "0", ClampMax = "0.5"))
	float DistrictNoiseStrength = 0.12f;

	/** Frequenz des Grenz-Noise (Wellen je Kartenbreite). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Districts", meta = (ClampMin = "0.1", ClampMax = "16"))
	float DistrictNoiseFrequency = 2.5f;

	/** Feste Zuweisungen einzelner Bloecke (X = Blockindex entlang X, Y = entlang Y). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Districts")
	TMap<FIntPoint, EMTADistrict> DistrictOverrides;

	/**
	 * Bezirksprofile, Index = EMTADistrict. Leere Liste oder fehlende Eintraege werden mit den
	 * Standardprofilen aus MTADistrict::MakeDefaultProfile aufgefuellt.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Districts", meta = (TitleProperty = "DisplayName"))
	TArray<FMTADistrictProfile> DistrictProfiles;

	// ---------------------------------------------------------------- Parameter: Meshes/Materialien

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Assets")
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Assets")
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Assets")
	TObjectPtr<UStaticMesh> SphereMesh;

	/** Fassaden (Fensterraster, Custom-Data). Leer = /Game/MTA6/Materials/M_MTA_Building. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Assets")
	TObjectPtr<UMaterialInterface> BuildingMaterial;

	/** Boden/Strasse (world-aligned Markierungen). Leer = M_MTA_Ground. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Assets")
	TObjectPtr<UMaterialInterface> GroundMaterial;

	/** Gehwegplatten. Leer = GroundMaterial (das Bodenmaterial zeichnet Gehwege selbst). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Assets")
	TObjectPtr<UMaterialInterface> SidewalkMaterial;

	/** Parkrasen. Leer = M_MTA_Park. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Assets")
	TObjectPtr<UMaterialInterface> ParkMaterial;

	/** Laternenkoepfe und Leuchtkrone der Landmarke (emissiv, nachts an). Leer = M_MTA_LampEmissive. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Assets")
	TObjectPtr<UMaterialInterface> LampMaterial;

	/** Masten, Antennen, Dachaufbauten (Metall). Leer = M_MTA_Metal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Assets")
	TObjectPtr<UMaterialInterface> MetalMaterial;

	/** Baumstaemme. Leer = M_MTA_TreeTrunk. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Assets")
	TObjectPtr<UMaterialInterface> TreeTrunkMaterial;

	/** Baumkronen (Farbe per Custom-Data). Leer = M_MTA_Foliage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Assets")
	TObjectPtr<UMaterialInterface> FoliageMaterial;

	/** Sattel-/Sheddaecher (Farbe per Custom-Data). Leer = M_MTA_Roof. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Assets")
	TObjectPtr<UMaterialInterface> RoofMaterial;

	// ---------------------------------------------------------------- Parameter: Boden

	/** Boden-Kacheln erzeugen (Kollision fuer Fahrzeuge). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Ground")
	bool bGenerateGround = true;

	/** Halbe Kantenlaenge der Bodenflaeche (cm), Godot: 2000 m. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Ground", meta = (ClampMin = "10000", EditCondition = "bGenerateGround"))
	float GroundExtent = 200000.f;

	/**
	 * Kachelgroesse (cm). Kleinere Kacheln = feinere Mesh-Distance-Fields fuer Lumen-Software-RT
	 * (eine einzige 4-km-Platte haette ein viel zu grobes SDF).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Ground", meta = (ClampMin = "2000", EditCondition = "bGenerateGround"))
	float GroundTileSize = 16000.f;

	// ---------------------------------------------------------------- Parameter: Rendering (RX 5700 XT)

	/** Ende der Sichtweite fuer Gebaeude (cm, 0 = nie ausblenden; Skyline soll sichtbar bleiben). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Rendering", meta = (ClampMin = "0"))
	int32 BuildingCullDistance = 0;

	/** Ende der Sichtweite fuer Dachaufbauten und Antennen (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Rendering", meta = (ClampMin = "0"))
	int32 RoofDetailCullDistance = 40000;

	/** Ende der Sichtweite fuer Laternen (cm). Nachts bleiben die Koepfe laenger sichtbar. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Rendering", meta = (ClampMin = "0"))
	int32 LampCullDistance = 25000;

	/** Ende der Sichtweite fuer Baeume (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Rendering", meta = (ClampMin = "0"))
	int32 TreeCullDistance = 30000;

	/** Kleine Props (Masten, Dachkisten) werfen Schatten; aus = spart VSM-Seiten. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Rendering")
	bool bSmallPropsCastShadow = true;

	/** Anzahl echter Spot-Lichter, die den naechsten Laternen folgen (ohne Schatten). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Lights", meta = (ClampMin = "0", ClampMax = "64"))
	int32 LampLightPoolSize = 12;

	/** Lichtstaerke eines Laternen-Spots bei voller Nacht (Candela). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Lights", meta = (ClampMin = "0"))
	float LampLightIntensity = 1500.f;

	/** Reichweite eines Laternen-Spots (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Lights", meta = (ClampMin = "100"))
	float LampLightRadius = 3000.f;

	/** In OnConstruction generieren (Editor-Vorschau). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Generation")
	bool bGenerateOnConstruction = true;

	/** In BeginPlay immer neu generieren (sonst nur, falls keine Instanzen gespeichert sind). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Generation")
	bool bRegenerateOnBeginPlay = false;

	// ---------------------------------------------------------------- Ergebnisse (gespeichert)

	/** Bezirk je Block, Index = BX + BY * BlocksX. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "City|Result", AdvancedDisplay)
	TArray<EMTADistrict> BlockDistricts;

	/** Laternen-Fusspunkte (Welt). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "City|Result", AdvancedDisplay)
	TArray<FVector> LampPositions;

	/** Farbe je Laterne (Bezirk), parallel zu LampPositions. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "City|Result", AdvancedDisplay)
	TArray<FLinearColor> LampColors;

	/** Anzahl erzeugter Gebaeude-Instanzen (inkl. Aufsaetze). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "City|Result")
	int32 GeneratedBuildingCount = 0;

	/** Gebaeude-Parzellen (Welt), siehe GetCityLots. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "City|Result", AdvancedDisplay)
	TArray<FMTACityLot> CityLots;

	/** Reservierter Platz je Bezirk, Index = EMTADistrict; (-1,-1) = keiner. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "City|Result", AdvancedDisplay)
	TArray<FIntPoint> DistrictPlazaBlocks;

	/** Blockindex (BX + BY * BlocksX) des Aurelia Tower, INDEX_NONE = keiner. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "City|Result", AdvancedDisplay)
	int32 LandmarkBlockIndex = INDEX_NONE;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ---------------------------------------------------------------- Komponenten

	UPROPERTY(VisibleAnywhere, Category = "City|Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "City|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> GroundTiles;

	UPROPERTY(VisibleAnywhere, Category = "City|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Sidewalks;

	UPROPERTY(VisibleAnywhere, Category = "City|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ParkLawns;

	UPROPERTY(VisibleAnywhere, Category = "City|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Buildings;

	UPROPERTY(VisibleAnywhere, Category = "City|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RoofDetails;

	UPROPERTY(VisibleAnywhere, Category = "City|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Antennas;

	UPROPERTY(VisibleAnywhere, Category = "City|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Chimneys;

	UPROPERTY(VisibleAnywhere, Category = "City|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> LampPoles;

	UPROPERTY(VisibleAnywhere, Category = "City|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> LampHeads;

	UPROPERTY(VisibleAnywhere, Category = "City|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TreeTrunks;

	UPROPERTY(VisibleAnywhere, Category = "City|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TreeCrowns;

	/** Sattel- und Sheddaecher (um 45 Grad gerollte Wuerfel, untere Haelfte im Gebaeude). */
	UPROPERTY(VisibleAnywhere, Category = "City|Components")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> PitchedRoofs;

	/** Laufzeit-Pool der Laternen-Spots (nicht gespeichert). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USpotLightComponent>> LampLightPool;

private:
	/** Sammelpuffer fuer eine HISM-Komponente: Transformationen + flache Custom-Data. */
	struct FInstanceBatch
	{
		TArray<FTransform> Transforms;
		TArray<float> CustomData;
		int32 NumCustom = 0;

		void Add(const FTransform& T, TConstArrayView<float> Data = {});
	};

	/** Laufzeitdaten einer Generierung. */
	struct FBuildContext
	{
		FInstanceBatch Ground, Sidewalk, Park, Building, Roof, Antenna, Chimney, Pole, Head, Trunk, Crown, Pitched;
		TArray<FMTADistrictProfile> Profiles;
		/** Parzellen in lokalen Koordinaten (Generate rechnet sie in Welt um). */
		TArray<FMTACityLot> Lots;
	};

	void ConfigureComponent(UHierarchicalInstancedStaticMeshComponent* Comp, UStaticMesh* Mesh, UMaterialInterface* Material,
		bool bCollision, bool bCastShadow, int32 CullEnd, int32 NumCustomData, bool bAffectDistanceField) const;
	void ApplyBatch(UHierarchicalInstancedStaticMeshComponent* Comp, const FInstanceBatch& Batch) const;

	void ResolveProfiles(TArray<FMTADistrictProfile>& OutProfiles) const;
	void AssignDistricts();
	EMTADistrict ComputeDistrict(int32 BX, int32 BY) const;

	void BuildGround(FBuildContext& Ctx) const;
	void BuildBlock(FBuildContext& Ctx, int32 BX, int32 BY, bool bLandmarkBlock) const;
	void BuildPark(FBuildContext& Ctx, FRandomStream& Rng, const FVector2D& Center, const FMTADistrictProfile& Profile, EMTADistrict District) const;
	void BuildIndustrialBlock(FBuildContext& Ctx, FRandomStream& Rng, const FVector2D& Center, const FMTADistrictProfile& Profile, EMTADistrict District) const;
	void BuildLandmarkTower(FBuildContext& Ctx, const FVector2D& Center) const;
	void BuildPlaza(FBuildContext& Ctx, FRandomStream& Rng, const FVector2D& Center, const FMTADistrictProfile& Profile, EMTADistrict District) const;
	/** Baut ein Gebaeude; OutLot (optional) bekommt Dachform und Dachhoehe (lokal). */
	void AddBuilding(FBuildContext& Ctx, FRandomStream& Rng, const FVector2D& Center, float Width, float Depth, float Height,
		const FLinearColor& Tint, const FMTADistrictProfile& Profile, EMTADistrict District, EMTABuildingStyle Style, float ZBase,
		FMTACityLot* OutLot) const;
	void AddGableRoof(FBuildContext& Ctx, const FVector2D& Center, float Width, float Depth, float Top, const FLinearColor& Color,
		EMTADistrict District, float Wear, bool bSawtooth) const;
	/** Parzellen-Datensatz (lokal) mit Strassenfront relativ zur Blockmitte. */
	FMTACityLot MakeLot(const FVector2D& LotCenter, const FVector2D& BlockCenter, double W, double D, int32 BX, int32 BY,
		EMTADistrict District, int32 LotIndex, bool bSingleLot) const;
	void ComputePlazaBlocks();
	bool IsPlazaBlock(int32 BX, int32 BY) const;
	/** Breite der Kuestenstreifen, ganzzahlig in die Bodenkachel eingepasst (cm). */
	float CoastStripStep() const;
	void AddTree(FBuildContext& Ctx, FRandomStream& Rng, const FVector2D& Pos, const FMTADistrictProfile& Profile, EMTADistrict District, bool bPalm) const;
	void BuildStreetTrees(FBuildContext& Ctx, FRandomStream& Rng, const FVector2D& Center, const FMTADistrictProfile& Profile, EMTADistrict District) const;
	void BuildLamps(FBuildContext& Ctx);

	float ComputeBuildingHeight(FRandomStream& Rng, const FVector2D& Center, const FMTADistrictProfile& Profile) const;
	FVector2D LocalBlockCenter(int32 BX, int32 BY) const;
	float LocalRoadCenter(int32 Axis, int32 Index) const;
	int32 FindLandmarkBlock() const;

	UMaterialInterface* ResolveMaterial(UMaterialInterface* Assigned, const TCHAR* DefaultPath) const;
	void EnsureLampLightPool();
	void DestroyLampLightPool();

	/** Zeitstempel/Positionen der letzten Pool-Zuordnung (vermeidet unnoetiges Umsortieren). */
	FVector LastLampViewLocation = FVector(TNumericLimits<float>::Max());
	float LastNightAlpha = -1.f;
};
