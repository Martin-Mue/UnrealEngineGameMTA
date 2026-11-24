#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "MTAMenuTypes.h"
#include "MTASaveManager.generated.h"

/**
 * Alles, was nur der GameMode kennt und der SaveManager nicht selbst aus den Subsystemen lesen kann.
 * Wird beim Speichern vom GameMode gefuellt und beim Laden vom Manager zurueckgegeben; der GameMode
 * wendet die Werte danach an (Geld, Uhrzeit, Positionen, Fahrzeugzustand, Fahndung, Mission).
 */
class UMTASaveGame;

struct FMTASaveContext
{
	/** Geld im GameMode (die Wirtschaft liegt zusaetzlich im Fortschritts-Bundle). */
	int32 Money = 0;
	/** Tageszeit in Stunden 0..24. */
	float Hour = 18.f;
	/** Gesamte Spielzeit in Sekunden. */
	float PlaytimeSeconds = 0.f;

	bool bHasPlayerTransform = false;
	FTransform PlayerTransform = FTransform::Identity;

	bool bHasVehicle = false;
	/** true, wenn der Spieler im Fahrzeug sitzt (sonst steht es nur geparkt). */
	bool bInVehicle = true;
	FTransform VehicleTransform = FTransform::Identity;
	float VehicleHealth = 100.f;
	/** Tankfuellung 0..1. */
	float VehicleFuel = 1.f;

	/** Globale Fahndung 0..5. */
	float Heat = 0.f;
	/** Aktueller Bezirk (Id, z. B. "saltmarket"); nur fuer die Anzeige im Menue. */
	FName District = NAME_None;

	/** Anzeigename der aktuellen Mission (Menue-Vorschau). */
	FString MissionLabel;
	/** true: Story-Mission (wird aus dem Story-Zustand neu bestimmt); false: freie Kurierfahrt. */
	bool bStoryMission = true;
	FVector MissionTarget = FVector::ZeroVector;
	float MissionTimeLeft = 0.f;
};

/** Erweiterte Metadaten eines Speicherplatzes (mehr, als FMTASaveSlotInfo im Menue zeigt). */
struct FMTASaveSlotMeta
{
	int32 Slot = 0;
	FString SlotName;
	/** Es gibt eine lesbare Datei (Hauptdatei oder Sicherung). */
	bool bExists = false;
	/** Datei vorhanden, aber Hauptdatei und Sicherung sind unbrauchbar. */
	bool bCorrupt = false;
	/** Hauptdatei war beschaedigt, die Metadaten stammen aus der Sicherung. */
	bool bFromBackup = false;
	/** Spielstand stammt aus einer neueren Spielversion (kann nicht geladen werden). */
	bool bTooNew = false;
	int32 Schema = 0;
	FString GameVersion;
	FDateTime SavedAt = FDateTime(0);
	float PlaytimeSeconds = 0.f;
	FString Mission;
	FString District;
	int32 Money = 0;
	float Hour = 0.f;
};

/**
 * Einheitlicher Spielstand (Konzept Kapitel 11): mehrere Plaetze (Autosave + 3 manuelle), pro Platz
 * Metadaten fuer das Menue, ein Bundle aller Subsysteme (jedes in einem eigenen Feld), Schema-Version
 * mit Migrationskette (v1/v2 = alter UMTASaveGame, v3 = JSON), Pruefsumme, atomares Schreiben mit
 * Sicherung (.bak) und Rueckfall auf die Sicherung bei beschaedigter Datei.
 *
 * Datei: <Projekt>/Saved/SaveGames/MTA6_<Platz>.json   (Auto, Slot1, Slot2, Slot3), daneben .bak/.tmp/.corrupt
 *   { "schema":3, "gameVersion", "savedAt", "slot", "meta":{...}, "checksum":"md5", "payload":"<JSON-Text>" }
 *   payload = { "context", "progression", "story", "npc", "discovery", "worldEvents", "interior", "heat" }
 *   progression = UMTAProgressionSubsystem::ExportBundleJson (Herkunft, Wirtschaft, Fortschritt, Garage, Inventar)
 *   npc = UMTANPCSubsystem::SaveStateToJson (enthaelt Beziehungen, Geruechte, Dialogzustand)
 *
 * Fehlende Felder lassen das jeweilige Subsystem unveraendert (Story und Entdeckungen werden
 * zurueckgesetzt); unbekannte Felder werden ignoriert. Details: docs/INTEGRATION_SPEICHERN.md.
 */
