#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "MTASettingsTypes.h"
#include "MTASettingsUser.generated.h"

/**
 * Erweiterung von UGameUserSettings um alle MTA-6-Optionen. Alles wird in
 * GameUserSettings.ini gespeichert (Abschnitt [/Script/MTA6.MTAGameUserSettings]).
 *
 * Aktivierung (einmalig, Config/DefaultEngine.ini, gehoert dem Hauptagenten):
 *   [/Script/Engine.Engine]
 *   GameUserSettingsClassName=/Script/MTA6.MTAGameUserSettings
 * Ohne diesen Eintrag legt UMTASettingsSubsystem selbst eine Instanz an (gleiche INI-Sektion).
 */
UCLASS(config = GameUserSettings, configdonotcheckdefaults)
class MTA6_API UMTAGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	UMTAGameUserSettings(const FObjectInitializer& ObjectInitializer);

	/** Liefert die MTA-Einstellungen (Cast von GEngine->GetGameUserSettings(), sonst nullptr). */
	static UMTAGameUserSettings* Get();

	// --- UGameUserSettings ---
	virtual void ApplySettings(bool bCheckForCommandLineOverrides) override;
	virtual void SetToDefaults() override;

	/** Wendet nur die MTA-spezifische Grafik an (CVars); ApplySettings ruft das ebenfalls auf. */
	void ApplyMTAGraphics();

	/** Setzt alle Grafikwerte gemaess Preset (Custom aendert nichts). */
	void ApplyGraphicsPreset(EMTAGraphicsPreset Preset);

	/**
	 * Erkennt die Hardware (GPU-Name, Speicher) und waehlt ein Preset. Die Zielhardware
	 * AMD RX 5700 XT / Ryzen 5 3600 landet bei Mittel mit Upscaler-Qualitaet Ausgewogen.
	 * @return gewaehltes Preset
	 */
	EMTAGraphicsPreset DetectHardwareAndApply();

	/** true, wenn das FSR-Plugin geladen ist (CVar r.FidelityFX.FSR3.Enabled existiert). */
	static bool IsFSRAvailable();

	/** Interner Renderanteil (0.25..1) aus Upscaler-Qualitaet und Aufloesungsskalierung. */
	float GetEffectiveScreenPercentage() const;

	/** Faktor je Upscaler-Qualitaet (1.0, 0.77, 0.67, 0.5). */
	static float GetUpscaleFactor(EMTAUpscaleQuality Quality);

	/** Hat der Spieler das Tutorial-Hinweis-ID schon gesehen? */
	bool HasSeenTutorialHint(FName HintId) const { return SeenTutorialHints.Contains(HintId); }
	void MarkTutorialHintSeen(FName HintId);

	// ------------------------------------------------------------------ Grafik
	UPROPERTY(Config) EMTAGraphicsPreset GraphicsPreset;
	UPROPERTY(Config) EMTAUpscaler Upscaler;
	UPROPERTY(Config) EMTAUpscaleQuality UpscaleQuality;
	/** Zusaetzliche Aufloesungsskalierung in Prozent (50..100). */
	UPROPERTY(Config) float ResolutionScalePct;
	/** 0..4 (Niedrig..Episch), wirkt auf sg.ShadowQuality. */
	UPROPERTY(Config) int32 ShadowQualityLevel;
	/** 0..4, wirkt auf sg.GlobalIlluminationQuality (Lumen). */
	UPROPERTY(Config) int32 GIQualityLevel;
	/** 0..3, steuert r.Nanite.MaxPixelsPerEdge. */
	UPROPERTY(Config) int32 NaniteQualityLevel;
	/** 0..3, steuert Lumen-Strahlendichte und Reflexionen. */
	UPROPERTY(Config) int32 LumenQualityLevel;
	/** 0..4, wirkt auf sg.ViewDistanceQuality. */
	UPROPERTY(Config) int32 ViewDistanceLevel;
	UPROPERTY(Config) int32 TextureQualityLevel;
	UPROPERTY(Config) int32 EffectsQualityLevel;
	UPROPERTY(Config) int32 PostProcessQualityLevel;
	UPROPERTY(Config) bool bMotionBlur;
	/** Wurde die automatische Hardware-Erkennung schon einmal ausgefuehrt? */
	UPROPERTY(Config) bool bHardwareDetected;

	// ------------------------------------------------------------------- Audio
	UPROPERTY(Config) float MasterVolume;
	UPROPERTY(Config) float MusicVolume;
	UPROPERTY(Config) float RadioVolume;
	UPROPERTY(Config) float SfxVolume;
	UPROPERTY(Config) float VoiceVolume;

	// --------------------------------------------------------------- Steuerung
	/** Lenkempfindlichkeit 0.2..2.0 (1.0 = unveraendert). */
	UPROPERTY(Config) float SteeringSensitivity;
	/** Totzone des Lenk-Sticks 0..0.4. */
	UPROPERTY(Config) float SteeringDeadzone;
	/** Totzone der Trigger 0..0.4. */
	UPROPERTY(Config) float TriggerDeadzone;
	UPROPERTY(Config) float CameraSensitivity;
	UPROPERTY(Config) bool bInvertCameraY;
	UPROPERTY(Config) bool bVibration;

	/**
	 * Tastenbelegung als Textliste (Momentaufnahme aller umbelegbaren Eintraege), Format
	 * "A|Name|Key" fuer Actions und "X|Name|Scale|Key" fuer Axes. Leer = Standard aus DefaultInput.ini.
	 * UInputSettings wird bewusst nicht gespeichert (defaultconfig), sondern hier.
	 */
	UPROPERTY(Config) TArray<FString> KeyBindingOverrides;

	// ---------------------------------------------------------------- Gameplay
	UPROPERTY(Config) EMTADifficulty Difficulty;
	/** Staerke der Fahndung 0.5..2.0 (1.0 = normal). */
	UPROPERTY(Config) float WantedStrength;
	UPROPERTY(Config) bool bBrakeAssist;
	UPROPERTY(Config) bool bRouteGuidance;
	UPROPERTY(Config) bool bSubtitles;
	/** 0 klein, 1 mittel, 2 gross. */
	UPROPERTY(Config) int32 SubtitleSize;
	UPROPERTY(Config) bool bSubtitleBackground;
	UPROPERTY(Config) bool bTutorialHints;
	/** Sprachcode ("de", "en"). */
	UPROPERTY(Config) FString Language;

	// --------------------------------------------------------- Barrierefreiheit
	UPROPERTY(Config) EMTAColorBlindMode ColorBlindMode;
	/** Staerke des Farbenblind-Filters 0..1. */
	UPROPERTY(Config) float ColorBlindStrength;
	UPROPERTY(Config) bool bLargeText;
	/** UI-Skalierung 0.75..1.5. */
	UPROPERTY(Config) float UiScale;
	UPROPERTY(Config) bool bHighContrast;
	UPROPERTY(Config) bool bSteeringAssist;
	/** Staerke des Lenkassistenten 0..1. */
	UPROPERTY(Config) float SteeringAssistStrength;
	UPROPERTY(Config) bool bOneButtonMode;
	UPROPERTY(Config) bool bReduceCameraMotion;
	/** Lichtblitze reduzieren (Muendungsfeuer, Blitze, Sirenen-Strobo). */
	UPROPERTY(Config) bool bReduceFlashes;
	/** Warnung bei Lichtblitzen beim Start anzeigen; false, sobald der Spieler sie bestaetigt hat. */
	UPROPERTY(Config) bool bShowFlashWarning;

	// ---------------------------------------------------------------- Tutorial
	UPROPERTY(Config) TArray<FName> SeenTutorialHints;
};
