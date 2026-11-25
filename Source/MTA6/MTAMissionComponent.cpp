#include "MTAMissionComponent.h"
#include "MTAMissionRunner.h"

#include "MTA6GameMode.h"
#include "MTAMinimapTypes.h"

#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

UMTAMissionComponent::UMTAMissionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// Engine-Grundformen, damit kein eigenes Asset noetig ist
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderFinder.Succeeded())
	{
		MarkerMesh = CylinderFinder.Object;
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (MaterialFinder.Succeeded())
	{
		MarkerMaterial = MaterialFinder.Object;
	}
}

UMTAStorySubsystem* UMTAMissionComponent::GetStory() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UMTAStorySubsystem>() : nullptr;
}

AMTA6GameMode* UMTAMissionComponent::GetMTAGameMode() const
{
	if (AMTA6GameMode* GM = Cast<AMTA6GameMode>(GetOwner()))
	{
		return GM;
	}
	const UWorld* World = GetWorld();
	return World ? World->GetAuthGameMode<AMTA6GameMode>() : nullptr;
}

bool UMTAMissionComponent::GetPlayerLocation(FVector& OutLocation) const
{
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		OutLocation = Pawn->GetActorLocation();
		return true;
	}
	return false;
}

void UMTAMissionComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (World && MarkerMesh)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		MarkerActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform::Identity, Params);
		if (MarkerActor)
		{
			MarkerActor->SetMobility(EComponentMobility::Movable);
			UStaticMeshComponent* SMC = MarkerActor->GetStaticMeshComponent();
			SMC->SetStaticMesh(MarkerMesh);
			SMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			SMC->SetCastShadow(false);
			if (MarkerMaterial)
			{
				MarkerMID = UMaterialInstanceDynamic::Create(MarkerMaterial, this);
				if (MarkerMID)
				{
					MarkerMID->SetVectorParameterValue(TEXT("Color"), MarkerColor);
					SMC->SetMaterial(0, MarkerMID);
				}
			}
			MarkerActor->SetActorScale3D(FVector(MarkerDiameter / 100.f, MarkerDiameter / 100.f, MarkerHeight / 100.f));
		}
	}
	// Die erste Mission wird im ersten Tick gewaehlt, wenn der Spieler-Pawn existiert.

	Runner = UMTAMissionRunner::Get(this);
	if (Runner)
	{
		Runner->OnMissionFinished.AddUniqueDynamic(this, &UMTAMissionComponent::HandleRunnerFinished);
	}
}

void UMTAMissionComponent::HandleRunnerFinished(bool bSuccess)
{
	if (!bSuccess || !Runner || !Runner->WasLastFinishedFromStory())
	{
		return;
	}
	UMTAStorySubsystem* Story = GetStory();
	if (!Story)
	{
		return;
	}
	const FString Id = Runner->GetLastFinishedMissionId();
	for (const FMTAMission& M : Story->GetAvailableMissions())
	{
		if (M.Id == Id)
		{
			CurrentMission = M;
			bHasStoryMission = true;
			BeginStoryDialog();   // Text -> Entscheidungen -> FinishStoryMission (Abschluss, Geld, Fahndung)
			return;
		}
	}
}

void UMTAMissionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MarkerActor)
	{
		MarkerActor->Destroy();
		MarkerActor = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void UMTAMissionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	FVector PlayerLoc;
	if (!GetPlayerLocation(PlayerLoc))
	{
		return;
	}
	if (!bInitialized)
	{
		bInitialized = true;
		StartNextMission();
	}

	if (Runner)
	{
		// Ziel und Zeit kommen vom Runner; eigener Marker bleibt verborgen, keine Ankunftspruefung mehr
		FVector Obj;
		if (Runner->GetPrimaryObjectiveLocation(Obj))
		{
			TargetLocation = Obj;
		}
		TimeLeft = Runner->GetTimeLeft();
		if (MarkerActor)
		{
			MarkerActor->SetActorHiddenInGame(true);
		}
		return;
	}

	// Ohne Mesh-Asset: Debug-Zylinder als Ersatzmarker (nur in Nicht-Shipping-Builds sichtbar)
	if (!MarkerActor)
	{
		DrawDebugCylinder(GetWorld(), TargetLocation, TargetLocation + FVector(0.f, 0.f, MarkerHeight),
			MarkerDiameter * 0.5f, 16, MarkerColor.ToFColor(true), false, -1.f, 0, 8.f);
	}

	// Waehrend eines Dialogs ruht das Spiel (wie im Prototyp)
	if (IsDialogOpen())
	{
		return;
	}

	TimeLeft -= DeltaTime;

	const float Dist2D = FVector::Dist2D(PlayerLoc, TargetLocation);
	if (Dist2D < ArrivalRadius)
	{
		if (bHasStoryMission)
		{
			BeginStoryDialog();
		}
		else
		{
			CompleteCourier();
		}
	}
}

