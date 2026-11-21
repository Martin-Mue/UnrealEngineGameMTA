#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "Math/RandomStream.h"
#include "MTANPCTypes.h"
#include "MTADialogueSubsystem.generated.h"

class UMTANPCSubsystem;
class UMTARelationshipSubsystem;
class UMTAGossipSubsystem;

/** Eine anzeigbare Antwortoption. */
USTRUCT(BlueprintType)
struct MTA6_API FMTADialogueOptionView
{
	GENERATED_BODY()
	/** Index fuer ChooseOption. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") int32 Index = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") FString Text;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") bool bIsTopic = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") bool bHasCheck = false;
	/** persuade (Ueberzeugen), intimidate (Einschuechtern), hack (Hacken), driving (Fahrerlebnis). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") FName Skill;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") int32 Difficulty = 0;
	/** Fertigkeit + Boni (deterministisch). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") int32 PlayerValue = 0;
	/** Ergebnis steht vorher fest (kein Wuerfel): UI kann "[Ueberzeugen 3/4]" anzeigen. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") bool bWillSucceed = false;
};

/** Der aktuell anzuzeigende Dialogzustand. */
USTRUCT(BlueprintType)
struct MTA6_API FMTADialogueView
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") bool bActive = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") FName DialogueId;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") FName NPCId;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") FString NodeId;
	/** "player", NPC-ID oder leer. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") FName SpeakerId;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") FString SpeakerName;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") FString Text;
	/** true: Optionen waehlen; false: Continue(). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") bool bIsChoice = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Dialogue") TArray<FMTADialogueOptionView> Options;
};

/** Kontext fuer Zurufe auf der Strasse. */
USTRUCT(BlueprintType)
struct MTA6_API FMTABarkContext
{
	GENERATED_BODY()
	/** greeting, reckless_driving, near_miss, crash, honk, sidewalk, nice_car, stolen_car, weather_rain, weather_fog,
	 *  weather_storm, weather_heat, time_morning, time_night, police_present, wanted_low, wanted_high, faction_idle,
	 *  announcement, conversation_interrupt, conversation_bye, trade_closed, not_available, recognize_player,
	 *  ambient_chat, argue, mediate */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Bark") FName Category;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Bark") FName District;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Bark") FName Faction;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Bark") FName Role;
	/** None = aktuelles Wetter des NPC-Systems. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Bark") FName Weather;
	/** < 0 = aktuelle Stunde. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Bark") float Hour = -1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Bark") int32 WantedLevel = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Bark") float SpeedKmh = 0.f;
	/** Cooldown ignorieren (z. B. fuer Ambient-Saetze der Simulation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Bark") bool bIgnoreCooldown = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTADialogueViewSignature, const FMTADialogueView&, View);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAConversationEnded, FName, NPCId, bool, bInterrupted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTATradeRequested, FName, NPCId, FName, ShopId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAStoryEffectRequested, const FString&, EffectJson);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FMTASkillCheckSignature, FName, Skill, int32, Difficulty, int32, Value, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMTABarkSignature, FName, NPCId, FName, Category, const FString&, Text);

/**
 * Dialogsystem: Dialoggraphen aus Content/Data/dialogues/*.json, Zurufe aus barks.json.
 *
 * KNOTEN: line (Sprecher + Text), choice (Optionen mit when/effects/check), check (Bedingung -> yes/no),
 *   effect (nur Effekte), topics ("Fragen zu ..."-Liste aus dialog.topics), rumor (NPC erzaehlt ein Geruecht),
 *   trade (Handel oeffnen ueber OnTradeRequested), end.
 * BEDINGUNGEN: Schema der Story-Engine (standing, npcTrust, flag, done, tag, aegis, any/all/not) plus
 *   skill, hour, weather, district, rumor [subject, type], relation [a, b, kind, op, wert]. "@npc" = Gespraechspartner.
 *   Story-Werte werden NUR LESEND ueber UMTAStorySubsystem::StateToJson() gelesen (Schnappschuss bei Gespraechsbeginn).
 * EFFEKTE: Story-Effekte (standing, aegis, money, unlock, lock, tag, Flags ohne Praefix dlg./npc.) werden als
 *   OnStoryEffectRequested (JSON im Story-Schema) gemeldet und bis zum naechsten Schnappschuss lokal ueberlagert.
 *   Lokal: npcTrust (Beziehungs-Subsystem; Story-Figuren zusaetzlich als Request), relation, rumor, mood, heat
 *   (direkt an UMTAHeatSubsystem), Flags dlg.* / npc.*.
 * PRUEFUNGEN: Wert = Spielerfertigkeit + Boni(when) >= Schwierigkeit -> Erfolg. Kein Zufall.
 */
