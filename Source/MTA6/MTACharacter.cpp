#include "MTACharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "Engine/HitResult.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "MTAChaosSubsystem.h"
#include "MTAInteractionComponent.h"
#include "MTAStealthComponent.h"
#include "MTAVehicleEntryComponent.h"

AMTACharacter::AMTACharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(38.f, 92.f);

	// Figur dreht sich in Bewegungsrichtung, Kamera folgt dem Controller.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0.f, 540.f, 0.f);
	Move->MaxWalkSpeed = JogSpeed;
	Move->MaxWalkSpeedCrouched = CrouchSpeed;
	Move->JumpZVelocity = 450.f;
	Move->AirControl = 0.3f;
	Move->MaxStepHeight = 40.f;
	Move->BrakingDecelerationWalking = 1600.f;
	Move->GetNavAgentPropertiesRef().bCanCrouch = true;
	Move->SetCrouchedHalfHeight(58.f);
	Move->bCanWalkOffLedgesWhenCrouching = true;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = ArmLength;
	SpringArm->SocketOffset = FVector(0.f, ShoulderOffset, ShoulderHeight);
	SpringArm->bUsePawnControlRotation = true;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 12.f;
	SpringArm->ProbeSize = 12.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->SetFieldOfView(BaseFOV);

	Interaction = CreateDefaultSubobject<UMTAInteractionComponent>(TEXT("Interaction"));
	VehicleEntry = CreateDefaultSubobject<UMTAVehicleEntryComponent>(TEXT("VehicleEntry"));
	Stealth = CreateDefaultSubobject<UMTAStealthComponent>(TEXT("Stealth"));

	CurrentShoulderY = ShoulderOffset;
}

void AMTACharacter::BeginPlay()
{
	Super::BeginPlay();
	Stamina = MaxStamina;
	CurrentShoulderY = ShoulderOffset * ShoulderSign;
	GetCharacterMovement()->MaxWalkSpeed = JogSpeed;
	GetCharacterMovement()->MaxWalkSpeedCrouched = CrouchSpeed;
	SavedGroundFriction = GetCharacterMovement()->GroundFriction;
	SavedBrakingDecel = GetCharacterMovement()->BrakingDecelerationWalking;
}

void AMTACharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (NewController && NewController->IsPlayerController())
	{
		if (UMTAChaosSubsystem* Chaos = GetWorld()->GetSubsystem<UMTAChaosSubsystem>())
		{
			Chaos->RegisterPlayerSubject(this);
		}
	}
}

// ---------------------------------------------------------------------------------------
// Eingabe
// ---------------------------------------------------------------------------------------

void AMTACharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	check(PlayerInputComponent);

	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &AMTACharacter::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &AMTACharacter::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("Turn"), this, &AMTACharacter::Turn);
	PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &AMTACharacter::LookUp);
	PlayerInputComponent->BindAxis(TEXT("TurnRate"), this, &AMTACharacter::TurnRate);
	PlayerInputComponent->BindAxis(TEXT("LookUpRate"), this, &AMTACharacter::LookUpRate);

	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &AMTACharacter::OnJumpPressed);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Released, this, &AMTACharacter::OnJumpReleased);
	PlayerInputComponent->BindAction(TEXT("Sprint"), IE_Pressed, this, &AMTACharacter::OnSprintPressed);
	PlayerInputComponent->BindAction(TEXT("Sprint"), IE_Released, this, &AMTACharacter::OnSprintReleased);
	PlayerInputComponent->BindAction(TEXT("Crouch"), IE_Pressed, this, &AMTACharacter::OnCrouchPressed);
	PlayerInputComponent->BindAction(TEXT("Crouch"), IE_Released, this, &AMTACharacter::OnCrouchReleased);
	PlayerInputComponent->BindAction(TEXT("WalkToggle"), IE_Pressed, this, &AMTACharacter::OnWalkToggle);
	PlayerInputComponent->BindAction(TEXT("Dodge"), IE_Pressed, this, &AMTACharacter::OnDodgePressed);
	PlayerInputComponent->BindAction(TEXT("Interact"), IE_Pressed, this, &AMTACharacter::OnInteractPressed);
	PlayerInputComponent->BindAction(TEXT("Interact"), IE_Released, this, &AMTACharacter::OnInteractReleased);
	PlayerInputComponent->BindAction(TEXT("EnterExitVehicle"), IE_Pressed, this, &AMTACharacter::OnEnterExitPressed);
	PlayerInputComponent->BindAction(TEXT("ShoulderSwap"), IE_Pressed, this, &AMTACharacter::OnShoulderSwapPressed);
}

