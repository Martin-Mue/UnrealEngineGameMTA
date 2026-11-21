#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "MTAProgressionTypes.h"
#include "MTAProgressionSubsystem.generated.h"

/** Ein Skill aus Content/Data/skills.json. */
USTRUCT(BlueprintType)
struct MTA6_API FMTASkillDef
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") FName TreeId;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") FString Description;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") int32 Tier = 1;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") int32 MaxRank = 1;
	/** Perk-Punkte je Rang (Index = Rang - 1). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") TArray<int32> CostPerRank;
	/** Voraussetzungen; Rang-Anforderung in RequiredRanks (gleicher Index). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") TArray<FName> Requires;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") TArray<int32> RequiredRanks;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") int32 RequiredLevel = 1;
	/** Benoetigte Baum-Meisterschaft (nutzungsbasiert, 9.3). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") int32 RequiredMastery = 0;
	/** Effekt je Rang (stapelt: Add * Rang, Mul ^ Rang). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") TArray<FMTAStatModifier> Effects;
	/** Freigeschaltete Faehigkeiten/Inhalte ("ability.pit", "unlock.gig_tier3_all"). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") TArray<FName> Tags;
};

USTRUCT(BlueprintType)
struct MTA6_API FMTASkillTreeDef
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") FString Description;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") TArray<FName> SkillIds;
};

/** Belohnung (oder Strafe) fuer einen Fraktionsstand. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAReputationReward
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") FName Faction;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") FString Name;
	/** Positiv: ab Stand >= Schwelle; negativ (Strafe): ab Stand <= Schwelle. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") float Threshold = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") bool bPenalty = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") int64 Money = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") int32 PerkPoints = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") TArray<FName> ItemIds;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") TArray<int32> ItemCounts;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") TArray<FMTAStatModifier> Effects;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Progression") TArray<FName> Tags;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTALevelUpSignature, int32, NewLevel, int32, PerkPoints);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTASkillUnlockedSignature, FName, SkillId, int32, Rank);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAReputationRewardSignature, const FMTAReputationReward&, Reward);
DECLARE_MULTICAST_DELEGATE(FMTAModifiersChangedNative);

/**
 * Erfahrung/Level, Perk-Punkte, vier Skillbaeume mit nutzungsbasierter Meisterschaft,
 * Modifikator-Aggregation (Skills + Herkunft + Ruf + externe Quellen) und Fraktionsbelohnungen.
 * Daten: Content/Data/skills.json. Andere Subsysteme fragen Werte ueber ApplyStat() ab.
 */
