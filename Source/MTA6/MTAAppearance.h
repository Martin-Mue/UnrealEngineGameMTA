#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Math/RandomStream.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MTADistrict.h"
#include "MTAAppearance.generated.h"

class USkeletalMeshComponent;
class USceneComponent;
class UPrimitiveComponent;
class UMTAWardrobeSubsystem;

/** Koerperrahmen (Basis fuer Mesh/MetaHuman-Koerper). Bewusst kein "Geschlecht" im Sinn von Spielregeln. */
UENUM(BlueprintType)
enum class EMTABodyFrame : uint8
{
	Feminine      UMETA(DisplayName = "Feminin"),
	Masculine     UMETA(DisplayName = "Maskulin"),
	Androgynous   UMETA(DisplayName = "Androgyn")
};

UENUM(BlueprintType)
enum class EMTABodyType : uint8
{
	Slim      UMETA(DisplayName = "Schlank"),
	Average   UMETA(DisplayName = "Durchschnitt"),
	Athletic  UMETA(DisplayName = "Athletisch"),
	Heavy     UMETA(DisplayName = "Kraeftig")
};

UENUM(BlueprintType)
enum class EMTALifeStage : uint8
{
	Child, Teen, Adult, Elder
};

UENUM(BlueprintType)
enum class EMTAMarkKind : uint8
{
	Scar     UMETA(DisplayName = "Narbe"),
	Tattoo   UMETA(DisplayName = "Tattoo"),
	Implant  UMETA(DisplayName = "Implantat")
};

UENUM(BlueprintType)
enum class EMTABodyRegion : uint8
{
	Face, Neck, Scalp, ArmLeft, ArmRight, Hands, Torso, Back, Legs
};

/** Kleidungs-Slots. Reihenfolge = Ebenen von innen nach aussen (Fullbody ersetzt Top/Bottom). */
UENUM(BlueprintType)
enum class EMTAWardrobeSlot : uint8
{
	Hat, Glasses, Mask, Neck, Top, Jacket, Fullbody, Bottom, Gloves, Shoes, Bag
};

UENUM(BlueprintType)
enum class EMTAClothingStyle : uint8
{
	Workwear, Business, Casual, Street, Sport, Formal, Uniform, Tactical, Underground, Luxury, Beach, Weather
};

/** Eine Narbe/ein Tattoo/ein Implantat am Koerper (Definition steht im Katalog "marks" der appearance.json). */
USTRUCT(BlueprintType)
struct MTA6_API FMTABodyMark
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mark")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mark")
	EMTAMarkKind Kind = EMTAMarkKind::Scar;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mark")
	EMTABodyRegion Region = EMTABodyRegion::Face;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mark", meta = (ClampMin = "0", ClampMax = "1"))
	float Intensity = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mark")
	FLinearColor Color = FLinearColor::White;
};

USTRUCT(BlueprintType)
struct MTA6_API FMTAMakeup
{
	GENERATED_BODY()

	/** 0 = kein Make-up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Makeup", meta = (ClampMin = "0", ClampMax = "1"))
	float Intensity = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Makeup")
	FLinearColor LipColor = FLinearColor(0.25f, 0.05f, 0.05f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Makeup")
	FLinearColor EyeColor = FLinearColor(0.05f, 0.03f, 0.03f);
};

/**
 * Vollstaendiges Aussehen einer Figur als reine Daten. Unabhaengig von konkreten Assets:
 * Mesh-/Morph-/Material-Zuordnung geschieht ueber UMTAAppearanceLibrary bzw. UMTAWardrobeSubsystem.
 */
USTRUCT(BlueprintType)
struct MTA6_API FMTAAppearance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FString DisplayName;

