#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MTAMenuTypes.h"
#include "MTAMenuController.generated.h"

class SWidget;
class UMTAMapDataStore;

/** Welcher Bildschirm gerade offen ist. */
UENUM()
enum class EMTAMenuScreen : uint8
{
	None, FlashWarning, Main, Pause, SlotsSave, SlotsLoad, Options, Map, Journal
};

/**
 * Steuert alle Slate-Menues (Hauptmenue, Pause, Slots, Optionen, Karte, Journal) ohne
 * Blueprint-/UMG-Assets. Wird einmal in die Ebene gestellt (oder per SpawnActor erzeugt);
 * zeigt die Widgets ueber UGameViewportClient::AddViewportWidgetContent.
 *
 * Hotkeys (abfragend, ohne Aenderung von DefaultInput.ini):
 *   Esc / Gamepad Start = Pause, M / Gamepad Back = Karte, J = Journal, F6 = Fotomodus.
 *
 * Anbindung durch den Hauptagenten:
 *   Menu->OnSaveRequested.AddUObject(this, &AMyMode::HandleSave);   // FString Slot
 *   Menu->OnLoadRequested.AddUObject(this, &AMyMode::HandleLoad);   // FString Slot
 *   Menu->QuerySlots.BindUObject(this, &AMyMode::FillSlotInfos);    // Vorschau-Daten
 *   Menu->SetMapDataProvider(MyProviderObject);                     // implementiert IMTAMapDataProvider
 */
UCLASS()
class MTA6_API AMTAMenuController : public AActor
{
	GENERATED_BODY()

public:
	AMTAMenuController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	// --- Bildschirme ---
	void ShowMainMenu(bool bHasSave);
	void ShowPauseMenu();
	void OpenMap();
	void OpenJournal();
	void OpenOptions();
	/** Schliesst alle Menues, hebt Pause auf, gibt die Eingabe ans Spiel zurueck. */
	void CloseAll();
	bool IsMenuOpen() const { return Screen != EMTAMenuScreen::None; }
	EMTAMenuScreen GetScreen() const { return Screen; }

	/** Kartendaten setzen (UObject muss IMTAMapDataProvider implementieren). */
	void SetMapDataProvider(UObject* Provider);
	UObject* GetMapDataProvider() const { return MapProvider; }

	// --- Anbindung (siehe Klassenkommentar) ---
	FMTASaveRequested OnSaveRequested;
	FMTALoadRequested OnLoadRequested;
	FMTANewGameRequested OnNewGameRequested;
	FMTAQuitRequested OnQuitRequested;
	FMTAQuerySlots QuerySlots;

	/** Hotkeys abfragen (abschaltbar, falls das Spiel eigene Eingabe verwendet). */
	UPROPERTY(EditAnywhere, Category = "MTA6|Menu")
	bool bPollHotkeys = true;

	/** Gesperrt, solange true (z. B. im Fotomodus oder in Zwischensequenzen). */
	bool bSuppressHotkeys = false;

private:
	void Show(EMTAMenuScreen NewScreen, TSharedRef<SWidget> Content, bool bPauseGame);
	void GoBack();
	void Rebuild();
	void BuildScreen(EMTAMenuScreen S);
	void GatherSlots(TArray<FMTASaveSlotInfo>& Out) const;
	void RemoveWidget();
	APlayerController* GetPC() const;
	void OnAccessibilityChanged();

	UPROPERTY()
	TObjectPtr<UObject> MapProvider;

	EMTAMenuScreen Screen = EMTAMenuScreen::None;
	TArray<EMTAMenuScreen> BackStack;
	TSharedPtr<SWidget> CurrentWidget;
	bool bGamePausedByMenu = false;
	bool bHasSaveForMain = false;
	/** true, solange das Hauptmenue (ausserhalb des Spiels) aktiv ist: dann wird nicht pausiert. */
	bool bFromMain = false;
	bool bRebuildPending = false;
	int32 JournalTab = 0;
	float LastFontScale = 1.f;
	bool bLastHighContrast = false;
	FDelegateHandle AccessHandle;
};
