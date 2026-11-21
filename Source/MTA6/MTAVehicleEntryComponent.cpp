#include "MTAVehicleEntryComponent.h"

#include "WheeledVehiclePawn.h"
#include "ChaosVehicleMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "MTACharacter.h"
#include "MTAChaosSubsystem.h"
#include "MTAStealthComponent.h"

#define LOCTEXT_NAMESPACE "MTAVehicleEntry"

UMTAVehicleEntryComponent::UMTAVehicleEntryComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UMTAVehicleEntryComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UMTAVehicleEntryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	PopExitInput();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UnwatchTimer);
	}
	StopWatchingVehicle();
	Super::EndPlay(EndPlayReason);
}

ACharacter* UMTAVehicleEntryComponent::GetCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

APlayerController* UMTAVehicleEntryComponent::GetPlayerController() const
{
	if (DrivingController)
	{
		return DrivingController;
	}
	const ACharacter* Character = GetCharacter();
	return Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
}

UMTAChaosSubsystem* UMTAVehicleEntryComponent::GetChaos() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UMTAChaosSubsystem>() : nullptr;
}

bool UMTAVehicleEntryComponent::IsBusy() const
{
	return Phase != EMTAVehicleEntryPhase::None && Phase != EMTAVehicleEntryPhase::Seated;
}

bool UMTAVehicleEntryComponent::HasForeignDriver(const AWheeledVehiclePawn* Vehicle)
{
	const AController* Controller = Vehicle ? Vehicle->GetController() : nullptr;
	return Controller != nullptr && !Controller->IsPlayerController();
}

FVector UMTAVehicleEntryComponent::GetDoorLocation(const AWheeledVehiclePawn* Vehicle) const
{
	if (!Vehicle)
	{
		return FVector::ZeroVector;
	}
	const USkeletalMeshComponent* Mesh = Vehicle->GetMesh();
	if (Mesh && Mesh->DoesSocketExist(DriverDoorSocket))
	{
		return Mesh->GetSocketLocation(DriverDoorSocket);
	}
	return Vehicle->GetActorTransform().TransformPosition(FallbackDoorOffset);
}

// ---------------------------------------------------------------------------------------
// Suche und Prompt
// ---------------------------------------------------------------------------------------

AWheeledVehiclePawn* UMTAVehicleEntryComponent::FindNearestVehicle() const
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return nullptr;
	}
	const FVector Origin = Owner->GetActorLocation();
	AWheeledVehiclePawn* Best = nullptr;
	float BestDistSq = FMath::Square(SearchRadius);
	// Fahrzeuge sind wenige Dutzend Aktoren; Iterator ist hier guenstiger als ein Overlap.
	for (TActorIterator<AWheeledVehiclePawn> It(World); It; ++It)
	{
		AWheeledVehiclePawn* Vehicle = *It;
		if (!Vehicle || Vehicle == CurrentVehicle || Vehicle->IsActorBeingDestroyed())
		{
			continue;
		}
		const float DistSq = FMath::Min(FVector::DistSquared(Origin, Vehicle->GetActorLocation()),
			FVector::DistSquared(Origin, GetDoorLocation(Vehicle)));
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Vehicle;
		}
	}
	return Best;
}

FMTAInteractionInfo UMTAVehicleEntryComponent::GetVehicleInteractionInfo(AWheeledVehiclePawn* Vehicle) const
{
	FMTAInteractionInfo Info;
	Info.Kind = EMTAInteractionKind::Vehicle;
	Info.Priority = 1;
	if (!Vehicle)
	{
		Info.bEnabled = false;
		return Info;
	}
	if (Vehicle->GetController() && Vehicle->GetController()->IsPlayerController())
	{
		Info.Prompt = LOCTEXT("Occupied", "Besetzt");
		Info.bEnabled = false;
		return Info;
	}

	bool bOwned = Vehicle->ActorHasTag(TEXT("PlayerOwned"));
	bool bStolen = false;
	bool bHot = false;
	if (UMTAChaosSubsystem* Chaos = GetChaos())
	{
		if (Chaos->HasVehicleRecord(Vehicle))
		{
			const FMTAVehicleRecord Record = Chaos->GetOrCreateVehicleRecord(Vehicle);
			bOwned |= Record.bPlayerOwned;
			bStolen = Record.bStolen;
			bHot = Record.bHot;
		}
	}

	if (HasForeignDriver(Vehicle))
	{
		Info.Prompt = LOCTEXT("PullOut", "Fahrer herausziehen");
		Info.bIsIllegal = true;
	}
	else if (bOwned || bStolen)
	{
		Info.Prompt = LOCTEXT("Enter", "Einsteigen");
	}
	else if (Vehicle->ActorHasTag(TEXT("Locked")))
	{
		Info.Prompt = LOCTEXT("BreakIn", "Aufbrechen");
		Info.bIsIllegal = true;
	}
	else
	{
		Info.Prompt = LOCTEXT("Steal", "Fahrzeug stehlen");
		Info.bIsIllegal = true;
	}
	if (bHot)
	{
		Info.SubText = LOCTEXT("Hot", "Kennzeichen zur Fahndung ausgeschrieben");
	}
	return Info;
}

