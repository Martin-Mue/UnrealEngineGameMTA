#include "MTAStealthHideout.h"

#include "Components/BoxComponent.h"
#include "Engine/HitResult.h"
#include "WheeledVehiclePawn.h"
#include "MTAStealthComponent.h"

#define LOCTEXT_NAMESPACE "MTAStealthHideout"

AMTAStealthHideout::AMTAStealthHideout()
{
	PrimaryActorTick.bCanEverTick = false;

	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	Volume->SetBoxExtent(FVector(400.f, 300.f, 200.f));
	Volume->SetCollisionProfileName(TEXT("Trigger"));
	Volume->SetGenerateOverlapEvents(true);
	SetRootComponent(Volume);
}

void AMTAStealthHideout::BeginPlay()
{
	Super::BeginPlay();
	Volume->OnComponentBeginOverlap.AddDynamic(this, &AMTAStealthHideout::HandleBeginOverlap);
	Volume->OnComponentEndOverlap.AddDynamic(this, &AMTAStealthHideout::HandleEndOverlap);
}

void AMTAStealthHideout::HandleBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || (!bAllowsVehicles && OtherActor->IsA<AWheeledVehiclePawn>()))
	{
		return;
	}
	// Figur direkt oder Fahrzeug mit Spielerfigur als Insasse.
	if (UMTAStealthComponent* Stealth = UMTAStealthComponent::FindStealthComponent(OtherActor))
	{
		Stealth->EnterHideout(this);
	}
}

void AMTAStealthHideout::HandleEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (UMTAStealthComponent* Stealth = UMTAStealthComponent::FindStealthComponent(OtherActor))
	{
		// Beim Aussteigen in der Garage endet nur das Fahrzeug-Overlap; die Figur ueberlappt noch.
		if (OtherActor != Stealth->GetOwner() && Volume->IsOverlappingActor(Stealth->GetOwner()))
		{
			return;
		}
		Stealth->LeaveHideout(this);
	}
}

AActor* AMTAStealthHideout::FindVehicleInside() const
{
	TArray<AActor*> Overlapping;
	GetOverlappingActors(Overlapping, AWheeledVehiclePawn::StaticClass());
	return Overlapping.Num() > 0 ? Overlapping[0] : nullptr;
}

FMTAInteractionInfo AMTAStealthHideout::GetInteractionInfo_Implementation(AActor* Interactor) const
{
	FMTAInteractionInfo Info;
	Info.Kind = EMTAInteractionKind::Hideout;
	Info.HoldDuration = ServiceHoldDuration;
	if (bOffersOutfitChange && OutfitOptions.Num() > 0)
	{
		Info.Prompt = LOCTEXT("Outfit", "Kleidung wechseln");
	}
	else if (bOffersPlateSwap)
	{
		Info.Prompt = LOCTEXT("Plate", "Kennzeichen tauschen");
		Info.SubText = FText::Format(LOCTEXT("PlateCost", "Kosten: {0} $"), FText::AsNumber(PlateSwapCost));
		if (!FindVehicleInside())
		{
			Info.bEnabled = false;
			Info.DisabledReason = LOCTEXT("NoVehicle", "Kein Fahrzeug in der Garage");
		}
	}
	return Info;
}

bool AMTAStealthHideout::CanInteract_Implementation(AActor* Interactor) const
{
	return (bOffersOutfitChange && OutfitOptions.Num() > 0) || (bOffersPlateSwap && FindVehicleInside() != nullptr);
}

void AMTAStealthHideout::Interact_Implementation(AActor* Interactor)
{
	UMTAStealthComponent* Stealth = UMTAStealthComponent::FindStealthComponent(Interactor);
	if (!Stealth)
	{
		return;
	}
	if (bOffersOutfitChange && OutfitOptions.Num() > 0)
	{
		// Naechstes Outfit der Liste (zyklisch).
		const int32 Current = OutfitOptions.IndexOfByKey(Stealth->GetOutfitId());
		const FName Next = OutfitOptions[(Current + 1) % OutfitOptions.Num()];
		Stealth->ChangeOutfit(Next);
		OnServiceUsed.Broadcast(Interactor, TEXT("outfit"), Next);
		return;
	}
	if (bOffersPlateSwap)
	{
		if (AActor* Vehicle = FindVehicleInside())
		{
			const FName NewPlate = Stealth->SwapVehiclePlate(Vehicle);
			OnServiceUsed.Broadcast(Interactor, TEXT("plate"), NewPlate);
		}
	}
}

#undef LOCTEXT_NAMESPACE
