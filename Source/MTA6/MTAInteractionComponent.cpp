#include "MTAInteractionComponent.h"

#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "WheeledVehiclePawn.h"
#include "MTAVehicleEntryComponent.h"

#define LOCTEXT_NAMESPACE "MTAInteraction"

UMTAInteractionComponent::UMTAInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UMTAInteractionComponent::BeginPlay()
{
	Super::BeginPlay();
	ScanTimer = FMath::FRandRange(0.f, ScanInterval);
}

UMTAVehicleEntryComponent* UMTAInteractionComponent::GetEntry() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UMTAVehicleEntryComponent>() : nullptr;
}

void UMTAInteractionComponent::SetInteractionEnabled(bool bEnabled)
{
	bEnabledScan = bEnabled;
	if (!bEnabled)
	{
		bHolding = false;
		HoldElapsed = 0.f;
		SetFocus(nullptr, FMTAInteractionInfo());
	}
}

void UMTAInteractionComponent::GetView(FVector& OutLocation, FVector& OutDirection) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (Pawn && Pawn->GetController())
	{
		FRotator ViewRot;
		Pawn->GetController()->GetPlayerViewPoint(OutLocation, ViewRot);
		OutDirection = ViewRot.Vector();
		return;
	}
	const AActor* Owner = GetOwner();
	OutLocation = Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
	OutDirection = Owner ? Owner->GetActorForwardVector() : FVector::ForwardVector;
}

bool UMTAInteractionComponent::IsCandidate(AActor* Actor) const
{
	if (!Actor || Actor == GetOwner() || Actor->IsActorBeingDestroyed())
	{
		return false;
	}
	if (Actor->Implements<UMTAInteractable>())
	{
		return true;
	}
	return Actor->IsA<AWheeledVehiclePawn>() && GetEntry() != nullptr;
}

bool UMTAInteractionComponent::QueryInfo(AActor* Actor, FMTAInteractionInfo& OutInfo) const
{
	if (AWheeledVehiclePawn* Vehicle = Cast<AWheeledVehiclePawn>(Actor))
	{
		if (UMTAVehicleEntryComponent* Entry = GetEntry())
		{
			OutInfo = Entry->GetVehicleInteractionInfo(Vehicle);
			return !OutInfo.Prompt.IsEmpty();
		}
		return false;
	}
	if (Actor && Actor->Implements<UMTAInteractable>())
	{
		AActor* Owner = GetOwner();
		OutInfo = IMTAInteractable::Execute_GetInteractionInfo(Actor, Owner);
		if (OutInfo.bEnabled && !IMTAInteractable::Execute_CanInteract(Actor, Owner))
		{
			OutInfo.bEnabled = false;
		}
		return !OutInfo.Prompt.IsEmpty();
	}
	return false;
}

void UMTAInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bEnabledScan)
	{
		return;
	}

	// Im Fahrzeug oder waehrend des Einstiegs keine Suche.
	if (const UMTAVehicleEntryComponent* Entry = GetEntry())
	{
		if (Entry->Phase != EMTAVehicleEntryPhase::None)
		{
			if (FocusedActor.IsValid())
			{
				SetFocus(nullptr, FMTAInteractionInfo());
			}
			return;
		}
	}

	ScanTimer -= DeltaTime;
	if (ScanTimer <= 0.f)
	{
		ScanTimer = ScanInterval;
		Scan();
	}

	if (bHolding)
	{
		AActor* Target = FocusedActor.Get();
		if (!Target || !FocusInfo.bEnabled)
		{
			bHolding = false;
			HoldElapsed = 0.f;
			OnHoldProgress.Broadcast(Target, 0.f);
			return;
		}
		HoldElapsed += DeltaTime;
		const float Alpha = GetHoldAlpha();
		OnHoldProgress.Broadcast(Target, Alpha);
		if (Target->Implements<UMTAInteractable>())
		{
			IMTAInteractable::Execute_OnInteractionHoldProgress(Target, GetOwner(), Alpha);
		}
		if (Alpha >= 1.f)
		{
			bHolding = false;
			HoldElapsed = 0.f;
			Execute(Target);
		}
	}
}

