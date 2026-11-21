#pragma once

#include "CoreMinimal.h"
#include "MTASettingsTypes.generated.h"

/**
 * Gemeinsame Enums und kleine Strukturen der Einstellungen (Grafik, Gameplay, Barrierefreiheit).
 * Reihenfolge der Enum-Werte ist Teil der gespeicherten Konfiguration (GameUserSettings.ini).
 */

/** Grafik-Preset (Konzept 12.5). Custom entsteht, sobald ein Einzelwert geaendert wird. */
UENUM(BlueprintType)
enum class EMTAGraphicsPreset : uint8
{
	Low     UMETA(DisplayName = "Niedrig"),
	Medium  UMETA(DisplayName = "Mittel"),
	High    UMETA(DisplayName = "Hoch"),
	Ultra   UMETA(DisplayName = "Ultra"),
	Custom  UMETA(DisplayName = "Benutzerdefiniert")
};

/** Upscaler: Native (TAA) / TSR (Epic) / FSR (AMD, Plugin; faellt sonst auf TSR zurueck). */
UENUM(BlueprintType)
enum class EMTAUpscaler : uint8
{
	NativeTAA UMETA(DisplayName = "Nativ (TAA)"),
	TSR       UMETA(DisplayName = "TSR"),
	FSR       UMETA(DisplayName = "FSR")
};

/** Qualitaetsstufe des Upscalers; bestimmt den internen Renderanteil. */
UENUM(BlueprintType)
enum class EMTAUpscaleQuality : uint8
{
	Native      UMETA(DisplayName = "Nativ 100%"),
	Quality     UMETA(DisplayName = "Qualitaet 77%"),
	Balanced    UMETA(DisplayName = "Ausgewogen 67%"),
	Performance UMETA(DisplayName = "Leistung 50%")
};

/** Farbenblind-Modus (Parameter fuer Filter/Materialien, siehe UMTAAccessibilitySubsystem). */
UENUM(BlueprintType)
enum class EMTAColorBlindMode : uint8
{
	Off          UMETA(DisplayName = "Aus"),
	Protanopia   UMETA(DisplayName = "Protanopie (Rot)"),
	Deuteranopia UMETA(DisplayName = "Deuteranopie (Gruen)"),
	Tritanopia   UMETA(DisplayName = "Tritanopie (Blau)")
};

/** Schwierigkeitsgrad (jederzeit im Pausenmenue aenderbar, Konzept 12.6). */
UENUM(BlueprintType)
enum class EMTADifficulty : uint8
{
	Easy   UMETA(DisplayName = "Leicht"),
	Normal UMETA(DisplayName = "Normal"),
	Hard   UMETA(DisplayName = "Schwer")
};

/** Lautstaerke-Busse (Anzeige/Delegate; die Audio-Systeme lesen GetBusVolume). */
UENUM(BlueprintType)
enum class EMTAAudioBus : uint8
{
	Master UMETA(DisplayName = "Gesamt"),
	Music  UMETA(DisplayName = "Musik"),
	Radio  UMETA(DisplayName = "Radio"),
	Sfx    UMETA(DisplayName = "Effekte"),
	Voice  UMETA(DisplayName = "Sprache"),
	Count  UMETA(Hidden)
};

/** Beschreibung eines umbelegbaren Eintrags der Steuerung. */
struct FMTAKeyBindingEntry
{
	/** Name der Action bzw. Axis in DefaultInput.ini (z. B. Handbrake, Throttle). */
	FName Name;
	/** true = Axis-Mapping, false = Action-Mapping. */
	bool bAxis = false;
	/** Skalierung des Axis-Mappings (z. B. Steer links = -1, rechts = +1). */
	float Scale = 1.f;
	/** true, wenn eine Gamepad-Taste umbelegbar angeboten wird. */
	bool bGamepad = false;
	/** Lokalisierungsschluessel der Beschriftung. */
	FString LabelKey;
};