bool AMTACharacter::CanMove() const
{
	if (bIsVaulting || bIsDodging || bIsBailingOut)
	{
		return false;
	}
	return !(VehicleEntry && VehicleEntry->IsBusy() && VehicleEntry->Phase != EMTAVehicleEntryPhase::Approaching);
}

void AMTACharacter::MoveForward(float Value)
{
	if (FMath::IsNearlyZero(Value) || !Controller)
	{
		return;
	}
	if (VehicleEntry && VehicleEntry->Phase == EMTAVehicleEntryPhase::Approaching && FMath::Abs(Value) > 0.5f)
	{
		VehicleEntry->CancelEntry(); // Spieler uebernimmt wieder
	}
	if (!CanMove())
	{
		return;
	}
	const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Value);
}

void AMTACharacter::MoveRight(float Value)
{
	if (FMath::IsNearlyZero(Value) || !Controller)
	{
		return;
	}
	if (VehicleEntry && VehicleEntry->Phase == EMTAVehicleEntryPhase::Approaching && FMath::Abs(Value) > 0.5f)
	{
		VehicleEntry->CancelEntry();
	}
	if (!CanMove())
	{
		return;
	}
	const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Value);
}

void AMTACharacter::Turn(float Value)
{
	AddControllerYawInput(Value);
}

void AMTACharacter::LookUp(float Value)
{
	AddControllerPitchInput(Value);
}

void AMTACharacter::TurnRate(float Value)
{
	if (const UWorld* World = GetWorld())
	{
		AddControllerYawInput(Value * TurnRateGamepad * World->GetDeltaSeconds());
	}
}

void AMTACharacter::LookUpRate(float Value)
{
	if (const UWorld* World = GetWorld())
	{
		AddControllerPitchInput(Value * TurnRateGamepad * World->GetDeltaSeconds());
	}
}

void AMTACharacter::OnJumpPressed()
{
	if (!CanMove())
	{
		return;
	}
	// Erst Ueberklettern pruefen, sonst normaler Sprung.
	if (TryVault())
	{
		return;
	}
	if (bIsCrouched)
	{
		UnCrouch();
		return;
	}
	if (CanJump() && ConsumeStamina(JumpStaminaCost))
	{
		Jump();
	}
}

void AMTACharacter::OnJumpReleased()
{
	StopJumping();
}

void AMTACharacter::OnSprintPressed()
{
	bWantsSprint = true;
	if (bIsCrouched)
	{
		UnCrouch();
	}
}

void AMTACharacter::OnSprintReleased()
{
	bWantsSprint = false;
}

void AMTACharacter::OnCrouchPressed()
{
	if (!CanMove() || GetCharacterMovement()->IsFalling())
	{
		return;
	}
	if (bToggleCrouch && bIsCrouched)
	{
		UnCrouch();
	}
	else
	{
		bWantsSprint = false;
		Crouch();
	}
}

void AMTACharacter::OnCrouchReleased()
{
	if (!bToggleCrouch && bIsCrouched)
	{
		UnCrouch();
	}
}

void AMTACharacter::OnWalkToggle()
{
	bWalkToggled = !bWalkToggled;
}

void AMTACharacter::OnDodgePressed()
{
	TryDodge();
}

void AMTACharacter::OnInteractPressed()
{
	if (Interaction)
	{
		Interaction->BeginInteract();
	}
}

void AMTACharacter::OnInteractReleased()
{
	if (Interaction)
	{
		Interaction->EndInteract();
	}
}

void AMTACharacter::OnEnterExitPressed()
{
	// Im Fahrzeug faengt die Eingabe-Komponente von VehicleEntry die Taste ab.
	if (VehicleEntry && !bIsVaulting && !bIsDodging)
	{
		VehicleEntry->ToggleEnterExit();
	}
}

void AMTACharacter::OnShoulderSwapPressed()
{
	SwapShoulder();
}

void AMTACharacter::SwapShoulder()
{
	ShoulderSign = -ShoulderSign;
}

// ---------------------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------------------

void AMTACharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	bIsInVehicle = VehicleEntry && VehicleEntry->IsInVehicle();
	if (!bIsInVehicle)
	{
		UpdateVault(DeltaSeconds);
		UpdateDodge(DeltaSeconds);
		UpdateGait();
		UpdateCamera(DeltaSeconds);
	}
	UpdateStamina(DeltaSeconds);
	UpdateAnimVariables();
}

