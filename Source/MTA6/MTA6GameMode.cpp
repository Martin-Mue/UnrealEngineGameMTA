#include "MTA6GameMode.h"

#include "MTACharacter.h"
#include "MTAHeatSubsystem.h"
#include "MTAHUD.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "MTAAudioAmbienceSubsystem.h"
#include "MTACharacterScreen.h"
#include "MTASaveManager.h"
#include "Sound/SoundBase.h"
#include "MTAInteriorSubsystem.h"
#include "MTABuildingEntry.h"
#include "MTADiscoverySubsystem.h"
#include "MTAEconomySubsystem.h"
#include "MTAProgressionTypes.h"
#include "MTARadioSubsystem.h"
#include "MTAWeatherManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "MTAInteractionInterface.h"
#include "TimerManager.h"
#include "MTACityGenerator.h"
#include "MTADialogueNPC.h"
#include "MTAWorldDetail.h"
#include "MTALivingNPC.h"
#include "MTALivingWorld.h"
#include "MTADialogueSubsystem.h"
#include "MTANPCSubsystem.h"
#include "MTAMenuController.h"
#include "MTAMenuMap.h"
#include "MTAWorldAIManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "MTAMissionComponent.h"
#include "MTAMissionRunner.h"
#include "MTAWildernessBuilder.h"
#include "MTAAudioVehicleComponent.h"
#include "MTASaveGame.h"
#include "MTAStorySubsystem.h"
#include "MTAVehiclePawn.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

AMTA6GameMode::AMTA6GameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	HUDClass = AMTAHUD::StaticClass();
	// Der Spieler startet zu Fuss (Mannequin) neben seinem Auto und steigt mit F ein.
	DefaultPawnClass = AMTACharacter::StaticClass();
	VehiclePawnClassPath = FSoftClassPath(TEXT("/Game/Vehicles/BP_MTAVehicle.BP_MTAVehicle_C"));

	MissionComponent = CreateDefaultSubobject<UMTAMissionComponent>(TEXT("MissionComponent"));
	MissionRunner = CreateDefaultSubobject<UMTAMissionRunner>(TEXT("MissionRunner"));
}

void AMTA6GameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

}

void AMTA6GameMode::SpawnWorldAI()
{
	UWorld* World = GetWorld();
	if (!World || WorldAI)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	WorldAI = World->SpawnActor<AMTAWorldAIManager>(AMTAWorldAIManager::StaticClass(), FTransform::Identity, Params);
	if (!WorldAI)
	{
		return;
	}
	WorldAI->OnPlayerHitByAI.AddDynamic(this, &AMTA6GameMode::HandlePlayerHitByAI);
	WorldAI->OnArrested.AddDynamic(this, &AMTA6GameMode::HandleArrested);
	OnGatherMinimapBlips.AddUObject(this, &AMTA6GameMode::GatherAIBlips);
}

void AMTA6GameMode::SpawnDialogueNPCs()
{
	// Lebendige Welt: 65 Profile plus Strassenfiguren mit Tagesablauf, Budget 80 sichtbare Figuren.
	if (AMTALivingWorldManager* Living = AMTALivingWorldManager::GetOrSpawn(this))
	{
		UE_LOG(LogTemp, Display, TEXT("MTA6: Lebendige Welt gestartet (%d Figuren)"), Living->GetAgentCount());
	}
}

void AMTA6GameMode::SetupWorldSystems()
{
	UGameInstance* GI = GetGameInstance();
	Weather = AMTAWeatherManager::GetOrSpawn(this);
	// Umgebungsklang: importierte Schleifen (/Game/Audio/Generated/amb_*) spielen lassen
	if (UWorld* World = GetWorld())
	{
		if (UMTAAmbienceSubsystem* Ambience = World->GetSubsystem<UMTAAmbienceSubsystem>())
		{
			Ambience->SetPlaybackEnabled(true);
		}
	}
	if (GI)
	{
		if (UMTARadioSubsystem* Radio = GI->GetSubsystem<UMTARadioSubsystem>())
		{
			Radio->TurnOn();
		}
	}
}

void AMTA6GameMode::TickWorldSystems(float DeltaSeconds)
{
	UGameInstance* GI = GetGameInstance();
	UWorld* World = GetWorld();
	if (!GI || !World)
	{
		return;
	}
	const float HoursPassed = DeltaSeconds / FMath::Max(0.1f, SecondsPerGameHour);
	if (UMTAEconomySubsystem* Econ = GI->GetSubsystem<UMTAEconomySubsystem>())
	{
		Econ->AdvanceTime(HoursPassed);
	}

	// Die uebrigen Systeme muessen nicht in jedem Frame aktualisiert werden.
	WorldSystemsTimer += DeltaSeconds;
	if (WorldSystemsTimer < 0.5f)
	{
		return;
	}
	const float Dt = WorldSystemsTimer;
	WorldSystemsTimer = 0.f;

	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	const FVector Loc = Player ? Player->GetActorLocation() : StartLocation;
	const int32 Day = FMath::FloorToInt(GameTime / (24.f * FMath::Max(0.1f, SecondsPerGameHour)));
	const AMTACityGenerator* City = AMTACityGenerator::FindCityGenerator(this);
	const EMTADistrict District = City ? City->GetDistrictAtLocation(Loc) : EMTADistrict::Core;
	const FName DistrictId = MTAProgression::DistrictToId(District);

	if (Weather)
	{
		Weather->SetGameHour(Hour);
		Weather->SetDistrict(DistrictId);
	}
	if (UMTARadioSubsystem* Radio = GI->GetSubsystem<UMTARadioSubsystem>())
	{
		Radio->SetGameHour(Hour);
		Radio->SetDistrict(DistrictId);
		Radio->SetWantedLevel(GetWantedStars());
	}
	if (UMTAAmbienceSubsystem* Ambience = World->GetSubsystem<UMTAAmbienceSubsystem>())
	{
		Ambience->SetListenerContext(District, Hour, 16.f, false);
	}
	if (UMTANPCSubsystem* Npcs = GI->GetSubsystem<UMTANPCSubsystem>())
	{
		Npcs->SetGameTime(Day, Hour);
		Npcs->SetPlayerDistrict(DistrictId);
	}
	if (UMTADiscoverySubsystem* Disc = GI->GetSubsystem<UMTADiscoverySubsystem>())
	{
		Disc->SetEnvironment(Hour, Day, FName("clear"));
		Disc->UpdatePlayer(Loc, Dt);
		const int32 Reward = Disc->ConsumeMoney();
		if (Reward != 0)
		{
			AddMoney(Reward);
			ShowToast(FString::Printf(TEXT("Entdeckung! +$%d"), Reward));
		}
		const FString Effects = Disc->ConsumeStoryEffectsJson();
		if (!Effects.IsEmpty() && Effects != TEXT("[]"))
		{
			if (UMTAStorySubsystem* Story = GI->GetSubsystem<UMTAStorySubsystem>())
			{
				Story->ApplyEffectsJson(Effects);
			}
		}
	}
}

