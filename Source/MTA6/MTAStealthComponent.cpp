#include "MTAStealthComponent.h"

#include "Components/LocalLightComponent.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "UObject/UObjectIterator.h"
#include "MTACharacter.h"
#include "MTAChaosSubsystem.h"
#include "MTAHeatSubsystem.h"
#include "MTAStealthHideout.h"
#include "MTATimeOfDay.h"

UMTAStealthComponent::UMTAStealthComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
}

// ---------------------------------------------------------------------------------------
// Statische Abfragen
// ---------------------------------------------------------------------------------------

UMTAStealthComponent* UMTAStealthComponent::FindStealthComponent(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (UMTAStealthComponent* Own = Actor->FindComponentByClass<UMTAStealthComponent>())
	{
		return Own;
	}
	TArray<AActor*> Attached;
	Actor->GetAttachedActors(Attached);
	for (const AActor* Child : Attached)
	{
		if (UMTAStealthComponent* ChildComp = Child ? Child->FindComponentByClass<UMTAStealthComponent>() : nullptr)
		{
			return ChildComp;
		}
	}
	return nullptr;
}

float UMTAStealthComponent::GetSubjectVisibility(const AActor* Subject)
{
	const UMTAStealthComponent* Stealth = FindStealthComponent(Subject);
	if (!Stealth)
	{
		return 1.f;
	}
	if (Stealth->GetOwner() != Subject)
	{
		// Subjekt ist das Fahrzeug: Karosserie bleibt sichtbar, Nacht daempft leicht.
		float Vehicle = Stealth->InVehicleVisibility * FMath::Lerp(1.f, 0.75f, Stealth->GetNightAlpha());
		if (Stealth->CurrentHideout)
		{
			Vehicle *= Stealth->CurrentHideout->bBlocksSight ? 0.f : 0.5f;
		}
		return Vehicle;
	}
	return Stealth->Visibility;
}

float UMTAStealthComponent::GetCaptureModifier(const AActor* Subject, bool bAsVehicle)
{
	const UMTAStealthComponent* Stealth = FindStealthComponent(Subject);
	if (!Stealth)
	{
		return 1.f;
	}
	float Modifier = bAsVehicle ? Stealth->PlateJammerFactor : (Stealth->bFaceCovered ? Stealth->FaceCoveredCaptureFactor : 1.f);
	if (Stealth->CurrentHideout && Stealth->CurrentHideout->bBlocksSight)
	{
		Modifier = 0.f;
	}
	return Modifier;
}

// ---------------------------------------------------------------------------------------
// Lebenszyklus
// ---------------------------------------------------------------------------------------

UMTAChaosSubsystem* UMTAStealthComponent::GetChaos() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UMTAChaosSubsystem>() : nullptr;
}

void UMTAStealthComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AMTATimeOfDay> It(World); It; ++It)
		{
			TimeOfDayActor = *It;
			break;
		}
	}
	if (UMTAChaosSubsystem* Chaos = GetChaos())
	{
		Chaos->OnSubjectSighted.AddDynamic(this, &UMTAStealthComponent::HandleSighting);
		Chaos->NotifyAppearanceChanged(GetOwner(), OutfitId);
	}
	RefreshLightCache();
	SampleLight();
}

void UMTAStealthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMTAChaosSubsystem* Chaos = GetChaos())
	{
		Chaos->OnSubjectSighted.RemoveDynamic(this, &UMTAStealthComponent::HandleSighting);
	}
	Super::EndPlay(EndPlayReason);
}

float UMTAStealthComponent::GetNightAlpha() const
{
	if (const AMTATimeOfDay* Tod = TimeOfDayActor.Get())
	{
		return FMath::Clamp(Tod->GetNightAlpha(), 0.f, 1.f);
	}
	// Platzhalter: Nacht 21-5 Uhr, Daemmerung je 2 h.
	const float H = FMath::Fmod(FMath::Max(FallbackTimeOfDayHours, 0.f), 24.f);
	if (H >= 7.f && H <= 19.f) { return 0.f; }
	if (H >= 21.f || H <= 5.f) { return 1.f; }
	return H < 12.f ? (7.f - H) / 2.f : (H - 19.f) / 2.f;
}

void UMTAStealthComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	LightCacheTimer -= DeltaTime;
	if (LightCacheTimer <= 0.f)
	{
		LightCacheTimer = 5.f;
		RefreshLightCache();
	}
	LightSampleTimer -= DeltaTime;
	if (LightSampleTimer <= 0.f)
	{
		LightSampleTimer = LightSampleInterval;
		SampleLight();
	}

	// Bewegung -> Grundgeraeusch, Ereignisse klingen ab.
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const AMTACharacter* MTAChar = Cast<AMTACharacter>(GetOwner());
	const float Speed = Character ? Character->GetVelocity().Size2D() : 0.f;
	const bool bCrouched = Character && Character->bIsCrouched;
	const bool bSprinting = MTAChar && MTAChar->bIsSprinting;
	MovementNoise = FMath::Clamp(Speed / 1200.f, 0.f, 0.6f) * (bCrouched ? 0.4f : 1.f);
	EventNoise = FMath::Max(0.f, EventNoise - NoiseDecay * DeltaTime);
	NoiseLevel = FMath::Clamp(FMath::Max(MovementNoise, EventNoise), 0.f, 1.f);

	// Sichtbarkeit.
	float Vis = LightLevel;
	if (bCrouched) { Vis *= CrouchVisibilityFactor; }
	if (bSprinting) { Vis += SprintVisibilityBonus; }
	Vis += FMath::Clamp(Speed / 2000.f, 0.f, 0.15f); // Bewegung faellt auf
	if (CurrentHideout)
	{
		Vis *= CurrentHideout->bBlocksSight ? 0.f : 0.5f;
	}
	Visibility = FMath::Clamp(Vis, 0.f, 1.f);

	UpdateEscapeState(DeltaTime);
}

// ---------------------------------------------------------------------------------------
// Licht
// ---------------------------------------------------------------------------------------

void UMTAStealthComponent::RefreshLightCache()
{
	CachedLights.Reset();
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}
	const FVector Origin = Owner->GetActorLocation();
	const float MaxDistSq = FMath::Square(LightSearchRadius);
	// Grob alle 5 s; Lampen der Stadt sind (Point/Spot) LocalLightComponents.
	for (TObjectIterator<ULocalLightComponent> It; It; ++It)
	{
		ULocalLightComponent* Light = *It;
		if (!Light || Light->GetWorld() != World || !Light->IsRegistered())
		{
			continue;
		}
		if (FVector::DistSquared(Light->GetComponentLocation(), Origin) <= MaxDistSq)
		{
			CachedLights.Add(Light);
		}
	}
}

void UMTAStealthComponent::SampleLight()
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}
	const float Ambient = FMath::Lerp(DayAmbient, NightAmbient, GetNightAlpha());
	const FVector Pos = Owner->GetActorLocation();

	struct FCandidate { float Contribution; FVector Location; };
	TArray<FCandidate> Candidates;
	for (const TWeakObjectPtr<ULocalLightComponent>& Weak : CachedLights)
	{
		const ULocalLightComponent* Light = Weak.Get();
		if (!Light || !Light->IsVisible() || Light->Intensity <= 0.f)
		{
			continue;
		}
		const float Radius = FMath::Max(Light->AttenuationRadius, 1.f);
		const float Dist = FVector::Dist(Light->GetComponentLocation(), Pos);
		if (Dist >= Radius)
		{
			continue;
		}
		const float Falloff = FMath::Square(1.f - Dist / Radius);
		const float Contribution = Falloff * FMath::Clamp(Light->Intensity / ReferenceLightIntensity, 0.f, 1.5f);
		Candidates.Add({ Contribution, Light->GetComponentLocation() });
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Contribution > B.Contribution; });

	float Local = 0.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MTAStealthLight), false, Owner);
	if (const AActor* Parent = Owner->GetAttachParentActor())
	{
		Params.AddIgnoredActor(Parent);
	}
	for (int32 i = 0; i < Candidates.Num() && i < MaxLightTraces; ++i)
	{
		FHitResult Hit;
		const bool bBlocked = World->LineTraceSingleByChannel(Hit, Pos + FVector(0.f, 0.f, 50.f), Candidates[i].Location, ECC_Visibility, Params);
		if (!bBlocked)
		{
			Local += Candidates[i].Contribution;
		}
	}
	LightLevel = FMath::Clamp(Ambient + Local, 0.f, 1.f);
}

// ---------------------------------------------------------------------------------------
// Geraeusch, Verkleidung
// ---------------------------------------------------------------------------------------

void UMTAStealthComponent::ReportNoise(float Loudness)
{
	EventNoise = FMath::Max(EventNoise, FMath::Clamp(Loudness, 0.f, 1.f));
	if (Loudness >= NoiseEventThreshold && GetOwner())
	{
		OnNoise.Broadcast(GetOwner()->GetActorLocation(), Loudness);
	}
}

