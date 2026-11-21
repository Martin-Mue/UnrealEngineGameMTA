#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Math/RandomStream.h"
#include "MTANPCTypes.h"
#include "MTAGossipSubsystem.generated.h"

class UMTANPCSubsystem;

/** Ein Geruecht. Das Original bleibt erhalten; jede Weitererzaehlung erhoeht die Verzerrung. */
USTRUCT(BlueprintType)
struct MTA6_API FMTARumor
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") int32 Id = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") FName Type;
	/** "player" oder NPC-ID. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") FName Subject;
	/** Geheimnis-ID bei npc_secret. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") FName SecretId;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") FName OriginDistrict;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") FName Location;
	/** 0..1, waechst mit Uebertreibung. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") float Severity = 0.5f;
	/** 0..1 Glaubwuerdigkeit; sinkt mit Verzerrung. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") float Credibility = 1.f;
	/** 0..1 Verzerrung; bestimmt die Textstufe (0: <0.34, 1: <0.67, 2: sonst). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") float Distortion = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") double CreatedHour = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") int32 Retellings = 0;
	/** Schluesselfiguren, die es kennen (mit eigener Verzerrung). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") TMap<FName, float> KnownBy;
	/** Bekanntheit je Bezirk 0..1 (statistisch: Anteil der Bevoelkerung). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") TMap<FName, float> DistrictReach;
	/** Geplante Ankunft in Nachbarbezirken (absolute Spielstunde). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") TMap<FName, double> PendingArrival;
	/** Der Spieler hat es gehoert. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") bool bHeardByPlayer = false;
	/** Wirkung wurde bereits auf die Fahndung angewendet (je Bezirk einmal). */
	TSet<FName> HeatApplied;
};

/** Wirkung der Geruechte eines Bezirks (fuer Wirtschaft, Haltung, Polizei). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAGossipDistrictEffect
{
	GENERATED_BODY()
	/** Preis-Multiplikator fuer den Spieler (1 = neutral). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") float PriceMultiplier = 1.f;
	/** Haltung der Bevoelkerung zum Spieler -1..1. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") float Attitude = 0.f;
	/** Fahndungsdruck 0..5 (Zusatz-Wachsamkeit der KCG). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") float HeatPressure = 0.f;
	/** Ruf je Fraktion (aufsummierte Verschiebung). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Gossip") TMap<FName, float> FactionShift;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTARumorSignature, const FMTARumor&, Rumor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTARumorArrived, const FMTARumor&, Rumor, FName, District);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAGossipEffectChanged, FName, District, const FMTAGossipDistrictEffect&, Effect);

/**
 * Geruechte-Verbreitung (Weltkonzept 9.2 "Zeugen"): Spielerereignisse werden von Zeugen weitererzaehlt,
 * veraendern sich (Verzerrung, Uebertreibung), erreichen Nachbarbezirke zeitverzoegert und beeinflussen
 * Preise, Haltung und Fahndung.
 *
 * VERBREITUNG je Spielstunde:
 *   - Persoenlich: bei NPC-NPC-Plaudern/Handeln/Zusammenarbeiten gibt der Erzaehler jedes bekannte Geruecht
 *     mit p = Talk * Glaubwuerdigkeit * 0.6 weiter; Verzerrung += (1 - Loyalty*0.5) * 0.12 * (0.5 + Humor),
 *     Schwere += Verzerrungsanteil * 0.15 (Uebertreibung).
 *   - Statistisch: DistrictReach waechst logistisch (Rate 0.08 * Glaubwuerdigkeit), zerfaellt nach 72 h.
 *   - Bezirke: erreicht Reach im Bezirk 0.35, wird die Ankunft in jedem Nachbarbezirk nach der Kanten-
 *     verzoegerung (npcs.json/districts.neighbors, Stunden) geplant; bei Ankunft Reach = 0.05, Verzerrung + 0.1.
 *   - Fahndung: Typ mit heat > 0 und Reach >= 0.5 im Bezirk -> einmalig UMTAHeatSubsystem::AddHeat(Bezirk,
 *     heat * Schwere) (wenn bApplyHeat) und OnGossipEffectChanged.
 */