void AMTA6GameMode::SpawnEnterableBuildings()
{
	UWorld* World = GetWorld();
	const AMTACityGenerator* City = AMTACityGenerator::FindCityGenerator(this);
	if (!World || !City)
	{
		return;
	}
	// Naechste Kreuzung zum Startpunkt
	const int32 IX = FMath::Clamp(MTACity::NearestLine(StartLocation.X), 0, MTACity::Blocks);
	const int32 IY = FMath::Clamp(MTACity::NearestLine(StartLocation.Y), 0, MTACity::Blocks);
	const FVector Crossing = City->GetIntersectionLocation(IX, IY);

	int32 Count = 0;
	for (int32 DX = -1; DX <= 0; ++DX)
	{
		for (int32 DY = -1; DY <= 0; ++DY)
		{
			const int32 BX = IX + DX;
			const int32 BY = IY + DY;
			if (BX < 0 || BY < 0 || BX >= MTACity::Blocks || BY >= MTACity::Blocks)
			{
				continue;
			}
			const FVector BlockCenter = City->GetBlockCenterLocation(BX, BY);
			FVector ToCrossing = Crossing - BlockCenter;
			ToCrossing.Z = 0.f;
			const FVector Street = ToCrossing.GetSafeNormal();
			const FVector LotCenter = BlockCenter + Street * 1600.f;
			if (AMTABuildingEntry::SpawnForLot(World, FVector(LotCenter.X, LotCenter.Y, 30.f), FVector2D(2400.f, 2400.f), 1500.f,
				Street, City->GetBlockDistrict(BX, BY), 100 + BX * 10 + BY))
			{
				++Count;
			}
		}
	}
	UE_LOG(LogTemp, Display, TEXT("MTA6: %d begehbare Gebaeude erzeugt"), Count);
}