float UMTAMissionComponent::FindGroundZ(float X, float Y) const
{
	if (const UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MTAMissionGround), false);
		if (MarkerActor)
		{
			Params.AddIgnoredActor(MarkerActor);
		}
		if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			Params.AddIgnoredActor(Pawn);
		}
		if (World->LineTraceSingleByChannel(Hit, FVector(X, Y, 100000.f), FVector(X, Y, -100000.f), ECC_Visibility, Params))
		{
			return Hit.ImpactPoint.Z;
		}
	}
	return 0.f;
}

void UMTAMissionComponent::PlaceMarker()
{
	if (MarkerActor)
	{
		MarkerActor->SetActorLocation(TargetLocation + FVector(0.f, 0.f, MarkerHeight * 0.5f));
	}
}

void UMTAMissionComponent::StartNextMission()
{
	if (Runner)
	{
		// Auftraege waehlt der Spieler ueber die Auftragsliste (Tab); keine automatische Auswahl mehr
		bHasStoryMission = false;
		CurrentMission = FMTAMission();
		OnMissionChanged.Broadcast();
		return;
	}
	// 1. Story-Mission, falls verfuegbar (Liste ist bereits nach Hauptstory/Akt sortiert)
	if (UMTAStorySubsystem* Story = GetStory())
	{
		const TArray<FMTAMission> Available = Story->GetAvailableMissions();
		if (Available.Num() > 0)
		{
			CurrentMission = Available[0];
			bHasStoryMission = true;
			const FVector2D P = MTAGrid::CellToWorld(CurrentMission.CellX, CurrentMission.CellY);
			TargetLocation = FVector(P.X, P.Y, FindGroundZ(P.X, P.Y));
			TimeLeft = 0.f;
			PlaceMarker();
			OnMissionChanged.Broadcast();
			return;
		}
	}

	// 2. Freie Kurierfahrt: zufaellige Kreuzung mit Mindestabstand
	bHasStoryMission = false;
	CurrentMission = FMTAMission();

	FVector PlayerLoc = FVector::ZeroVector;
	GetPlayerLocation(PlayerLoc);

	FVector2D Best(MTAGrid::R(0), MTAGrid::R(0));
	float BestDist = -1.f;
	for (int32 Try = 0; Try < 30; ++Try)
	{
		const FVector2D P = MTAGrid::CellToWorld(FMath::RandRange(0, MTAGrid::Blocks), FMath::RandRange(0, MTAGrid::Blocks));
		const float D = FVector2D::Distance(P, FVector2D(PlayerLoc.X, PlayerLoc.Y));
		if (D > BestDist)
		{
			Best = P;
			BestDist = D;
		}
		if (D > CourierMinDistance)
		{
			break;
		}
	}

	TargetLocation = FVector(Best.X, Best.Y, FindGroundZ(Best.X, Best.Y));
	TimeLeft = (CourierReferenceSpeed > 0.f ? BestDist / CourierReferenceSpeed : 0.f) + CourierExtraTime;
	PlaceMarker();
	OnMissionChanged.Broadcast();
}

void UMTAMissionComponent::RestoreCourier(const FVector& Target, float InTimeLeft)
{
	if (bHasStoryMission)
	{
		return;
	}
	TargetLocation = FVector(Target.X, Target.Y, FindGroundZ(Target.X, Target.Y));
	TimeLeft = InTimeLeft;
	PlaceMarker();
	OnMissionChanged.Broadcast();
}

void UMTAMissionComponent::CompleteCourier()
{
	const int32 Gain = CourierBaseReward + FMath::FloorToInt(FMath::Max(0.f, TimeLeft) * CourierBonusPerSecond);
	StartNextMission();
	if (AMTA6GameMode* GM = GetMTAGameMode())
	{
		GM->AddMoney(Gain);
		GM->SaveGameToSlot(true);
		GM->ShowToast(FString::Printf(TEXT("Ziel erreicht!  +$%d"), Gain));
	}
}

