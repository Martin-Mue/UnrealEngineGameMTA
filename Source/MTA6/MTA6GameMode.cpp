#include "MTA6GameMode.h"

#include "MTACharacter.h"
#include "MTAHeatSubsystem.h"
#include "MTAHUD.h"
#include "EngineUtils.h"
#include "MTAAudioAmbienceSubsystem.h"
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
#include "MTADialogueSubsystem.h"
#include "MTANPCSubsystem.h"
#include "MTAMenuController.h"
#include "MTAMenuMap.h"
#include "MTAWorldAIManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "MTAMissionComponent.h"
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

namespace
{
	/** Bezirks-Kennung aus npcs.json -> Enum des Stadtgenerators. */
	EMTADistrict DistrictFromId(FName Id)
	{
		if (Id == FName("saltmarket")) return EMTADistrict::Saltmarket;
		if (Id == FName("lantern")) return EMTADistrict::LanternHill;
		if (Id == FName("greenmoor")) return EMTADistrict::Greenmoor;
		if (Id == FName("rustline")) return EMTADistrict::Rustline;
		if (Id == FName("sunstrip")) return EMTADistrict::Sunstrip;
		return EMTADistrict::Core;
	}
}

void AMTA6GameMode::SpawnDialogueNPCs()
{
	UWorld* World = GetWorld();
	UGameInstance* GI = GetGameInstance();
	UMTANPCSubsystem* Npcs = GI ? GI->GetSubsystem<UMTANPCSubsystem>() : nullptr;
	UMTADialogueSubsystem* Dialog = GI ? GI->GetSubsystem<UMTADialogueSubsystem>() : nullptr;
	if (!World || !Npcs || !Dialog)
	{
		return;
	}
	const AMTACityGenerator* City = AMTACityGenerator::FindCityGenerator(this);

	TMap<EMTADistrict, int32> UsedPerDistrict;
	int32 Spawned = 0;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	for (const FName& Id : Npcs->GetAllNPCIds())
	{
		FMTANPCProfile Profile;
		if (Dialog->GetDialogueForNPC(Id).IsNone() || !Npcs->GetProfile(Id, Profile))
		{
			continue;
		}

		FVector Loc;
		if (Spawned < 3)
		{
			// Die ersten drei stehen direkt beim Start, damit man sofort ein Gespraech ausprobieren kann.
			const FVector Forward = StartRotation.Vector();
			const FVector Right = FRotationMatrix(StartRotation).GetUnitAxis(EAxis::Y);
			Loc = StartLocation + Forward * 1000.f + Right * (-450.f + 300.f * Spawned) + FVector(0.f, 0.f, 20.f);
		}
		else if (City)
		{
			// Sonst am Gehweg einer Kreuzung im eigenen Bezirk
			const EMTADistrict D = DistrictFromId(Profile.District);
			int32 Pick = UsedPerDistrict.FindOrAdd(D)++;
			FVector Spot = FVector::ZeroVector;
			bool bFound = false;
			for (int32 BX = 0; BX < MTACity::Blocks && !bFound; ++BX)
			{
				for (int32 BY = 0; BY < MTACity::Blocks; ++BY)
				{
					if (City->GetBlockDistrict(BX, BY) == D && Pick-- == 0)
					{
						Spot = City->GetIntersectionLocation(BX, BY) + FVector(MTACity::SidewalkOffset + 150.f, MTACity::SidewalkOffset + 150.f, 20.f);
						bFound = true;
						break;
					}
				}
			}
			if (!bFound)
			{
				continue;
			}
			Loc = Spot;
		}
		else
		{
			continue;
		}

		if (AMTADialogueNPC* Npc = World->SpawnActor<AMTADialogueNPC>(AMTADialogueNPC::StaticClass(), Loc, FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Params))
		{
			Npc->SetupNPC(Id, Profile.DisplayName);
			++Spawned;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("MTA6: %d ansprechbare NPCs erzeugt"), Spawned);
}

void AMTA6GameMode::SetupWorldSystems()
{
	UGameInstance* GI = GetGameInstance();
	Weather = AMTAWeatherManager::GetOrSpawn(this);
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

	// 2) NPC-Gespraech: zur ersten Figur gehen, Interaktion ausloesen, Gespraech durchklicken
	TArray<AMTADialogueNPC*> Npcs;
	for (TActorIterator<AMTADialogueNPC> It(World); It; ++It) { Npcs.Add(*It); }
	Check(TEXT("16 ansprechbare NPCs"), Npcs.Num() == 16);
	UMTADialogueSubsystem* Dialog = GI ? GI->GetSubsystem<UMTADialogueSubsystem>() : nullptr;
	if (Hero && Npcs.Num() > 0 && Dialog)
	{
		Hero->SetActorLocation(Npcs[0]->GetActorLocation() + FVector(-150.f, 0.f, 30.f));
		IMTAInteractable::Execute_Interact(Npcs[0], Hero);
		Check(TEXT("Gespraech startet"), Dialog->IsInConversation());
		int32 Steps = 0;
		while (Dialog->IsInConversation() && Steps < 40)
		{
			const FMTADialogueView View = Dialog->GetCurrentView();
			if (View.bIsChoice && View.Options.Num() > 0)
			{
				// Die letzte Antwort ist meist der Abschied; die erste kreist oft im Themenmenue.
				Dialog->ChooseOption(View.Options.Last().Index);
			}
			else
			{
				Dialog->Continue();
			}
			++Steps;
		}
		UE_LOG(LogTemp, Display, TEXT("SELFTEST Gespraech: %d Schritte bis zum Ende"), Steps);
		Check(TEXT("Gespraech endet"), !Dialog->IsInConversation());
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

	// 3) Speichern und Laden
	Money = 4321;
	const bool bSaved = SaveGameToSlot(true);
	Money = 1;
	const bool bLoaded = LoadGameFromSlot();
	Check(TEXT("Speichern"), bSaved);
	Check(TEXT("Laden"), bLoaded);
	Check(TEXT("Geld nach Laden"), Money == 4321);

	// 4) Story
	UMTAStorySubsystem* Story = GI ? GI->GetSubsystem<UMTAStorySubsystem>() : nullptr;
	Check(TEXT("Story verfuegbare Missionen"), Story && Story->GetAvailableMissions().Num() > 0);

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
}

void AMTA6GameMode::HandleMenuSave(FString Slot)
{
	SaveGameToSlot(false);
}

void AMTA6GameMode::HandleMenuLoad(FString Slot)
{
	LoadGameFromSlot();
}

void AMTA6GameMode::HandleMenuNew()
{
	UGameplayStatics::OpenLevel(this, FName(TEXT("Kestrel_Bay")));
}

void AMTA6GameMode::HandleMenuQuit()
{
	UKismetSystemLibrary::QuitGame(this, UGameplayStatics::GetPlayerController(this, 0), EQuitPreference::Quit, false);
}

void AMTA6GameMode::FillSlotInfos(TArray<FMTASaveSlotInfo>& OutSlots)
{
	// Ein Speicherplatz (UMTASaveGame::SlotName); weitere Slots folgen mit dem SaveManager.
	FMTASaveSlotInfo Info;
	Info.SlotName = UMTASaveGame::SlotName();
	Info.bExists = UGameplayStatics::DoesSaveGameExist(UMTASaveGame::SlotName(), UMTASaveGame::UserIndex);
	Info.PlaytimeSeconds = GameTime;
	Info.SavedAt = FDateTime::Now();
	OutSlots.Add(Info);
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
	SpawnEnterableBuildings();
	SetupWorldSystems();
	SpawnMenu();
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
	return MissionComponent && MissionComponent->IsDialogOpen();
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

bool AMTA6GameMode::SaveGameToSlot(bool bAutosave)
{
	UMTASaveGame* Save = Cast<UMTASaveGame>(UGameplayStatics::CreateSaveGameObject(UMTASaveGame::StaticClass()));
	if (!Save)
	{
		ShowToast(TEXT("Speichern fehlgeschlagen"));
		return false;
	}

	Save->Version = UMTASaveGame::CurrentVersion;
	Save->Money = Money;
	Save->Heat = GetHeat();
	Save->Hour = Hour;

	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Save->VehicleTransform = Pawn->GetActorTransform();
	}
	if (const AMTAVehiclePawn* Vehicle = GetPlayerVehicle())
	{
		Save->VehicleHealth = Vehicle->Health;
	}
	if (MissionComponent)
	{
		Save->MissionTarget = MissionComponent->GetTargetLocation();
		Save->MissionTimeLeft = MissionComponent->GetTimeLeft();
	}
	if (UGameInstance* GI = GetGameInstance())
	{
		if (const UMTAStorySubsystem* Story = GI->GetSubsystem<UMTAStorySubsystem>())
		{
			Save->StoryJson = Story->StateToJson();
		}
	}

	const bool bOk = UGameplayStatics::SaveGameToSlot(Save, UMTASaveGame::SlotName(), UMTASaveGame::UserIndex);
	if (!bOk)
	{
		ShowToast(TEXT("Speichern fehlgeschlagen"));
	}
	else if (!bAutosave)
	{
		ShowToast(TEXT("Gespeichert (F5)"));
	}
	return bOk;
}

bool AMTA6GameMode::LoadGameFromSlot()
{
	if (!UGameplayStatics::DoesSaveGameExist(UMTASaveGame::SlotName(), UMTASaveGame::UserIndex))
	{
		ShowToast(TEXT("Kein Spielstand vorhanden"));
		return false;
	}

	const UMTASaveGame* Save = Cast<UMTASaveGame>(UGameplayStatics::LoadGameFromSlot(UMTASaveGame::SlotName(), UMTASaveGame::UserIndex));
	if (!Save || (Save->Version != 1 && Save->Version != UMTASaveGame::CurrentVersion))
	{
		ShowToast(TEXT("Spielstand unbrauchbar"));
		return false;
	}

	Money = Save->Money;
	SetHeat(Save->Heat);
	SetHour(Save->Hour);
	LastCrimeTime = GameTime;
	BustTimer = 0.f;
	WreckTimer = 0.f;

	// Fahrzeug: Position etwas anheben, nur Gierwinkel uebernehmen, Physik stoppen
	const FVector Loc = Save->VehicleTransform.GetLocation() + FVector(0.f, 0.f, 30.f);
	const FRotator Rot(0.f, Save->VehicleTransform.Rotator().Yaw, 0.f);
	if (AMTAVehiclePawn* Vehicle = GetPlayerVehicle())
	{
		Vehicle->Repair();
		Vehicle->Health = FMath::Clamp(Save->VehicleHealth, 1.f, Vehicle->MaxHealth);
		Vehicle->TeleportVehicle(Loc, Rot);
	}
	else if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Pawn->SetActorLocationAndRotation(Loc, Rot, false, nullptr, ETeleportType::TeleportPhysics);
	}

	// Story-Zustand (Version 1 hatte keinen)
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UMTAStorySubsystem* Story = GI->GetSubsystem<UMTAStorySubsystem>())
		{
			if (Save->StoryJson.IsEmpty() || !Story->StateFromJson(Save->StoryJson))
			{
				Story->ResetState();
			}
		}
	}

	if (MissionComponent)
	{
		MissionComponent->CloseDialog();
		MissionComponent->StartNextMission();
		if (!MissionComponent->IsStoryMission() && !Save->MissionTarget.IsNearlyZero())
		{
			MissionComponent->RestoreCourier(Save->MissionTarget, Save->MissionTimeLeft);
		}
	}

	OnGameLoaded.Broadcast();
	ShowToast(TEXT("Geladen (F9)"));
	return true;
}