void AMTA6GameMode::RunSelfTest()
{
	UWorld* World = GetWorld();
	UGameInstance* GI = GetGameInstance();
	int32 Failures = 0;
	auto Check = [&Failures](const TCHAR* Name, bool bOk)
	{
		UE_LOG(LogTemp, Display, TEXT("SELFTEST %s: %s"), Name, bOk ? TEXT("OK") : TEXT("FEHLER"));
		if (!bOk) { ++Failures; }
	};

	// 1) Grundobjekte
	APawn* Hero = UGameplayStatics::GetPlayerPawn(this, 0);
	Check(TEXT("Spielfigur vorhanden"), Cast<AMTACharacter>(Hero) != nullptr);
	Check(TEXT("Startauto vorhanden"), StartVehicle != nullptr);
	Check(TEXT("Welt-KI vorhanden"), WorldAI != nullptr);
	Check(TEXT("Menue vorhanden"), Menu != nullptr);
	Check(TEXT("Stadtgenerator vorhanden"), AMTACityGenerator::FindCityGenerator(this) != nullptr);

	// 2) Lebendige Welt und NPC-Gespraech
	AMTALivingWorldManager* Living = AMTALivingWorldManager::Get(this);
	Check(TEXT("Lebendige Welt vorhanden"), Living != nullptr);
	UMTADialogueSubsystem* Dialog = GI ? GI->GetSubsystem<UMTADialogueSubsystem>() : nullptr;
	if (Living)
	{
		const FString LivingReport = Living->RunLivingWorldSelfTest();
		Check(TEXT("Lebendige Welt Selbsttest"), LivingReport.Contains(TEXT("ERGEBNIS: 0 Fehler")));
	}
	if (Hero && Living && Dialog)
	{
		// Eine ansprechbare Dialogfigur neben die Spielfigur holen und das Gespraech durchklicken
		AMTALivingNPC* Npc = Living->BringDialogueNPCTo(Hero->GetActorLocation() + Hero->GetActorForwardVector() * 150.f);
		Check(TEXT("Dialogfigur geholt"), Npc != nullptr);
		if (Npc)
		{
			IMTAInteractable::Execute_Interact(Npc, Hero);
			if (!Dialog->IsInConversation())
			{
				// Figur hat laut Tagesablauf gerade keine Zeit: Dialog direkt starten
				Dialog->StartDialogue(Dialog->GetDialogueForNPC(Npc->NPCId), Npc->NPCId);
			}
			Check(TEXT("Gespraech startet"), Dialog->IsInConversation());
			int32 Steps = 0;
			while (Dialog->IsInConversation() && Steps < 40)
			{
				const FMTADialogueView View = Dialog->GetCurrentView();
				if (View.bIsChoice && View.Options.Num() > 0) { Dialog->ChooseOption(View.Options.Last().Index); }
				else { Dialog->Continue(); }
				++Steps;
			}
			UE_LOG(LogTemp, Display, TEXT("SELFTEST Gespraech: %d Schritte bis zum Ende"), Steps);
			Check(TEXT("Gespraech endet"), !Dialog->IsInConversation());
		}
	}

	// 2b) Weltsysteme und begehbare Gebaeude
	Check(TEXT("Wetter-Manager"), Weather != nullptr);
	Check(TEXT("Radio-Subsystem"), GI && GI->GetSubsystem<UMTARadioSubsystem>() != nullptr);
	Check(TEXT("Entdeckungs-Subsystem"), GI && GI->GetSubsystem<UMTADiscoverySubsystem>() != nullptr);
	Check(TEXT("Wirtschafts-Subsystem"), GI && GI->GetSubsystem<UMTAEconomySubsystem>() != nullptr);
	TArray<AMTABuildingEntry*> Entries;
	for (TActorIterator<AMTABuildingEntry> It(World); It; ++It) { Entries.Add(*It); }
	Check(TEXT("begehbare Gebaeude vorhanden"), Entries.Num() >= 1);
	UMTAInteriorSubsystem* Interiors = UMTAInteriorSubsystem::Get(this);
	Check(TEXT("Innenraum-Subsystem"), Interiors != nullptr);
	if (Hero && Interiors && Entries.Num() > 0)
	{
		const bool bEntered = Interiors->EnterInterior(Cast<APawn>(Hero), Entries[0], 0);
		UE_LOG(LogTemp, Display, TEXT("SELFTEST Betreten (Gebaeude %s): %s"), *Entries[0]->GetName(), bEntered ? TEXT("ja") : TEXT("nein (z. B. verschlossen)"));
	}

	// 2b2) Importierte Audio-Assets
	Check(TEXT("Radio-Musik importiert"), LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/Generated/radio_neon_tide_a.radio_neon_tide_a")) != nullptr);
	Check(TEXT("Umgebungsklang importiert"), LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/Generated/amb_city.amb_city")) != nullptr);

	// 2b3) Welt-Details (Meer, Berge, Landmarken, Moebel, begehbare Gebaeude)
	if (AMTAWorldDetail* Detail = AMTAWorldDetail::FindWorldDetail(this))
	{
		const FString WorldReport = Detail->RunWorldSelfTest();
		UE_LOG(LogTemp, Display, TEXT("SELFTEST Welt: %s"), *WorldReport.Left(240));
		Check(TEXT("Welt-Details Selbsttest"), WorldReport.StartsWith(TEXT("WORLD SELFTEST OK")));
	}
	else
	{
		Check(TEXT("Welt-Details vorhanden"), false);
	}

	// 2c) Einfuehrung und Kodex
	Check(TEXT("Einfuehrung geladen (7 Seiten)"), IntroPages.Num() == 7);
	Check(TEXT("Kodex geladen (5 Kategorien)"), CodexCategories.Num() == 5);
	OpenCodex();
	Check(TEXT("Kodex-Index oeffnet"), IsCodexIndexOpen());
	OpenCodexCategory(3);
	Check(TEXT("Kodex-Kategorie Personen"), GetStoryPageCount() == 10);
	CloseStoryPages();
	Check(TEXT("Esc im Kodex fuehrt zum Index"), IsCodexIndexOpen());
	CloseStoryPages();
	Check(TEXT("Kodex schliesst"), !IsStoryPageOpen());
	ShowIntro();
	for (int32 i = 0; i < 20 && IsStoryPageOpen(); ++i) { AdvanceStoryPage(); }
	Check(TEXT("Einfuehrung laeuft durch"), !IsStoryPageOpen());

	// 3) Speichern und Laden
	Money = 4321;
	const bool bSaved = SaveGameToSlot(false, 1);
	Money = 1;
	const bool bLoaded = LoadGameFromSlot(1);
	Check(TEXT("Speichern"), bSaved);
	Check(TEXT("Laden"), bLoaded);
	Check(TEXT("Geld nach Laden"), Money == 4321);

	// 3b) Kompletter Speicher-Selbsttest des SaveManagers (alle Subsysteme, Migration, beschaedigte Dateien)
	if (UMTASaveManager* SaveMgr = GI ? GI->GetSubsystem<UMTASaveManager>() : nullptr)
	{
		const FString Report = SaveMgr->RunSaveSelfTest();
		UE_LOG(LogTemp, Display, TEXT("%s"), *Report);
		Check(TEXT("SaveManager-Selbsttest"), Report.Contains(TEXT(": 0 Fehler,")));
	}
	Check(TEXT("Charakter-Oberflaeche"), CharacterScreen != nullptr);

	// 4) Story
	UMTAStorySubsystem* Story = GI ? GI->GetSubsystem<UMTAStorySubsystem>() : nullptr;
	Check(TEXT("Story verfuegbare Missionen"), Story && Story->GetAvailableMissions().Num() > 0);

	// 5) Missionssystem (simulierte Auftraege aller Typen, Fehlschlaege, Durchspielen der Story-Missionen)
	if (MissionRunner)
	{
		const FString MissionReport = MissionRunner->RunMissionSelfTest();
		UE_LOG(LogTemp, Display, TEXT("%s"), *MissionReport);
		Check(TEXT("Missionssystem"), MissionReport.Contains(TEXT("MISSIONTEST ERGEBNIS")) && !MissionReport.Contains(TEXT(": FEHLER")));
	}

	// 6) Fahrzeug- und Figurenmodelle der Welt-KI
	if (WorldAI)
	{
		const FString ModelReport = WorldAI->RunModelSelfTest();
		UE_LOG(LogTemp, Display, TEXT("%s"), *ModelReport);
		Check(TEXT("Modelle"), ModelReport.Contains(TEXT("MODELLTEST ERGEBNIS: 0 Fehler")));
	}

	{
		const FString AudioReport = UMTAVehicleAudioComponent::RunVehicleAudioSelfTest();
		UE_LOG(LogTemp, Display, TEXT("%s"), *AudioReport);
		Check(TEXT("Fahrzeugsound"), AudioReport.Contains(TEXT("FAHRZEUGSOUND ERGEBNIS: 0 Fehler")));
	}

	if (AMTAWildernessBuilder* Wild = AMTAWildernessBuilder::FindWildernessBuilder(this))
	{
		const FString WildReport = Wild->RunWildernessSelfTest();
		UE_LOG(LogTemp, Display, TEXT("%s"), *WildReport);
		Check(TEXT("Wildnis"), WildReport.Contains(TEXT("WILDNISTEST ERGEBNIS: 0 Fehler")));
	}
	else
	{
		Check(TEXT("Wildnis vorhanden"), false);
	}

	UE_LOG(LogTemp, Display, TEXT("SELFTEST ERGEBNIS: %d Fehler"), Failures);
	FPlatformMisc::RequestExit(false);
}

void AMTA6GameMode::SpawnMenu()
{
	UWorld* World = GetWorld();
	if (!World || Menu)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Menu = World->SpawnActor<AMTAMenuController>(AMTAMenuController::StaticClass(), FTransform::Identity, Params);
	if (!Menu)
	{
		return;
	}
	Menu->OnSaveRequested.AddUObject(this, &AMTA6GameMode::HandleMenuSave);
	Menu->OnLoadRequested.AddUObject(this, &AMTA6GameMode::HandleMenuLoad);
	Menu->OnNewGameRequested.AddUObject(this, &AMTA6GameMode::HandleMenuNew);
	Menu->OnQuitRequested.AddUObject(this, &AMTA6GameMode::HandleMenuQuit);
	Menu->QuerySlots.BindUObject(this, &AMTA6GameMode::FillSlotInfos);
	MapStore = NewObject<UMTAMapDataStore>(this);
	Menu->SetMapDataProvider(MapStore);

	// Charakter-Oberflaeche (Taste I)
	CharacterScreen = World->SpawnActor<AMTACharacterScreenController>(AMTACharacterScreenController::StaticClass(), FTransform::Identity, Params);
	if (CharacterScreen)
	{
		CharacterScreen->SetMenuController(Menu);
	}
}

void AMTA6GameMode::HandleMenuSave(FString Slot)
{
	const int32 Index = UMTASaveManager::NameToSlot(Slot);
	if (Index > UMTASaveManager::AutosaveSlot)   // der Autosave-Platz ist im Speichern-Dialog gesperrt
	{
		SaveGameToSlot(false, Index);
	}
}

void AMTA6GameMode::HandleMenuLoad(FString Slot)
{
	const int32 Index = UMTASaveManager::NameToSlot(Slot);
	if (Index >= 0)
	{
		LoadGameFromSlot(Index);
	}
}

void AMTA6GameMode::HandleMenuNew()
{
	if (MissionRunner) { MissionRunner->Abort(); }
	UGameplayStatics::OpenLevel(this, FName(TEXT("Kestrel_Bay")));
}

void AMTA6GameMode::HandleMenuQuit()
{
	UKismetSystemLibrary::QuitGame(this, UGameplayStatics::GetPlayerController(this, 0), EQuitPreference::Quit, false);
}

void AMTA6GameMode::FillSlotInfos(TArray<FMTASaveSlotInfo>& OutSlots)
{
	// Autosave und drei manuelle Plaetze mit Spielzeit, Mission, Bezirk, Geld und Datum
	if (UGameInstance* GI = GetGameInstance())
	{
		if (const UMTASaveManager* SaveMgr = GI->GetSubsystem<UMTASaveManager>())
		{
			SaveMgr->GetSlotInfos(OutSlots);
		}
	}
}

void AMTA6GameMode::HandlePlayerHitByAI(float Damage, float Impulse, FVector Dir)
{
	if (AMTAVehiclePawn* Vehicle = GetPlayerVehicle())
	{
		Vehicle->ApplyVehicleDamage(Damage);
		Vehicle->AddCameraShake(FMath::Clamp(Impulse / 150000.f, 0.2f, 1.f));
	}
}

void AMTA6GameMode::HandleArrested()
{
	Bust();
}

void AMTA6GameMode::GatherAIBlips(TArray<FMTAMinimapBlip>& OutBlips)
{
	if (!WorldAI)
	{
		return;
	}
	TArray<FMTAMinimapAgent> Agents;
	WorldAI->GetMinimapAgents(Agents, false);
	for (const FMTAMinimapAgent& A : Agents)
	{
		const EMTAMinimapBlipKind Kind = A.Kind == EMTAAgentKind::Police ? EMTAMinimapBlipKind::Police : EMTAMinimapBlipKind::Traffic;
		OutBlips.Emplace(FVector2D(A.Location.X, A.Location.Y), Kind);
	}
}

void AMTA6GameMode::SpawnStartVehicle()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Fahrzeug-Blueprint nur laden, wenn das Paket existiert; sonst die C++-Klasse (Sportwagen aus der Vorlage).
	UClass* CarClass = AMTAVehiclePawn::StaticClass();
	if (VehiclePawnClassPath.IsValid() && FPackageName::DoesPackageExist(VehiclePawnClassPath.GetLongPackageName()))
	{
		if (UClass* Loaded = VehiclePawnClassPath.TryLoadClass<APawn>())
		{
			CarClass = Loaded;
		}
	}

	// Startauto neben der Spielfigur, in Fahrtrichtung der Figur gedreht
	const FVector Forward = StartRotation.Vector();
	const FVector Right = FRotationMatrix(StartRotation).GetUnitAxis(EAxis::Y);
	const FVector CarLocation = StartLocation + Forward * 500.f + Right * 350.f + FVector(0.f, 0.f, 80.f);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	StartVehicle = World->SpawnActor<APawn>(CarClass, CarLocation, StartRotation, Params);
}

void AMTA6GameMode::BeginPlay()
{
	Super::BeginPlay();
	SpawnStartVehicle();
	SpawnWorldAI();
	SpawnDialogueNPCs();
	// Welt-Details (Meer, Berge, Landmarken, Moebel) und die begehbaren Gebaeude (mindestens 60, hier 72)
	if (AMTAWorldDetail* Detail = AMTAWorldDetail::FindOrSpawnWorldDetail(this))
	{
		Detail->SpawnEnterableBuildings(72);
	}
	// Wildnis Verdanta (nach den Welt-Details, die Platzsuche prueft Strassen und Wasser)
	AMTAWildernessBuilder::FindOrSpawn(this);
	SetupWorldSystems();
	SpawnMenu();
	LoadIntroData();
	if (FParse::Param(FCommandLine::Get(), TEXT("MTA6Shots")))
	{
		// Bildkontrolle: feste Kamerapunkte anfahren und Screenshots speichern (Saved/Screenshots), danach beenden
		FTimerHandle ShotHandle;
		GetWorldTimerManager().SetTimer(ShotHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UWorld* ShotWorld = GetWorld();
			APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
			APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
			if (!ShotWorld || !Pawn || !PC) { return; }
			static int32 ShotIndex = 0;
			static FVector CityStart = Pawn->GetActorLocation();
			AMTAWildernessBuilder* Wild = AMTAWildernessBuilder::FindWildernessBuilder(this);
			TArray<TPair<FVector, FVector>> Views;   // Standort, Blickziel
			Views.Add({ CityStart + FVector(0.f, 0.f, 150.f), CityStart + FVector(3000.f, 0.f, 150.f) });
			Views.Add({ CityStart + FVector(-2500.f, 0.f, 450.f), CityStart + FVector(4000.f, 0.f, 150.f) });
			Views.Add({ CityStart + FVector(6000.f, 0.f, 300.f), CityStart + FVector(-2000.f, 0.f, 200.f) });
			Views.Add({ CityStart + FVector(0.f, 4000.f, 3500.f), CityStart + FVector(0.f, 4600.f, 0.f) });
			if (Wild)
			{
				const FVector Fall = Wild->GetWaterfallLocation();
				const FVector Park = Wild->GetParkingLocation();
				const FVector Cave = Wild->GetSecretEntranceLocation();
				Views.Add({ Park + FVector(0.f, 0.f, 200.f), Fall });
				Views.Add({ Fall + (Park - Fall).GetSafeNormal2D() * 3500.f + FVector(0.f, 0.f, 500.f), Fall + FVector(0.f, 0.f, 800.f) });
				Views.Add({ Fall + (Park - Fall).GetSafeNormal2D() * 1200.f + FVector(0.f, 0.f, 250.f), Cave + FVector(0.f, 0.f, 150.f) });
				Views.Add({ Cave + FVector(0.f, 0.f, 180.f), Cave + (Cave - Fall).GetSafeNormal2D() * 800.f + FVector(0.f, 0.f, 150.f) });
			}
			if (WorldAI)
			{
				// Nahaufnahmen: naechstes Auto, naechster Polizeiwagen, zwei Passanten (Standort zur Aufnahmezeit)
				TArray<FMTAMinimapAgent> Agents;
				WorldAI->GetMinimapAgents(Agents, true);
				auto AddClose = [&](EMTAAgentKind Kind, int32 Skip, float Dist, float Height)
				{
					int32 Seen = 0;
					TArray<const FMTAMinimapAgent*> Sorted;
					for (const FMTAMinimapAgent& A : Agents) { if (A.Kind == Kind) { Sorted.Add(&A); } }
					Sorted.Sort([&](const FMTAMinimapAgent& L, const FMTAMinimapAgent& R) { return FVector::DistSquared(L.Location, CityStart) < FVector::DistSquared(R.Location, CityStart); });
					if (!Sorted.IsValidIndex(Skip)) { return; }
					const FMTAMinimapAgent& A = *Sorted[Skip];
					const FVector Fwd = FRotator(0.f, A.YawDegrees, 0.f).Vector();
					Views.Add({ A.Location - Fwd * Dist + FVector(0.f, 0.f, Height), A.Location + FVector(0.f, 0.f, 90.f) });
					(void)Seen;
				};
				AddClose(EMTAAgentKind::Traffic, 0, 750.f, 180.f);
				AddClose(EMTAAgentKind::Traffic, 3, 600.f, 120.f);
				AddClose(EMTAAgentKind::Police, 0, 700.f, 160.f);
				AddClose(EMTAAgentKind::Pedestrian, 0, 350.f, 80.f);
				AddClose(EMTAAgentKind::Pedestrian, 5, 350.f, 80.f);
			}
			if (ShotIndex >= Views.Num())
			{
				FPlatformMisc::RequestExit(false);
				return;
			}
			const TPair<FVector, FVector>& V = Views[ShotIndex];
			Pawn->SetActorLocationAndRotation(V.Key, FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
			PC->SetControlRotation((V.Value - V.Key).Rotation());
			Hour = 14.f;
			PC->ConsoleCommand(FString::Printf(TEXT("HighResShot 1600x900 filename=\"E:/Projekte/GTA 6/Saved/MTA6_shot_%d.png\""), ShotIndex), false);
			UE_LOG(LogTemp, Display, TEXT("SHOT %d bei %s"), ShotIndex, *V.Key.ToString());
			++ShotIndex;
		}), 6.f, true, 25.f);
	}
	if (!FParse::Param(FCommandLine::Get(), TEXT("MTA6SelfTest")) && !FParse::Param(FCommandLine::Get(), TEXT("MTA6Shots")))
	{
		// Einfuehrung kurz nach dem Start zeigen
		GetWorldTimerManager().SetTimer(IntroHandle, this, &AMTA6GameMode::ShowIntro, 1.5f, false);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("MTA6SelfTest")))
	{
		GetWorldTimerManager().SetTimer(SelfTestHandle, this, &AMTA6GameMode::RunSelfTest, 6.f, false);
	}
	Money = StartMoney;
	Hour = FMath::Fmod(StartHour, 24.f);
	GameTime = 0.f;
	LastCrimeTime = -99.f;
}

