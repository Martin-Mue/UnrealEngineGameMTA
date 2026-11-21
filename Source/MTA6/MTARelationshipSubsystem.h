#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Math/RandomStream.h"
#include "MTANPCTypes.h"
#include "MTARelationshipSubsystem.generated.h"

class UMTANPCSubsystem;

/** Kante im Beziehungsgraphen zwischen zwei NPCs (ungerichtet, A < B lexikalisch). */
USTRUCT(BlueprintType)
struct MTA6_API FMTARelationship
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Relationship") FName A;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Relationship") FName B;
	/** -100..100 */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Relationship") float Friendship = 0.f;
	/** 0..100 */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Relationship") float Rivalry = 0.f;
	/** 0..100 */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Relationship") float Love = 0.f;
	/** Schulden: > 0 heisst A schuldet B, < 0 heisst B schuldet A (Geldeinheiten). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Relationship") float Debt = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Relationship") bool bKin = false;
	/** Wissen A/B von einem Verrat? (Verrat einmal entdeckt -> Rivalitaet bleibt). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Relationship") bool bBetrayed = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Relationship") FString Note;
	/** Startwerte fuer Drift-Statistik. */
	float InitialFriendship = 0.f;
	float InitialRivalry = 0.f;
};

/**
 * Beziehungsgraph (NPC-NPC und Spieler-NPC) und Auswahl/Wirkung von NPC-NPC-Interaktionen.
 *
 * AUSWAHL (je Spielstunde, je Ort mit >= 2 anwesenden, aktiven, nicht beschaeftigten NPCs):
 *   Gewicht(Typ) = Basis(Typ) * Persoenlichkeit * Beziehung * Ort/Aktivitaet * Tageszeit.
 *   Beispiele: Plaudern ~ Talk + Freundschaft, Bar/Markt/Abend verstaerkt; Streiten ~ Temper + Rivalitaet,
 *   Bar spaet nachts verstaerkt; Handeln ~ Greed, Markt/Arbeit eines Haendlers; Vermitteln braucht einen
 *   Dritten mit Streit; Beschatten ~ Suspicion + Curiosity des Beschattenden, Ziel mit Geheimnis;
 *   Verraten ~ Greed * (1 - Loyalty) + Rivalitaet, nur selten.
 *   Wirkung: Freundschaft/Rivalitaet/Schulden/Liebe aendern sich; Beschatten/Verraten erzeugen Geruechte.
 * SIM-LOD: Orte mit Near/Mid-NPC werden paarweise detailliert simuliert (Events, Ambient-Satz);
 *   reine Far-Orte statistisch: erwartete Anzahl = Paare * Rate, gleiche Wirkungen ohne Events.
 * SPIELER: Vertrauen -3..+3 je NPC (wie npcTrust der Story-Engine) plus Zuneigung -100..100.
 */
UCLASS()
class MTA6_API UMTARelationshipSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Startkanten aus npcs.json/relations neu aufbauen. */
	void LoadFromNPCData(const TSharedPtr<class FJsonObject>& Root);

	UFUNCTION(BlueprintPure, Category = "MTA6|Relationship")
	bool GetRelationship(FName A, FName B, FMTARelationship& Out) const;

	/** Alle Kanten eines NPC. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Relationship")
	TArray<FMTARelationship> GetRelationshipsOf(FName NPCId) const;

	/** Aendert einen Wert (Kind = friendship|rivalry|love|debt). Legt Kante bei Bedarf an. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Relationship")
	void ModifyRelation(FName A, FName B, EMTARelationKind Kind, float Delta);

	/** Einfache Zusammenfassung -100..100 (Freundschaft + Liebe/2 - Rivalitaet). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Relationship")
	float GetAffinity(FName A, FName B) const;

	// --- Spieler ----------------------------------------------------------------------------

	/** Vertrauen des NPC in den Spieler -3..+3. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Relationship")
	float GetPlayerTrust(FName NPCId) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Relationship")
	void ModifyPlayerTrust(FName NPCId, float Delta);

	/** Weiche Haltung gegenueber dem Spieler -1..1 (Vertrauen + Geruechte + Fraktion), fuer Barks/Preise. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Relationship")
	float GetPlayerAttitude(FName NPCId) const;

	// --- Simulation -------------------------------------------------------------------------

	/** Eine Spielstunde NPC-NPC-Leben. Liefert die erzeugten Interaktionen. */
	void SimulateHour(FRandomStream& Rng, TArray<FMTANPCInteractionEvent>& OutEvents);

	/** Waehlt fuer ein Paar den Interaktionstyp (oder false = keine Interaktion). Oeffentlich fuer Tests. */
	bool ChooseInteraction(FName A, FName B, const TArray<FName>& Present, FRandomStream& Rng, FMTANPCInteractionEvent& Out) const;

	/** Wendet die Wirkung einer Interaktion auf die Beziehungswerte an. */
	void ApplyInteraction(const FMTANPCInteractionEvent& Event, FRandomStream& Rng);

	/** Setzt die Bezugswerte fuer die Drift-Statistik auf den aktuellen Stand. */
	void MarkDriftBaseline();

	/** Statistik: mittlere absolute Drift seit Laden bzw. MarkDriftBaseline. */
	void ComputeDrift(float& OutFriendship, float& OutRivalry, int32& OutNewEdges) const;

	FString ToJson() const;
	bool FromJson(const FString& Json);

	/** Rate je Paar und Stunde fuer die Grundwahrscheinlichkeit einer Interaktion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Relationship")
	float PairInteractionRate = 0.18f;

	/** Zerfall von Rivalitaet je Stunde ohne Kontakt (vergessen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Relationship")
	float RivalryDecayPerHour = 0.01f;

private:
	static uint64 PairKey(FName A, FName B);
	FMTARelationship& FindOrAddEdge(FName A, FName B);
	const FMTARelationship* FindEdge(FName A, FName B) const;
	UMTANPCSubsystem* NPCs() const;
	void SetIndexForEdge(int32 Index);

	TArray<FMTARelationship> Edges;
	TMap<uint64, int32> EdgeIndex;
	TMap<FName, TArray<int32>> EdgesByNPC;
	TMap<FName, float> PlayerTrust;
	int32 LoadedEdgeCount = 0;
};