bool AMTACharacter::ConsumeStamina(float Amount)
{
	if (Amount <= 0.f)
	{
		return true;
	}
	if (Stamina < Amount * 0.5f)
	{
		return false; // Rest darf knapp unterschritten werden, sonst fuehlt es sich zaeh an
	}
	Stamina = FMath::Max(0.f, Stamina - Amount);
	StaminaRegenDelayLeft = StaminaRegenDelay;
	if (Stamina <= 0.f)
	{
		bIsExhausted = true;
	}
	OnStaminaChanged.Broadcast(Stamina, MaxStamina);
	return true;
}

void AMTACharacter::UpdateStamina(float DeltaSeconds)
{
	const float Old = Stamina;
	if (bIsSprinting && Speed > 50.f)
	{
		Stamina = FMath::Max(0.f, Stamina - SprintDrainPerSecond * DeltaSeconds);
		StaminaRegenDelayLeft = StaminaRegenDelay;
		if (Stamina <= 0.f)
		{
			bIsExhausted = true;
		}
	}
	else if (StaminaRegenDelayLeft > 0.f)
	{
		StaminaRegenDelayLeft -= DeltaSeconds;
	}
	else
	{
		// Im Stand und geduckt schneller erholen.
		const float Rate = StaminaRegenPerSecond * (Speed < 10.f ? 1.5f : 1.f);
		Stamina = FMath::Min(MaxStamina, Stamina + Rate * DeltaSeconds);
	}
	if (bIsExhausted && Stamina >= MaxStamina * ExhaustionRecoverFraction)
	{
		bIsExhausted = false;
	}
	if (!FMath::IsNearlyEqual(Old, Stamina))
	{
		OnStaminaChanged.Broadcast(Stamina, MaxStamina);
	}
}

void AMTACharacter::UpdateGait()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (bIsDodging)
	{
		return; // Dodge setzt eigene Werte
	}
	const bool bMoving = GetLastMovementInputVector().SizeSquared() > 0.01f || Speed > 50.f;
	bIsSprinting = bWantsSprint && !bIsCrouched && !bIsExhausted && Stamina > 0.f && bMoving && !Move->IsFalling();

	if (bIsSprinting)
	{
		Gait = EMTAGait::Sprint;
		Move->MaxWalkSpeed = SprintSpeed;
	}
	else if (bWalkToggled)
	{
		Gait = EMTAGait::Walk;
		Move->MaxWalkSpeed = WalkSpeed;
	}
	else
	{
		Gait = EMTAGait::Jog;
		Move->MaxWalkSpeed = bIsExhausted ? FMath::Lerp(WalkSpeed, JogSpeed, 0.6f) : JogSpeed;
	}
}

void AMTACharacter::UpdateCamera(float DeltaSeconds)
{
	CurrentShoulderY = FMath::FInterpTo(CurrentShoulderY, ShoulderOffset * ShoulderSign, DeltaSeconds, CameraInterpSpeed);
	const float TargetArm = bIsCrouched ? CrouchArmLength : ArmLength;
	SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, TargetArm, DeltaSeconds, CameraInterpSpeed * 0.5f);
	SpringArm->SocketOffset = FVector(0.f, CurrentShoulderY, ShoulderHeight);

	const float TargetFOV = bIsSprinting ? SprintFOV : BaseFOV;
	FollowCamera->SetFieldOfView(FMath::FInterpTo(FollowCamera->FieldOfView, TargetFOV, DeltaSeconds, CameraInterpSpeed * 0.5f));
}

void AMTACharacter::UpdateAnimVariables()
{
	const FVector Velocity = GetVelocity();
	Speed = Velocity.Size2D();
	bIsInAir = GetCharacterMovement()->IsFalling();
	bIsCrouching = bIsCrouched;

	if (Speed > 1.f)
	{
		const double VelYaw = Velocity.Rotation().Yaw;
		Direction = static_cast<float>(FRotator::NormalizeAxis(VelYaw - GetActorRotation().Yaw));
	}
	else
	{
		Direction = 0.f;
	}
	if (Controller)
	{
		AimPitch = static_cast<float>(FRotator::NormalizeAxis(Controller->GetControlRotation().Pitch));
	}
}

// ---------------------------------------------------------------------------------------
// Klettern
// ---------------------------------------------------------------------------------------

