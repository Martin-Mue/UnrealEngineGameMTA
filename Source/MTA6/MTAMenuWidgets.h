#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "MTAMenuTypes.h"
#include "MTASettingsTypes.h"

class SWidgetSwitcher;
class SVerticalBox;
class UMTASettingsSubsystem;
class UMTAGameUserSettings;

/** Rahmen, der Escape/Gamepad-B als "Zurueck" abfaengt. */
class MTA6_API SMTAScreenFrame : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMTAScreenFrame) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	FSimpleDelegate OnBack;
};

/** Hauptmenue und Pausenmenue: Titel plus senkrechte Buttonliste. */
class MTA6_API SMTAButtonMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMTAButtonMenu) {}
		SLATE_ARGUMENT(FString, TitleKey)
		SLATE_ARGUMENT(FString, SubtitleKey)
		SLATE_ARGUMENT(bool, bShowBackdrop)
		SLATE_ARGUMENT(TArray<FMTAMenuItem>, Items)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
};

/** Slot-Auswahl mit Vorschau (Spielzeit, Mission, Ort, Datum); Ueberschreiben wird bestaetigt. */
class MTA6_API SMTASlotSelect : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMTASlotSelect) {}
		SLATE_ARGUMENT(bool, bSaveMode)
		SLATE_ARGUMENT(TArray<FMTASaveSlotInfo>, Slots)
		SLATE_EVENT(FMTASlotChosen, OnChosen)
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	void RebuildDetails();
	bool IsSelectable(const FMTASaveSlotInfo& S) const;

	bool bSaveMode = false;
	TArray<FMTASaveSlotInfo> Slots;
	int32 Selected = INDEX_NONE;
	bool bConfirmOverwrite = false;
	FMTASlotChosen OnChosen;
	TSharedPtr<SVerticalBox> Details;
};

/** Optionen: Grafik, Audio, Steuerung, Gameplay, Barrierefreiheit. */
class MTA6_API SMTAOptionsMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMTAOptionsMenu) {}
		SLATE_EVENT(FSimpleDelegate, OnBack)
		/** Wird ausgeloest, wenn sich Schrift/Kontrast aendern und die Ansicht neu aufgebaut werden soll. */
		SLATE_EVENT(FSimpleDelegate, OnRebuildRequested)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;

	/** Zuletzt benutzter Reiter (bleibt beim Neuaufbau erhalten). */
	static int32 LastTab;

private:
	TSharedRef<SWidget> BuildGraphics();
	TSharedRef<SWidget> BuildAudio();
	TSharedRef<SWidget> BuildControls();
	TSharedRef<SWidget> BuildGameplay();
	TSharedRef<SWidget> BuildAccessibility();
	void RebuildBindings();
	void TryRebind(const FKey& Key);
	void NotifyChanged();

	UMTASettingsSubsystem* Sub = nullptr;
	UMTAGameUserSettings* S = nullptr;
	TSharedPtr<SWidgetSwitcher> Switcher;
	TSharedPtr<SVerticalBox> BindingList;
	FSimpleDelegate OnRebuild;

	// Zustand der Tasten-Aufnahme
	bool bCapturing = false;
	int32 CaptureEntry = INDEX_NONE;
	bool bCaptureGamepad = false;
	int32 CaptureSlot = 0;
	FString StatusKey;
	FString StatusArg;
};
