#include "MTAInteractionActors.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MTAChaosAegisCamera.h"
#include "MTAChaosSubsystem.h"
#include "MTAHeatSubsystem.h"
#include "MTAStealthComponent.h"

#define LOCTEXT_NAMESPACE "MTAInteractionActors"

// ---------------------------------------------------------------------------------------
// Basis
// ---------------------------------------------------------------------------------------

AMTAInteractableActor::AMTAInteractableActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Prompt = LOCTEXT("Use", "Benutzen");
}

FMTAInteractionInfo AMTAInteractableActor::GetInteractionInfo_Implementation(AActor* Interactor) const
{
	FMTAInteractionInfo Info;
	Info.Prompt = Prompt;
	Info.Kind = GetKind();
	Info.HoldDuration = HoldDuration;
	Info.bEnabled = bInteractionEnabled;
	Info.Priority = Priority;
	return Info;
}

bool AMTAInteractableActor::CanInteract_Implementation(AActor* Interactor) const
{
	return bInteractionEnabled;
}

void AMTAInteractableActor::Interact_Implementation(AActor* Interactor)
{
	if (!IMTAInteractable::Execute_CanInteract(this, Interactor))
	{
		return;
	}
	PerformInteraction(Interactor);
	OnInteracted.Broadcast(Interactor, InteractionId);
}

void AMTAInteractableActor::ReportCrime(uint8 CrimeType, AActor* Interactor, float Severity) const
{
	if (UWorld* World = GetWorld())
	{
		if (UMTAChaosSubsystem* Chaos = World->GetSubsystem<UMTAChaosSubsystem>())
		{
			Chaos->ReportSimpleCrime(static_cast<EMTACrimeType>(CrimeType), Interactor, GetActorLocation(),
				const_cast<AMTAInteractableActor*>(this), Severity);
		}
	}
}

// ---------------------------------------------------------------------------------------
// Tuer
// ---------------------------------------------------------------------------------------

AMTAInteractDoor::AMTAInteractDoor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(Root);
	// Tuerblatt dreht am Scharnier (Mesh-Pivot an der Scharnierkante platzieren).
	Mesh->SetupAttachment(Hinge);
	Prompt = LOCTEXT("Open", "Tuer oeffnen");
}

FMTAInteractionInfo AMTAInteractDoor::GetInteractionInfo_Implementation(AActor* Interactor) const
{
	FMTAInteractionInfo Info = Super::GetInteractionInfo_Implementation(Interactor);
	if (bLocked)
	{
		if (bCanBreakIn)
		{
			Info.Prompt = LOCTEXT("BreakIn", "Tuer aufbrechen");
			Info.HoldDuration = BreakInDuration;
			Info.bIsIllegal = true;
		}
		else
		{
			Info.Prompt = LOCTEXT("Locked", "Abgeschlossen");
			Info.bEnabled = false;
		}
	}
	else
	{
		Info.Prompt = bOpen ? LOCTEXT("Close", "Tuer schliessen") : LOCTEXT("OpenDoor", "Tuer oeffnen");
		Info.bIsIllegal = bPrivateProperty && !bOpen;
	}
	return Info;
}

bool AMTAInteractDoor::CanInteract_Implementation(AActor* Interactor) const
{
	return bInteractionEnabled && (!bLocked || bCanBreakIn);
}

void AMTAInteractDoor::PerformInteraction(AActor* Interactor)
{
	if (bLocked)
	{
		bLocked = false;
		ReportCrime(static_cast<uint8>(EMTACrimeType::BreakIn), Interactor);
		if (UMTAStealthComponent* Stealth = UMTAStealthComponent::FindStealthComponent(Interactor))
		{
			Stealth->ReportNoise(0.8f);
		}
	}
	else if (bPrivateProperty && !bOpen)
	{
		ReportCrime(static_cast<uint8>(EMTACrimeType::Trespass), Interactor, 0.5f);
	}
	bOpen = !bOpen;
	SetActorTickEnabled(true);
}

void AMTAInteractDoor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Target = bOpen ? 1.f : 0.f;
	OpenAlpha = FMath::FInterpConstantTo(OpenAlpha, Target, DeltaSeconds, OpenSpeed);
	Hinge->SetRelativeRotation(FRotator(0.f, OpenAngle * OpenAlpha, 0.f));
	if (FMath::IsNearlyEqual(OpenAlpha, Target))
	{
		SetActorTickEnabled(false);
	}
}

