#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Widgets/SCompoundWidget.h"
#include "InputCoreTypes.h"
#include "MTACharacterScreen.generated.h"

class SBox;
class SScrollBox;
class UGameInstance;
class AMTAMenuController;

/** Reiter der Charakter-Oberflaeche. */
enum class EMTACharTab : uint8
{
	Skills = 0, Garage, Inventory, Account, Factions, Origin, Count
};

/**
 * Charakter-Oberflaeche (Slate, ohne Blueprint-Assets): Faehigkeiten, Garage, Inventar, Konto,
 * Fraktionen, Herkunft. Liest die Daten bei jedem Neuaufbau frisch aus den Subsystemen.
 *
 * Bedienung: Q/E, PageUp/PageDown oder LB/RB wechseln den Reiter, Pfeile/Steuerkreuz/Stick waehlen
 * Schaltflaechen, Enter/A bestaetigt, rechter Stick oder Mausrad scrollt, Esc/B/I schliesst.
 * Schriftgroessen: Fliesstext 22 Punkt (mal UI-Skalierung der Barrierefreiheit), Ueberschriften 30-44.
 */
class MTA6_API SMTACharScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMTACharScreen) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UGameInstance>, GameInstance)
		SLATE_ARGUMENT(int32, InitialTab)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;

	int32 GetTab() const { return Tab; }

private:
	void SetTab(int32 NewTab);
	void StepTab(int32 Delta);
	/** Baut den Inhalt im naechsten Slate-Tick neu auf (nie mitten im Klick-Handler). */
	void RequestRefresh();
	void RebuildContent(bool bRestoreFocus = true);
	void SetStatus(const FString& Text, bool bGood);

	TSharedRef<SWidget> BuildSkills();
	TSharedRef<SWidget> BuildGarage();
	TSharedRef<SWidget> BuildInventory();
	TSharedRef<SWidget> BuildAccount();
	TSharedRef<SWidget> BuildFactions();
	TSharedRef<SWidget> BuildOrigin();
	TSharedRef<SWidget> BuildMissing(const FString& SystemName) const;

	/** Bezirk fuer Preise/Reparatur (aktueller Bezirk der Fahndung, sonst "core"). */
	FName CurrentDistrict() const;

	TWeakObjectPtr<UGameInstance> GI;
	FSimpleDelegate OnCloseDelegate;
	int32 Tab = 0;
	bool bRefreshQueued = false;
	float KeepScrollOffset = 0.f;

	TSharedPtr<SBox> ContentBox;
	TSharedPtr<SScrollBox> ContentScroll;

	// Auswahl und Rueckmeldung
	FName SelectedVehicle;
	FName PendingRespecTree;
	FString StatusText;
	bool bStatusGood = true;
};

/**
 * Oeffnet die Charakter-Oberflaeche mit Taste I (Gamepad: Steuerkreuz oben). Wird einmal per
 * SpawnActor erzeugt (siehe docs/INTEGRATION_SPEICHERN.md); arbeitet wie AMTAMenuController
 * (abfragende Hotkeys, Eingabemodus Nur-UI, Spiel pausiert, solange offen).
 */
UCLASS()
class MTA6_API AMTACharacterScreenController : public AActor
{
	GENERATED_BODY()

public:
	AMTACharacterScreenController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	void OpenScreen(int32 InitialTab = 0);
	void CloseScreen();
	void ToggleScreen();
	bool IsOpen() const { return Widget.IsValid(); }

	/** Optional: Menue-Controller, damit nicht gleichzeitig zwei Bildschirme offen sind. */
	void SetMenuController(AMTAMenuController* InMenu);

	UPROPERTY(EditAnywhere, Category = "MTA6|CharacterScreen")
	bool bPollHotkeys = true;

	UPROPERTY(EditAnywhere, Category = "MTA6|CharacterScreen")
	FKey OpenKey = EKeys::I;

	UPROPERTY(EditAnywhere, Category = "MTA6|CharacterScreen")
	FKey OpenGamepadKey = EKeys::Gamepad_DPad_Up;

	/** Gesperrt, solange true (Fotomodus, Zwischensequenzen). */
	bool bSuppressHotkeys = false;

private:
	APlayerController* GetPC() const;
	void OnAccessibilityChanged();

	TSharedPtr<SMTACharScreen> Widget;
	TWeakObjectPtr<AMTAMenuController> Menu;
	bool bPausedByUs = false;
	bool bRebuildPending = false;
	int32 SavedTab = 0;
	float LastFontScale = 1.f;
	bool bLastHighContrast = false;
	FDelegateHandle AccessHandle;
};
