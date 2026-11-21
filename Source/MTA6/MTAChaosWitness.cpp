#include "MTAChaosWitness.h"

#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "MTAChaosSubsystem.h"
#include "MTAStealthComponent.h"

UMTAWitnessComponent::UMTAWitnessComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UMTAChaosSubsystem* UMTAWitnessComponent::GetChaos() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UMTAChaosSubsystem>() : nullptr;
}

void UMTAWitnessComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UMTAChaosSubsystem* Chaos = GetChaos())
	{
		Chaos->RegisterWitness(this);
	}
	if (RecognitionInterval > 0.f)
	{
		// Zufaelliger Start verteilt die Traces vieler Zeugen ueber die Frames.
		GetWorld()->GetTimerManager().SetTimer(RecognitionTimer, this, &UMTAWitnessComponent::RecognitionTick,
			RecognitionInterval, true, FMath::FRandRange(0.f, RecognitionInterval));
	}
	LastRecognitionTime = GetWorld()->GetTimeSeconds();
}

void UMTAWitnessComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RecognitionTimer);
	}
	if (UMTAChaosSubsystem* Chaos = GetChaos())
	{
		// Tote/entfernte Zeugen melden nichts mehr.
		Chaos->UnregisterWitness(this);
	}
	Super::EndPlay(EndPlayReason);
}

void UMTAWitnessComponent::GetEyes(FVector& OutLocation, FRotator& OutRotation) const
{
	if (const AActor* Owner = GetOwner())
	{
		Owner->GetActorEyesViewPoint(OutLocation, OutRotation);
	}
	else
	{
		OutLocation = FVector::ZeroVector;
		OutRotation = FRotator::ZeroRotator;
	}
}

bool UMTAWitnessComponent::HasLineOfSightTo(const FVector& Target, const AActor* IgnoreActor) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FVector Eyes;
	FRotator EyeRot;
	GetEyes(Eyes, EyeRot);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MTAWitnessSight), false, GetOwner());
	if (IgnoreActor)
	{
		Params.AddIgnoredActor(IgnoreActor);
		// Fahrzeug/Insasse gemeinsam ignorieren.
		if (const AActor* Parent = IgnoreActor->GetAttachParentActor())
		{
			Params.AddIgnoredActor(Parent);
		}
	}
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, Eyes, Target, ECC_Visibility, Params);
}

bool UMTAWitnessComponent::ConsiderCrime(const FMTACrimeEvent& Crime, const FMTACrimeRule& Rule, bool bIsVictim, float& OutReportDelay)
{
	OutReportDelay = -1.f;
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}
	// Taeter bezeugen ihre eigene Tat nicht.
	if (UMTAChaosSubsystem::IsSubjectOfCrime(Crime, Owner))
	{
		return false;
	}

	FVector Eyes;
	FRotator EyeRot;
	GetEyes(Eyes, EyeRot);
	const FVector Target = Crime.Location + FVector(0.f, 0.f, 60.f);
	const float Distance = FVector::Dist(Eyes, Target);

	float Confidence = 0.f;
	bool bSawIt = false;

	if (bIsVictim)
	{
		Confidence = 1.f;
		bSawIt = true;
	}
	else
	{
		const float Range = FMath::Min(SightRange, Rule.NoticeRadius);
		if (Distance <= Range)
		{
			const FVector ToTarget = (Target - Eyes).GetSafeNormal();
			const float CosAngle = FVector::DotProduct(EyeRot.Vector(), ToTarget);
			const bool bInCone = CosAngle >= FMath::Cos(FMath::DegreesToRadians(ViewHalfAngle));
			if (bInCone && HasLineOfSightTo(Target, Crime.Perpetrator.Get()))
			{
				const float Visibility = UMTAStealthComponent::GetSubjectVisibility(Crime.Perpetrator.Get());
				Confidence = (1.f - Distance / FMath::Max(Range, 1.f)) * 0.7f + 0.3f;
				Confidence *= Visibility;
				bSawIt = Confidence >= MinConfidence;
			}
		}
		// Hoeren: laute Taten auch ohne Sichtlinie (halber Radius * Lautstaerke).
		if (!bSawIt && Distance <= Rule.NoticeRadius * 0.5f * Rule.Loudness * 2.f && Rule.Loudness > 0.f)
		{
			Confidence = 0.3f * Rule.Loudness;
		}
	}

	if (Confidence < MinConfidence * 0.5f)
	{
		return false;
	}

	// Gedaechtnis anlegen.
	if (Memories.Num() >= FMath::Max(1, MaxMemories))
	{
		Memories.RemoveAt(0);
	}
	FMTAWitnessMemory& Memory = Memories.AddDefaulted_GetRef();
	Memory.CrimeId = Crime.CrimeId;
	Memory.Type = Crime.Type;
	Memory.Location = Crime.Location;
	Memory.Perpetrator = Crime.Perpetrator;
	Memory.Confidence = Confidence;
	Memory.TimeSeen = GetWorld()->GetTimeSeconds();
	if (bSawIt && Confidence > 0.4f)
	{
		Memory.RememberedOutfit = Crime.OutfitId;
	}
	if (bSawIt && !Crime.PlateId.IsNone() && Distance <= PlateReadRange && Confidence > 0.5f)
	{
		Memory.RememberedPlate = Crime.PlateId;
	}

	OnSawCrime.Broadcast(Crime, Confidence);

	// Meldeentscheidung.
	if (bCanReport && !bIntimidated)
	{
		const float Chance = FMath::Clamp(Courage + (bIsVictim ? 0.3f : 0.f) + (bIsPolice ? 1.f : 0.f), 0.f, 1.f);
		if (FMath::FRand() <= Chance)
		{
			OutReportDelay = bIsPolice ? 0.5f
				: FMath::FRandRange(Rule.ReportDelayMin, FMath::Max(Rule.ReportDelayMin, Rule.ReportDelayMax)) * ReportDelayScale;
		}
	}
	return true;
}

