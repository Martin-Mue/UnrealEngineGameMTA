#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Math/RandomStream.h"
#include "MTAAppearance.h"
#include "MTAWardrobe.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class EMTAGenderFit : uint8
{
	Any, Feminine, Masculine
};

/** Beschreibung eines Kleidungsstuecks (Daten aus Content/Data/wardrobe.json). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAWardrobeItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe") EMTAWardrobeSlot Slot = EMTAWardrobeSlot::Top;
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe") EMTAClothingStyle Style = EMTAClothingStyle::Casual;
	/** Leer = in allen Bezirken ueblich. */
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe") TArray<EMTADistrict> Districts;
	/** Leer = fraktionsneutral. */
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe") TArray<FName> Factions;
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe") float TempMinC = -10.f;
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe") float TempMaxC = 40.f;
	/** Regentauglichkeit 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe") float RainResistance = 0.f;
	/** Wie stark das Stueck Aegis/Zeugen auffaellt (Uniform, Neon, Gesichtsverdeckung ...), 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe") float Conspicuousness = 0.2f;
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe") int32 Price = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe") EMTAGenderFit GenderFit = EMTAGenderFit::Any;
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe") bool bChildSize = false;
	/** Slots, die dieses Stueck verdeckt/ersetzt (z. B. Overall -> Top, Bottom). */
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe") TArray<EMTAWardrobeSlot> BlocksSlots;
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe") TArray<FName> Tags;
	/** Zugeordnetes Skeletal Mesh (leer = Platzhalter; siehe tools/ue_characters/assign_clothing_meshes.py). */
	UPROPERTY(BlueprintReadOnly, Category = "Wardrobe") TSoftObjectPtr<USkeletalMesh> Mesh;

	bool HasMesh() const { return !Mesh.IsNull(); }
	bool HasTag(FName Tag) const { return Tags.Contains(Tag); }
};

USTRUCT(BlueprintType)
struct MTA6_API FMTAOutfitSlotRule
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Outfit") TArray<FName> Items;
	/** Wahrscheinlichkeit, dass der Slot belegt wird (1 = immer). */
	UPROPERTY(BlueprintReadOnly, Category = "Outfit") float Chance = 1.f;
};

/** Outfit-Preset (je Fraktion, Bezirk, Archetyp oder Schluesselfigur). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAOutfitPreset
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Outfit") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "Outfit") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "Outfit") FName Faction;
	UPROPERTY(BlueprintReadOnly, Category = "Outfit") TArray<EMTADistrict> Districts;
	UPROPERTY(BlueprintReadOnly, Category = "Outfit") EMTAClothingStyle Style = EMTAClothingStyle::Casual;
	UPROPERTY(BlueprintReadOnly, Category = "Outfit") TArray<FName> Tags;
	UPROPERTY(BlueprintReadOnly, Category = "Outfit") TMap<EMTAWardrobeSlot, FMTAOutfitSlotRule> Slots;
	/** Palettenname je Slot (Farben in wardrobe.json "palettes"). */
	UPROPERTY(BlueprintReadOnly, Category = "Outfit") TMap<EMTAWardrobeSlot, FName> Palette;
};

/** Filter fuer Kleidungsabfragen. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAWardrobeQuery
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query") bool bFilterSlot = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query") EMTAWardrobeSlot Slot = EMTAWardrobeSlot::Top;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query") bool bFilterDistrict = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query") EMTADistrict District = EMTADistrict::Core;
	/** NAME_None = keine Fraktionsfilterung. Fraktionsneutrale Stuecke passen immer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query") FName Faction;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query") bool bFilterWeather = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query") float TemperatureC = 18.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query") EMTABodyFrame Frame = EMTABodyFrame::Androgynous;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query") bool bChild = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query") float MaxConspicuousness = 1.f;
	/** Uniformen/Dienstausgabe (Tag kcg_issue, nicht verkaeuflich) ausschliessen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Query") bool bOnlyForSale = false;
};

USTRUCT(BlueprintType)
struct MTA6_API FMTAOutfitReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Outfit") TArray<FString> Errors;
	UPROPERTY(BlueprintReadOnly, Category = "Outfit") TArray<FString> Warnings;

	bool IsValid() const { return Errors.Num() == 0; }
};

/** Kombinationsregeln aus wardrobe.json "rules". */
struct FMTAWardrobeRules
{
	TArray<EMTAWardrobeSlot> RequiredSlots;
	TArray<EMTAWardrobeSlot> RequiredUnlessFullbody;
	TArray<TPair<FName, FName>> TagConflicts;
	TArray<TPair<EMTAClothingStyle, EMTAClothingStyle>> StyleClashes;
	TArray<EMTAWardrobeSlot> FullbodyDefaultBlocks;
	float ColdBelowC = 8.f;
	float HotAboveC = 30.f;
};

