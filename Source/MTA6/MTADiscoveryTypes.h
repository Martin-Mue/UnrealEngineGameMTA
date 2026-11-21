#pragma once

#include "CoreMinimal.h"
#include "MTADistrict.h"
#include "MTADiscoveryTypes.generated.h"

/** Art einer Entdeckung (secrets.json: "type"). */
UENUM(BlueprintType)
enum class EMTADiscoveryType : uint8
{
	Other,
	RooftopGarden,
	Mural,
	Viewpoint,
	Tunnel,
	Stunt,
	AbandonedStation,
	HackerTerminal,
	Concert,
	SmugglerCave,
	BlackMarket,
	UrbanLegend,
	Animal,
	EchoGlitch,
	TimeCapsule,
	LoveLetter,
	GhostShip,
	HiddenGarage,
	Memorial,
	Oddity,
	Ruin,
	Waterfall,
	Cave
};

/** Wie eine Entdeckung ausgeloest wird (secrets.json: "discover"). */
UENUM(BlueprintType)
enum class EMTADiscoverMode : uint8
{
	/** Betreten des Radius genuegt. */
	Proximity,
	/** Im Radius verweilen (DwellSeconds), z. B. Sonnenuntergang, Konzert. */
	Dwell,
	/** Interaktionstaste im Radius (TryInteract). */
	Interact,
	/** Foto im Fotomodus mit Blick auf den Ort (ReportPhoto). */
	Photo,
	/** Nur ueber Code/Komponente (ForceDiscover, UMTACollectibleComponent). */
	Script
};

/** Zustand einer Entdeckung aus Sicht des Spielers. */
UENUM(BlueprintType)
enum class EMTADiscoveryState : uint8
{
	Hidden,
	Hinted,
	Found
};

UENUM(BlueprintType)
enum class EMTARewardType : uint8
{
	Money,
	Vehicle,
	SkillPoint,
	Lore,
	Standing,
	Cosmetic,
	Item,
	Aegis
};

/** Hinweiskanal: keine Minimap-Marker, nur Welt-Hinweise. */
UENUM(BlueprintType)
enum class EMTAHintKind : uint8
{
	Rumor,
	Sound,
	Visual,
	Radio,
	Note,
	Smell
};

USTRUCT(BlueprintType)
struct MTA6_API FMTADiscoveryReward
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	EMTARewardType Type = EMTARewardType::Money;

	/** Fraktion, Fahrzeug-, Item-, Kosmetik-, Lore- oder Skill-Id je nach Typ. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	int32 Amount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FString Text;
};

USTRUCT(BlueprintType)
struct MTA6_API FMTADiscoveryHint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	EMTAHintKind Kind = EMTAHintKind::Rumor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FString Text;

	/** Nur Kind == Sound: Name des Sound-Cues (Audio-Paket loest ihn auf). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FName SoundCue;

	/** Nur Kind == Sound: Hoerweite in Metern; im Radius wird der Hinweis automatisch "gehoert". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	float AudibleRadiusM = 0.f;
};

/** Blickfang: Licht-/Farbakzent und Sichtlinie (Level-Design-Vorgabe, keine Laufzeitlogik). */
USTRUCT(BlueprintType)
struct MTA6_API FMTADiscoveryAccent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FName LightStyle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FString Sightline;
};