bool AMTA6GameMode::HasPlayerStart() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		return true;
	}
	return false;
}

void AMTA6GameMode::RestartPlayer(AController* NewPlayer)
{
	// Karten ohne PlayerStart (generierte Stadt): an der Godot-Startposition spawnen
	if (NewPlayer && !HasPlayerStart())
	{
		RestartPlayerAtTransform(NewPlayer, FTransform(StartRotation, StartLocation));
		return;
	}
	Super::RestartPlayer(NewPlayer);
}

UMTAHeatSubsystem* AMTA6GameMode::GetHeatSubsystem() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UMTAHeatSubsystem>() : nullptr;
}

AMTAVehiclePawn* AMTA6GameMode::GetPlayerVehicle() const
{
	return Cast<AMTAVehiclePawn>(UGameplayStatics::GetPlayerPawn(this, 0));
}

bool AMTA6GameMode::IsDialogOpen() const
{
	// Auch Einfuehrung, Kodex und Auftragsbriefing pausieren das Spiel
	return (MissionComponent && MissionComponent->IsDialogOpen()) || IsStoryPageOpen();
}

// --- Einfuehrung, Kodex, Briefing -----------------------------------------------------------

void AMTA6GameMode::LoadIntroData()
{
	IntroPages.Reset();
	CodexCategoryNames.Reset();
	CodexCategories.Reset();

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/intro.json"))))
	{
		UE_LOG(LogTemp, Warning, TEXT("MTA6: intro.json nicht lesbar"));
		return;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("MTA6: intro.json ungueltig"));
		return;
	}

	auto ParsePage = [](const TSharedPtr<FJsonObject>& O) -> FMTAStoryPage
	{
		FMTAStoryPage P;
		if (O.IsValid())
		{
			O->TryGetStringField(TEXT("title"), P.Title);
			O->TryGetStringField(TEXT("body"), P.Body);
		}
		return P;
	};

	const TArray<TSharedPtr<FJsonValue>>* Intro = nullptr;
	if (Root->TryGetArrayField(TEXT("intro"), Intro))
	{
		for (const TSharedPtr<FJsonValue>& V : *Intro) { IntroPages.Add(ParsePage(V->AsObject())); }
	}
	const TArray<TSharedPtr<FJsonValue>>* Cats = nullptr;
	if (Root->TryGetArrayField(TEXT("codex"), Cats))
	{
		for (const TSharedPtr<FJsonValue>& C : *Cats)
		{
			const TSharedPtr<FJsonObject> CO = C->AsObject();
			if (!CO.IsValid()) { continue; }
			FString Name;
			CO->TryGetStringField(TEXT("category"), Name);
			TArray<FMTAStoryPage> Pages;
			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			if (CO->TryGetArrayField(TEXT("entries"), Entries))
			{
				for (const TSharedPtr<FJsonValue>& E : *Entries) { Pages.Add(ParsePage(E->AsObject())); }
			}
			CodexCategoryNames.Add(Name);
			CodexCategories.Add(Pages);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("MTA6: Einfuehrung %d Seiten, Kodex %d Kategorien"), IntroPages.Num(), CodexCategories.Num());
}

