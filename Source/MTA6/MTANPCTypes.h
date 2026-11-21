#pragma once

#include "CoreMinimal.h"
#include "MTANPCTypes.generated.h"

/**
 * Gemeinsame Typen des NPC-Lebens (Tagesablauf, Beziehungen, Geruechte, Dialoge).
 * Inhalte stehen in Content/Data/npcs.json, barks.json und dialogues/*.json.
 * Beschreibung der Systeme: docs/NPC_LEBEN.md.
 */

/** Aktivitaet eines NPC in einem Zeitblock. Reihenfolge ist Teil des Speicherformats (Name wird gespeichert, nicht Index). */
UENUM(BlueprintType)
enum class EMTAActivity : uint8
{
	Sleep, Work, Shift, Patrol, Eat, Bar, Market, Leisure, Home, Commute, Perform, Worship, Hide, Strike, Shelter,
	Talk UMETA(DisplayName = "Im Gespraech"),
	Count UMETA(Hidden)
};

/** Simulationsstufe (Sim-LOD, Weltkonzept M5): Near = sichtbar mit Actor, Mid = im Bezirk des Spielers, Far = nur statistisch. */
UENUM(BlueprintType)
enum class EMTASimLOD : uint8
{
	Near, Mid, Far
};

/** Beziehungsart einer Kante im Beziehungsgraphen. */
UENUM(BlueprintType)
enum class EMTARelationKind : uint8
{
	Friendship, Rivalry, Debt, Love, Kin
};

/** NPC-NPC-Interaktionstypen. */
UENUM(BlueprintType)
enum class EMTAInteractionType : uint8
{
	Chat       UMETA(DisplayName = "Plaudern"),
	Trade      UMETA(DisplayName = "Handeln"),
	Argue      UMETA(DisplayName = "Streiten"),
	Mediate    UMETA(DisplayName = "Vermitteln"),
	Cooperate  UMETA(DisplayName = "Zusammenarbeiten"),
	Shadow     UMETA(DisplayName = "Beschatten"),
	Betray     UMETA(DisplayName = "Verraten"),
	Count UMETA(Hidden)
};