void UMTAInteractionComponent::Scan()
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}

	FVector ViewLoc, ViewDir;
	GetView(ViewLoc, ViewDir);
	const FVector OwnerLoc = Owner->GetActorLocation();

	AActor* Best = nullptr;
	FMTAInteractionInfo BestInfo;
	float BestScore = -BIG_NUMBER;

	auto Consider = [&](AActor* Actor, float Bonus)
	{
		if (!IsCandidate(Actor))
		{
			return;
		}
		// Abstand zur Figur ueber die naechste Stelle der Bounds (Fahrzeuge sind gross).
		FVector Origin, Extent;
		Actor->GetActorBounds(true, Origin, Extent);
		const FBox Box(Origin - Extent, Origin + Extent);
		const float Dist = FMath::Sqrt(Box.ComputeSquaredDistanceToPoint(OwnerLoc));
		if (Dist > MaxInteractDistance)
		{
			return;
		}
		FMTAInteractionInfo Info;
		if (!QueryInfo(Actor, Info))
		{
			return;
		}
		const float Facing = FVector::DotProduct(ViewDir, (Origin - ViewLoc).GetSafeNormal());
		if (Bonus <= 0.f && Facing < MinFacingDot)
		{
			return;
		}
		const float Score = Bonus + Facing * 2.f - Dist / MaxInteractDistance + Info.Priority * 0.5f + (Info.bEnabled ? 0.5f : 0.f);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Actor;
			BestInfo = Info;
		}
	};

	// 1) Blick-Trace (Fadenkreuz): Distanz Kamera->Figur plus Reichweite.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MTAInteractionTrace), false, Owner);
	const float CamDist = FVector::Dist(ViewLoc, OwnerLoc);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, ViewLoc, ViewLoc + ViewDir * (CamDist + TraceDistance), TraceChannel, Params))
	{
		Consider(Hit.GetActor(), 3.f);
	}

	// 2) Naehe-Overlap (Objekte am Rand des Blickfelds, kleine Beweisstuecke).
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjParams.AddObjectTypesToQuery(ECC_PhysicsBody);
	ObjParams.AddObjectTypesToQuery(ECC_Vehicle);
	ObjParams.AddObjectTypesToQuery(ECC_Pawn);
	World->OverlapMultiByObjectType(Overlaps, OwnerLoc, FQuat::Identity, ObjParams, FCollisionShape::MakeSphere(ProximityRadius), Params);
	TSet<AActor*> Seen;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (Actor && !Seen.Contains(Actor))
		{
			Seen.Add(Actor);
			Consider(Actor, 0.f);
		}
	}

	if (Best != FocusedActor.Get())
	{
		SetFocus(Best, BestInfo);
	}
	else if (Best)
	{
		// Text kann sich aendern (Tuer offen/zu), ohne Fokuswechsel.
		const bool bChanged = !BestInfo.Prompt.EqualTo(FocusInfo.Prompt) || BestInfo.bEnabled != FocusInfo.bEnabled;
		FocusInfo = BestInfo;
		if (bChanged)
		{
			OnFocusChanged.Broadcast(Best, FocusInfo);
		}
	}
}

void UMTAInteractionComponent::SetFocus(AActor* NewFocus, const FMTAInteractionInfo& NewInfo)
{
	AActor* Old = FocusedActor.Get();
	if (Old && Old->Implements<UMTAInteractable>())
	{
		IMTAInteractable::Execute_OnInteractionFocus(Old, GetOwner(), false);
	}
	FocusedActor = NewFocus;
	FocusInfo = NewInfo;
	bHolding = false;
	HoldElapsed = 0.f;
	if (NewFocus && NewFocus->Implements<UMTAInteractable>())
	{
		IMTAInteractable::Execute_OnInteractionFocus(NewFocus, GetOwner(), true);
	}
	OnFocusChanged.Broadcast(NewFocus, FocusInfo);
}

void UMTAInteractionComponent::BeginInteract()
{
	AActor* Target = FocusedActor.Get();
	if (!Target || !FocusInfo.bEnabled)
	{
		return;
	}
	if (FocusInfo.HoldDuration <= 0.f)
	{
		Execute(Target);
		return;
	}
	bHolding = true;
	HoldElapsed = 0.f;
}

void UMTAInteractionComponent::EndInteract()
{
	if (bHolding)
	{
		bHolding = false;
		HoldElapsed = 0.f;
		AActor* Target = FocusedActor.Get();
		OnHoldProgress.Broadcast(Target, 0.f);
		if (Target && Target->Implements<UMTAInteractable>())
		{
			IMTAInteractable::Execute_OnInteractionHoldProgress(Target, GetOwner(), 0.f);
		}
	}
}

float UMTAInteractionComponent::GetHoldAlpha() const
{
	return FocusInfo.HoldDuration > 0.f ? FMath::Clamp(HoldElapsed / FocusInfo.HoldDuration, 0.f, 1.f) : 0.f;
}

void UMTAInteractionComponent::Execute(AActor* Target)
{
	if (!Target)
	{
		return;
	}
	if (AWheeledVehiclePawn* Vehicle = Cast<AWheeledVehiclePawn>(Target))
	{
		if (UMTAVehicleEntryComponent* Entry = GetEntry())
		{
			Entry->RequestEnter(Vehicle);
		}
	}
	else if (Target->Implements<UMTAInteractable>())
	{
		IMTAInteractable::Execute_Interact(Target, GetOwner());
	}
	OnInteracted.Broadcast(Target);
	ScanTimer = 0.f; // sofort neu bewerten (Tuer offen -> "schliessen")
}

FText UMTAInteractionComponent::GetPromptText() const
{
	if (!FocusedActor.IsValid() || FocusInfo.Prompt.IsEmpty())
	{
		return FText::GetEmpty();
	}
	if (!FocusInfo.bEnabled)
	{
		return FocusInfo.DisabledReason.IsEmpty()
			? FocusInfo.Prompt
			: FText::Format(LOCTEXT("Disabled", "{0} ({1})"), FocusInfo.Prompt, FocusInfo.DisabledReason);
	}
	if (FocusInfo.HoldDuration > 0.f)
	{
		return FText::Format(LOCTEXT("Hold", "[{0} halten] {1}"), KeyLabel, FocusInfo.Prompt);
	}
	return FText::Format(LOCTEXT("Press", "[{0}] {1}"), KeyLabel, FocusInfo.Prompt);
}

#undef LOCTEXT_NAMESPACE