/**
 * Garderobe: Kleidungsstuecke, Kombinationsregeln und Outfit-Presets (Content/Data/wardrobe.json).
 * Arbeitet ohne Assets (Mesh leer) - dann bleiben nur die Daten/Tints wirksam.
 */
UCLASS()
class MTA6_API UMTAWardrobeSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Wardrobe")
	bool LoadWardrobe(const FString& Path = TEXT(""));

	UFUNCTION(BlueprintPure, Category = "MTA6|Wardrobe")
	int32 GetNumItems() const { return Items.Num(); }

	UFUNCTION(BlueprintPure, Category = "MTA6|Wardrobe")
	int32 GetNumPresets() const { return Presets.Num(); }

	const TArray<FMTAWardrobeItem>& GetItems() const { return Items; }
	const TArray<FMTAOutfitPreset>& GetPresets() const { return Presets; }
	const FMTAWardrobeRules& GetRules() const { return Rules; }

	const FMTAWardrobeItem* FindItem(FName Id) const;
	const FMTAOutfitPreset* FindPreset(FName Id) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Wardrobe")
	bool GetItem(FName Id, FMTAWardrobeItem& OutItem) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Wardrobe")
	TArray<FName> QueryItems(const FMTAWardrobeQuery& Query) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Wardrobe")
	TArray<FName> GetPresetsForFaction(FName Faction) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Wardrobe")
	TArray<FName> GetPresetsForDistrict(EMTADistrict District) const;

	/** Palette (Name -> sRGB-Farben als linear). */
	const TArray<FLinearColor>* FindPalette(FName Name) const;

	/**
	 * Baut deterministisch ein Outfit aus einem Preset. Slots mit Chance < 1 werden zufaellig belegt,
	 * Fullbody-Stuecke sperren Top/Bottom usw. (BlocksSlots).
	 */
	bool BuildOutfitFromPreset(FName PresetId, FRandomStream& Stream, EMTABodyFrame Frame, bool bChild,
		TMap<EMTAWardrobeSlot, FName>& OutItems, TMap<EMTAWardrobeSlot, FLinearColor>& OutTints) const;

	/** Passt ein Outfit an Temperatur/Regen an (Regenjacke/Schirm, Winterjacke, Sommer: Jacke/Handschuhe/Schal weg). */
	void AdaptOutfitToWeather(TMap<EMTAWardrobeSlot, FName>& InOutItems, TMap<EMTAWardrobeSlot, FLinearColor>& InOutTints,
		const FMTALookContext& Context, FRandomStream& Stream, EMTABodyFrame Frame, bool bChild, FName Faction) const;

	/** Prueft Kombinationsregeln (Pflichtslots, Blockierung, Tag-Konflikte, Stilbruch als Warnung). */
	FMTAOutfitReport ValidateOutfit(const TMap<EMTAWardrobeSlot, FName>& Outfit) const;

	/** Auffaelligkeit fuer Aegis 0..1 (gewichtetes Maximum/Mittel der Stuecke). */
	float ComputeConspicuousness(const TMap<EMTAWardrobeSlot, FName>& Outfit) const;

	int32 ComputeOutfitPrice(const TMap<EMTAWardrobeSlot, FName>& Outfit) const;

	/**
	 * Wendet das Outfit auf einen Koerper an: je Slot eine Kind-Komponente (Leader-Pose = Body).
	 * Stuecke ohne Mesh werden uebersprungen (Platzhalter). OutSlotComponents wird wiederverwendet.
	 */
	void ApplyOutfit(USkeletalMeshComponent* Body, const FMTAAppearance& Appearance,
		TMap<EMTAWardrobeSlot, USkeletalMeshComponent*>& InOutSlotComponents) const;

	/** Querverweise innerhalb der Garderobe pruefen (Preset -> Items, Paletten). Liefert Fehleranzahl. */
	int32 ValidateData(TArray<FString>& OutErrors) const;

private:
	bool ItemMatches(const FMTAWardrobeItem& Item, const FMTAWardrobeQuery& Query) const;

	TArray<FMTAWardrobeItem> Items;
	TMap<FName, int32> ItemIndex;
	TArray<FMTAOutfitPreset> Presets;
	TMap<FName, int32> PresetIndex;
	TMap<FName, TArray<FLinearColor>> Palettes;
	FMTAWardrobeRules Rules;
};