// ---------------------------------------------------------------------------------------
// Ablauf
// ---------------------------------------------------------------------------------------

void UMTAVehicleEntryComponent::ToggleEnterExit()
{
	if (Phase == EMTAVehicleEntryPhase::Seated)
	{
		RequestExit();
	}
	else if (Phase == EMTAVehicleEntryPhase::None)
	{
		RequestEnter(FindNearestVehicle());
	}
	else if (Phase == EMTAVehicleEntryPhase::Approaching)
	{
		CancelEntry();
	}
}

bool UMTAVehicleEntryComponent::RequestEnter(AWheeledVehiclePawn* Vehicle)
{
	ACharacter* Character = GetCharacter();
	if (!Vehicle || !Character || Phase != EMTAVehicleEntryPhase::None)
	{
		return false;
	}
	if (Vehicle->GetController() && Vehicle->GetController()->IsPlayerController())
	{
		return false;
	}
	if (!Cast<APlayerController>(Character->GetController()))
	{
		return false;
	}
	if (FVector::Dist(Character->GetActorLocation(), GetDoorLocation(Vehicle)) > MaxEnterDistance + SearchRadius)
	{
		return false;
	}

	TargetVehicle = Vehicle;
	bTargetHadDriver = HasForeignDriver(Vehicle);
	bTargetWasLocked = !bTargetHadDriver && Vehicle->ActorHasTag(TEXT("Locked"));
	SetComponentTickEnabled(true);
	SetPhase(EMTAVehicleEntryPhase::Approaching, ApproachTimeout);
	return true;
}

void UMTAVehicleEntryComponent::CancelEntry()
{
	if (Phase == EMTAVehicleEntryPhase::Approaching || Phase == EMTAVehicleEntryPhase::OpeningDoor
		|| Phase == EMTAVehicleEntryPhase::BreakingIn)
	{
		TargetVehicle = nullptr;
		DoorOpenAlpha = 0.f;
		SetPhase(EMTAVehicleEntryPhase::None, 0.f);
		SetComponentTickEnabled(false);
	}
}

void UMTAVehicleEntryComponent::SetPhase(EMTAVehicleEntryPhase NewPhase, float Duration)
{
	Phase = NewPhase;
	PhaseDuration = FMath::Max(Duration, 0.f);
	PhaseTimeLeft = PhaseDuration;
	OnPhaseChanged.Broadcast(Phase, TargetVehicle ? TargetVehicle.Get() : CurrentVehicle.Get());
}

void UMTAVehicleEntryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}

	// Ziel waehrend des Einstiegs verloren (zerstoert, weggefahren)?
	const bool bEntering = Phase == EMTAVehicleEntryPhase::Approaching || Phase == EMTAVehicleEntryPhase::BreakingIn
		|| Phase == EMTAVehicleEntryPhase::OpeningDoor || Phase == EMTAVehicleEntryPhase::PullingOutDriver
		|| Phase == EMTAVehicleEntryPhase::EnteringSeat;
	if (bEntering && (!TargetVehicle || TargetVehicle->IsActorBeingDestroyed()))
	{
		TargetVehicle = nullptr;
		SetPhase(EMTAVehicleEntryPhase::None, 0.f);
		SetComponentTickEnabled(false);
		return;
	}

	PhaseTimeLeft -= DeltaTime;
	const float PhaseAlpha = PhaseDuration > 0.f ? FMath::Clamp(1.f - PhaseTimeLeft / PhaseDuration, 0.f, 1.f) : 1.f;

	switch (Phase)
	{
	case EMTAVehicleEntryPhase::Approaching:
	{
		// Zur Fahrertuer laufen (einfaches Steering, kein Pfad - Distanzen sind kurz).
		const FVector Door = GetDoorLocation(TargetVehicle);
		FVector ToDoor = Door - Character->GetActorLocation();
		ToDoor.Z = 0.f;
		const float Dist = ToDoor.Size();
		if (Dist < 70.f)
		{
			AdvancePhase();
		}
		else if (PhaseTimeLeft <= 0.f)
		{
			// Nah genug: Rest per Snap; sonst abbrechen (Hindernis im Weg).
			if (Dist < 200.f)
			{
				AdvancePhase();
			}
			else
			{
				CancelEntry();
			}
		}
		else
		{
			Character->AddMovementInput(ToDoor / Dist, 1.f);
		}
		break;
	}
	case EMTAVehicleEntryPhase::BreakingIn:
		if (PhaseTimeLeft <= 0.f) { AdvancePhase(); }
		break;
	case EMTAVehicleEntryPhase::OpeningDoor:
		DoorOpenAlpha = PhaseAlpha;
		if (PhaseTimeLeft <= 0.f) { AdvancePhase(); }
		break;
	case EMTAVehicleEntryPhase::PullingOutDriver:
	case EMTAVehicleEntryPhase::EnteringSeat:
		if (PhaseTimeLeft <= 0.f) { AdvancePhase(); }
		break;
	case EMTAVehicleEntryPhase::ExitingSeat:
	case EMTAVehicleEntryPhase::BailingOut:
		DoorOpenAlpha = 1.f - PhaseAlpha;
		if (PhaseTimeLeft <= 0.f)
		{
			DoorOpenAlpha = 0.f;
			SetPhase(EMTAVehicleEntryPhase::None, 0.f);
			SetComponentTickEnabled(false);
		}
		break;
	default:
		SetComponentTickEnabled(false);
		break;
	}
}

void UMTAVehicleEntryComponent::AdvancePhase()
{
	ACharacter* Character = GetCharacter();
	switch (Phase)
	{
	case EMTAVehicleEntryPhase::Approaching:
		if (Character && TargetVehicle)
		{
			// An der Tuer ausrichten (Blick zum Fahrzeug).
			FVector ToVehicle = TargetVehicle->GetActorLocation() - Character->GetActorLocation();
			ToVehicle.Z = 0.f;
			if (!ToVehicle.IsNearlyZero())
			{
				Character->SetActorRotation(ToVehicle.Rotation());
			}
			Character->GetCharacterMovement()->StopMovementImmediately();
		}
		if (bTargetWasLocked)
		{
			SetPhase(EMTAVehicleEntryPhase::BreakingIn, BreakInTime);
			if (UMTAStealthComponent* Stealth = UMTAStealthComponent::FindStealthComponent(Character))
			{
				Stealth->ReportNoise(0.7f); // Scheibe einschlagen/Schloss knacken
			}
		}
		else
		{
			SetPhase(EMTAVehicleEntryPhase::OpeningDoor, DoorOpenTime);
		}
		break;
	case EMTAVehicleEntryPhase::BreakingIn:
		SetPhase(EMTAVehicleEntryPhase::OpeningDoor, DoorOpenTime);
		break;
	case EMTAVehicleEntryPhase::OpeningDoor:
		DoorOpenAlpha = 1.f;
		if (TargetVehicle && HasForeignDriver(TargetVehicle))
		{
			SetPhase(EMTAVehicleEntryPhase::PullingOutDriver, PullOutTime);
		}
		else
		{
			SetPhase(EMTAVehicleEntryPhase::EnteringSeat, EnterSeatTime);
		}
		break;
	case EMTAVehicleEntryPhase::PullingOutDriver:
		EjectDriver(TargetVehicle);
		SetPhase(EMTAVehicleEntryPhase::EnteringSeat, EnterSeatTime);
		break;
	case EMTAVehicleEntryPhase::EnteringSeat:
		CompleteEnter(false);
		break;
	default:
		break;
	}
}

