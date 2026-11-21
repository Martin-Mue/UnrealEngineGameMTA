#include "MTAChaosAegisCamera.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "TimerManager.h"
#include "MTAChaosSubsystem.h"
#include "MTAStealthComponent.h"

UMTAAegisCameraComponent::UMTAAegisCameraComponent()
{
	// Tick nur fuer den Schwenk; Scans laufen ueber einen Timer.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

FMTAAegisCaptureProfile UMTAAegisCameraComponent::GetTierDefaults(EMTAAegisTier InTier)
{
	FMTAAegisCaptureProfile P;
	switch (InTier)
	{
	case EMTAAegisTier::Street:
		P.BaseChance = 0.45f; P.SpeedLimitKmh = 0.f; P.bFaceRecognition = false;
		break;
	case EMTAAegisTier::PrivateGate:
		// Greenmoor: Kennzeichenleser an Toren, sehr zuverlaessig, langsamer Verkehr.
		P.BaseChance = 0.8f; P.SpeedPenaltyPerKmh = 0.002f; P.SpeedLimitKmh = 40.f;
		break;
	case EMTAAegisTier::Advertising:
		// Sunstrip: Gesichtserkennung fuer Werbung, Kennzeichen eher nebenbei.
		P.BaseChance = 0.35f; P.bFaceRecognition = true;
		break;
	case EMTAAegisTier::Checkpoint:
		P.BaseChance = 0.95f; P.SpeedPenaltyPerKmh = 0.001f; P.bFaceRecognition = true; P.SpeedLimitKmh = 80.f;
		P.ProfileMultiplierAtZero = 0.8f; P.ProfileMultiplierAtMax = 1.2f;
		break;
	case EMTAAegisTier::Derelict:
		// Rustline/Lantern Hill: halb blind, oft gehackt.
		P.BaseChance = 0.15f; P.SpeedPenaltyPerKmh = 0.008f; P.bReportsHotPlates = false;
		break;
	}
	return P;
}

UMTAChaosSubsystem* UMTAAegisCameraComponent::GetChaos() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UMTAChaosSubsystem>() : nullptr;
}

void UMTAAegisCameraComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bUseTierDefaults)
	{
		CaptureProfile = GetTierDefaults(Tier);
	}
	BaseRelativeRotation = GetRelativeRotation();
	SetComponentTickEnabled(SweepAmplitude > 0.f && SweepPeriod > 0.f);

	if (UMTAChaosSubsystem* Chaos = GetChaos())
	{
		Chaos->RegisterCamera(this);
	}
	GetWorld()->GetTimerManager().SetTimer(ScanTimer, this, &UMTAAegisCameraComponent::Scan,
		ScanInterval, true, FMath::FRandRange(0.f, ScanInterval));
}

void UMTAAegisCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ScanTimer);
		World->GetTimerManager().ClearTimer(HackTimer);
	}
	if (UMTAChaosSubsystem* Chaos = GetChaos())
	{
		Chaos->UnregisterCamera(this);
	}
	Super::EndPlay(EndPlayReason);
}

void UMTAAegisCameraComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bOnline)
	{
		return;
	}
	SweepTime += DeltaTime;
	const float Yaw = SweepAmplitude * FMath::Sin(SweepTime * 2.f * PI / SweepPeriod);
	SetRelativeRotation(BaseRelativeRotation + FRotator(0.f, Yaw, 0.f));
}

bool UMTAAegisCameraComponent::IsInCone(const FVector& Point) const
{
	const FVector Origin = GetComponentLocation();
	const FVector Delta = Point - Origin;
	const float DistSq = Delta.SizeSquared();
	if (DistSq > FMath::Square(Range) || DistSq < 1.f)
	{
		return false;
	}
	const float CosAngle = FVector::DotProduct(GetForwardVector(), Delta.GetSafeNormal());
	return CosAngle >= FMath::Cos(FMath::DegreesToRadians(ConeHalfAngle));
}

