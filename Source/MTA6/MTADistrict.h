#pragma once

#include "CoreMinimal.h"
#include "MTADistrict.generated.h"

/**
 * Die sechs Bezirke von Kestrel Bay (siehe docs/WELTKONZEPT.md, Kapitel 8.2).
 * Reihenfolge ist Teil der Datenschnittstelle (Index wird als Per-Instance-Custom-Data
 * ans Material gegeben) und darf nicht umsortiert werden.
 */
UENUM(BlueprintType)
enum class EMTADistrict : uint8
{
	Core        UMETA(DisplayName = "Kestrel Core"),
	Saltmarket  UMETA(DisplayName = "Saltmarket"),
	LanternHill UMETA(DisplayName = "Lantern Hill"),
	Greenmoor   UMETA(DisplayName = "Greenmoor"),
	Rustline    UMETA(DisplayName = "Rustline"),
	Sunstrip    UMETA(DisplayName = "Sunstrip"),
	Count       UMETA(Hidden)
};

/** Fassadenstil eines Gebaeudes; wird als Custom-Data-Wert ans Gebaeudematerial gegeben. */
UENUM(BlueprintType)
enum class EMTABuildingStyle : uint8
{
	GlassSteel  UMETA(DisplayName = "Glas/Stahl (Hochhaus)"),
	Plaster     UMETA(DisplayName = "Putz/Backstein (Wohnhaus)"),
	Industrial  UMETA(DisplayName = "Industriehalle"),
	Villa       UMETA(DisplayName = "Villa/Einfamilienhaus")
};

/**
 * Gestaltungsprofil eines Bezirks. Steuert Hoehen, Dichte, Palette, Park-/Industrieanteil
 * und Licht. Alle Laengen in cm (Unreal-Einheiten).
 */
USTRUCT(BlueprintType)
struct MTA6_API FMTADistrictProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District")
	FText DisplayName;

	/** Minimale Gebaeudehoehe (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Height", meta = (ClampMin = "300"))
	float MinHeight = 1000.f;

	/** Maximale Gebaeudehoehe (cm) ausserhalb des Downtown-Einflusses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Height", meta = (ClampMin = "300"))
	float MaxHeight = 3000.f;

	/** Exponent auf die Zufallszahl: > 1 = meist niedrig, < 1 = meist hoch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Height", meta = (ClampMin = "0.1", ClampMax = "8"))
	float HeightBias = 1.5f;

	/** Wie stark das Downtown-Hoehenprofil (Generator) auf diesen Bezirk wirkt (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Height", meta = (ClampMin = "0", ClampMax = "1"))
	float DowntownInfluence = 0.5f;

	/** Gewichte fuer die Parzellierung eines Blocks: 1, 2, 4 oder 9 Gebaeude. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Density")
	FVector4 LotSplitWeights = FVector4(1.0, 2.0, 1.0, 0.0);

	/** Wahrscheinlichkeit, dass eine Parzelle leer bleibt (Hinterhof, Brache, Garten). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Density", meta = (ClampMin = "0", ClampMax = "1"))
	float EmptyLotChance = 0.f;

	/** Anteil der Bloecke, die Park werden (wird mit dem globalen Parkanteil multipliziert). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Landuse", meta = (ClampMin = "0", ClampMax = "1"))
	float ParkChance = 0.14f;

	/** Anteil der Bloecke mit Industriehallen und Schornsteinen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Landuse", meta = (ClampMin = "0", ClampMax = "1"))
	float IndustrialChance = 0.f;

	/** Baeume pro Block am Strassenrand/in Gaerten (zusaetzlich zu Parks). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Landuse", meta = (ClampMin = "0", ClampMax = "64"))
	int32 StreetTreesPerBlock = 0;

	/** Vorherrschender Fassadenstil. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Look")
	EMTABuildingStyle PrimaryStyle = EMTABuildingStyle::Plaster;

	/** Leitfarbe des Bezirks (Konzept 8.2, Wiedererkennungsregel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Look")
	FLinearColor LeadColor = FLinearColor(0.7f, 0.7f, 0.7f);

	/** Fassadenpalette; der Generator waehlt pro Gebaeude deterministisch eine Farbe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Look")
	TArray<FLinearColor> Palette;

	/** Farbton der Baeume (Palmen in Sunstrip etwas gelblicher). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Look")
	FLinearColor FoliageColor = FLinearColor(0.17f, 0.42f, 0.16f);

	/** Verschleiss 0..1 (Rustline hoch, Core niedrig), geht als Custom-Data ins Material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Look", meta = (ClampMin = "0", ClampMax = "1"))
	float Wear = 0.3f;

	/** Anteil der nachts beleuchteten Fenster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Light", meta = (ClampMin = "0", ClampMax = "1"))
	float LitWindowRatio = 0.45f;

	/** Farbe der Strassenlaternen (natriumorange in Rustline, kaltweiss im Core). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Light")
	FLinearColor LampColor = FLinearColor(1.f, 0.8f, 0.55f);

	/** Laternenabstand-Faktor: 1 = jede Position, 0.5 = jede zweite (dunkle Viertel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "District|Light", meta = (ClampMin = "0", ClampMax = "1"))
	float LampDensity = 1.f;
};

/** Hilfsfunktionen fuer Bezirke (reines C++, keine Abhaengigkeit zu anderen Modulteilen). */
namespace MTADistrict
{
	/** Liefert das Standardprofil eines Bezirks gemaess Weltkonzept Kapitel 8.2. */
	MTA6_API FMTADistrictProfile MakeDefaultProfile(EMTADistrict District);

	/**
	 * Rastervorgabe: ordnet normierte Kartenkoordinaten einem Bezirk zu.
	 * U = West(0) -> Ost(1) (Unreal +Y), V = Sued(0) -> Nord(1) (Unreal +X).
	 * Meer im Westen, Berge im Osten/Norden, Rustline im Sueden, Core in der Mitte.
	 */
	MTA6_API EMTADistrict ClassifyNormalized(float U, float V);

	/** Anzahl der Bezirke (ohne Count). */
	constexpr int32 Num() { return static_cast<int32>(EMTADistrict::Count); }
}