bool AMTACharacter::TryVault()
{
	UWorld* World = GetWorld();
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!World || bIsVaulting || bIsCrouched || Move->IsFalling() || Stamina < VaultStaminaCost * 0.5f)
	{
		return false;
	}

	const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Loc = GetActorLocation();
	const float FeetZ = Loc.Z - HalfHeight;

	// Richtung: Eingabe, sonst Blickrichtung der Figur.
	FVector Forward = GetLastMovementInputVector().GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		Forward = GetActorForwardVector().GetSafeNormal2D();
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MTAVault), false, this);
	FHitResult WallHit;

	// 1) Hindernis auf Kniehoehe vorn?
	const FVector KneeStart(Loc.X, Loc.Y, FeetZ + VaultMinHeight);
	const FVector KneeEnd = KneeStart + Forward * (Radius + VaultReach);
	if (!World->SweepSingleByChannel(WallHit, KneeStart, KneeEnd, FQuat::Identity, ECC_Visibility,
		FCollisionShape::MakeSphere(10.f), Params))
	{
		return false;
	}
	// Nur annaehernd senkrechte Flaechen (keine Rampen).
	if (FMath::Abs(WallHit.ImpactNormal.Z) > 0.3f)
	{
		return false;
	}
	const FVector WallDir = -FVector(WallHit.ImpactNormal.X, WallHit.ImpactNormal.Y, 0.f).GetSafeNormal();

	// 2) Oberkante finden: von oben herab kurz hinter der Wandflaeche.
	const FVector TopProbe = WallHit.ImpactPoint + WallDir * 15.f;
	FHitResult TopHit;
	const FVector TopStart(TopProbe.X, TopProbe.Y, FeetZ + VaultMaxHeight + 30.f);
	const FVector TopEnd(TopProbe.X, TopProbe.Y, FeetZ + VaultMinHeight * 0.5f);
	if (!World->LineTraceSingleByChannel(TopHit, TopStart, TopEnd, ECC_Visibility, Params) || TopHit.bStartPenetrating)
	{
		return false; // zu hoch (Start steckt in der Wand) oder keine Kante
	}
	const float ObstacleHeight = TopHit.ImpactPoint.Z - FeetZ;
	if (ObstacleHeight < VaultMinHeight || ObstacleHeight > VaultMaxHeight || TopHit.ImpactNormal.Z < 0.7f)
	{
		return false;
	}
	const float TopZ = TopHit.ImpactPoint.Z;

	// 3) Tiefe: liegt hinter VaultMaxDepth wieder Boden tiefer als die Kante -> darueber; sonst hinauf.
	const FVector BeyondProbe = TopProbe + WallDir * (VaultMaxDepth + Radius);
	FHitResult BeyondHit;
	const bool bHasBeyond = World->LineTraceSingleByChannel(BeyondHit,
		FVector(BeyondProbe.X, BeyondProbe.Y, TopZ + 20.f), FVector(BeyondProbe.X, BeyondProbe.Y, FeetZ - 200.f), ECC_Visibility, Params);
	const bool bVaultOver = !bHasBeyond || BeyondHit.ImpactPoint.Z < TopZ - 30.f;

	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	FVector Apex(TopProbe.X, TopProbe.Y, TopZ + HalfHeight + 10.f);
	FVector End;
	if (bVaultOver)
	{
		const float LandZ = bHasBeyond ? BeyondHit.ImpactPoint.Z : FeetZ;
		End = FVector(BeyondProbe.X, BeyondProbe.Y, LandZ + HalfHeight + 2.f);
		Apex += WallDir * (VaultMaxDepth * 0.5f);
	}
	else
	{
		End = FVector(TopProbe.X, TopProbe.Y, TopZ + HalfHeight + 2.f) + WallDir * Radius;
	}

	// 4) Platz am Scheitel und am Ziel?
	if (World->OverlapBlockingTestByChannel(Apex, FQuat::Identity, ECC_Pawn, Capsule, Params)
		|| World->OverlapBlockingTestByChannel(End, FQuat::Identity, ECC_Pawn, Capsule, Params))
	{
		return false;
	}

	if (!ConsumeStamina(VaultStaminaCost))
	{
		return false;
	}

	VaultStart = Loc;
	VaultApex = Apex;
	VaultEnd = End;
	VaultElapsed = 0.f;
	VaultTotal = bVaultOver ? VaultDuration : MantleDuration;
	bIsVaulting = true;
	bIsMantling = !bVaultOver;
	VaultAlpha = 0.f;

	SetActorRotation(FRotator(0.f, WallDir.Rotation().Yaw, 0.f));
	Move->StopMovementImmediately();
	Move->SetMovementMode(MOVE_Flying);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Ignore);
	if (Stealth)
	{
		Stealth->ReportNoise(0.25f);
	}
	return true;
}