	/** Archetyp, aus dem die Figur erzeugt wurde (leer bei Schluesselfiguren/Spielerfigur). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FName ArchetypeId;

	/** Herkunft (ID kommt vom IMTAOriginProvider; hier nur durchgereicht). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FName OriginId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	EMTABodyFrame Frame = EMTABodyFrame::Feminine;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body")
	EMTABodyType BodyType = EMTABodyType::Average;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body", meta = (ClampMin = "3", ClampMax = "95"))
	int32 Age = 30;

	/** Koerpergroesse in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body", meta = (ClampMin = "90", ClampMax = "215"))
	float HeightCm = 172.f;

	/** Koerperfuelle 0..1 (0 = sehr schlank, 1 = sehr kraeftig). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body", meta = (ClampMin = "0", ClampMax = "1"))
	float Build = 0.5f;

	/** Muskeltonus 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Body", meta = (ClampMin = "0", ClampMax = "1"))
	float Muscle = 0.4f;

	/** Hautton 0..1 (0 = hell, 1 = dunkel; Stuetzstellen in appearance.json "skinTones"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (ClampMin = "0", ClampMax = "1"))
	float SkinTone = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skin", meta = (ClampMin = "0", ClampMax = "1"))
	float Freckles = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	FLinearColor EyeColor = FLinearColor(0.1f, 0.06f, 0.03f);

	/** Gesichts-Morph-Parameter (Namen siehe UMTAAppearanceLibrary::GetFaceMorphNames), je -1..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Face")
	TMap<FName, float> FaceMorphs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hair")
	FName HairStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hair")
	FLinearColor HairColor = FLinearColor(0.02f, 0.01f, 0.01f);

	/** Ergrauung 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hair", meta = (ClampMin = "0", ClampMax = "1"))
	float HairGray = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hair")
	FName BeardStyle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hair")
	FLinearColor BeardColor = FLinearColor(0.02f, 0.01f, 0.01f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Makeup")
	FMTAMakeup Makeup;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Marks")
	TArray<FMTABodyMark> Marks;

	/** Kleidung je Slot (Item-ID aus wardrobe.json). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outfit")
	TMap<EMTAWardrobeSlot, FName> Outfit;

	/** Farbton je Slot (sRGB-Farbe als linear gespeichert). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outfit")
	TMap<EMTAWardrobeSlot, FLinearColor> OutfitTints;

	EMTALifeStage GetLifeStage() const;

	/** Anzeige-Hautfarbe aus Hautton-Stuetzstellen (Fallback: eingebaute Palette). */
	FLinearColor GetSkinColor() const;

	TSharedRef<class FJsonObject> ToJson() const;
	bool FromJson(const TSharedPtr<class FJsonObject>& Json);
	FString ToJsonString(bool bPretty = true) const;
	bool FromJsonString(const FString& Text);

	bool SaveToFile(const FString& FilePath) const;
	bool LoadFromFile(const FString& FilePath);

	/** Standardordner fuer gespeicherte Figuren: Saved/Characters. */
	static FString GetDefaultSaveDir();
};

/** Katalogeintrag einer Narbe/eines Tattoos/Implantats. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAMarkDef
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Mark") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "Mark") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "Mark") EMTAMarkKind Kind = EMTAMarkKind::Scar;
	UPROPERTY(BlueprintReadOnly, Category = "Mark") TArray<EMTABodyRegion> Regions;
	UPROPERTY(BlueprintReadOnly, Category = "Mark") bool bGlows = false;
	/** Wie stark die Markierung Aegis (Gesichts-/Merkmalserkennung) auffaellt, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Mark") float Conspicuous = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Mark") FLinearColor Color = FLinearColor::White;
	/** Optionales Decal/Textur (Pfad-Platzhalter, leer = kein Asset zugeordnet). */
	UPROPERTY(BlueprintReadOnly, Category = "Mark") FSoftObjectPath Decal;
};

USTRUCT(BlueprintType)
struct MTA6_API FMTAHairStyleDef
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Hair") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "Hair") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "Hair") TArray<EMTABodyFrame> Frames;
	UPROPERTY(BlueprintReadOnly, Category = "Hair") int32 AgeMin = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Hair") int32 AgeMax = 99;
	/** Strand-Groom (nur Hauptfiguren). Pfad-Platzhalter, leer = nicht zugeordnet. */
	UPROPERTY(BlueprintReadOnly, Category = "Hair") FSoftObjectPath Groom;
	/** Haar-Karten-Mesh fuer Crowd/niedrige LODs. */
	UPROPERTY(BlueprintReadOnly, Category = "Hair") FSoftObjectPath Cards;
};

/** Ein Archetyp (Figurentyp) mit Wahrscheinlichkeiten je Bezirk. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAArchetype
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Archetype") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") TArray<FName> Tags;
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") FName Faction;
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") int32 AgeMin = 20;
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") int32 AgeMax = 55;
	/** Gewichte Feminin/Maskulin/Androgyn. */
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") FVector FrameWeights = FVector(0.5, 0.5, 0.0);
	/** Gewichte Schlank/Durchschnitt/Athletisch/Kraeftig. */
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") FVector4 BodyWeights = FVector4(0.15, 0.5, 0.25, 0.1);
	/** Gewicht je Bezirk, Index = EMTADistrict. */
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") TArray<float> DistrictWeights;
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") float DayWeight = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") float NightWeight = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") TArray<FName> OutfitPresets;
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") float BeardChance = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") float MakeupChance = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") float ScarChance = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") float TattooChance = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") float ImplantChance = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") float DyedHairChance = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") float HeightOffsetCm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") TArray<FName> MarkPool;
	/** Crowd-Mesh-Gruppe: adult, child oder elder (Frame wird ergaenzt: adult_fem/adult_masc). */
	UPROPERTY(BlueprintReadOnly, Category = "Archetype") FName MeshGroup;
};