bool UMTAWitnessComponent::CanReportNow() const
{
	const AActor* Owner = GetOwner();
	return bCanReport && !bIntimidated && Owner && !Owner->IsActorBeingDestroyed();
}

FMTAWitnessMemory* UMTAWitnessComponent::FindMemory(int32 CrimeId)
{
	return Memories.FindByPredicate([CrimeId](const FMTAWitnessMemory& M) { return M.CrimeId == CrimeId; });
}

const FMTAWitnessMemory* UMTAWitnessComponent::FindMemory(int32 CrimeId) const
{
	return Memories.FindByPredicate([CrimeId](const FMTAWitnessMemory& M) { return M.CrimeId == CrimeId; });
}

void UMTAWitnessComponent::MarkReported(int32 CrimeId)
{
	if (FMTAWitnessMemory* Memory = FindMemory(CrimeId))
	{
		Memory->bReported = true;
	}
}

bool UMTAWitnessComponent::RemembersPlate(int32 CrimeId) const
{
	const FMTAWitnessMemory* Memory = FindMemory(CrimeId);
	return Memory && !Memory->RememberedPlate.IsNone();
}

bool UMTAWitnessComponent::RemembersOutfit(int32 CrimeId) const
{
	const FMTAWitnessMemory* Memory = FindMemory(CrimeId);
	return Memory && !Memory->RememberedOutfit.IsNone();
}

void UMTAWitnessComponent::Intimidate()
{
	bIntimidated = true;
	if (UMTAChaosSubsystem* Chaos = GetChaos())
	{
		Chaos->CancelPendingReports(this);
	}
}

void UMTAWitnessComponent::RecognitionTick()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();
	const float Dt = Now - LastRecognitionTime;
	LastRecognitionTime = Now;

	// Erinnerungen verblassen.
	const float Decay = MemoryDuration > 0.f ? Dt / MemoryDuration : 1.f;
	for (int32 i = Memories.Num() - 1; i >= 0; --i)
	{
		Memories[i].Confidence -= Decay;
		if (Memories[i].Confidence <= 0.f || !Memories[i].Perpetrator.IsValid())
		{
			Memories.RemoveAt(i);
		}
	}
	if (Memories.Num() == 0 || !CanReportNow())
	{
		return;
	}

	UMTAChaosSubsystem* Chaos = GetChaos();
	if (!Chaos)
	{
		return;
	}

	FVector Eyes;
	FRotator EyeRot;
	GetEyes(Eyes, EyeRot);
	const float CosHalf = FMath::Cos(FMath::DegreesToRadians(ViewHalfAngle));

	for (AActor* Subject : Chaos->GetPlayerSubjects())
	{
		const FVector Target = Subject->GetActorLocation();
		const float Distance = FVector::Dist(Eyes, Target);
		if (Distance > SightRange * 0.6f)
		{
			continue;
		}
		if (FVector::DotProduct(EyeRot.Vector(), (Target - Eyes).GetSafeNormal()) < CosHalf)
		{
			continue;
		}
		for (const FMTAWitnessMemory& Memory : Memories)
		{
			const AActor* Perp = Memory.Perpetrator.Get();
			if (Perp != Subject && (!Perp || Perp->GetAttachParentActor() != Subject))
			{
				continue;
			}
			// Kleidungswechsel erschwert Wiedererkennung.
			float Chance = Memory.Confidence * UMTAStealthComponent::GetSubjectVisibility(Subject);
			const UMTAStealthComponent* Stealth = UMTAStealthComponent::FindStealthComponent(Subject);
			if (Stealth && !Memory.RememberedOutfit.IsNone() && Stealth->GetOutfitId() != Memory.RememberedOutfit)
			{
				Chance *= DisguisedRecognitionFactor;
			}
			if (FMath::FRand() > Chance || !HasLineOfSightTo(Target, Subject))
			{
				continue;
			}
			OnRecognizedPerpetrator.Broadcast(Subject, Memory.CrimeId);
			Chaos->NotifySighting(Subject, Target, EMTASightingSource::Witness);
			return; // eine Sichtung pro Intervall genuegt
		}
	}
}