void AMTA6GameMode::ShowStoryPages(const TArray<FMTAStoryPage>& Pages, const FString& Heading, bool bInCodexMode)
{
	if (Pages.IsEmpty())
	{
		return;
	}
	StoryPages = Pages;
	StoryPageIndex = 0;
	StoryHeading = Heading;
	bCodexIndex = false;
	bCodexMode = bInCodexMode;
}

void AMTA6GameMode::AdvanceStoryPage()
{
	if (StoryPages.IsEmpty())
	{
		return;
	}
	if (StoryPageIndex + 1 < StoryPages.Num())
	{
		++StoryPageIndex;
	}
	else
	{
		CloseStoryPages();
	}
}

void AMTA6GameMode::CloseStoryPages()
{
	// Im Kodex fuehrt Esc zuerst zurueck zum Index, danach schliesst es
	if (bCodexMode && !bCodexIndex && !StoryPages.IsEmpty())
	{
		StoryPages.Reset();
		bCodexIndex = true;
		return;
	}
	StoryPages.Reset();
	StoryPageIndex = 0;
	bCodexIndex = false;
	bCodexMode = false;
}

void AMTA6GameMode::OpenCodex()
{
	if (CodexCategories.IsEmpty())
	{
		return;
	}
	StoryPages.Reset();
	bCodexMode = true;
	bCodexIndex = true;
}

