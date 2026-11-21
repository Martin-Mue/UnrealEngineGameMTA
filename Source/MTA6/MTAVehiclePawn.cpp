#include "MTAVehiclePawn.h"

#include "MTA6GameMode.h"
#include "MTAMissionComponent.h"
#include "MTAMinimapTypes.h"

#include "Camera/CameraComponent.h"
#include "ChaosVehicleMovementComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"

AMTAVehiclePawn::AMTAVehiclePawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	// Treffer-Ereignisse fuer Schaden und Kamera-Schuetteln
	if (USkeletalMeshComponent* VehicleMesh = GetMesh())
	{
		VehicleMesh->BodyInstance.bNotifyRigidBodyCollision = true;
	}

	// Verfolgerkamera: Godot-Vorbild 11 m hinter, 4,6 m ueber dem Auto
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(GetMesh());
	SpringArm->SetRelativeLocation(FVector(0.f, 0.f, 160.f));
	SpringArm->SetRelativeRotation(FRotator(-11.f, 0.f, 0.f));
	SpringArm->TargetArmLength = ArmLengthMin;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 120.f);
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritRoll = false;
	SpringArm->bInheritYaw = true;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 8.f;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraRotationLagSpeed = 6.f;
	SpringArm->bDoCollisionTest = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;
	Camera->SetFieldOfView(FOVMin);

	AutoPossessPlayer = EAutoReceiveInput::Disabled;
}

void AMTAVehiclePawn::BeginPlay()
{
	Super::BeginPlay();
	Health = MaxHealth;
	bWrecked = false;
	CameraBaseLocation = Camera->GetRelativeLocation();
}

void AMTAVehiclePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	check(PlayerInputComponent);

	PlayerInputComponent->BindAxis(TEXT("Throttle"), this, &AMTAVehiclePawn::OnThrottle);
	PlayerInputComponent->BindAxis(TEXT("Brake"), this, &AMTAVehiclePawn::OnBrake);
	PlayerInputComponent->BindAxis(TEXT("Steer"), this, &AMTAVehiclePawn::OnSteer);

	PlayerInputComponent->BindAction(TEXT("Handbrake"), IE_Pressed, this, &AMTAVehiclePawn::OnHandbrakePressed);
	PlayerInputComponent->BindAction(TEXT("Handbrake"), IE_Released, this, &AMTAVehiclePawn::OnHandbrakeReleased);
	PlayerInputComponent->BindAction(TEXT("Reset"), IE_Pressed, this, &AMTAVehiclePawn::OnResetPressed);
	PlayerInputComponent->BindAction(TEXT("Save"), IE_Pressed, this, &AMTAVehiclePawn::OnSavePressed);
	PlayerInputComponent->BindAction(TEXT("Load"), IE_Pressed, this, &AMTAVehiclePawn::OnLoadPressed);
	PlayerInputComponent->BindAction(TEXT("Choice1"), IE_Pressed, this, &AMTAVehiclePawn::OnChoice1);
	PlayerInputComponent->BindAction(TEXT("Choice2"), IE_Pressed, this, &AMTAVehiclePawn::OnChoice2);
	PlayerInputComponent->BindAction(TEXT("Choice3"), IE_Pressed, this, &AMTAVehiclePawn::OnChoice3);
	PlayerInputComponent->BindAction(TEXT("Choice4"), IE_Pressed, this, &AMTAVehiclePawn::OnChoice4);
	PlayerInputComponent->BindAction(TEXT("Confirm"), IE_Pressed, this, &AMTAVehiclePawn::OnConfirm);
}

void AMTAVehiclePawn::OnThrottle(float Value) { ThrottleInput = FMath::Clamp(Value, 0.f, 1.f); }
void AMTAVehiclePawn::OnBrake(float Value) { BrakeInput = FMath::Clamp(Value, 0.f, 1.f); }
void AMTAVehiclePawn::OnSteer(float Value) { SteerInput = FMath::Clamp(Value, -1.f, 1.f); }
void AMTAVehiclePawn::OnHandbrakePressed() { bHandbrake = true; }
void AMTAVehiclePawn::OnHandbrakeReleased() { bHandbrake = false; }

void AMTAVehiclePawn::OnResetPressed()
{
	if (!IsInputBlocked())
	{
		ResetVehicle();
	}
}

void AMTAVehiclePawn::OnSavePressed()
{
	if (AMTA6GameMode* GM = GetMTAGameMode())
	{
		GM->SaveGameToSlot(false);
	}
}

void AMTAVehiclePawn::OnLoadPressed()
{
	if (AMTA6GameMode* GM = GetMTAGameMode())
	{
		GM->LoadGameFromSlot();
	}
}