/** Kontext fuer Archetyp-Wahl und Kleidungsanpassung. */
USTRUCT(BlueprintType)
struct MTA6_API FMTALookContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context")
	EMTADistrict District = EMTADistrict::Core;

	/** Tageszeit 0..24. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context", meta = (ClampMin = "0", ClampMax = "24"))
	float Hour = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context")
	float TemperatureC = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Context")
	bool bRaining = false;

	bool IsNight() const { return Hour < 6.f || Hour >= 20.f; }
};

/**
 * Daten-Asset fuer ein gespeichertes Aussehen (Hauptfiguren/Presets).
 * Python (tools/ue_characters) fuellt es per ImportFromJsonString.
 */
UCLASS(BlueprintType)
class MTA6_API UMTAAppearanceAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FMTAAppearance Appearance;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Appearance")
	bool ImportFromJsonString(const FString& JsonText);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Appearance")
	FString ExportToJsonString() const;
};

/** Statische Hilfsfunktionen: Namen, Parsing, Anwendung auf Komponenten. */
UCLASS()
class MTA6_API UMTAAppearanceLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Namen der Gesichts-Morph-Parameter (identisch zu appearance.json "faceMorphs"). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Appearance")
	static const TArray<FName>& GetFaceMorphNames();

	static FString SlotToId(EMTAWardrobeSlot Slot);
	static bool SlotFromId(const FString& Id, EMTAWardrobeSlot& OutSlot);
	static FString DistrictToId(EMTADistrict District);
	static bool DistrictFromId(const FString& Id, EMTADistrict& OutDistrict);
	static bool FrameFromId(const FString& Id, EMTABodyFrame& Out);
	static FString FrameToId(EMTABodyFrame Frame);
	static bool BodyTypeFromId(const FString& Id, EMTABodyType& Out);
	static FString BodyTypeToId(EMTABodyType Type);
	static bool StyleFromId(const FString& Id, EMTAClothingStyle& Out);

	/**
	 * Schreibt Gesichts-Morphs als Morph Targets "mta_<name>" (ausserdem mta_build, mta_muscle, mta_age).
	 * Fehlende Morph Targets werden von der Engine ignoriert.
	 */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Appearance")
	static void ApplyMorphTargets(USkeletalMeshComponent* Body, const FMTAAppearance& Appearance);

	/**
	 * Setzt Material-Parameter auf allen Materialien der Komponente (dynamische Instanzen):
	 * Scalar: MTA_SkinTone, MTA_Freckles, MTA_HairGray, MTA_MakeupIntensity, MTA_Age
	 * Vector: MTA_SkinColor, MTA_EyeColor, MTA_HairColor, MTA_BeardColor, MTA_LipColor, MTA_EyeShadow
	 * Nicht vorhandene Parameter werden von der Engine ignoriert.
	 */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Appearance")
	static void ApplyMaterialParameters(UPrimitiveComponent* Component, const FMTAAppearance& Appearance);

	/** Skaliert den Koerper entsprechend HeightCm relativ zur Referenzgroesse des Meshes (Standard 175 cm). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Appearance")
	static void ApplyBodyScale(USceneComponent* Body, const FMTAAppearance& Appearance, float ReferenceHeightCm = 175.f);
};

/** Kleine JSON-/Farb-Helfer, gemeinsam genutzt von Appearance-, Wardrobe- und Crowd-Code. */
namespace MTAAppearanceJson
{
	/** Liest eine JSON-Datei; Pfade relativ zu Content/ (z. B. Data/appearance.json) oder absolut. */
	MTA6_API bool LoadJsonFile(const FString& Path, TSharedPtr<class FJsonObject>& OutObject);
	/** Parst #RRGGBB (sRGB) in lineare Farbe. */
	MTA6_API FLinearColor ParseColor(const FString& Hex, const FLinearColor& Fallback);
	/** Wandelt lineare Farbe in #RRGGBB (sRGB). */
	MTA6_API FString ColorToHex(const FLinearColor& Color);
}

/**
 * Archetypen, Haar-/Bart-/Marken-Kataloge, Schluesselfiguren und Creator-Presets.
 * Daten: Content/Data/appearance.json. Erzeugung ist deterministisch (FRandomStream).
 */