void UMTAMissionComponent::ShowDialog(EMTADialogStage Stage, const FString& Title, const FString& Text, const TArray<FMTAChoiceOption>& Options)
{
	DialogStage = Stage;
	DialogTitle = Title;
	DialogText = Text;
	DialogOptions = Options;
	OnDialogChanged.Broadcast();
}

void UMTAMissionComponent::CloseDialog()
{
	const bool bWasOpen = IsDialogOpen();
	DialogStage = EMTADialogStage::None;
	DialogOptions.Reset();
	DialogQueue.Reset();
	DialogChoiceIndex = INDEX_NONE;
	if (bWasOpen)
	{
		OnDialogChanged.Broadcast();
	}
}

void UMTAMissionComponent::BeginStoryDialog()
{
	DialogMission = CurrentMission;
	DialogQueue.Reset();
	for (int32 i = 0; i < DialogMission.NumChoices; ++i)
	{
		DialogQueue.Add(i);
	}
	ShowDialog(EMTADialogStage::Text, DialogMission.Title, DialogMission.Text, TArray<FMTAChoiceOption>());
}

void UMTAMissionComponent::NextChoiceOrFinish()
{
	UMTAStorySubsystem* Story = GetStory();
	while (Story && DialogQueue.Num() > 0)
	{
		const int32 ChoiceIndex = DialogQueue[0];
		DialogQueue.RemoveAt(0);
		// Optionen erst jetzt abfragen: fruehere Entscheidungen koennen Bedingungen aendern
		const TArray<FMTAChoiceOption> Options = Story->GetChoiceOptions(DialogMission.Id, ChoiceIndex);
		if (Options.Num() == 0)
		{
			continue;
		}
		DialogChoiceIndex = ChoiceIndex;
		ShowDialog(EMTADialogStage::Choice, DialogMission.Title, Story->GetChoiceText(DialogMission.Id, ChoiceIndex), Options);
		return;
	}
	FinishStoryMission();
}

void UMTAMissionComponent::ApplyStoryOutputs()
{
	UMTAStorySubsystem* Story = GetStory();
	AMTA6GameMode* GM = GetMTAGameMode();
	if (!Story || !GM)
	{
		return;
	}
	GM->AddMoney(Story->ConsumeMoney());
	const float Heat = Story->ConsumeHeat();
	if (Heat > 0.f)
	{
		GM->ReportCrime(Heat);
	}
}

void UMTAMissionComponent::FinishStoryMission()
{
	UMTAStorySubsystem* Story = GetStory();
	if (Story)
	{
		Story->CompleteMission(DialogMission.Id);
	}
	ApplyStoryOutputs();

	const FMTAMission Finished = DialogMission;
	CloseDialog();
	StartNextMission();

	if (AMTA6GameMode* GM = GetMTAGameMode())
	{
		GM->SaveGameToSlot(true);
		GM->ShowToast(FString::Printf(TEXT("Auftrag abgeschlossen: %s"), *Finished.Title));
	}

	if (Finished.bFinale && Story)
	{
		const FMTAEnding Ending = Story->EvaluateEnding();
		ShowDialog(EMTADialogStage::Ending, TEXT("ENDE: ") + Ending.Title,
			Ending.Epilogue + TEXT("\n\nDu kannst weiter frei fahren."), TArray<FMTAChoiceOption>());
	}
}

void UMTAMissionComponent::SelectOption(int32 Index)
{
	if (DialogStage != EMTADialogStage::Choice || !DialogOptions.IsValidIndex(Index))
	{
		return;
	}
	if (UMTAStorySubsystem* Story = GetStory())
	{
		Story->ChooseOption(DialogMission.Id, DialogChoiceIndex, DialogOptions[Index].Id);
	}
	ApplyStoryOutputs();
	NextChoiceOrFinish();
}

void UMTAMissionComponent::ConfirmDialog()
{
	switch (DialogStage)
	{
	case EMTADialogStage::Text:
		NextChoiceOrFinish();
		break;
	case EMTADialogStage::Ending:
		CloseDialog();
		break;
	default:
		break;
	}
}

FString UMTAMissionComponent::GetMissionDisplayTitle() const
{
	return bHasStoryMission ? FString::Printf(TEXT("Hauptstory: %s"), *CurrentMission.Title) : FString(TEXT("Kurierfahrt"));
}

float UMTAMissionComponent::GetDistanceToTarget() const
{
	FVector PlayerLoc;
	return GetPlayerLocation(PlayerLoc) ? FVector::Dist2D(PlayerLoc, TargetLocation) : -1.f;
}