float UMTAAegisCameraComponent::ComputeCaptureChance(AActor* Subject, bool bAsVehicle) const
{
	if (!Subject || !bOnline)
	{
		return 0.f;
	}
	const UMTAChaosSubsystem* Chaos = GetChaos();
	const float Profile01 = Chaos ? Chaos->GetAegisProfile() / 100.f : 0.5f;
	const FMTAAegisCaptureProfile& P = CaptureProfile;

	float Chance = P.BaseChance * FMath::Lerp(P.ProfileMultiplierAtZero, P.ProfileMultiplierAtMax, Profile01);

	const float SpeedKmh = Subject->GetVelocity().Size() * 0.036f;
	Chance *= FMath::Clamp(1.f - FMath::Max(0.f, SpeedKmh - 50.f) * P.SpeedPenaltyPerKmh, 0.1f, 1.f);

	const float Distance = FVector::Dist(GetComponentLocation(), Subject->GetActorLocation());
	Chance *= 1.f - 0.5f * FMath::Clamp(Distance / FMath::Max(Range, 1.f), 0.f, 1.f);

	// Tarnung: Licht/Hocke zu Fuss, Kennzeichen-Stoerer/Maske als Datenhook.
	if (!bAsVehicle)
	{
		Chance *= UMTAStealthComponent::GetSubjectVisibility(Subject);
	}
	Chance *= UMTAStealthComponent::GetCaptureModifier(Subject, bAsVehicle);

	return FMath::Clamp(Chance, 0.f, 1.f);
}

void UMTAAegisCameraComponent::Scan()
{
	if (!bOnline)
	{
		return;
	}
	UMTAChaosSubsystem* Chaos = GetChaos();
	UWorld* World = GetWorld();
	if (!Chaos || !World)
	{
		return;
	}
	const float Now = World->GetTimeSeconds();

	for (AActor* Subject : Chaos->GetPlayerSubjects())
	{
		const FObjectKey Key(Subject);
		if (const float* Until = Cooldowns.Find(Key))
		{
			if (*Until > Now)
			{
				continue;
			}
		}

		const FVector Target = Subject->GetActorLocation() + FVector(0.f, 0.f, 50.f);
		if (!IsInCone(Target))
		{
			continue;
		}
		const bool bAsVehicle = Chaos->HasVehicleRecord(Subject);
		if (!bAsVehicle && !CaptureProfile.bFaceRecognition)
		{
			continue; // Reine Kennzeichenkamera: Fussgaenger interessieren nicht.
		}

		FCollisionQueryParams Params(SCENE_QUERY_STAT(MTAAegisSight), false, GetOwner());
		Params.AddIgnoredActor(Subject);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, GetComponentLocation(), Target, ECC_Visibility, Params))
		{
			continue; // verdeckt
		}

		const float Chance = ComputeCaptureChance(Subject, bAsVehicle);
		if (FMath::FRand() > Chance)
		{
			continue;
		}

		Cooldowns.Add(Key, Now + SubjectCooldown);
		const float SpeedKmh = Subject->GetVelocity().Size() * 0.036f;
		const bool bFace = !bAsVehicle && CaptureProfile.bFaceRecognition;
		OnCaptured.Broadcast(Subject, bFace);
		Chaos->HandleCameraCapture(Subject, this, SpeedKmh, bFace);
	}
}

void UMTAAegisCameraComponent::SetOnline(bool bNewOnline)
{
	if (bPermanentlyDisabled)
	{
		bNewOnline = false;
	}
	if (bOnline != bNewOnline)
	{
		bOnline = bNewOnline;
		OnOnlineChanged.Broadcast(bOnline);
	}
}

void UMTAAegisCameraComponent::Hack(float Duration, AActor* InstigatorActor)
{
	SetOnline(false);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(HackTimer, this, &UMTAAegisCameraComponent::RestoreFromHack, FMath::Max(Duration, 0.1f), false);
	}
	if (InstigatorActor)
	{
		if (UMTAChaosSubsystem* Chaos = GetChaos())
		{
			Chaos->ReportSimpleCrime(EMTACrimeType::CameraTampering, InstigatorActor, GetComponentLocation(), GetOwner(), 0.5f);
		}
	}
}

void UMTAAegisCameraComponent::RestoreFromHack()
{
	SetOnline(true);
}

void UMTAAegisCameraComponent::Disable(AActor* InstigatorActor)
{
	bPermanentlyDisabled = true;
	SetOnline(false);
	if (InstigatorActor)
	{
		if (UMTAChaosSubsystem* Chaos = GetChaos())
		{
			Chaos->ReportSimpleCrime(EMTACrimeType::CameraTampering, InstigatorActor, GetComponentLocation(), GetOwner(), 1.f);
		}
	}
}

AMTAAegisCameraActor::AMTAAegisCameraActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));

	Camera = CreateDefaultSubobject<UMTAAegisCameraComponent>(TEXT("AegisCamera"));
	Camera->SetupAttachment(Root);
	// Kamera-Kopf ueblicherweise auf 4,5 m Masthoehe, leicht nach unten geneigt.
	Camera->SetRelativeLocation(FVector(0.f, 0.f, 450.f));
	Camera->SetRelativeRotation(FRotator(-12.f, 0.f, 0.f));
}
