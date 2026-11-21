#include "MTAMenuController.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/SWeakWidget.h"
#include "MTAAccessibilitySubsystem.h"
#include "MTADialogueSubsystem.h"
#include "MTALocalizationSubsystem.h"
#include "MTAMenuMap.h"
#include "MTAMenuStyle.h"
#include "MTAMenuWidgets.h"
#include "MTAPhotoMode.h"
#include "MTASettingsSubsystem.h"
#include "MTASettingsUser.h"

AMTAMenuController::AMTAMenuController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AMTAMenuController::BeginPlay()
{
	Super::BeginPlay();

	if (!MapProvider)
	{
		MapProvider = NewObject<UMTAMapDataStore>(this);
	}
	if (UMTAAccessibilitySubsystem* Acc = UMTAAccessibilitySubsystem::GetActive())
	{
		LastFontScale = Acc->GetUiFontScale();
		bLastHighContrast = Acc->IsHighContrast();
		AccessHandle = Acc->OnAccessibilityChanged.AddUObject(this, &AMTAMenuController::OnAccessibilityChanged);
	}
}

void AMTAMenuController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UMTAAccessibilitySubsystem* Acc = UMTAAccessibilitySubsystem::GetActive())
	{
		Acc->OnAccessibilityChanged.Remove(AccessHandle);
	}
	CloseAll();
	Super::EndPlay(Reason);
}

APlayerController* AMTAMenuController::GetPC() const
{
	return UGameplayStatics::GetPlayerController(this, 0);
}

void AMTAMenuController::SetMapDataProvider(UObject* Provider)
{
	if (Provider && Provider->GetClass()->ImplementsInterface(UMTAMapDataProvider::StaticClass()))
	{
		MapProvider = Provider;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("MTA6: Kartenprovider implementiert IMTAMapDataProvider nicht."));
	}
}

void AMTAMenuController::OnAccessibilityChanged()
{
	const UMTAAccessibilitySubsystem* Acc = UMTAAccessibilitySubsystem::GetActive();
	if (Acc && (!FMath::IsNearlyEqual(Acc->GetUiFontScale(), LastFontScale) || Acc->IsHighContrast() != bLastHighContrast))
	{
		LastFontScale = Acc->GetUiFontScale();
		bLastHighContrast = Acc->IsHighContrast();
		bRebuildPending = true; // im naechsten Tick (nicht mitten im Klick-Handler) neu aufbauen
	}
}

void AMTAMenuController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bRebuildPending && Screen != EMTAMenuScreen::None)
	{
		bRebuildPending = false;
		Rebuild();
		return;
	}
	bRebuildPending = false;

	APlayerController* PC = GetPC();
	if (!bPollHotkeys || bSuppressHotkeys || !PC || Screen != EMTAMenuScreen::None)
	{
		return;
	}
	if (const UMTAPhotoModeSubsystem* Photo = UMTAPhotoModeSubsystem::Get(this))
	{
		if (Photo->IsActive()) { return; } // Fotomodus hat eigene Eingabe
	}

	// Waehrend eines NPC-Gespraechs beendet Esc das Gespraech (das HUD behandelt es); kein Pausenmenue.
	bool bTalking = false;
	if (const UGameInstance* GI = GetGameInstance())
	{
		const UMTADialogueSubsystem* Dialog = GI->GetSubsystem<UMTADialogueSubsystem>();
		bTalking = Dialog && Dialog->IsInConversation();
	}

	if (!bTalking && (PC->WasInputKeyJustPressed(EKeys::Escape) || PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Right)))
	{
		ShowPauseMenu();
	}
	else if (PC->WasInputKeyJustPressed(EKeys::M) || PC->WasInputKeyJustPressed(EKeys::Gamepad_Special_Left))
	{
		OpenMap();
	}
	else if (PC->WasInputKeyJustPressed(EKeys::J))
	{
		OpenJournal();
	}
	else if (PC->WasInputKeyJustPressed(EKeys::F6))
	{
		if (UMTAPhotoModeSubsystem* Photo = UMTAPhotoModeSubsystem::Get(this)) { Photo->TogglePhotoMode(); }
	}
}

// ---------------------------------------------------------------------------- Anzeige

void AMTAMenuController::RemoveWidget()
{
	if (CurrentWidget.IsValid())
	{
		if (UWorld* W = GetWorld())
		{
			if (UGameViewportClient* VC = W->GetGameViewport())
			{
				VC->RemoveViewportWidgetContent(CurrentWidget.ToSharedRef());
			}
		}
		CurrentWidget.Reset();
	}
}

void AMTAMenuController::Show(EMTAMenuScreen NewScreen, TSharedRef<SWidget> Content, bool bPauseGame)
{
	RemoveWidget();
	Screen = NewScreen;

	TSharedRef<SMTAScreenFrame> Frame = SNew(SMTAScreenFrame)
		.OnBack(FSimpleDelegate::CreateUObject(this, &AMTAMenuController::GoBack))
		[ Content ];
	CurrentWidget = Frame;

	UWorld* World = GetWorld();
	UGameViewportClient* VC = World ? World->GetGameViewport() : nullptr;
	if (VC) { VC->AddViewportWidgetContent(Frame, 100); }

	if (APlayerController* PC = GetPC())
	{
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Frame);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
		if (bPauseGame && !bFromMain && !UGameplayStatics::IsGamePaused(this))
		{
			UGameplayStatics::SetGamePaused(this, true);
			bGamePausedByMenu = true;
		}
	}
}

