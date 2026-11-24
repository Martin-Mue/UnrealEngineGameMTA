#include "MTAMissionPickup.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AMTAMissionPickup::AMTAMissionPickup()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	SceneRoot->SetMobility(EComponentMobility::Movable);

	BeaconMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeaconMesh"));
	BeaconMesh->SetupAttachment(SceneRoot);
	BeaconMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BeaconMesh->SetCastShadow(false);

	GroundDisc = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GroundDisc"));
	GroundDisc->SetupAttachment(SceneRoot);
	GroundDisc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GroundDisc->SetCastShadow(false);

	InteractVolume = CreateDefaultSubobject<USphereComponent>(TEXT("InteractVolume"));
	InteractVolume->SetupAttachment(SceneRoot);
	InteractVolume->SetSphereRadius(250.f);
	InteractVolume->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
	InteractVolume->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	InteractVolume->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CylinderFinder.Succeeded())
	{
		BeaconMesh->SetStaticMesh(CylinderFinder.Object);
		GroundDisc->SetStaticMesh(CylinderFinder.Object);
	}
	if (MaterialFinder.Succeeded())
	{
		BaseMaterial = MaterialFinder.Object;
	}
}

void AMTAMissionPickup::BeginPlay()
{
	Super::BeginPlay();
	if (BaseMaterial)
	{
		BeaconMID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		DiscMID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		if (BeaconMID) { BeaconMesh->SetMaterial(0, BeaconMID); }
		if (DiscMID) { GroundDisc->SetMaterial(0, DiscMID); }
	}
	ApplyLook();
}

FLinearColor AMTAMissionPickup::GetKindColor(EMTAPickupKind InKind)
{
	switch (InKind)
	{
	case EMTAPickupKind::Pickup:     return FLinearColor(1.f, 0.82f, 0.2f);
	case EMTAPickupKind::Dropoff:    return FLinearColor(0.25f, 1.f, 0.35f);
	case EMTAPickupKind::Checkpoint: return FLinearColor(0.2f, 0.85f, 1.f);
	case EMTAPickupKind::Terminal:   return FLinearColor(0.75f, 0.3f, 1.f);
	case EMTAPickupKind::Zone:       return FLinearColor(1.f, 0.55f, 0.15f);
	case EMTAPickupKind::SafeZone:   return FLinearColor(0.3f, 0.45f, 1.f);
	default:                         return FLinearColor(1.f, 0.82f, 0.2f);
	}
}

void AMTAMissionPickup::Setup(EMTAPickupKind InKind, float InRadius, const FString& InPrompt)
{
	Kind = InKind;
	Radius = FMath::Max(100.f, InRadius);
	PromptText = InPrompt;
	bInteracted = false;
	Progress = 0.f;
	ApplyLook();
}

void AMTAMissionPickup::SetMarkerState(bool bInCurrent, bool bInPreview)
{
	bCurrent = bInCurrent;
	bPreview = bInPreview && !bInCurrent;
	ApplyLook();
}

void AMTAMissionPickup::SetProgress(float Alpha)
{
	const float Clamped = FMath::Clamp(Alpha, 0.f, 1.f);
	if (!FMath::IsNearlyEqual(Clamped, Progress, 0.01f))
	{
		Progress = Clamped;
		ApplyLook();
	}
}

bool AMTAMissionPickup::ConsumeInteraction()
{
	const bool bWas = bInteracted;
	bInteracted = false;
	return bWas;
}

void AMTAMissionPickup::ApplyLook()
{
	const bool bVisible = bCurrent || bPreview;
	SetActorHiddenInGame(!bVisible);

	// Engine-Zylinder: 100 cm Durchmesser, 100 cm hoch, Ursprung in der Mitte
	const float Height = BeaconHeight * (bPreview ? 0.35f : 1.f) * (0.6f + 0.4f * (1.f - Progress));
	const float Diameter = BeaconDiameter * (bPreview ? 0.6f : 1.f);
	BeaconMesh->SetRelativeScale3D(FVector(Diameter / 100.f, Diameter / 100.f, Height / 100.f));
	BeaconMesh->SetRelativeLocation(FVector(0.f, 0.f, Height * 0.5f));

	GroundDisc->SetRelativeScale3D(FVector(Radius * 2.f / 100.f, Radius * 2.f / 100.f, 0.08f));
	GroundDisc->SetRelativeLocation(FVector(0.f, 0.f, 5.f));
	GroundDisc->SetVisibility(bCurrent && Kind != EMTAPickupKind::Checkpoint);

	FLinearColor Color = GetKindColor(Kind);
	if (bPreview)
	{
		Color *= 0.35f;
	}
	Color = FMath::Lerp(Color, FLinearColor::White, Progress * 0.7f);
	Color.A = 1.f;
	if (BeaconMID)
	{
		BeaconMID->SetVectorParameterValue(TEXT("Color"), Color);
	}
	if (DiscMID)
	{
		DiscMID->SetVectorParameterValue(TEXT("Color"), Color * 0.6f);
	}

	const bool bInteractive = bCurrent && !PromptText.IsEmpty();
	InteractVolume->SetCollisionEnabled(bInteractive ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

// --- IMTAInteractable ------------------------------------------------------------------------

FMTAInteractionInfo AMTAMissionPickup::GetInteractionInfo_Implementation(AActor* Interactor) const
{
	FMTAInteractionInfo Info;
	Info.Prompt = FText::FromString(PromptText);
	Info.Kind = Kind == EMTAPickupKind::Terminal ? EMTAInteractionKind::Terminal : EMTAInteractionKind::Generic;
	Info.HoldDuration = InteractHoldDuration;
	Info.bEnabled = bCurrent && !PromptText.IsEmpty();
	Info.Priority = 5;
	Info.bIsIllegal = Kind == EMTAPickupKind::Terminal;
	return Info;
}

bool AMTAMissionPickup::CanInteract_Implementation(AActor* Interactor) const
{
	return bCurrent && !PromptText.IsEmpty();
}

void AMTAMissionPickup::Interact_Implementation(AActor* Interactor)
{
	if (bCurrent && !PromptText.IsEmpty())
	{
		bInteracted = true;
	}
}

void AMTAMissionPickup::OnInteractionHoldProgress_Implementation(AActor* Interactor, float Alpha)
{
	// Rueckmeldung beim Halten: Saeule pulsiert ueber den Fortschritt
	SetProgress(FMath::Max(Progress, Alpha * 0.25f));
}
