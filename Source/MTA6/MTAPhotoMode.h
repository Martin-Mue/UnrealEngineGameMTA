#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "Subsystems/WorldSubsystem.h"
#include "MTAPhotoMode.generated.h"

class SWidget;
class UMTAPhotoModeSubsystem;

/** Filter-Presets des Fotomodus. */
UENUM()
enum class EMTAPhotoFilter : uint8
{
	None, Vivid, Noir, Vintage, Cold, Warm, Cinematic, Count UMETA(Hidden)
};

/** Alle einstellbaren Werte des Fotomodus. */
struct FMTAPhotoParams
{
	float Fov = 70.f;
	/** Blende (f-Zahl), 1.4 .. 22. */
	float Aperture = 8.f;
	/** Fokusabstand in cm. */
	float FocusDistance = 1500.f;
	bool bDepthOfField = true;
	/** Belichtungskorrektur in EV. */
	float Exposure = 0.f;
	float Contrast = 1.f;
	float Saturation = 1.f;
	float Grain = 0.f;
	float Vignette = 0.2f;
	/** Weisspunkt in Kelvin. */
	float Temperature = 6500.f;
	EMTAPhotoFilter Filter = EMTAPhotoFilter::None;
	/** Tageszeit in Stunden (0..24), nur Anzeige ueber Delegate. */
	float TimeOfDay = 12.f;
	/** 0 klar, 1 bewoelkt, 2 Regen, 3 Nebel, 4 Gewitter. */
	int32 Weather = 0;
};

/** Sperre-Abfrage: true = Fotomodus gesperrt; OutReasonKey ist ein Lokalisierungsschluessel. */
DECLARE_DELEGATE_RetVal_OneParam(bool, FMTAPhotoLockQuery, FString& /*OutReasonKey*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FMTAPhotoModeChanged, bool /*bActive*/);
/** Tageszeit-Hook: Hours 0..24; -1 = urspruenglichen Wert wiederherstellen (Fotomodus beendet). */
DECLARE_MULTICAST_DELEGATE_OneParam(FMTAPhotoTimeRequested, float /*Hours*/);
/** Wetter-Hook: Index siehe FMTAPhotoParams::Weather; -1 = wiederherstellen. */
DECLARE_MULTICAST_DELEGATE_OneParam(FMTAPhotoWeatherRequested, int32 /*Weather*/);

/** Freie Kamera des Fotomodus; laeuft auch bei pausiertem Spiel. */
UCLASS(NotPlaceable)
class MTA6_API AMTAPhotoCamera : public ACameraActor
{
	GENERATED_BODY()

public:
	AMTAPhotoCamera();
	virtual void Tick(float DeltaSeconds) override;

	TWeakObjectPtr<UMTAPhotoModeSubsystem> Owner;
	/** Basisgeschwindigkeit in cm/s. */
	float MoveSpeed = 800.f;

private:
	float LockCheckTimer = 0.f;
};

/**
 * Fotomodus (Konzept 12.9): freie Kamera, Zeit einfrieren, Blende/Fokus/Belichtung/Filter,
 * Tageszeit-/Wetter-Hooks, Screenshot ueber FScreenshotRequest.
 *
 * Steuerung: WASD bewegen, Q/E runter/hoch, Shift schnell, Strg langsam, Pfeiltasten oder
 * rechte Maustaste + Maus drehen, Mausrad = Brennweite, Enter/P = Foto, H = Panel ein/aus,
 * Esc/F6 = beenden. Gamepad: linker Stick bewegen, rechter Stick drehen, Trigger hoch/runter,
 * A = Foto, B = beenden.
 *
 * Anbindung:
 *   Photo->LockQuery.BindLambda([this](FString& Reason){ if (InCombatOrWanted()) { Reason = "photo.locked_combat"; return true; } return false; });
 *   Photo->OnTimeOfDayRequested.AddUObject(WeatherSys, &UMyWeather::SetPreviewHour);
 *   Photo->OnWeatherRequested.AddUObject(WeatherSys, &UMyWeather::SetPreviewWeather);
 */
UCLASS()
class MTA6_API UMTAPhotoModeSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UMTAPhotoModeSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
	virtual void Deinitialize() override;

	bool IsActive() const { return bActive; }
	void TogglePhotoMode() { if (bActive) { ExitPhotoMode(); } else { EnterPhotoMode(); } }

	/** Startet den Fotomodus; liefert false, wenn gesperrt (Meldung wird angezeigt). */
	bool EnterPhotoMode();
	void ExitPhotoMode();

	/** Speichert ein Foto unter Saved/Screenshots/MTA6_Photos. */
	void TakePhoto();

	/** Wendet FMTAPhotoParams auf Kamera und Hooks an. */
	void ApplyParams();
	void SetFilter(EMTAPhotoFilter NewFilter);
	void SetFreezeTime(bool bFreeze);
	void SetPanelVisible(bool bVisible);
	bool IsPanelVisible() const { return bPanelVisible; }
	bool IsTimeFrozen() const { return bFrozen; }
	bool IsHudHidden() const { return bHideHud; }
	void SetHideHud(bool bHide);

	/** Prueft die Sperre (Kampf, Fahndung, Mission). */
	bool IsLocked(FString& OutReasonKey) const;

	FMTAPhotoParams Params;

	FMTAPhotoLockQuery LockQuery;
	FMTAPhotoModeChanged OnPhotoModeChanged;
	FMTAPhotoTimeRequested OnTimeOfDayRequested;
	FMTAPhotoWeatherRequested OnWeatherRequested;

	/** Letzter gespeicherter Foto-Pfad. */
	FString LastPhotoPath;

private:
	void ShowMessage(const FString& Key);

	bool bActive = false;
	bool bFrozen = true;
	bool bHideHud = false;
	bool bPanelVisible = true;
	bool bPausedByUs = false;
	bool bHudWasVisible = true;

	UPROPERTY()
	TObjectPtr<AMTAPhotoCamera> Camera;
	TWeakObjectPtr<AActor> PreviousViewTarget;
	TWeakObjectPtr<APawn> DisabledPawn;
	TSharedPtr<SWidget> PanelWidget;
	TSharedPtr<SWidget> MessageWidget;
};
