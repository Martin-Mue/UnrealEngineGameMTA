#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MTASettingsTypes.h"
#include "MTAAccessibilitySubsystem.generated.h"

class UMTASettingsSubsystem;
class UMaterialParameterCollection;

DECLARE_MULTICAST_DELEGATE(FMTAAccessibilityChanged);

/** Farbpalette der Menues (beruecksichtigt Hohen Kontrast und Farbenblind-Modus). */
struct FMTAUiPalette
{
	FLinearColor Background;
	FLinearColor Panel;
	FLinearColor PanelAlt;
	FLinearColor Button;
	FLinearColor ButtonSelected;
	FLinearColor Text;
	FLinearColor TextDim;
	FLinearColor Accent;
	FLinearColor Warning;
	FLinearColor Good;
};

/**
 * Barrierefreiheit (Konzept 12.6): liest die Einstellungen und stellt sie als kleine,
 * abfragbare Werte bereit. Andere Systeme (HUD, Kamera, Fahrzeug, Audio) fragen nur dieses
 * Subsystem ab und kennen die Einstellungsdatei nicht.
 *
 *  - Farbenblind: 3x3-Matrix (Daltonisierung) fuer Material-Parameter und UI-Farben.
 *  - Schrift: Skalierungsfaktor fuer UI und Untertitel.
 *  - Hoher Kontrast: eigene Palette.
 *  - Lenkassistent: dämpft Lenkausschlag bei hoher Geschwindigkeit.
 *  - Ein-Tasten-Modus, reduzierte Kamerabewegung, Lichtblitz-Reduktion.
 */
UCLASS()
class MTA6_API UMTAAccessibilitySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UMTAAccessibilitySubsystem* GetActive() { return ActiveInstance; }

	/** Liest die Werte neu aus den Einstellungen und benachrichtigt die Lauscher. */
	void Refresh();

	FMTAAccessibilityChanged OnAccessibilityChanged;

	// ---------------------------------------------------------------- Darstellung
	/** Gesamtfaktor fuer Schriftgroessen (UI-Skalierung mal Grosse Schrift). */
	float GetUiFontScale() const { return FontScale; }

	/** Faktor fuer Untertitel (0.85 / 1.0 / 1.3, zusaetzlich Grosse Schrift). */
	float GetSubtitleScale() const { return SubtitleScale; }

	bool IsHighContrast() const { return bHighContrast; }
	bool AreSubtitlesBackgrounded() const { return bSubtitleBackground; }
	const FMTAUiPalette& GetPalette() const { return Palette; }

	// -------------------------------------------------------------- Farbenblind
	EMTAColorBlindMode GetColorBlindMode() const { return ColorBlindMode; }

	/**
	 * Zeilen der 3x3-Korrekturmatrix (Linear-RGB). Identitaet bei Modus "Aus".
	 * Ausgabe = Matrix * (R, G, B).
	 */
	void GetColorBlindMatrixRows(FVector3f& OutRow0, FVector3f& OutRow1, FVector3f& OutRow2) const;

	/** Wendet die Korrektur auf eine UI-/Marker-Farbe an. */
	FLinearColor TransformColor(const FLinearColor& Color) const;

	/**
	 * Parameterwerte fuer ein optionales Material (MPC unter /Game/Materials/MPC_Accessibility):
	 * CB_Row0..2 (Vektor), CB_Enabled (Skalar). Existiert die Collection nicht, passiert nichts.
	 */
	void PushColorBlindToWorld(UWorld* World) const;

	// ------------------------------------------------------ Kamera, Licht, Eingabe
	bool ShouldReduceCameraMotion() const { return bReduceCameraMotion; }
	/** Faktor fuer Kamera-Wackeln/Bobbing (0 bei reduzierter Kamerabewegung). */
	float GetCameraShakeScale() const { return bReduceCameraMotion ? 0.f : 1.f; }
	bool ShouldReduceFlashes() const { return bReduceFlashes; }
	/** Faktor fuer Blitze, Muendungsfeuer, Strobo (0.2 wenn reduziert). */
	float GetFlashIntensityScale() const { return bReduceFlashes ? 0.2f : 1.f; }
	bool IsOneButtonMode() const { return bOneButtonMode; }
	bool IsSteeringAssistEnabled() const { return bSteeringAssist; }

	/**
	 * Lenkassistent: reduziert den Lenkausschlag mit der Geschwindigkeit (bis zu 60 % bei
	 * 150 km/h und voller Staerke) und glaettet kleine Ausschlaege.
	 */
	float ApplySteeringAssist(float Steer, float SpeedKmh) const;

	/** Soll beim Start die Lichtblitz-Warnung gezeigt werden? */
	bool ShouldShowFlashWarning() const;
	void AcknowledgeFlashWarning();

private:
	UPROPERTY()
	TObjectPtr<UMTASettingsSubsystem> SettingsSub;

	FDelegateHandle SettingsHandle;
	FDelegateHandle WorldInitHandle;

	float FontScale = 1.f;
	float SubtitleScale = 1.f;
	float AssistStrength = 0.5f;
	float ColorBlindStrength = 1.f;
	bool bHighContrast = false;
	bool bSubtitleBackground = true;
	bool bReduceCameraMotion = false;
	bool bReduceFlashes = false;
	bool bOneButtonMode = false;
	bool bSteeringAssist = false;
	EMTAColorBlindMode ColorBlindMode = EMTAColorBlindMode::Off;
	FMTAUiPalette Palette;

	static UMTAAccessibilitySubsystem* ActiveInstance;
};