void AMTA6GameMode::OpenCodexCategory(int32 CategoryIndex)
{
	if (!CodexCategories.IsValidIndex(CategoryIndex))
	{
		return;
	}
	bCodexIndex = false;
	ShowStoryPages(CodexCategories[CategoryIndex], FString::Printf(TEXT("KODEX: %s"), *CodexCategoryNames[CategoryIndex].ToUpper()), true);
}

TArray<FString> AMTA6GameMode::GetCodexCategoryNames() const
{
	return CodexCategoryNames;
}

void AMTA6GameMode::CheckMissionBriefing()
{
	if (!MissionRunner || IsStoryPageOpen() || IsDialogOpen())
	{
		return;
	}
	if (MissionRunner->GetState() == EMTAMissionRunState::Idle)
	{
		LastBriefedMissionId.Reset();   // naechster Auftrag (auch Neustart) zeigt wieder ein Briefing
	}
	if (MissionRunner->GetState() != EMTAMissionRunState::Briefing)
	{
		return;
	}
	const FMTAMissionSpec& Spec = MissionRunner->GetActiveMissionRef();
	if (Spec.Id == LastBriefedMissionId)
	{
		return;
	}
	LastBriefedMissionId = Spec.Id;
	FMTAStoryPage Page;
	Page.Title = Spec.Title;
	Page.Body = Spec.Text + TEXT("\n\nAuftrag: ") + UMTAMissionRunner::GetTypeDisplayName(Spec.Type)
		+ TEXT("   Belohnung: ") + Spec.RewardText
		+ TEXT("\nErstes Ziel: ") + (Spec.Steps.Num() > 0 ? Spec.Steps[0].Label : FString());
	ShowStoryPages({ Page }, TEXT("AUFTRAG"));
}

// --- Geld / Fahndung / Zeit -------------------------------------------------------------

