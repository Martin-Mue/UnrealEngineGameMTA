#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MTAMenuTypes.h"
#include "MTAMinimapTypes.h"
#include "MTA6GameMode.generated.h"

class UMTAMissionComponent;
class UMTAHeatSubsystem;
class AMTAVehiclePawn;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMTAGameEventSignature);

/**
 * Standard-GameMode von MTA 6 (Port des Spielflusses aus godot/main.gd).
 * Wird in Config/DefaultEngine.ini als globaler Default gesetzt.
 * Haelt Geld, Tageszeit, Fahndung (ueber UMTAHeatSubsystem), Festnahme, Wrack-Logik,
 * Toast-Meldungen, Minimap-Daten und Speichern/Laden. Missionen: UMTAMissionComponent.
 */
UCLASS(Config = Game)
class MTA6_API AMTA6GameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMTA6GameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	/** Das beim Start neben der Spielfigur erzeugte Auto. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Setup")
	TObjectPtr<APawn> StartVehicle;

	/** Erzeugt das Startauto neben der Startposition. */
	void SpawnStartVehicle();

	/** Verkehr, Fussgaenger und Polizei (Simulation und Darstellung). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|AI")
	TObjectPtr<class AMTAWorldAIManager> WorldAI;

	/** Startet die Welt-KI und bindet Treffer, Festnahme und Minimap an. */
	void SpawnWorldAI();

	UFUNCTION()
	void HandlePlayerHitByAI(float Damage, float Impulse, FVector Dir);

	UFUNCTION()
	void HandleArrested();

	/** Liefert die KI-Agenten als Minimap-Punkte. */
	void GatherAIBlips(TArray<FMTAMinimapBlip>& OutBlips);

	/** Menue (Esc = Pause, M = Karte, J = Journal, F6 = Fotomodus) und Datenspeicher der Karte. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|UI")
	TObjectPtr<class AMTAMenuController> Menu;

	UPROPERTY()
	TObjectPtr<class UMTAMapDataStore> MapStore;

	/** Erzeugt die ansprechbaren Schluessel-NPCs (nur Figuren mit Dialog) in ihren Bezirken. */
	void SpawnDialogueNPCs();

	/** Automatischer Selbsttest (Start mit Parameter -MTA6SelfTest): prueft Kernfunktionen und beendet das Spiel. */
	void RunSelfTest();
	FTimerHandle SelfTestHandle;

	void SpawnMenu();
	void HandleMenuSave(FString Slot);
	void HandleMenuLoad(FString Slot);
	void HandleMenuNew();
	void HandleMenuQuit();
	void FillSlotInfos(TArray<FMTASaveSlotInfo>& OutSlots);
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// --- Einstellungen --------------------------------------------------------------

	/**
	 * Fahrzeug-Blueprint, der DefaultPawnClass ersetzt, falls das Asset existiert.
	 * Ueberschreibbar in DefaultGame.ini:
	 * [/Script/MTA6.MTA6GameMode]
	 * VehiclePawnClassPath=/Game/Vehicles/BP_MTAVehicle.BP_MTAVehicle_C
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "MTA6|Setup", meta = (MetaClass = "/Script/Engine.Pawn"))
	FSoftClassPath VehiclePawnClassPath;

	/** Startposition, falls die Karte keinen PlayerStart hat (Godot: (-4, 1.2, -40) m). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "MTA6|Setup")
	FVector StartLocation = FVector(-400.f, -4000.f, 150.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "MTA6|Setup")
	FRotator StartRotation = FRotator(0.f, 90.f, 0.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "MTA6|Economy")
	int32 StartMoney = 1000;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "MTA6|Time")
	float StartHour = 18.f;

	/** Echtzeit-Sekunden pro Spielstunde. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "MTA6|Time", meta = (ClampMin = "0.1"))
	float SecondsPerGameHour = 20.f;

	/** Bezirk, auf den die (vorerst globale) Fahndung im UMTAHeatSubsystem gebucht wird. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Law")
	FName HeatDistrict = TEXT("KestrelBay");

	/** Fahndungsabbau pro Sekunde, wenn keine Polizei in der Naehe ist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Law")
	float HeatDecayPerSecond = 0.12f;

	/** Sekunden ohne neues Vergehen, bevor die Fahndung sinkt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Law")
	float HeatDecayDelay = 8.f;

	/** Polizei naeher als dieser Radius (cm) verhindert den Fahndungsabbau. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Law")
	float PoliceNearRadius = 6000.f;

	/** Festnahme: Polizei naeher als BustRadius, Spieler langsamer als BustMaxSpeed, laenger als BustTime. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Law")
	float BustRadius = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Law")
	float BustMaxSpeed = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Law")
	float BustTime = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Law")
	int32 BustFine = 300;

	/** Sekunden bis ein Wrack abgeschleppt und repariert wird. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle")
	float WreckRespawnDelay = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Vehicle")
	int32 RepairCost = 200;

	// --- Komponenten / Ereignisse -----------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MTA6|Mission")
	TObjectPtr<UMTAMissionComponent> MissionComponent;

	/** Nach einer Festnahme (KI soll Streifenwagen entfernen). */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Law")
	FMTAGameEventSignature OnBusted;

	/** Nach dem Laden eines Spielstands (KI soll Streifenwagen entfernen). */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Save")
	FMTAGameEventSignature OnGameLoaded;

	/** C++-Alternative zu IMTAMinimapProvider: Punkte fuer Minimap und Festnahme liefern. */
	FMTAGatherMinimapBlips OnGatherMinimapBlips;

	// --- Geld / Fahndung / Zeit ---------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	int32 GetMoney() const { return Money; }

	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	void AddMoney(int32 Amount);

	/** Vergehen melden: erhoeht die Fahndung und startet die Abbau-Wartezeit neu. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Law")
	void ReportCrime(float HeatAmount);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Law")
	void SetHeat(float NewHeat);

	UFUNCTION(BlueprintPure, Category = "MTA6|Law")
	float GetHeat() const;

	/** 0..5 Sterne (Godot: ceil(heat - 0.001)). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Law")
	int32 GetWantedStars() const;

	/** Gewuenschte Zahl an Streifenwagen fuer die KI (Godot: 0 oder min(8, 1 + Sterne)). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Law")
	int32 GetDesiredPoliceCount() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Time")
	float GetHour() const { return Hour; }

	UFUNCTION(BlueprintCallable, Category = "MTA6|Time")
	void SetHour(float NewHour);

	/** Spielzeit in s, laeuft waehrend Dialogen nicht (fuer Blinken, Abbau-Wartezeit). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Time")
	float GetGameTime() const { return GameTime; }

	// --- Spielfluss -------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	UMTAMissionComponent* GetMissionComponent() const { return MissionComponent; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	bool IsDialogOpen() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Vehicle")
	AMTAVehiclePawn* GetPlayerVehicle() const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|UI")
	void ShowToast(const FString& Text, float Duration = 3.f);

	UFUNCTION(BlueprintPure, Category = "MTA6|UI")
	FString GetToastText() const { return ToastText; }

	/** Deckkraft der Toast-Meldung 0..1 (blendet in der letzten Sekunde aus). */
	UFUNCTION(BlueprintPure, Category = "MTA6|UI")
	float GetToastAlpha() const { return FMath::Clamp(ToastTimeLeft, 0.f, 1.f); }

	/** true, solange die FESTGENOMMEN-Einblendung laeuft. */
	UFUNCTION(BlueprintPure, Category = "MTA6|UI")
	bool IsBustedOverlayActive() const { return BustedOverlayTimeLeft > 0.f; }

	// --- Minimap ------------------------------------------------------------------------

	/** Objekt (Actor, Komponente, Subsystem), das IMTAMinimapProvider implementiert, anmelden. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Minimap")
	void RegisterMinimapProvider(UObject* Provider);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Minimap")
	void UnregisterMinimapProvider(UObject* Provider);

	/** In diesem Frame gesammelte Punkte. */
	const TArray<FMTAMinimapBlip>& GetMinimapBlips() const { return CachedBlips; }

	// --- Speichern / Laden ----------------------------------------------------------------

	/** F5 bzw. Autosave (bAutosave: keine eigene Toast-Meldung). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Save")
	bool SaveGameToSlot(bool bAutosave = false);

	/** F9. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Save")
	bool LoadGameFromSlot();

private:
	void GatherMinimapBlips();
	void UpdateLaw(float DeltaSeconds);
	void UpdateWreck(float DeltaSeconds);
	void Bust();
	void MovePlayerToStart();
	bool HasPlayerStart() const;
	UMTAHeatSubsystem* GetHeatSubsystem() const;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UObject>> MinimapProviders;

	TArray<FMTAMinimapBlip> CachedBlips;

	int32 Money = 1000;
	float Hour = 18.f;
	float GameTime = 0.f;
	float LastCrimeTime = -99.f;
	float BustTimer = 0.f;
	float WreckTimer = 0.f;
	float FallbackHeat = 0.f;

	FString ToastText;
	float ToastTimeLeft = 0.f;
	float BustedOverlayTimeLeft = 0.f;
};