void UMTAVehicleEntryComponent::EjectDriver(AWheeledVehiclePawn* Vehicle)
{
	if (!Vehicle)
	{
		return;
	}
	AController* OldController = Vehicle->GetController();
	if (!OldController || OldController->IsPlayerController())
	{
		return;
	}
	OldController->UnPossess();

	APawn* NewNpc = nullptr;
	UWorld* World = GetWorld();
	if (World && EjectedDriverClass)
	{
		// Neben der Tuer, vom Fahrzeug weg versetzt.
		const FVector Door = GetDoorLocation(Vehicle);
		FVector Away = Door - Vehicle->GetActorLocation();
		Away.Z = 0.f;
		const FVector SpawnLoc = Door + Away.GetSafeNormal() * 120.f + FVector(0.f, 0.f, 100.f);

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		NewNpc = World->SpawnActor<ACharacter>(EjectedDriverClass, SpawnLoc, Away.Rotation(), Params);
		if (NewNpc && !NewNpc->GetController())
		{
			NewNpc->SpawnDefaultController();
		}
	}
	OldController->Destroy();
	EjectedDriver = NewNpc;
	OnDriverEjected.Broadcast(Vehicle, NewNpc);
}

void UMTAVehicleEntryComponent::ReportTheftIfNeeded(AWheeledVehiclePawn* Vehicle)
{
	UMTAChaosSubsystem* Chaos = GetChaos();
	ACharacter* Character = GetCharacter();
	if (!Chaos || !Vehicle || !Character)
	{
		return;
	}
	const FMTAVehicleRecord Record = Chaos->GetOrCreateVehicleRecord(Vehicle);
	const bool bOwned = Record.bPlayerOwned || Vehicle->ActorHasTag(TEXT("PlayerOwned"));
	if (bOwned || (Record.bStolen && !bTargetHadDriver))
	{
		return; // eigenes oder bereits gestohlenes Auto
	}

	FMTACrimeEvent Crime;
	Crime.Type = bTargetHadDriver ? EMTACrimeType::Carjacking
		: (bTargetWasLocked ? EMTACrimeType::BreakIn : EMTACrimeType::VehicleTheft);
	Crime.Perpetrator = Character;
	Crime.PerpetratorVehicle = Vehicle;
	Crime.Victim = EjectedDriver ? static_cast<AActor*>(EjectedDriver.Get()) : static_cast<AActor*>(Vehicle);
	Crime.Location = Vehicle->GetActorLocation();
	Chaos->ReportCrime(Crime);

	// Aufbruch ist zugleich Diebstahl.
	if (Crime.Type == EMTACrimeType::BreakIn)
	{
		Chaos->ReportSimpleCrime(EMTACrimeType::VehicleTheft, Character, Vehicle->GetActorLocation(), Vehicle, 1.f);
	}
	Chaos->MarkVehicleStolen(Vehicle);
}

void UMTAVehicleEntryComponent::CompleteEnter(bool bSilent)
{
	ACharacter* Character = GetCharacter();
	AWheeledVehiclePawn* Vehicle = TargetVehicle;
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Character || !Vehicle || !PC)
	{
		TargetVehicle = nullptr;
		SetPhase(EMTAVehicleEntryPhase::None, 0.f);
		return;
	}

	if (!bSilent)
	{
		ReportTheftIfNeeded(Vehicle);
	}

	// Figur in den Sitz setzen.
	UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	Move->StopMovementImmediately();
	Move->DisableMovement();
	Character->SetActorEnableCollision(false);

	USkeletalMeshComponent* VehicleMesh = Vehicle->GetMesh();
	const FAttachmentTransformRules Rules(EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, EAttachmentRule::KeepWorld, false);
	if (VehicleMesh && VehicleMesh->DoesSocketExist(DriverSeatSocket))
	{
		Character->AttachToComponent(VehicleMesh, Rules, DriverSeatSocket);
	}
	else
	{
		Character->AttachToComponent(Vehicle->GetRootComponent(), Rules);
		Character->SetActorRelativeLocation(FallbackSeatOffset);
		Character->SetActorRelativeRotation(FRotator::ZeroRotator);
	}
	Character->SetActorHiddenInGame(bHideCharacterWhileDriving);

	// Fahrzeug startklar machen.
	if (UChaosVehicleMovementComponent* VMove = Vehicle->GetVehicleMovementComponent())
	{
		VMove->SetParked(false);
		VMove->SetHandbrakeInput(false);
		VMove->SetThrottleInput(0.f);
		VMove->SetBrakeInput(0.f);
	}

	// Possess wechseln. Possess() loest die Figur automatisch vom Controller.
	DrivingController = PC;
	PC->Possess(Vehicle);
	PushExitInput(PC);

	CurrentVehicle = Vehicle;
	TargetVehicle = nullptr;
	DoorOpenAlpha = 0.f;

	if (UMTAChaosSubsystem* Chaos = GetChaos())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(UnwatchTimer);
		}
		StopWatchingVehicle();
		Chaos->RegisterPlayerSubject(Vehicle);
		Chaos->WatchVehicle(Vehicle, Character);
		LastWatchedVehicle = Vehicle;
	}

	SetPhase(EMTAVehicleEntryPhase::Seated, 0.f);
	SetComponentTickEnabled(false);
	OnEnteredVehicle.Broadcast(Vehicle);
}