void AMTA6GameMode::AddMoney(int32 Amount)
{
	Money = FMath::Max(0, Money + Amount);
}

float AMTA6GameMode::GetHeat() const
{
	if (const UMTAHeatSubsystem* Heat = GetHeatSubsystem())
	{
		return Heat->GetHeat(HeatDistrict);
	}
	return FallbackHeat;
}

void AMTA6GameMode::SetHeat(float NewHeat)
{
	NewHeat = FMath::Clamp(NewHeat, 0.f, 5.f);
	if (UMTAHeatSubsystem* Heat = GetHeatSubsystem())
	{
		Heat->AddHeat(HeatDistrict, NewHeat - Heat->GetHeat(HeatDistrict));
	}
	FallbackHeat = NewHeat;
}

void AMTA6GameMode::ReportCrime(float HeatAmount)
{
	if (HeatAmount > 0.f)
	{
		SetHeat(GetHeat() + HeatAmount);
	}
	LastCrimeTime = GameTime;
}

int32 AMTA6GameMode::GetWantedStars() const
{
	return FMath::Clamp(FMath::CeilToInt(GetHeat() - 0.001f), 0, 5);
}

int32 AMTA6GameMode::GetDesiredPoliceCount() const
{
	return GetHeat() <= 0.f ? 0 : FMath::Min(8, 1 + GetWantedStars());
}

void AMTA6GameMode::SetHour(float NewHour)
{
	Hour = FMath::Fmod(NewHour, 24.f);
	if (Hour < 0.f)
	{
		Hour += 24.f;
	}
}

void AMTA6GameMode::ShowToast(const FString& Text, float Duration)
{
	ToastText = Text;
	ToastTimeLeft = Duration;
}

// --- Minimap ------------------------------------------------------------------------------

void AMTA6GameMode::RegisterMinimapProvider(UObject* Provider)
{
	if (Provider && Provider->GetClass()->ImplementsInterface(UMTAMinimapProvider::StaticClass()))
	{
		MinimapProviders.AddUnique(Provider);
	}
}

void AMTA6GameMode::UnregisterMinimapProvider(UObject* Provider)
{
	MinimapProviders.Remove(Provider);
}

void AMTA6GameMode::GatherMinimapBlips()
{
	CachedBlips.Reset();
	for (int32 i = MinimapProviders.Num() - 1; i >= 0; --i)
	{
		UObject* Obj = MinimapProviders[i].Get();
		if (!Obj)
		{
			MinimapProviders.RemoveAt(i);
			continue;
		}
		if (const IMTAMinimapProvider* Provider = Cast<IMTAMinimapProvider>(Obj))
		{
			Provider->GatherMinimapBlips(CachedBlips);
		}
	}
	OnGatherMinimapBlips.Broadcast(CachedBlips);
}

// --- Tick -----------------------------------------------------------------------------------

void AMTA6GameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Tageszeit laeuft immer (Godot: _process), 1 Spielstunde = SecondsPerGameHour
	SetHour(Hour + DeltaSeconds / FMath::Max(0.1f, SecondsPerGameHour));

	ToastTimeLeft = FMath::Max(0.f, ToastTimeLeft - DeltaSeconds);
	BustedOverlayTimeLeft = FMath::Max(0.f, BustedOverlayTimeLeft - DeltaSeconds);

	GatherMinimapBlips();
	CheckMissionBriefing();

	// Karte im Menue: Spielerposition und Blickrichtung nachfuehren
	if (MapStore)
	{
		if (const APawn* P = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			MapStore->SetPlayerWorld(FVector2D(P->GetActorLocation().X, P->GetActorLocation().Y), P->GetActorRotation().Yaw);
		}
	}

	// Waehrend eines Dialogs ruht die Spiellogik (Godot: _update_game nur ohne Dialog)
	if (IsDialogOpen())
	{
		return;
	}

	GameTime += DeltaSeconds;
	TickWorldSystems(DeltaSeconds);
	UpdateLaw(DeltaSeconds);
	UpdateWreck(DeltaSeconds);
}

void AMTA6GameMode::UpdateLaw(float DeltaSeconds)
{
	// Mit aktiver Welt-KI uebernimmt der Manager Abkuehlung und Festnahme (OnArrested -> Bust).
	if (WorldAI)
	{
		return;
	}
	const AMTAVehiclePawn* Vehicle = GetPlayerVehicle();
	const APawn* PlayerPawn = Vehicle ? Vehicle : UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn)
	{
		return;
	}

	const FVector2D PlayerPos(PlayerPawn->GetActorLocation());
	bool bNearPolice = false;
	bool bClosePolice = false;
	for (const FMTAMinimapBlip& Blip : CachedBlips)
	{
		if (Blip.Kind != EMTAMinimapBlipKind::Police)
		{
			continue;
		}
		const float D = FVector2D::Distance(Blip.WorldPos, PlayerPos);
		bNearPolice |= D < PoliceNearRadius;
		bClosePolice |= D < BustRadius;
	}

	const float Heat = GetHeat();
	if (Heat > 0.f && GameTime - LastCrimeTime > HeatDecayDelay && !bNearPolice)
	{
		SetHeat(Heat - HeatDecayPerSecond * DeltaSeconds);
	}

	if (Heat > 0.f && bClosePolice && PlayerPawn->GetVelocity().Size() < BustMaxSpeed)
	{
		BustTimer += DeltaSeconds;
		if (BustTimer > BustTime)
		{
			Bust();
		}
	}
	else
	{
		BustTimer = 0.f;
	}
}

void AMTA6GameMode::UpdateWreck(float DeltaSeconds)
{
	AMTAVehiclePawn* Vehicle = GetPlayerVehicle();
	if (!Vehicle || !Vehicle->bWrecked)
	{
		WreckTimer = 0.f;
		return;
	}

	WreckTimer += DeltaSeconds;
	if (WreckTimer >= WreckRespawnDelay)
	{
		WreckTimer = 0.f;
		const int32 Cost = FMath::Min(Money, RepairCost);
		Money -= Cost;
		Vehicle->Repair();
		MovePlayerToStart();
		ShowToast(FString::Printf(TEXT("Abgeschleppt und repariert  -$%d"), Cost));
	}
}