void UMTAStealthComponent::ChangeOutfit(FName NewOutfitId)
{
	if (NewOutfitId == OutfitId || NewOutfitId.IsNone())
	{
		return;
	}
	const FName Old = OutfitId;
	OutfitId = NewOutfitId;

	if (UMTAChaosSubsystem* Chaos = GetChaos())
	{
		const float Now = GetWorld()->GetTimeSeconds();
		// Nur ein bekanntes Outfit abzulegen senkt das Profil; Abklingzeit gegen Kleiderschrank-Spam.
		if (Chaos->GetKnownOutfit() == Old && Now - LastOutfitChangeTime > 120.f)
		{
			Chaos->AddAegisProfile(-OutfitChangeAegisReduction);
			LastOutfitChangeTime = Now;
		}
		Chaos->NotifyAppearanceChanged(GetOwner(), OutfitId);
	}
	OnOutfitChanged.Broadcast(Old, OutfitId);
}

FName UMTAStealthComponent::SwapVehiclePlate(AActor* Vehicle, FName NewPlate)
{
	UMTAChaosSubsystem* Chaos = GetChaos();
	if (!Chaos || !Vehicle)
	{
		return NAME_None;
	}
	const bool bWasHot = Chaos->GetOrCreateVehicleRecord(Vehicle).bHot;
	const FName Plate = Chaos->SwapPlate(Vehicle, NewPlate);
	if (bWasHot)
	{
		Chaos->AddAegisProfile(-PlateSwapAegisReduction);
	}
	OnPlateChanged.Broadcast(Vehicle, Plate);
	return Plate;
}

// ---------------------------------------------------------------------------------------
// Verstecke und Flucht
// ---------------------------------------------------------------------------------------

void UMTAStealthComponent::EnterHideout(AMTAStealthHideout* Hideout)
{
	if (Hideout && CurrentHideout != Hideout)
	{
		CurrentHideout = Hideout;
		OnEnteredHideout.Broadcast(Hideout);
	}
}

void UMTAStealthComponent::LeaveHideout(AMTAStealthHideout* Hideout)
{
	if (Hideout && CurrentHideout == Hideout)
	{
		CurrentHideout = nullptr;
		OnLeftHideout.Broadcast(Hideout);
	}
}

void UMTAStealthComponent::HandleSighting(AActor* Subject, FVector Location, EMTASightingSource Source)
{
	if (!Subject)
	{
		return;
	}
	if (Subject == GetOwner() || Subject == GetOwner()->GetAttachParentActor())
	{
		LastSightedTime = GetWorld()->GetTimeSeconds();
	}
}

FName UMTAStealthComponent::GetCurrentDistrict() const
{
	const UMTAChaosSubsystem* Chaos = GetChaos();
	return Chaos && GetOwner() ? Chaos->ResolveDistrict(GetOwner()->GetActorLocation()) : NAME_None;
}

void UMTAStealthComponent::UpdateEscapeState(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	UMTAHeatSubsystem* Heat = World->GetSubsystem<UMTAHeatSubsystem>();
	const FName District = GetCurrentDistrict();
	const float CurrentHeat = Heat ? Heat->GetHeat(District) : 0.f;
	const float Now = World->GetTimeSeconds();
	const bool bRecentlySeen = Now - LastSightedTime < MinUnseenTimeToLieLow;

	EMTAEscapeState NewState = EMTAEscapeState::Clear;
	if (CurrentHideout && !bRecentlySeen)
	{
		NewState = EMTAEscapeState::LyingLow;
		// Untertauchen: Aegis-Profil und Fahndung sinken.
		if (UMTAChaosSubsystem* Chaos = GetChaos())
		{
			Chaos->AddAegisProfile(-CurrentHideout->AegisDecayPerSecond * DeltaTime);
		}
		if (Heat && CurrentHeat > 0.f)
		{
			Heat->AddHeat(District, -FMath::Min(CurrentHeat, CurrentHideout->HeatDecayPerSecond * DeltaTime));
		}
	}
	else if (bRecentlySeen && CurrentHeat > 0.f)
	{
		NewState = EMTAEscapeState::Spotted;
	}
	else if (CurrentHeat > 0.f)
	{
		NewState = EMTAEscapeState::Wanted;
	}

	if (NewState != EscapeState)
	{
		EscapeState = NewState;
		OnEscapeStateChanged.Broadcast(EscapeState);
	}
}