UCLASS()
class MTA6_API UMTAGossipSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Laedt rumorTypes aus barks.json. */
	bool LoadRumorTypes(const TSharedPtr<class FJsonObject>& BarksRoot);

	/** Eingang fuer Zeugen-/Interaktions-/Chaos-Systeme: Spieler hat etwas getan. Liefert Geruecht-ID. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Gossip")
	int32 ReportPlayerEvent(const FMTAPlayerEventReport& Report);

	/** Ein NPC erzaehlt etwas (Dialog-Effekt "rumor" oder Beschatten/Verrat). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Gossip")
	int32 CreateRumor(FName Type, FName Subject, FName Teller, FName District, FName Location, float Severity, FName SecretId = NAME_None);

	/** Persoenliche Weitergabe zwischen zwei NPCs (Teller -> Listener). Liefert Anzahl weitergegebener Geruechte. */
	int32 ShareBetween(FName Teller, FName Listener, FRandomStream& Rng);

	/** Eine Spielstunde statistische Ausbreitung. */
	void SimulateHour(FRandomStream& Rng);

	/** Geruechte, die ein NPC kennt (optional nur ueber ein Subjekt/Typ). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Gossip")
	TArray<FMTARumor> GetRumorsKnownBy(FName NPCId, FName SubjectFilter = NAME_None, FName TypeFilter = NAME_None) const;

	/** Kennt der NPC (oder sein Bezirk mit Reach >= 0.3) ein Geruecht dieses Typs ueber das Subjekt? */
	UFUNCTION(BlueprintPure, Category = "MTA6|Gossip")
	bool DoesNPCKnow(FName NPCId, FName Subject, FName Type) const;

	/** Text eines Geruechts aus Sicht eines Erzaehlers (Verzerrungsstufe, Platzhalter ersetzt). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Gossip")
	FString GetRumorText(int32 RumorId, FName Teller) const;

	/** Bestes Geruecht, das der NPC dem Spieler erzaehlen wuerde (markiert es als gehoert). -1 = keins. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Gossip")
	int32 PickRumorForPlayer(FName Teller, FName SubjectFilter = NAME_None);

	UFUNCTION(BlueprintPure, Category = "MTA6|Gossip")
	FMTAGossipDistrictEffect GetDistrictEffect(FName District) const;

	/** Preisfaktor fuer den Spieler bei einem Haendler (Bezirk + Fraktion). Fuer den Wirtschafts-Agenten. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Gossip")
	float GetPriceModifier(FName District, FName Faction) const;

	/** Bekanntheit des Spielers in einem Bezirk 0..1 (fuer Barks "recognize_player"). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Gossip")
	float GetPlayerNotoriety(FName District) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Gossip")
	int32 GetRumorCount() const { return Rumors.Num(); }

	const FMTARumor* FindRumor(int32 Id) const;

	/** Wenn true, ruft das System UMTAHeatSubsystem::AddHeat direkt (Bezirks-IDs wie in npcs.json). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Gossip")
	bool bApplyHeat = true;

	/** Maximale Anzahl gleichzeitig gefuehrter Geruechte (aelteste schwache werden entfernt). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Gossip")
	int32 MaxRumors = 256;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Gossip")
	FMTARumorSignature OnRumorCreated;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Gossip")
	FMTARumorArrived OnRumorReachedDistrict;

	/** Preise/Haltung/Fahndung eines Bezirks haben sich veraendert. Wirtschaft/Polizei/HUD abonnieren. */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Gossip")
	FMTAGossipEffectChanged OnGossipEffectChanged;

	FString ToJson() const;
	bool FromJson(const FString& Json);

	/** Statistik fuer RunNPCSimulation. */
	int32 StatCreated = 0;
	int32 StatRetellings = 0;
	int32 StatArrivals = 0;

private:
	struct FRumorTypeDef
	{
		float Price = 0.f;
		float Attitude = 0.f;
		float Heat = 0.f;
		TMap<FName, float> Factions;
		TArray<TArray<FString>> Text; // [Stufe][Variante]
	};

	UMTANPCSubsystem* NPCs() const;
	FMTARumor* FindRumorMut(int32 Id);
	void RecomputeDistrictEffect(FName District);
	void ApplyHeatIfNeeded(FMTARumor& R, FName District);
	void Prune();
	static int32 DistortionLevel(float D) { return D < 0.34f ? 0 : (D < 0.67f ? 1 : 2); }

	TMap<FName, FRumorTypeDef> Types;
	TArray<FMTARumor> Rumors;
	TMap<FName, FMTAGossipDistrictEffect> Effects;
	int32 NextId = 1;
};