void AMTAVehiclePawn::OnChoice1() { SelectChoice(0); }
void AMTAVehiclePawn::OnChoice2() { SelectChoice(1); }
void AMTAVehiclePawn::OnChoice3() { SelectChoice(2); }
void AMTAVehiclePawn::OnChoice4() { SelectChoice(3); }

void AMTAVehiclePawn::SelectChoice(int32 Index)
{
	if (AMTA6GameMode* GM = GetMTAGameMode())
	{
		if (UMTAMissionComponent* Mission = GM->GetMissionComponent())
		{
			Mission->SelectOption(Index);
		}
	}
}

void AMTAVehiclePawn::OnConfirm()
{
	if (AMTA6GameMode* GM = GetMTAGameMode())
	{
		if (UMTAMissionComponent* Mission = GM->GetMissionComponent())
		{
			Mission->ConfirmDialog();
		}
	}
}

AMTA6GameMode* AMTAVehiclePawn::GetMTAGameMode() const
{
	UWorld* World = GetWorld();
	return World ? World->GetAuthGameMode<AMTA6GameMode>() : nullptr;
}

bool AMTAVehiclePawn::IsInputBlocked() const
{
	if (bWrecked)
	{
		return true;
	}
	const AMTA6GameMode* GM = GetMTAGameMode();
	return GM && GM->IsDialogOpen();
}

float AMTAVehiclePawn::GetSpeedKmh() const
{
	return GetVelocity().Size() * 0.036f; // cm/s -> km/h
}

void AMTAVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	HitCooldownLeft = FMath::Max(0.f, HitCooldownLeft - DeltaSeconds);
	UpdateDriving(DeltaSeconds);
	UpdateCamera(DeltaSeconds);
	LastVelocity = GetVelocity();
}

void AMTAVehiclePawn::UpdateDriving(float DeltaSeconds)
{
	UChaosVehicleMovementComponent* Move = GetVehicleMovementComponent();
	if (!Move)
	{
		return;
	}

	if (IsInputBlocked())
	{
		// Dialog offen oder Wrack: Auto festhalten (Godot: brake = 60)
		SmoothedSteer = 0.f;
		Move->SetThrottleInput(0.f);
		Move->SetBrakeInput(1.f);
		Move->SetSteeringInput(0.f);
		Move->SetHandbrakeInput(true);
		return;
	}

	const float SpeedKmh = GetSpeedKmh();
	const float ForwardKmh = Move->GetForwardSpeed() * 0.036f;

	// Hoechstgeschwindigkeit: oberhalb kein Gas mehr (nur vorwaerts begrenzen)
	float Throttle = ThrottleInput;
	if (ForwardKmh >= MaxSpeedKmh)
	{
		Throttle = 0.f;
	}

	// Lenkung wird mit der Geschwindigkeit weicher (Godot: 1 - clamp(v / 60 m/s, 0, 0.7))
	const float SpeedAlpha = SteerReductionSpeedKmh > 0.f ? SpeedKmh / SteerReductionSpeedKmh : 0.f;
	const float SteerScale = 1.f - FMath::Clamp(SpeedAlpha, 0.f, SteerSpeedReduction);
	SmoothedSteer = FMath::FInterpTo(SmoothedSteer, SteerInput * SteerScale, DeltaSeconds, SteerInterpSpeed);

	Move->SetThrottleInput(Throttle);
	Move->SetBrakeInput(BrakeInput);
	Move->SetSteeringInput(SmoothedSteer);
	Move->SetHandbrakeInput(bHandbrake);
}

void AMTAVehiclePawn::UpdateCamera(float DeltaSeconds)
{
	if (!SpringArm || !Camera)
	{
		return;
	}

	// Geschwindigkeitsabhaengiger Abstand und Blickwinkel
	const float Alpha = CameraMaxSpeedKmh > 0.f ? FMath::Clamp(GetSpeedKmh() / CameraMaxSpeedKmh, 0.f, 1.f) : 0.f;
	const float TargetArm = FMath::Lerp(ArmLengthMin, ArmLengthMax, Alpha);
	const float TargetFOV = FMath::Lerp(FOVMin, FOVMax, Alpha);
	SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, TargetArm, DeltaSeconds, CameraInterpSpeed);
	Camera->SetFieldOfView(FMath::FInterpTo(Camera->FieldOfView, TargetFOV, DeltaSeconds, CameraInterpSpeed));

	// Kamera-Schuetteln (Trauma-Modell: Staerke quadratisch, linear abklingend)
	ShakeTrauma = FMath::Max(0.f, ShakeTrauma - ShakeDecay * DeltaSeconds);
	const float Shake = ShakeTrauma * ShakeTrauma;
	if (Shake > KINDA_SMALL_NUMBER)
	{
		const FVector Offset(0.f, FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f));
		Camera->SetRelativeLocation(CameraBaseLocation + Offset * ShakeMaxOffset * Shake);
		Camera->SetRelativeRotation(FRotator(
			FMath::FRandRange(-1.f, 1.f) * ShakeMaxAngle * Shake,
			FMath::FRandRange(-1.f, 1.f) * ShakeMaxAngle * Shake,
			FMath::FRandRange(-1.f, 1.f) * ShakeMaxAngle * Shake));
	}
	else
	{
		Camera->SetRelativeLocation(CameraBaseLocation);
		Camera->SetRelativeRotation(FRotator::ZeroRotator);
	}
}