UCLASS()
class MTA6_API UMTASaveManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentSchema = 3;
	/** Platz 0 = Autosave, 1..NumManualSlots = manuell. */
	static constexpr int32 AutosaveSlot = 0;
	static constexpr int32 NumManualSlots = 3;
	/** Versteckter Platz fuer den Selbsttest (nie im Menue). */
	static constexpr int32 SelfTestSlot = 99;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Bequemer Zugriff ueber irgendein Objekt der Welt. */
	static UMTASaveManager* Get(const UObject* WorldContext);

	// --- Speichern / Laden --------------------------------------------------------------------

	/** Speichert den gesamten Zustand in den Platz. Bei Fehler false, Grund in GetLastError(). */
	bool SaveToSlot(int32 Slot, const FMTASaveContext& Ctx);

	/**
	 * Laedt den Platz und wendet alle Subsystem-Zustaende an. Liefert den GameMode-Anteil in OutCtx.
	 * Bei unbrauchbarer Datei (und Sicherung) bleibt der Spielzustand unveraendert und false kommt zurueck.
	 */
	bool LoadFromSlot(int32 Slot, FMTASaveContext& OutCtx);

	/** Autosave in Platz 0. */
	bool Autosave(const FMTASaveContext& Ctx);

	/** Wie Autosave, aber hoechstens alle MinIntervalSeconds echte Sekunden (false = uebersprungen). */
	bool TryAutosave(const FMTASaveContext& Ctx, float MinIntervalSeconds = 30.f);

	bool DoesSlotExist(int32 Slot) const;
	bool DeleteSlot(int32 Slot);

	/** Slot-Liste fuer das Menue: Autosave + 3 manuelle Plaetze (SlotName "Auto", "Slot1".."Slot3"). */
	void GetSlotInfos(TArray<FMTASaveSlotInfo>& OutInfos) const;
	void GetSlotMetas(TArray<FMTASaveSlotMeta>& OutMetas) const;
	bool ReadSlotMeta(int32 Slot, FMTASaveSlotMeta& OutMeta) const;

	/** Platz mit dem juengsten Speicherzeitpunkt (fuer "Weiter"), INDEX_NONE wenn keiner existiert. */
	int32 GetNewestSlot() const;

	/** "Auto"/"Slot1".. <-> Platznummer. NameToSlot liefert INDEX_NONE bei unbekanntem Namen. */
	static FString SlotToName(int32 Slot);
	static int32 NameToSlot(const FString& SlotName);

	FString GetSlotFilePath(int32 Slot) const;

	/** Letzte Fehlermeldung (deutsch, fuer Toast/Dialog); leer, wenn alles gut ging. */
	const FString& GetLastError() const { return LastError; }
	/** Warnungen des letzten Ladens (z. B. ein Subsystem meldete Fehler beim Wiederherstellen). */
	const TArray<FString>& GetLastWarnings() const { return LastWarnings; }
	/** Das letzte Laden kam aus der Sicherung (Hauptdatei war beschaedigt). */
	bool DidLastLoadUseBackup() const { return bLastLoadUsedBackup; }

	/** Uebernimmt einen alten Spielstand (UMTASaveGame, "MTA6_Slot1") einmalig in Platz 1. Wird bei Initialize aufgerufen. */
	bool MigrateLegacySave();

	// --- Test -----------------------------------------------------------------------------------

	/**
	 * Selbsttest: veraendert Zustand in mehreren Subsystemen, speichert, aendert wieder, laedt und
	 * prueft Gleichheit je Subsystem; testet Sicherung/Beschaedigung, Migration (v1/v2) und zu neue
	 * Versionen. Der urspruengliche Zustand wird am Ende wiederhergestellt. Bericht als Text.
	 */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Save")
	FString RunSaveSelfTest();

private:
	// Dateiebene
	bool ReadSlotFile(const FString& Path, TSharedPtr<FJsonObject>& OutRoot, FString& OutError) const;
	bool WriteSlotFile(int32 Slot, const TSharedRef<FJsonObject>& Root);
	TSharedRef<FJsonObject> BuildFileRoot(int32 Slot, const FMTASaveContext& Ctx, const TSharedRef<FJsonObject>& Payload) const;
	/** Liest Hauptdatei, sonst Sicherung. Schema-Pruefung/Migration erfolgt separat. */
	bool ReadWithFallback(int32 Slot, TSharedPtr<FJsonObject>& OutRoot, bool& bOutFromBackup, FString& OutError) const;

	// Schema
	static bool MigrateRoot(TSharedPtr<FJsonObject>& InOutRoot, FString& OutError);
	static void MigrateV1ToV2(const TSharedPtr<FJsonObject>& Root);
	static bool MigrateV2ToV3(TSharedPtr<FJsonObject>& InOutRoot, FString& OutError);
	static FString ComputeChecksum(const FString& Payload);
	static TSharedRef<FJsonObject> LegacyToV2Json(const UMTASaveGame& Legacy);

	// Zustand
	TSharedRef<FJsonObject> BuildPayload(const FMTASaveContext& Ctx) const;
	bool ApplyPayload(const TSharedPtr<FJsonObject>& Payload, FMTASaveContext& OutCtx, TArray<FString>& OutWarnings);

	FString LastError;
	TArray<FString> LastWarnings;
	bool bLastLoadUsedBackup = false;
	double LastAutosaveRealTime = -1000.0;
};