void UMTAVehicleEntryComponent::PlaceInVehicleImmediately(AWheeledVehiclePawn* Vehicle)
{
	if (!Vehicle || Phase != EMTAVehicleEntryPhase::None)
	{
		return;
	}
	TargetVehicle = Vehicle;
	bTargetHadDriver = false;
	bTargetWasLocked = false;
	CompleteEnter(true);
}

// ---------------------------------------------------------------------------------------
// Aussteigen
// ---------------------------------------------------------------------------------------

bool UMTAVehicleEntryComponent::RequestExit()
{
	if (Phase != EMTAVehicleEntryPhase::Seated || !CurrentVehicle)
	{
		return false;
	}
	float SpeedKmh = CurrentVehicle->GetVelocity().Size() * 0.036f;
	if (const UChaosVehicleMovementComponent* VMove = CurrentVehicle->GetVehicleMovementComponent())
	{
		SpeedKmh = FMath::Abs(VMove->GetForwardSpeed()) * 0.036f;
	}
	CompleteExit(SpeedKmh > MaxCalmExitSpeedKmh);
	return true;
}

bool UMTAVehicleEntryComponent::FindExitLocation(const AWheeledVehiclePawn* Vehicle, FVector& OutLocation) const
{
	const ACharacter* Character = GetCharacter();
	UWorld* World = GetWorld();
	if (!Character || !Vehicle || !World)
	{
		return false;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MTAVehicleExit), false, Vehicle);
	Params.AddIgnoredActor(Character);

	const FTransform VT = Vehicle->GetActorTransform();
	// Reihenfolge: Fahrertuer, Beifahrerseite, hinten, vorne, Dach.
	TArray<FVector> Candidates;
	Candidates.Add(GetDoorLocation(Vehicle));
	Candidates.Add(VT.TransformPosition(FVector(FallbackDoorOffset.X, -FallbackDoorOffset.Y, FallbackDoorOffset.Z)));
	Candidates.Add(VT.TransformPosition(FVector(-350.f, 0.f, 0.f)));
	Candidates.Add(VT.TransformPosition(FVector(350.f, 0.f, 0.f)));
	Candidates.Add(VT.TransformPosition(FVector(0.f, 0.f, 250.f)));

	for (const FVector& Candidate : Candidates)
	{
		// Boden suchen und Kapsel darauf stellen.
		FVector Pos = Candidate + FVector(0.f, 0.f, HalfHeight + 20.f);
		FHitResult Ground;
		if (World->LineTraceSingleByChannel(Ground, Pos + FVector(0.f, 0.f, 100.f), Pos - FVector(0.f, 0.f, 500.f), ECC_Visibility, Params))
		{
			Pos.Z = Ground.ImpactPoint.Z + HalfHeight + 5.f;
		}
		if (!World->OverlapBlockingTestByChannel(Pos, FQuat::Identity, ECC_Pawn, Shape, Params))
		{
			OutLocation = Pos;
			return true;
		}
	}
	return false;
}