UCLASS()
class MTA6_API UMTADialogueSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Dialogue")
	int32 LoadDialogues();

	UFUNCTION(BlueprintCallable, Category = "MTA6|Dialogue")
	bool LoadBarks(const FString& Path = TEXT(""));

	// --- Gespraech --------------------------------------------------------------------------

	/** Einstieg fuer die Interaktions-Komponente. false = NPC nicht ansprechbar (dann Bark "not_available"). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Dialogue")
	bool StartConversation(FName NPCId);

	/** Startet einen bestimmten Dialog (Story/Mission), ignoriert Verfuegbarkeit. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Dialogue")
	bool StartDialogue(FName DialogueId, FName NPCId = NAME_None);

	UFUNCTION(BlueprintPure, Category = "MTA6|Dialogue")
	bool IsInConversation() const { return bActive; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Dialogue")
	FMTADialogueView GetCurrentView() const { return View; }

	/** Weiter bei Zeilenknoten. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Dialogue")
	void Continue();

	UFUNCTION(BlueprintCallable, Category = "MTA6|Dialogue")
	bool ChooseOption(int32 Index);

	/** Unterbrechung (Polizei, Unfall, Wetter, Schuesse): NPC sagt einen Bark, Gespraech endet, Fortsetzung moeglich. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Dialogue")
	void InterruptConversation(FName Reason);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Dialogue")
	void EndConversation();

	/** Dialog-ID eines NPC (erste Datei mit "npc" == NPCId). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Dialogue")
	FName GetDialogueForNPC(FName NPCId) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Dialogue")
	TArray<FName> GetAllDialogueIds() const;

	// --- Spielerfertigkeiten (Progression-Agent setzt sie) ---------------------------------

	UFUNCTION(BlueprintCallable, Category = "MTA6|Dialogue")
	void SetPlayerSkill(FName Skill, int32 Level);

	UFUNCTION(BlueprintPure, Category = "MTA6|Dialogue")
	int32 GetPlayerSkill(FName Skill) const;

	// --- Flags ------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|Dialogue")
	FString GetLocalFlag(const FString& Name) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Dialogue")
	void SetLocalFlag(const FString& Name, const FString& Value);

	/** Story-Schnappschuss neu lesen (nur lesend). Verwirft die lokale Ueberlagerung. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Dialogue")
	void RefreshStorySnapshot();

	/** Wertet eine Bedingung (JSON-Objekt als Text) im Kontext eines NPC aus. Fuer Tests/Debug-Konsole. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Dialogue")
	bool EvaluateConditionJson(const FString& CondJson, FName NPCId);

	// --- Zurufe -----------------------------------------------------------------------------

	/** Waehlt einen passenden Zuruf (leer, wenn Cooldown oder kein Treffer). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Bark")
	FString GetBark(const FMTABarkContext& Context);

	/** Zuruf fuer einen NPC (Kontext aus Profil/Welt) und an dessen Actor/Delegate geben. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Bark")
	FString PlayNPCBark(FName NPCId, FName Category, int32 WantedLevel = 0, float SpeedKmh = 0.f);

	UFUNCTION(BlueprintPure, Category = "MTA6|Bark")
	int32 GetBarkLineCount() const;

	// --- Delegates --------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Dialogue") FMTADialogueViewSignature OnDialogueView;
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Dialogue") FMTAConversationEnded OnConversationEnded;
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Dialogue") FMTATradeRequested OnTradeRequested;
	/** Story-/Wirtschaftseffekt im Schema der Story-Engine, z. B. {"standing":["union",5]} oder {"money":-50}. */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Dialogue") FMTAStoryEffectRequested OnStoryEffectRequested;
	/** Fuer nutzungsbasierte Fertigkeiten (Progression-Agent). */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Dialogue") FMTASkillCheckSignature OnSkillCheck;
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Bark") FMTABarkSignature OnBark;

	FString ToJson() const;
	bool FromJson(const FString& Json);

