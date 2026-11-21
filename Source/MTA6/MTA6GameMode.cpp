#include "MTA6GameMode.h"

#include "MTACharacter.h"
#include "MTAHeatSubsystem.h"
#include "MTAHUD.h"
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

	// Waehrend eines Dialogs ruht die Spiellogik (Godot: _update_game nur ohne Dialog)
	if (IsDialogOpen())
	{
		return;
	}

	GameTime += DeltaSeconds;
	UpdateLaw(DeltaSeconds);
	UpdateWreck(DeltaSeconds);
}

void AMTA6GameMode::UpdateLaw(float DeltaSeconds)
{
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