UCLASS()
class MTA6_API UMTAAppearanceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Appearance")
	bool LoadAppearanceData(const FString& Path = TEXT(""));

	UFUNCTION(BlueprintPure, Category = "MTA6|Appearance")
	bool IsLoaded() const { return Archetypes.Num() > 0; }

	const TArray<FMTAArchetype>& GetArchetypes() const { return Archetypes; }
	const TArray<FMTAMarkDef>& GetMarkDefs() const { return MarkDefs; }
	const TArray<FMTAHairStyleDef>& GetHairStyles() const { return HairStyles; }
	const TArray<FName>& GetBeardStyles() const { return BeardStyles; }
	const TArray<FMTAAppearance>& GetCreatorPresets() const { return CreatorPresets; }
	const TArray<FMTAAppearance>& GetKeyCharacters() const { return KeyCharacters; }
	const TArray<FLinearColor>& GetSkinTonePalette() const { return SkinTones; }
	const TMap<FName, TArray<FSoftObjectPath>>& GetCrowdMeshGroups() const { return CrowdMeshGroups; }

	UFUNCTION(BlueprintCallable, Category = "MTA6|Appearance")
	bool FindArchetype(FName Id, FMTAArchetype& OutArchetype) const;

	const FMTAArchetype* FindArchetypePtr(FName Id) const;
	const FMTAMarkDef* FindMarkDef(FName Id) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Appearance")
	bool GetKeyCharacter(FName Id, FMTAAppearance& OutAppearance) const;

	/** Outfit-Preset-Vorschlag fuer eine Herkunft (originOutfits in appearance.json), sonst NAME_None. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Appearance")
	FName GetOriginOutfitPreset(FName OriginId) const;

	/** Gewichtete Archetyp-Wahl nach Bezirk und Tageszeit. */
	FName PickArchetype(const FMTALookContext& Context, FRandomStream& Stream) const;

	/** Erzeugt deterministisch ein Aussehen. Gleicher Seed + Kontext = gleiches Ergebnis. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Appearance")
	FMTAAppearance GenerateFromArchetype(FName ArchetypeId, int32 Seed, const FMTALookContext& Context) const;

	/** Stream-Variante (Archetyp wird verbraucht in der Reihenfolge der JSON-Daten). */
	FMTAAppearance GenerateFromArchetypeStream(const FMTAArchetype& Archetype, FRandomStream& Stream, const FMTALookContext& Context) const;

	/** Zufalls-Aussehen fuer den Charakter-Editor (Erwachsene, Standard-Outfit der Herkunft/Bezirks). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Appearance")
	FMTAAppearance GenerateRandomPlayerLook(int32 Seed) const;

	/** Aegis-Auffaelligkeit 0..1 aus Kleidung, Marken (Implantate leuchten) und Make-up. */
	float ComputeAegisConspicuousness(const FMTAAppearance& Appearance) const;

	/** Farbauswahl fuer Editor (Augen bzw. natuerliche + gefaerbte Haarfarben). */
	TArray<FLinearColor> GetEyeColorOptions() const;
	TArray<FLinearColor> GetHairColorOptions() const;

	/** Hilfen fuer Editor/Generator. */
	FLinearColor SampleSkinColor(float Tone) const;
	TArray<FName> GetHairStylesFor(EMTABodyFrame Frame, int32 Age) const;

	/** Querverweis-Pruefung (Archetyp -> Outfit-Preset -> Kleidung, Marken, Schluesselfiguren). Liefert Anzahl Fehler. */
	int32 ValidateData(TArray<FString>& OutErrors) const;

private:
	FMTAAppearance AppearanceFromJson(const TSharedPtr<class FJsonObject>& Obj) const;
	FLinearColor PickWeightedColor(const TArray<TPair<FLinearColor, float>>& Colors, FRandomStream& Stream) const;

	TArray<FMTAArchetype> Archetypes;
	TMap<FName, int32> ArchetypeIndex;
	TArray<FMTAMarkDef> MarkDefs;
	TArray<FMTAHairStyleDef> HairStyles;
	TArray<FName> BeardStyles;
	TArray<FLinearColor> SkinTones;
	TArray<TPair<FLinearColor, float>> EyeColors;
	TArray<TPair<FLinearColor, float>> NaturalHair;
	TArray<TPair<FLinearColor, float>> DyedHair;
	TArray<FMTAAppearance> KeyCharacters;
	TArray<FMTAAppearance> CreatorPresets;
	TMap<FName, FName> OriginOutfits;
	TMap<FName, TArray<FSoftObjectPath>> CrowdMeshGroups;
};