void AMTACharacter::UpdateVault(float DeltaSeconds)
{
	if (!bIsVaulting)
	{
		return;
	}
	VaultElapsed += DeltaSeconds;
	VaultAlpha = VaultTotal > 0.f ? FMath::Clamp(VaultElapsed / VaultTotal, 0.f, 1.f) : 1.f;

	// Quadratische Bezier-Kurve Start -> Scheitel -> Ziel (Platzhalter fuer Root Motion).
	const float T = VaultAlpha;
	const FVector P = FMath::Square(1.f - T) * VaultStart + 2.f * (1.f - T) * T * VaultApex + FMath::Square(T) * VaultEnd;
	SetActorLocation(P, false, nullptr, ETeleportType::None);

	if (VaultAlpha >= 1.f)
	{
		bIsVaulting = false;
		bIsMantling = false;
		GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		GetCharacterMovement()->SetMovementMode(MOVE_Falling); // landet sauber per Physik
	}
}

// ---------------------------------------------------------------------------------------
// Ausweichrolle / Abspringen
// ---------------------------------------------------------------------------------------

bool AMTACharacter::TryDodge()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!CanMove() || DodgeCooldownLeft > 0.f || Move->IsFalling() || bIsInVehicle)
	{
		return false;
	}
	if (!ConsumeStamina(DodgeStaminaCost))
	{
		return false;
	}
	if (bIsCrouched)
	{
		UnCrouch();
	}

	FVector Dir = GetLastMovementInputVector().GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		Dir = GetActorForwardVector().GetSafeNormal2D();
	}
	SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));

	SavedGroundFriction = Move->GroundFriction;
	SavedBrakingDecel = Move->BrakingDecelerationWalking;
	Move->GroundFriction = 0.f;
	Move->BrakingDecelerationWalking = 0.f;
	Move->MaxWalkSpeed = DodgeSpeed;
	Move->Velocity = Dir * DodgeSpeed;

	bIsDodging = true;
	DodgeTimeLeft = DodgeDuration;
	if (Stealth)
	{
		Stealth->ReportNoise(0.3f);
	}
	return true;
}

void AMTACharacter::EndDodge()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->GroundFriction = SavedGroundFriction;
	Move->BrakingDecelerationWalking = SavedBrakingDecel;
	Move->Velocity = Move->Velocity.GetClampedToMaxSize(JogSpeed);
	bIsDodging = false;
	DodgeCooldownLeft = DodgeCooldown;
}

void AMTACharacter::StartBailOutRoll(FVector BailDirection)
{
	if (bIsDodging)
	{
		EndDodge();
	}
	if (!BailDirection.IsNearlyZero())
	{
		SetActorRotation(FRotator(0.f, BailDirection.Rotation().Yaw, 0.f));
	}
	bIsBailingOut = true;
	BailOutTimeLeft = BailOutDuration;
	if (Stealth)
	{
		Stealth->ReportNoise(0.6f);
	}
}

void AMTACharacter::UpdateDodge(float DeltaSeconds)
{
	if (DodgeCooldownLeft > 0.f)
	{
		DodgeCooldownLeft -= DeltaSeconds;
	}
	if (bIsDodging)
	{
		DodgeTimeLeft -= DeltaSeconds;
		if (DodgeTimeLeft <= 0.f || GetCharacterMovement()->IsFalling())
		{
			EndDodge();
		}
	}
	if (bIsBailingOut)
	{
		BailOutTimeLeft -= DeltaSeconds;
		if (BailOutTimeLeft <= 0.f && !GetCharacterMovement()->IsFalling())
		{
			bIsBailingOut = false;
		}
	}
}

// ---------------------------------------------------------------------------------------
// Ereignisse
// ---------------------------------------------------------------------------------------

void AMTACharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	if (Stealth)
	{
		const float FallSpeed = FMath::Abs(GetCharacterMovement()->Velocity.Z);
		Stealth->ReportNoise(FMath::Clamp(FallSpeed / 1500.f, 0.1f, 1.f));
	}
}

void AMTACharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	bWantsSprint = false;
}

void AMTACharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
}