void UMTAVehicleEntryComponent::CompleteExit(bool bBailOut)
{
	ACharacter* Character = GetCharacter();
	AWheeledVehiclePawn* Vehicle = CurrentVehicle;
	APlayerController* PC = GetPlayerController();
	if (!Character || !Vehicle)
	{
		return;
	}

	FVector ExitLocation;
	if (!FindExitLocation(Vehicle, ExitLocation))
	{
		// Kein Platz (Auto zwischen Waenden): Notloesung ueber dem Dach.
		ExitLocation = Vehicle->GetActorLocation() + FVector(0.f, 0.f, 300.f);
	}
	const FVector VehicleVelocity = Vehicle->GetVelocity();

	PopExitInput();

	// Fahrzeug sichern: abgesprungene Autos rollen weiter.
	if (UChaosVehicleMovementComponent* VMove = Vehicle->GetVehicleMovementComponent())
	{
		VMove->SetThrottleInput(0.f);
		VMove->SetSteeringInput(0.f);
		VMove->SetHandbrakeInput(!bBailOut);
	}

	Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Character->SetActorLocation(ExitLocation, false, nullptr, ETeleportType::TeleportPhysics);
	FVector Away = ExitLocation - Vehicle->GetActorLocation();
	Away.Z = 0.f;
	Character->SetActorRotation(FRotator(0.f, Away.IsNearlyZero() ? Vehicle->GetActorRotation().Yaw : Away.Rotation().Yaw, 0.f));
	Character->SetActorHiddenInGame(false);
	Character->SetActorEnableCollision(true);
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);

	if (PC)
	{
		const FRotator ControlRot(0.f, Vehicle->GetActorRotation().Yaw, 0.f);
		PC->Possess(Character);
		PC->SetControlRotation(ControlRot);
	}
	DrivingController = nullptr;

	if (UMTAChaosSubsystem* Chaos = GetChaos())
	{
		Chaos->UnregisterPlayerSubject(Vehicle);
	}
	// Kollisionen des rollenden Fahrzeugs noch kurz dem Spieler zurechnen (Fahrerflucht, Sachschaden).
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(UnwatchTimer, this, &UMTAVehicleEntryComponent::StopWatchingVehicle, bBailOut ? 8.f : 1.f, false);
	}

	CurrentVehicle = nullptr;
	DoorOpenAlpha = 1.f;
	SetComponentTickEnabled(true);

	if (bBailOut)
	{
		SetPhase(EMTAVehicleEntryPhase::BailingOut, ExitTime);
		const FVector Side = Away.GetSafeNormal();
		Character->LaunchCharacter(VehicleVelocity * 0.4f + Side * 500.f + FVector(0.f, 0.f, 250.f), true, true);
		if (AMTACharacter* MTAChar = Cast<AMTACharacter>(Character))
		{
			MTAChar->StartBailOutRoll(Side);
		}
	}
	else
	{
		SetPhase(EMTAVehicleEntryPhase::ExitingSeat, ExitTime);
	}
	OnExitedVehicle.Broadcast(Vehicle);
}

void UMTAVehicleEntryComponent::StopWatchingVehicle()
{
	AWheeledVehiclePawn* Vehicle = LastWatchedVehicle.Get();
	if (Vehicle && Vehicle != CurrentVehicle)
	{
		if (UMTAChaosSubsystem* Chaos = GetChaos())
		{
			Chaos->UnwatchVehicle(Vehicle);
		}
		LastWatchedVehicle.Reset();
	}
}

// ---------------------------------------------------------------------------------------
// Eingabe waehrend der Fahrt
// ---------------------------------------------------------------------------------------

void UMTAVehicleEntryComponent::PushExitInput(APlayerController* PC)
{
	if (!PC)
	{
		return;
	}
	PopExitInput();
	// Eigene Eingabe-Komponente auf dem Controller-Stack, damit AMTAVehiclePawn nichts binden muss.
	ExitInput = NewObject<UInputComponent>(GetOwner(), TEXT("MTAVehicleExitInput"));
	ExitInput->Priority = 10;
	ExitInput->bBlockInput = false;
	ExitInput->BindAction(EnterExitActionName, IE_Pressed, this, &UMTAVehicleEntryComponent::HandleExitPressed);
	ExitInput->RegisterComponent();
	PC->PushInputComponent(ExitInput);
}

void UMTAVehicleEntryComponent::PopExitInput()
{
	if (!ExitInput)
	{
		return;
	}
	if (APlayerController* PC = DrivingController.Get())
	{
		PC->PopInputComponent(ExitInput);
	}
	ExitInput->DestroyComponent();
	ExitInput = nullptr;
}

void UMTAVehicleEntryComponent::HandleExitPressed()
{
	RequestExit();
}

#undef LOCTEXT_NAMESPACE