/** Persoenlichkeit als Merkmalsvektor, alle Werte 0..1. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAPersonality
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|NPC") float Curiosity = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|NPC") float Suspicion = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|NPC") float Humor = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|NPC") float Courage = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|NPC") float Greed = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|NPC") float Loyalty = 0.5f;
	/** Gespraechigkeit: steuert Weitererzaehlen von Geruechten und Plaudern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|NPC") float Talk = 0.5f;
	/** Reizbarkeit: steuert Streit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|NPC") float Temper = 0.5f;
};

USTRUCT(BlueprintType)
struct MTA6_API FMTANPCSecret
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FString Text;
	/** 0..1: wie schwer die Enthuellung wiegt. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") float Severity = 0.5f;
};

/** Statisches Profil einer Schluesselfigur (aus npcs.json). */
USTRUCT(BlueprintType)
struct MTA6_API FMTANPCProfile
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FString DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") int32 Age = 30;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FString Occupation;
	/** Rollen-ID (z. B. bartender, mechanic, police). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FName Role;
	/** Bezirks-ID: core, saltmarket, lantern, greenmoor, rustline, sunstrip. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FName District;
	/** Fraktions-ID wie in story.json: aurelia, kcg, union, collective, wrenchers, none. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FName Faction;
	/** Schluessel im npcTrust der Story-Engine (leer, wenn keine Story-Figur). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FString StoryKey;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FMTAPersonality Personality;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") TArray<FString> Habits;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") TArray<FMTANPCSecret> Secrets;
	/** Orts-Tags; die Welt-/Gebaeudesysteme loesen sie auf Koordinaten auf. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FName HomeLocation;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FName WorkLocation;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") TArray<FName> Haunts;
	/** Aussehen-Archetyp-ID fuer das Erscheinungsbild (anderer Agent). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FName AppearanceArchetype;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FName ScheduleId;
	/** Laden-ID, falls der NPC handelt (leer = kein Handel). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FName ShopId;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") TArray<FName> Goods;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FString Bio;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") TArray<FString> Conflicts;
};

/** Ein Block im Tagesablauf: [Start, Ende) in Spielstunden. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAScheduleBlock
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Schedule") float StartHour = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Schedule") float EndHour = 24.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Schedule") EMTAActivity Activity = EMTAActivity::Home;
	/** Orts-Tag oder Platzhalter (@home, @work, @haunt, @haunt2, @shelter). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Schedule") FName Location;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Schedule") bool bTalkable = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Schedule") bool bTrading = false;
};

/** Aufgeloester Zustand eines NPC zu einem Zeitpunkt (Ergebnis der Zeitplanauswertung). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAScheduleState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Schedule") EMTAActivity Activity = EMTAActivity::Home;
	/** Konkreter Orts-Tag (Platzhalter aufgeloest). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Schedule") FName Location;
	/** Bezirk des Ortes. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Schedule") FName District;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Schedule") bool bTalkable = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Schedule") bool bTrading = false;
	/** Grund einer Abweichung vom Plan (Wetter-/Ereignis-ID), sonst None. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Schedule") FName OverrideReason;
	/** Ende des aktuellen Blocks (Spielstunde). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Schedule") float BlockEndHour = 24.f;
};

/** Laufzeitzustand eines NPC (wird gespeichert). */
USTRUCT(BlueprintType)
struct MTA6_API FMTANPCRuntime
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FMTAScheduleState Current;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") EMTASimLOD LOD = EMTASimLOD::Far;
	/** Stimmung -1..1, driftet zu 0 zurueck. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") float Mood = 0.f;
	/** Spielstunde (absolut), bis zu der der NPC beschaeftigt ist (Interaktion/Gespraech). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") double BusyUntil = 0.0;
	/** false = aus der Welt genommen (z. B. verhaftet, geflohen; Story-Agent entscheidet). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") bool bActive = true;
};

/** Eine Interaktion zwischen zwei NPCs (fuer Delegates, Debug und Ambient-Dialoge). */
USTRUCT(BlueprintType)
struct MTA6_API FMTANPCInteractionEvent
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FName A;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FName B;
	/** Dritter Beteiligter (Vermitteln: Streitpartner; Verraten: Opfer; Beschatten: Ziel). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FName Third;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") EMTAInteractionType Type = EMTAInteractionType::Chat;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FName Location;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") double GameHour = 0.0;
	/** true, wenn mindestens ein Beteiligter Near/Mid war (sichtbar abspielbar). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") bool bVisible = false;
	/** Optionaler Ambient-Satz aus barks.json (ambient_chat/argue/mediate). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FString Line;
};

/** Spielerereignis, das Zeugen weitererzaehlen (Eingang der Geruechte). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAPlayerEventReport
{
	GENERATED_BODY()
	/** Geruechtetyp aus barks.json/rumorTypes, z. B. reckless_driving, crash, hit_pedestrian, police_chase, helped_someone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Gossip") FName Type;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Gossip") FName District;
	/** Orts-Tag oder Anzeigename (fuer {place}). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Gossip") FName Location;
	/** Schluesselfiguren, die es gesehen haben (leer = nur anonyme Passanten). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Gossip") TArray<FName> Witnesses;
	/** Anzahl anonymer Zeugen (Massen-Fussgaenger des WorldAI-Agenten). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Gossip") int32 AnonymousWitnesses = 0;
	/** 0..1 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Gossip") float Severity = 0.5f;
};

/** Ergebnis von RunNPCSimulation. */
USTRUCT(BlueprintType)
struct MTA6_API FMTANPCSimStats
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") int32 Days = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") int32 ActivityChanges = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") int32 Interactions = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") int32 DetailedInteractions = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") int32 StatisticalInteractions = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") TMap<FString, int32> InteractionsByType;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") int32 RumorsCreated = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") int32 RumorRetellings = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") int32 RumorDistrictArrivals = 0;
	/** Mittlere absolute Aenderung der Freundschaftswerte je Kante. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") float MeanFriendshipDrift = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") float MeanRivalryDrift = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") int32 NewEdges = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|NPC") FString Report;
};

/** Hilfsfunktionen (Enum <-> String fuer JSON). */
namespace MTANPC
{
	MTA6_API FString ActivityToString(EMTAActivity A);
	MTA6_API EMTAActivity ActivityFromString(const FString& S);
	MTA6_API FString InteractionToString(EMTAInteractionType T);
	MTA6_API FString RelationKindToString(EMTARelationKind K);
	MTA6_API bool RelationKindFromString(const FString& S, EMTARelationKind& Out);
	/** Laedt eine JSON-Datei unter Content/Data/<RelativePath>. */
	MTA6_API TSharedPtr<class FJsonObject> LoadJsonFile(const FString& RelativeOrAbsolutePath);
	MTA6_API FString JsonToString(const TSharedPtr<class FJsonObject>& Obj);
	MTA6_API TSharedPtr<class FJsonObject> JsonFromString(const FString& Text);
}