// ---------------------------------------------------------------------------------------
// Haendler, Garage
// ---------------------------------------------------------------------------------------

AMTAInteractVendor::AMTAInteractVendor()
{
	Prompt = LOCTEXT("Shop", "Handeln");
}

FMTAInteractionInfo AMTAInteractVendor::GetInteractionInfo_Implementation(AActor* Interactor) const
{
	FMTAInteractionInfo Info = Super::GetInteractionInfo_Implementation(Interactor);
	if (const UWorld* World = GetWorld())
	{
		const UMTAChaosSubsystem* Chaos = World->GetSubsystem<UMTAChaosSubsystem>();
		const FName District = Chaos ? Chaos->ResolveDistrict(GetActorLocation()) : NAME_None;
		if (Chaos && Chaos->GetDistrictReputation(District) < MinDistrictReputation)
		{
			Info.bEnabled = false;
			Info.DisabledReason = LOCTEXT("BadRep", "Will nichts mit dir zu tun haben");
		}
		const UMTAHeatSubsystem* Heat = World->GetSubsystem<UMTAHeatSubsystem>();
		if (Heat && Heat->GetHeat(District) > MaxHeat)
		{
			Info.bEnabled = false;
			Info.DisabledReason = LOCTEXT("Heat", "Laden schliesst - zu viel Polizei");
		}
	}
	return Info;
}

AMTAInteractGarage::AMTAInteractGarage()
{
	Prompt = LOCTEXT("Garage", "Garage oeffnen");
}

// ---------------------------------------------------------------------------------------
// Terminal
// ---------------------------------------------------------------------------------------

AMTAInteractTerminal::AMTAInteractTerminal()
{
	Prompt = LOCTEXT("Hack", "Terminal hacken");
	HoldDuration = 3.f;
}

bool AMTAInteractTerminal::CanInteract_Implementation(AActor* Interactor) const
{
	const UWorld* World = GetWorld();
	return bInteractionEnabled && World && World->GetTimeSeconds() >= NextAvailableTime;
}

FMTAInteractionInfo AMTAInteractTerminal::GetInteractionInfo_Implementation(AActor* Interactor) const
{
	FMTAInteractionInfo Info = Super::GetInteractionInfo_Implementation(Interactor);
	Info.bIsIllegal = true;
	const UWorld* World = GetWorld();
	if (World && World->GetTimeSeconds() < NextAvailableTime)
	{
		Info.bEnabled = false;
		Info.DisabledReason = LOCTEXT("Cooldown", "Netz wird neu gestartet");
	}
	return Info;
}

void AMTAInteractTerminal::PerformInteraction(AActor* Interactor)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	NextAvailableTime = World->GetTimeSeconds() + Cooldown;

	TSet<UMTAAegisCameraComponent*> Targets;
	for (AMTAAegisCameraActor* CameraActor : LinkedCameras)
	{
		if (CameraActor && CameraActor->Camera)
		{
			Targets.Add(CameraActor->Camera);
		}
	}
	if (CameraRadius > 0.f)
	{
		for (TActorIterator<AMTAAegisCameraActor> It(World); It; ++It)
		{
			if (It->Camera && FVector::Dist(It->GetActorLocation(), GetActorLocation()) <= CameraRadius)
			{
				Targets.Add(It->Camera);
			}
		}
	}
	// Nur das erste Hacken meldet Sabotage (ein Verbrechen pro Terminal-Hack).
	bool bReported = false;
	for (UMTAAegisCameraComponent* Camera : Targets)
	{
		Camera->Hack(HackDuration, bReported ? nullptr : Interactor);
		bReported = true;
	}
}

// ---------------------------------------------------------------------------------------
// Beweis
// ---------------------------------------------------------------------------------------

AMTAInteractEvidence::AMTAInteractEvidence()
{
	Prompt = LOCTEXT("Pickup", "Aufheben");
	Priority = 2;
	Mesh->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
}

void AMTAInteractEvidence::PerformInteraction(AActor* Interactor)
{
	if (bIsPoliceEvidence)
	{
		ReportCrime(static_cast<uint8>(EMTACrimeType::EvidenceTampering), Interactor);
	}
	// Nach dem Broadcast in Interact_Implementation entfernen.
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetLifeSpan(0.1f);
}

#undef LOCTEXT_NAMESPACE