void AMTAVehiclePawn::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved,
	FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);

	// Bodenkontakte ignorieren
	if (HitNormal.Z > 0.7f || HitCooldownLeft > 0.f)
	{
		return;
	}

	// Aufprallgeschwindigkeit aus der Geschwindigkeit des letzten Frames (nach dem Kontakt ist sie schon reduziert)
	const float ImpactSpeed = FVector::DotProduct(LastVelocity, -HitNormal);
	if (ImpactSpeed < DamageSpeedThreshold)
	{
		return;
	}

	HitCooldownLeft = HitCooldown;
	const float OverMps = (ImpactSpeed - DamageSpeedThreshold) / 100.f;
	ApplyVehicleDamage(OverMps * DamagePerMps);
}

void AMTAVehiclePawn::ApplyVehicleDamage(float Amount)
{
	if (Amount <= 0.f || bWrecked)
	{
		return;
	}

	Health = FMath::Max(0.f, Health - Amount);
	AddCameraShake(FMath::Clamp(Amount / 25.f, 0.25f, 1.f));
	OnDamaged.Broadcast(Health);

	if (Health <= 0.f)
	{
		bWrecked = true;
		OnWrecked.Broadcast();
	}
}

void AMTAVehiclePawn::AddCameraShake(float Intensity)
{
	ShakeTrauma = FMath::Clamp(ShakeTrauma + Intensity, 0.f, 1.f);
}

void AMTAVehiclePawn::Repair()
{
	Health = MaxHealth;
	bWrecked = false;
	OnDamaged.Broadcast(Health);
}

void AMTAVehiclePawn::TeleportVehicle(const FVector& Location, const FRotator& Rotation)
{
	SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	if (USkeletalMeshComponent* VehicleMesh = GetMesh())
	{
		VehicleMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		VehicleMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}
	LastVelocity = FVector::ZeroVector;
	SmoothedSteer = 0.f;
}

void AMTAVehiclePawn::ResetVehicle()
{
	const FVector Loc = GetActorLocation();

	// Naechste Strassenachse im Raster suchen
	auto NearestLine = [](float V) -> float
	{
		const int32 K = FMath::Clamp(FMath::RoundToInt((V + MTAGrid::Half) / MTAGrid::Period), 0, MTAGrid::Blocks);
		return MTAGrid::R(K);
	};
	const float LineX = NearestLine(Loc.X);
	const float LineY = NearestLine(Loc.Y);

	FVector Fwd = GetActorForwardVector();
	Fwd.Z = 0.f;

	FVector Target = Loc;
	float Yaw = 0.f;
	if (FMath::Abs(Loc.X - LineX) <= FMath::Abs(Loc.Y - LineY))
	{
		// Strasse verlaeuft entlang Y (Achse bei X = LineX); rechte Spur je nach Fahrtrichtung
		const float Sign = Fwd.Y >= 0.f ? 1.f : -1.f;
		Yaw = Sign > 0.f ? 90.f : -90.f;
		Target.X = LineX - Sign * ResetLaneOffset;
		Target.Y = FMath::Clamp(Loc.Y, -MTAGrid::Half, MTAGrid::Half);
	}
	else
	{
		// Strasse verlaeuft entlang X (Achse bei Y = LineY)
		const float Sign = Fwd.X >= 0.f ? 1.f : -1.f;
		Yaw = Sign > 0.f ? 0.f : 180.f;
		Target.Y = LineY + Sign * ResetLaneOffset;
		Target.X = FMath::Clamp(Loc.X, -MTAGrid::Half, MTAGrid::Half);
	}

	// Bodenhoehe per Linetrace bestimmen
	Target.Z = Loc.Z + 150.f;
	if (UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MTAVehicleReset), false, this);
		const FVector Start(Target.X, Target.Y, Loc.Z + 50000.f);
		const FVector End(Target.X, Target.Y, Loc.Z - 50000.f);
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			Target.Z = Hit.ImpactPoint.Z + 120.f;
		}
	}

	TeleportVehicle(Target, FRotator(0.f, Yaw, 0.f));
}