void AMTA6GameMode::MovePlayerToStart()
{
	if (AMTAVehiclePawn* Vehicle = GetPlayerVehicle())
	{
		Vehicle->TeleportVehicle(StartLocation, StartRotation);
	}
	else if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Pawn->SetActorLocationAndRotation(StartLocation, StartRotation, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void AMTA6GameMode::Bust()
{
	const int32 Fine = FMath::Min(Money, BustFine);
	Money -= Fine;
	SetHeat(0.f);
	BustTimer = 0.f;
	MovePlayerToStart();
	BustedOverlayTimeLeft = 3.f;
	ShowToast(FString::Printf(TEXT("FESTGENOMMEN  -$%d"), Fine));
	OnBusted.Broadcast();
}

// --- Speichern / Laden ------------------------------------------------------------------------

void AMTA6GameMode::BuildSaveContext(FMTASaveContext& C) const
{
	C.Money = Money;
	C.Hour = Hour;
	C.PlaytimeSeconds = GameTime;
	C.Heat = GetHeat();
	if (const UMTAHeatSubsystem* HeatSub = GetHeatSubsystem())
	{
		C.District = HeatSub->GetCurrentDistrict(0);
	}
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		C.bHasPlayerTransform = true;
		C.PlayerTransform = Pawn->GetActorTransform();
	}
	if (const AMTAVehiclePawn* Vehicle = GetPlayerVehicle())
	{
		C.bHasVehicle = true;
		C.bInVehicle = true;
		C.VehicleTransform = Vehicle->GetActorTransform();
		C.VehicleHealth = Vehicle->Health;
	}
	if (MissionComponent)
	{
		C.MissionLabel = MissionComponent->GetMissionDisplayTitle();
		C.bStoryMission = MissionComponent->IsStoryMission();
		C.MissionTarget = MissionComponent->GetTargetLocation();
		C.MissionTimeLeft = MissionComponent->GetTimeLeft();
	}
}

bool AMTA6GameMode::SaveGameToSlot(bool bAutosave, int32 Slot)
{
	UGameInstance* GI = GetGameInstance();
	UMTASaveManager* SaveMgr = GI ? GI->GetSubsystem<UMTASaveManager>() : nullptr;
	if (!SaveMgr)
	{
		ShowToast(TEXT("Speichern fehlgeschlagen"));
		return false;
	}
	if (Slot < 0)
	{
		Slot = bAutosave ? UMTASaveManager::AutosaveSlot : 1;
	}

	FMTASaveContext Ctx;
	BuildSaveContext(Ctx);
	const bool bOk = SaveMgr->SaveToSlot(Slot, Ctx);
	if (!bOk)
	{
		ShowToast(SaveMgr->GetLastError().IsEmpty() ? FString(TEXT("Speichern fehlgeschlagen")) : SaveMgr->GetLastError());
	}
	else if (!bAutosave)
	{
		ShowToast(TEXT("Gespeichert"));
	}
	return bOk;
}

bool AMTA6GameMode::LoadGameFromSlot(int32 Slot)
{
	UGameInstance* GI = GetGameInstance();
	UMTASaveManager* SaveMgr = GI ? GI->GetSubsystem<UMTASaveManager>() : nullptr;
	if (!SaveMgr)
	{
		return false;
	}
	if (Slot < 0)
	{
		Slot = SaveMgr->GetNewestSlot();
	}
	if (Slot < 0)
	{
		ShowToast(TEXT("Kein Spielstand vorhanden"));
		return false;
	}

	FMTASaveContext Ctx;
	if (!SaveMgr->LoadFromSlot(Slot, Ctx))
	{
		ShowToast(SaveMgr->GetLastError());
		return false;
	}
	ApplySaveContext(Ctx);
	OnGameLoaded.Broadcast();
	ShowToast(SaveMgr->DidLastLoadUseBackup() ? SaveMgr->GetLastError() : FString(TEXT("Geladen")));
	return true;
}

void AMTA6GameMode::ApplySaveContext(const FMTASaveContext& Ctx)
{
	Money = Ctx.Money;
	SetHeat(Ctx.Heat);
	SetHour(Ctx.Hour);
	GameTime = Ctx.PlaytimeSeconds;
	LastCrimeTime = GameTime;
	BustTimer = 0.f;
	WreckTimer = 0.f;

	// Fahrzeug: Position etwas anheben, nur Gierwinkel uebernehmen, Physik stoppen
	if (Ctx.bHasVehicle)
	{
		const FVector Loc = Ctx.VehicleTransform.GetLocation() + FVector(0.f, 0.f, 30.f);
		const FRotator Rot(0.f, Ctx.VehicleTransform.Rotator().Yaw, 0.f);
		if (AMTAVehiclePawn* Vehicle = GetPlayerVehicle())
		{
			Vehicle->Repair();
			Vehicle->Health = FMath::Clamp(Ctx.VehicleHealth, 1.f, Vehicle->MaxHealth);
			Vehicle->TeleportVehicle(Loc, Rot);
		}
		else if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			Pawn->SetActorLocationAndRotation(Loc, Rot, false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	else if (Ctx.bHasPlayerTransform)
	{
		if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			Pawn->SetActorLocationAndRotation(Ctx.PlayerTransform.GetLocation() + FVector(0.f, 0.f, 30.f), Ctx.PlayerTransform.Rotator(), false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	if (MissionRunner) { MissionRunner->Abort(); }
	if (MissionComponent)
	{
		MissionComponent->CloseDialog();
		MissionComponent->StartNextMission();
		if (!MissionComponent->IsStoryMission() && !Ctx.MissionTarget.IsNearlyZero())
		{
			MissionComponent->RestoreCourier(Ctx.MissionTarget, Ctx.MissionTimeLeft);
		}
	}
}