private:
	struct FBarkGroup
	{
		FName Category;
		TArray<FName> Districts, Factions, Roles, WeatherIds;
		float HourFrom = -1.f, HourTo = -1.f;
		int32 MinWanted = -1, MaxWanted = -1;
		float MinSpeed = -1.f;
		TArray<FString> Lines;
	};

	UMTANPCSubsystem* NPCs() const;
	UMTARelationshipSubsystem* Relations() const;
	UMTAGossipSubsystem* GossipSys() const;

	TSharedPtr<FJsonObject> GetNode(const FString& NodeId) const;
	void EnterNode(const FString& NodeId);
	void BuildView(const TSharedPtr<FJsonObject>& Node);
	void BuildOptions(const TSharedPtr<FJsonObject>& Node);
	FString ResolveSpeaker(const FString& Who, FName& OutId) const;
	FName ResolveNPCRef(const FString& Ref) const;

	bool Check(const TSharedPtr<FJsonObject>& Cond) const;
	bool CheckAll(const TArray<TSharedPtr<FJsonValue>>* Conds) const;
	void Apply(const TSharedPtr<FJsonObject>& Effect);
	void ApplyAll(const TArray<TSharedPtr<FJsonValue>>* Effects);
	void RequestStoryEffect(const TSharedPtr<FJsonObject>& Effect);
	int32 ComputeCheckValue(const TSharedPtr<FJsonObject>& CheckObj) const;
	float GetTrustValue(FName NPCId) const;
	FString GetFlagValue(const FString& Name) const;
	static bool IsLocalFlag(const FString& Name);
	static bool CmpNum(const FString& Op, double A, double B);
	static FString ValueToString(const TSharedPtr<FJsonValue>& V);

	// Geladene Daten
	TMap<FName, TSharedPtr<FJsonObject>> Dialogues;
	TMap<FName, FName> DialogueByNPC;
	TArray<FBarkGroup> BarkGroups;
	TMap<FName, float> BarkCooldowns;
	TMap<FName, double> LastBarkTime;
	TArray<uint32> RecentBarkHashes;
	FRandomStream BarkRng;

	// Story-Schnappschuss (nur gelesen) + lokale Ueberlagerung
	TMap<FString, float> StoryStanding;
	TMap<FString, float> StoryTrust;
	TMap<FString, FString> StoryFlags;
	TMap<FString, TArray<FString>> StoryTags;
	TArray<FString> StoryCompleted;
	float StoryAegis = 50.f;

	// Lokaler Zustand (gespeichert)
	TMap<FString, FString> LocalFlags;
	TMap<FName, int32> PlayerSkills;
	/** Unterbrochene Gespraeche: NPC -> Knoten zum Fortsetzen. */
	TMap<FName, FString> ResumeNodes;

	// Laufendes Gespraech
	bool bActive = false;
	FName ActiveDialogue;
	FName ActiveNPC;
	FString CurrentNode;
	TSharedPtr<FJsonObject> ActiveGraph;
	FMTADialogueView View;
	/** Zuordnung Anzeigeindex -> (Option-Index | Topic-Index | -1 = zurueck). */
	TArray<TPair<int32, int32>> OptionMap; // Key: 0 = Option, 1 = Topic, 2 = Zurueck; Value: Index
};