/** Eine handgeschriebene Entdeckung aus secrets.json oder wilderness.json. */
USTRUCT(BlueprintType)
struct MTA6_API FMTADiscoveryDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FName Id;

	/** core, saltmarket, lantern, greenmoor, rustline, sunstrip oder Wildnisregion (verdanta). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FName DistrictId;

	/** Gesetzt fuer Wildnis-Entdeckungen: Position ist dann LocalM relativ zur registrierten Region. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FName RegionId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	EMTADiscoveryType Type = EMTADiscoveryType::Other;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	EMTADiscoverMode Mode = EMTADiscoverMode::Proximity;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FString Title;

	/** Fundtext (Lore), wird nach dem Fund im Journal gezeigt. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FString Lore;

	/** Parzellenraster: Block (X = bu West->Ost/Unreal +Y, Y = bv Sued->Nord/Unreal +X). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FIntPoint Block = FIntPoint::ZeroValue;

	/** Lage im Block 0..1 (0.5 = Blockmitte). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FVector2D BlockAt = FVector2D(0.5, 0.5);

	/** Hoehe ueber Gehweg in m (0 = Bodenniveau, Distanz dann nur horizontal). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	float HeightM = 0.f;

	/** Wildnis: Position in m relativ zum Ursprung der Region. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FVector LocalM = FVector::ZeroVector;

	/** Actor-Tag, dessen erster Treffer die Position vorgibt (Standard: Id). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FName AnchorTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	float RadiusM = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	float DwellSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FName ChainId;

	/** 1-basiert; 0 = keine Kette. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	int32 ChainStep = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	TArray<FMTADiscoveryHint> Hints;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	TArray<FMTADiscoveryReward> Rewards;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FMTADiscoveryAccent Accent;

	/** Bedingungen als JSON-Array (Debug/Editor-Anzeige; ausgewertet wird die geparste Fassung). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FString VisibleWhenJson;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FString UnlockWhenJson;
};

/** "Entdeckt 12 von 34" je Bezirk. */
USTRUCT(BlueprintType)
struct MTA6_API FMTADistrictProgress
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	FName DistrictId;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	int32 Found = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	int32 Total = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	FText Summary;
};

USTRUCT(BlueprintType)
struct MTA6_API FMTAChainDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FString Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	int32 NumSteps = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	TArray<FMTADiscoveryReward> FinalRewards;
};

USTRUCT(BlueprintType)
struct MTA6_API FMTAChainProgress
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	FName ChainId;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	FString Title;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	int32 StepsFound = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	int32 NumSteps = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	bool bComplete = false;
};

USTRUCT(BlueprintType)
struct MTA6_API FMTACollectionDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FName DistrictId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FString Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	int32 Count = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	TArray<FMTADiscoveryReward> Rewards;
};

USTRUCT(BlueprintType)
struct MTA6_API FMTACollectionProgress
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	FName CollectionId;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	FString Title;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	int32 Collected = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	int32 Total = 0;
};

/** Landmarke zur Orientierung ohne Karte (Weltkonzept 8.4). */
USTRUCT(BlueprintType)
struct MTA6_API FMTALandmark
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FName DistrictId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FString Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FIntPoint Block = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	float HeightM = 0.f;

	/** 1 = stadtweit (Aurelia Tower), 2 = Bezirksmarke, 3 = Nachbarschaft. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	int32 Tier = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FLinearColor AccentColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FString NightLight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MTA6|Discovery")
	FString SightlineRule;
};

/** Daten-Hook fuer das NPC-System: Geruecht, das ein NPC im Bezirk erzaehlen kann. */
USTRUCT(BlueprintType)
struct MTA6_API FMTARumorHook
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	FName DiscoveryId;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	FName DistrictId;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	EMTAHintKind Kind = EMTAHintKind::Rumor;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	FString Text;
};

/** Daten-Hook fuer das Audio-System: Geraeuschquelle eines noch nicht gefundenen Ortes. */
USTRUCT(BlueprintType)
struct MTA6_API FMTASoundHook
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	FName DiscoveryId;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	FName SoundCue;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Discovery")
	float RadiusCm = 0.f;
};

/** Hilfsfunktionen fuer Bezirks-Ids und String-Enums (reines C++). */
namespace MTADiscovery
{
	/** core, saltmarket, lantern, greenmoor, rustline, sunstrip. */
	MTA6_API FName DistrictToId(EMTADistrict District);
	MTA6_API bool IdToDistrict(FName Id, EMTADistrict& OutDistrict);
	MTA6_API EMTADiscoveryType TypeFromString(const FString& S);
	MTA6_API EMTADiscoverMode ModeFromString(const FString& S);
	MTA6_API EMTARewardType RewardTypeFromString(const FString& S);
	MTA6_API EMTAHintKind HintKindFromString(const FString& S);
}