UCLASS()
class MTA6_API UMTAProgressionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 SchemaVersion = 1;
	static constexpr int32 BundleSchemaVersion = 1;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Progression")
	bool LoadSkills(const FString& RelativePath = TEXT("Data/skills.json"));

	UFUNCTION(BlueprintCallable, Category = "MTA6|Progression")
	void ResetProgression();

	// --- Erfahrung ---------------------------------------------------------------------------

	/** Fuegt XP hinzu (mit Modifikator xp.gain); gibt die Zahl der Stufenaufstiege zurueck. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Progression")
	int32 AddXp(int32 Amount);

	/** XP und Baum-Erfahrung aus einer benannten Quelle (skills.json "xpSources"/"treeXpSources"), Count-fach. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Progression")
	int32 AwardXpFromSource(FName Source, float Count = 1.f);

	/** Nutzungsbasierte Erfahrung je Baum (Drift-Meter, gehackte Kameras, gelungene Dialoge). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Progression")
	void AddTreeXp(FName TreeId, float Amount);

	UFUNCTION(BlueprintPure, Category = "MTA6|Progression") int32 GetLevel() const { return Level; }
	UFUNCTION(BlueprintPure, Category = "MTA6|Progression") int64 GetXp() const { return Xp; }
	UFUNCTION(BlueprintPure, Category = "MTA6|Progression") int32 GetPerkPoints() const { return PerkPoints; }

	/** Gesamt-XP, die fuer das Erreichen von InLevel noetig sind. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Progression")
	int64 GetXpForLevel(int32 InLevel) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Progression")
	float GetLevelProgress() const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Progression")
	void AddPerkPoints(int32 Amount);

	UFUNCTION(BlueprintPure, Category = "MTA6|Progression")
	int32 GetTreeMastery(FName TreeId) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Progression")
	float GetTreeXp(FName TreeId) const;

	// --- Skills -----------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|Progression")
	TArray<FMTASkillTreeDef> GetSkillTrees() const { return Trees; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Progression")
	bool GetSkill(FName SkillId, FMTASkillDef& OutSkill) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Progression")
	int32 GetSkillRank(FName SkillId) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Progression")
	bool CanUnlockSkill(FName SkillId, FString& OutReason) const;

	/** Kauft den naechsten Rang. bFree: ohne Perk-Kosten/Voraussetzungen (Herkunft, Story). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Progression")
	bool UnlockSkill(FName SkillId, bool bFree = false);

	/** Setzt einen Baum zurueck und erstattet die Punkte; kostet Geld (economy.json prices.respec*). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Progression")
	bool RespecTree(FName TreeId);

	UFUNCTION(BlueprintPure, Category = "MTA6|Progression")
	int32 GetNumSkills() const { return Skills.Num(); }

	// --- Modifikatoren ----------------------------------------------------------------------

	/** (Base + Add) * Mul ueber alle aktiven Quellen. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Progression")
	float ApplyStat(FName Stat, float Base) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Progression")
	FMTAStatModifier GetStatModifier(FName Stat) const;

	/** Alle aktiven Stat-Modifikatoren (fuer Charaktermenue). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Progression")
	TArray<FMTAStatModifier> GetAllModifiers() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Progression")
	bool HasTag(FName Tag) const;

	/** Externe Quelle (Herkunft "origin", Module, Story-Effekte) setzen/ersetzen. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Progression")
	void SetExternalModifiers(FName Source, const TArray<FMTAStatModifier>& Mods, const TArray<FName>& InTags);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Progression")
	void ClearExternalModifiers(FName Source);

	// --- Ruf --------------------------------------------------------------------------------

	/** Stufe zum aktuellen Story-Stand ("enemy", "suspicious", "neutral", "allied", "trusted"). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Progression")
	FName GetReputationTier(FName Faction) const;

	/**
	 * Gleicht Fraktionsstaende (UMTAStorySubsystem) mit den Belohnungen ab: neue Belohnungen werden
	 * einmalig ausgezahlt (Geld, Perk-Punkte, Gegenstaende), Effekte/Tags bleiben aktiv, solange die
	 * Schwelle gehalten wird. Strafen gelten, solange der Stand darunter liegt. Nach jeder Mission aufrufen.
	 */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Progression")
	TArray<FMTAReputationReward> EvaluateReputationRewards();

	UFUNCTION(BlueprintPure, Category = "MTA6|Progression")
	TArray<FMTAReputationReward> GetRewardsForFaction(FName Faction) const;

	// --- Persistenz -------------------------------------------------------------------------

	TSharedRef<FJsonObject> ToJson() const;
	bool FromJson(const TSharedPtr<FJsonObject>& Json);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Progression")
	FString SaveStateToString() const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Progression")
	bool LoadStateFromString(const FString& Json);

	/**
	 * Sammelt Herkunft, Wirtschaft, Fortschritt, Garage und Inventar in einem JSON-Block
	 * ({ "schema", "origin", "economy", "progression", "garage", "inventory" }) fuer den SaveManager.
	 */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Progression")
	FString ExportProgressionBundle() const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Progression")
	bool ImportProgressionBundle(const FString& Json);

	TSharedRef<FJsonObject> ExportBundleJson() const;
	bool ImportBundleJson(const TSharedPtr<FJsonObject>& Bundle);

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Progression") FMTALevelUpSignature OnLevelUp;
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Progression") FMTASkillUnlockedSignature OnSkillUnlocked;
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Progression") FMTAReputationRewardSignature OnReputationReward;
	/** Feuert, wenn sich Modifikatoren aendern (Garage berechnet dann Fahrzeugwerte neu). */
	FMTAModifiersChangedNative OnModifiersChanged;

private:
	TSharedPtr<FJsonObject> Data;
	TArray<FMTASkillTreeDef> Trees;
	TMap<FName, FMTASkillDef> Skills;
	TArray<FMTAReputationReward> Rewards;
	TArray<float> MasteryThresholds;

	int64 Xp = 0;
	int32 Level = 1;
	int32 PerkPoints = 0;
	TMap<FName, int32> SkillRanks;
	TMap<FName, float> TreeXp;
	TArray<FName> ClaimedRewards;
	/** Aktive Ruf-Effekte (Belohnung gehalten oder Strafe aktiv). */
	TArray<FName> ActiveRepEffects;
	TMap<FName, TArray<FMTAStatModifier>> ExternalMods;
	TMap<FName, TArray<FName>> ExternalTags;

	mutable TMap<FName, FMTAStatModifier> ModCache;
	mutable TSet<FName> TagCache;
	mutable bool bCacheDirty = true;

	void RebuildCache() const;
	void MarkDirty();
	const FMTAReputationReward* FindReward(FName Id) const;
};