void AMTAMenuController::CloseAll()
{
	RemoveWidget();
	Screen = EMTAMenuScreen::None;
	BackStack.Reset();
	if (APlayerController* PC = GetPC())
	{
		if (bGamePausedByMenu) { UGameplayStatics::SetGamePaused(this, false); }
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
	bGamePausedByMenu = false;
	bFromMain = false;
}

void AMTAMenuController::GoBack()
{
	if (Screen == EMTAMenuScreen::Main || Screen == EMTAMenuScreen::FlashWarning)
	{
		return; // vom Hauptmenue gibt es kein Zurueck
	}
	if (BackStack.Num() > 0)
	{
		const EMTAMenuScreen Prev = BackStack.Pop();
		BuildScreen(Prev);
	}
	else
	{
		CloseAll();
	}
}

void AMTAMenuController::Rebuild()
{
	BuildScreen(Screen);
}

void AMTAMenuController::ShowMainMenu(bool bHasSave)
{
	bHasSaveForMain = bHasSave;
	bFromMain = true;
	BackStack.Reset();
	const UMTAAccessibilitySubsystem* Acc = UMTAAccessibilitySubsystem::GetActive();
	if (Acc && Acc->ShouldShowFlashWarning())
	{
		BuildScreen(EMTAMenuScreen::FlashWarning);
		return;
	}
	BuildScreen(EMTAMenuScreen::Main);
}

void AMTAMenuController::ShowPauseMenu() { bFromMain = false; BackStack.Reset(); BuildScreen(EMTAMenuScreen::Pause); }
void AMTAMenuController::OpenMap() { bFromMain = false; BackStack.Reset(); JournalTab = 0; BuildScreen(EMTAMenuScreen::Map); }
void AMTAMenuController::OpenJournal() { bFromMain = false; BackStack.Reset(); JournalTab = 1; BuildScreen(EMTAMenuScreen::Journal); }
void AMTAMenuController::OpenOptions() { BuildScreen(EMTAMenuScreen::Options); }

void AMTAMenuController::GatherSlots(TArray<FMTASaveSlotInfo>& Out) const
{
	if (QuerySlots.IsBound())
	{
		QuerySlots.Execute(Out);
	}
	if (Out.Num() == 0)
	{
		// Ersatzliste ohne Daten: Autosave plus fuenf leere Slots.
		FMTASaveSlotInfo Auto; Auto.SlotName = TEXT("Auto"); Auto.DisplayName = TEXT("Auto"); Auto.bAutosave = true;
		Out.Add(Auto);
		for (int32 i = 1; i <= 5; ++i)
		{
			FMTASaveSlotInfo S; S.SlotName = FString::Printf(TEXT("Slot%d"), i); S.DisplayName = FString::Printf(TEXT("Slot %d"), i);
			Out.Add(S);
		}
	}
}

void AMTAMenuController::BuildScreen(EMTAMenuScreen S)
{
	auto Item = [](const TCHAR* Key, FSimpleDelegate D, bool bEnabled = true)
	{
		FMTAMenuItem I; I.LabelKey = Key; I.OnClick = D; I.bEnabled = bEnabled; return I;
	};
	// Merkt den aktuellen Bildschirm fuer "Zurueck", wenn wir tiefer navigieren.
	auto Push = [this](EMTAMenuScreen Next)
	{
		if (Screen != EMTAMenuScreen::None) { BackStack.Add(Screen); }
		BuildScreen(Next);
	};

	switch (S)
	{
	case EMTAMenuScreen::FlashWarning:
	{
		TArray<FMTAMenuItem> Items;
		Items.Add(Item(TEXT("flash.ok"), FSimpleDelegate::CreateLambda([this]()
		{
			if (UMTAAccessibilitySubsystem* Acc = UMTAAccessibilitySubsystem::GetActive()) { Acc->AcknowledgeFlashWarning(); }
			BuildScreen(EMTAMenuScreen::Main);
		})));
		Items.Add(Item(TEXT("flash.reduce"), FSimpleDelegate::CreateLambda([this]()
		{
			if (UMTAAccessibilitySubsystem* Acc = UMTAAccessibilitySubsystem::GetActive()) { Acc->AcknowledgeFlashWarning(); }
			if (UMTASettingsSubsystem* Sub = UMTASettingsSubsystem::GetActive())
			{
				if (UMTAGameUserSettings* St = Sub->GetSettings()) { St->bReduceFlashes = true; }
				Sub->ApplyAndSave();
			}
			BuildScreen(EMTAMenuScreen::Main);
		})));
		Show(S, SNew(SMTAButtonMenu).TitleKey(TEXT("flash.title")).SubtitleKey(TEXT("flash.text")).bShowBackdrop(true).Items(Items), false);
		break;
	}
	case EMTAMenuScreen::Main:
	{
		BackStack.Reset();
		TArray<FMTAMenuItem> Items;
		Items.Add(Item(TEXT("menu.main.continue"), FSimpleDelegate::CreateLambda([this]()
		{
			// Neuesten vorhandenen Slot laden.
			TArray<FMTASaveSlotInfo> Slots; GatherSlots(Slots);
			const FMTASaveSlotInfo* Best = nullptr;
			for (const FMTASaveSlotInfo& I : Slots) { if (I.bExists && (!Best || I.SavedAt > Best->SavedAt)) { Best = &I; } }
			if (Best) { const FString Name = Best->SlotName; CloseAll(); OnLoadRequested.Broadcast(Name); }
		}), bHasSaveForMain));
		Items.Add(Item(TEXT("menu.main.new"), FSimpleDelegate::CreateLambda([this]() { CloseAll(); OnNewGameRequested.Broadcast(); })));
		Items.Add(Item(TEXT("menu.main.load"), FSimpleDelegate::CreateLambda([this, Push]() { Push(EMTAMenuScreen::SlotsLoad); })));
		Items.Add(Item(TEXT("menu.main.options"), FSimpleDelegate::CreateLambda([this, Push]() { Push(EMTAMenuScreen::Options); })));
		Items.Add(Item(TEXT("menu.main.quit"), FSimpleDelegate::CreateLambda([this]() { OnQuitRequested.Broadcast(); })));
		Show(S, SNew(SMTAButtonMenu).TitleKey(TEXT("menu.title")).SubtitleKey(TEXT("menu.subtitle")).bShowBackdrop(true).Items(Items), false);
		break;
	}
	case EMTAMenuScreen::Pause:
	{
		TArray<FMTAMenuItem> Items;
		Items.Add(Item(TEXT("menu.pause.resume"), FSimpleDelegate::CreateLambda([this]() { CloseAll(); })));
		Items.Add(Item(TEXT("menu.pause.save"), FSimpleDelegate::CreateLambda([this, Push]() { Push(EMTAMenuScreen::SlotsSave); })));
		Items.Add(Item(TEXT("menu.pause.load"), FSimpleDelegate::CreateLambda([this, Push]() { Push(EMTAMenuScreen::SlotsLoad); })));
		Items.Add(Item(TEXT("menu.pause.options"), FSimpleDelegate::CreateLambda([this, Push]() { Push(EMTAMenuScreen::Options); })));
		Items.Add(Item(TEXT("menu.pause.map"), FSimpleDelegate::CreateLambda([this, Push]() { JournalTab = 0; Push(EMTAMenuScreen::Map); })));
		Items.Add(Item(TEXT("menu.pause.journal"), FSimpleDelegate::CreateLambda([this, Push]() { JournalTab = 1; Push(EMTAMenuScreen::Journal); })));
		Items.Add(Item(TEXT("menu.pause.photo"), FSimpleDelegate::CreateLambda([this]()
		{
			CloseAll();
			if (UMTAPhotoModeSubsystem* Photo = UMTAPhotoModeSubsystem::Get(this)) { Photo->EnterPhotoMode(); }
		})));
		Items.Add(Item(TEXT("menu.pause.quit"), FSimpleDelegate::CreateLambda([this]() { CloseAll(); OnQuitRequested.Broadcast(); })));
		Show(S, SNew(SMTAButtonMenu).TitleKey(TEXT("menu.pause.title")).SubtitleKey(TEXT("menu.subtitle")).bShowBackdrop(false).Items(Items), true);
		break;
	}
	case EMTAMenuScreen::SlotsSave:
	case EMTAMenuScreen::SlotsLoad:
	{
		const bool bSave = (S == EMTAMenuScreen::SlotsSave);
		TArray<FMTASaveSlotInfo> Slots; GatherSlots(Slots);
		Show(S, SNew(SMTASlotSelect).bSaveMode(bSave).Slots(Slots)
			.OnChosen(FMTASlotChosen::CreateLambda([this, bSave](const FString& Slot)
			{
				if (bSave) { OnSaveRequested.Broadcast(Slot); BuildScreen(EMTAMenuScreen::SlotsSave); } // Liste neu lesen
				else { CloseAll(); OnLoadRequested.Broadcast(Slot); }
			}))
			.OnBack(FSimpleDelegate::CreateUObject(this, &AMTAMenuController::GoBack)), true);
		break;
	}
	case EMTAMenuScreen::Options:
		Show(S, SNew(SMTAOptionsMenu)
			.OnBack(FSimpleDelegate::CreateUObject(this, &AMTAMenuController::GoBack))
			.OnRebuildRequested(FSimpleDelegate::CreateLambda([this]() { bRebuildPending = true; })), true);
		break;
	case EMTAMenuScreen::Map:
	case EMTAMenuScreen::Journal:
		Show(S, SNew(SMTAJournal).Provider(MapProvider.Get()).InitialTab(S == EMTAMenuScreen::Map ? 0 : JournalTab)
			.OnClose(FSimpleDelegate::CreateUObject(this, &AMTAMenuController::GoBack)), true);
		break;
	default:
		CloseAll();
		break;
	}
}
