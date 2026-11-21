#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MTASettingsTypes.h"
#include "MTASettingsSubsystem.generated.h"

class UMTAGameUserSettings;

DECLARE_MULTICAST_DELEGATE(FMTASettingsChanged);
DECLARE_MULTICAST_DELEGATE_TwoParams(FMTAAudioVolumeChanged, EMTAAudioBus /*Bus*/, float /*EffectiveVolume*/);

/**
 * Zentrale Anlaufstelle fuer alle Optionen (Grafik, Audio, Steuerung, Gameplay,
 * Barrierefreiheit). Haelt die UMTAGameUserSettings, wendet sie an, speichert sie in
 * GameUserSettings.ini und verwaltet die Tastenbelegung ueber UInputSettings.
 *
 * Anbindung der Audio-Systeme: GetBusVolume(Bus) abfragen und OnAudioVolumeChanged binden.
 * Anbindung der Fahrzeugsteuerung: ApplySteeringSensitivity(Roh) auf die Lenkachse anwenden;
 * Totzonen werden bereits ueber die Achsenkonfiguration von UInputSettings angewendet.
 */
UCLASS()
class MTA6_API UMTASettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UMTASettingsSubsystem* GetActive() { return ActiveInstance; }

	/** Die Einstellungen (nie nullptr nach Initialize). */
	UMTAGameUserSettings* GetSettings() const { return Settings; }

	// ------------------------------------------------------------------ Anwenden
	/** Wendet alle Werte an, speichert sie und benachrichtigt die Lauscher. */
	void ApplyAndSave();

	/** Verwirft ungespeicherte Aenderungen (laedt aus der INI). */
	void RevertChanges();

	/** Setzt alle Optionen auf Standard (Sprache bleibt). */
	void ResetAllToDefaults();

	/** Preset anwenden (ohne zu speichern; ApplyAndSave folgt in der UI). */
	void ApplyPreset(EMTAGraphicsPreset Preset);

	/** Markiert das Grafikpreset als "Benutzerdefiniert" (nach Einzeländerung). */
	void MarkCustomPreset();

	/** Erkennt die Hardware und waehlt das passende Preset (RX 5700 XT -> Mittel). */
	EMTAGraphicsPreset AutoDetectHardware();

	/** Meldung "Hardware wurde erkannt" fuer die Anzeige. */
	FString GetDetectedHardwareName() const;

	/** Wird nach ApplyAndSave und Zuruecksetzen ausgeloest. */
	FMTASettingsChanged OnSettingsChanged;

	// --------------------------------------------------------------------- Audio
	/** Effektive Lautstaerke 0..1 (Bus * Master; Master liefert nur den Master-Wert). */
	float GetBusVolume(EMTAAudioBus Bus) const;

	/** Setzt eine Busslautstaerke und leitet sie sofort weiter (Hoerprobe beim Ziehen). */
	void SetBusVolume(EMTAAudioBus Bus, float Volume);

	FMTAAudioVolumeChanged OnAudioVolumeChanged;

	// ------------------------------------------------------------------ Steuerung
	/** Liste aller umbelegbaren Eintraege (Reihenfolge = Anzeige). */
	static const TArray<FMTAKeyBindingEntry>& GetBindingEntries();

	/** Liest die Tasten eines Eintrags (Tastatur/Maus oder Gamepad getrennt). */
	void GetBindingKeys(const FMTAKeyBindingEntry& Entry, bool bGamepad, TArray<FKey>& OutKeys) const;

	/**
	 * Belegt Slot (0/1) eines Eintrags neu. Wird die Taste bereits von einem anderen
	 * Eintrag derselben Geraeteart benutzt, wird sie dort entfernt.
	 * @return Lokalisierungsschluessel des verdraengten Eintrags oder leer
	 */
	FString Rebind(const FMTAKeyBindingEntry& Entry, bool bGamepad, int32 Slot, FKey NewKey);

	/** Stellt die Standardbelegung (wie DefaultInput.ini plus Gamepad) wieder her. */
	void ResetBindingsToDefault();

	/** Legt Gamepad-Standardbelegung an, falls fuer eine Achse/Action noch keine existiert. */
	void EnsureGamepadDefaults();

	/** Uebertraegt Totzonen und Kamera-Empfindlichkeit in UInputSettings::AxisConfig. */
	void ApplyControllerSettings();

	/** Lenkempfindlichkeit auf einen Rohwert -1..1 anwenden (Ergebnis -1..1). */
	float ApplySteeringSensitivity(float Raw) const;

	// ------------------------------------------------------------------- Hilfsfunktionen
	/** Sichtweite etc. fuer Anzeige: Namen der Preset-Stufe als Lokalisierungsschluessel. */
	static FString PresetKey(EMTAGraphicsPreset Preset);

private:
	void ApplyAudio();

	UPROPERTY()
	TObjectPtr<UMTAGameUserSettings> Settings;

	static UMTASettingsSubsystem* ActiveInstance;
};
