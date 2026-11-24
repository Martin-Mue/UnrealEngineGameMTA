#include "MTALivingNPC.h"

#include "MTALivingWorld.h"
#include "MTADialogueSubsystem.h"
#include "MTANPCSubsystem.h"

#include "Animation/AnimInstance.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Erzeugt ein kleines, schattenloses Textschild ueber dem Kopf. */
	UTextRenderComponent* LivingMakeLabel(AMTALivingNPC* Self, USceneComponent* Parent, const TCHAR* Name, float Z, float Size, FColor Color)
	{
		UTextRenderComponent* Label = Self->CreateDefaultSubobject<UTextRenderComponent>(Name);
		Label->SetupAttachment(Parent);
		Label->SetRelativeLocation(FVector(0.f, 0.f, Z));
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetVerticalAlignment(EVRTA_TextBottom);
		Label->SetWorldSize(Size);
		Label->SetTextRenderColor(Color);
		Label->SetCastShadow(false);
		Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Label->SetVisibility(false);
		return Label;
	}
}

AMTALivingNPC::AMTALivingNPC()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	AutoPossessAI = EAutoPossessAI::Disabled;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	Capsule->InitCapsuleSize(38.f, 92.f);
	// Kamera soll nicht an Passanten haengen bleiben.
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	// Bewegung ohne Controller (kein AIModule noetig): Eingabe kommt vom Manager per AddMovementInput.
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bRunPhysicsWithNoController = true;
		Move->bOrientRotationToMovement = true;
		Move->bUseControllerDesiredRotation = false;
		Move->RotationRate = FRotator(0.f, 300.f, 0.f);
		Move->MaxWalkSpeed = 150.f;
		Move->MaxAcceleration = 600.f;
		Move->BrakingDecelerationWalking = 900.f;
		// Einfache Ausweichlogik untereinander (RVO, ohne NavMesh).
		Move->bUseRVOAvoidance = true;
		Move->AvoidanceConsiderationRadius = 300.f;
	}

	// Figur: Mannequin aus den Epic-Vorlagen (wie AMTADialogueNPC)
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		static ConstructorHelpers::FObjectFinderOptional<USkeletalMesh> LivingMesh(TEXT("/Game/Mannequin/Character/Mesh/SK_Mannequin.SK_Mannequin"));
		static ConstructorHelpers::FClassFinder<UAnimInstance> LivingAnim(TEXT("/Game/Mannequin/Animations/ThirdPerson_AnimBP"));
		if (LivingMesh.Succeeded())
		{
			Body->SetSkeletalMesh(LivingMesh.Get());
		}
		if (LivingAnim.Succeeded())
		{
			Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			Body->SetAnimInstanceClass(LivingAnim.Class);
		}
		Body->SetRelativeLocation(FVector(0.f, 0.f, -92.f));
		Body->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
		Body->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		// Leistung (bis zu 80 Figuren): Pose nur berechnen, wenn sichtbar; Update-Rate-Optimierung.
		Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		Body->bEnableUpdateRateOptimizations = true;
	}

	NameLabel = LivingMakeLabel(this, RootComponent, TEXT("NameLabel"), 118.f, 14.f, FColor(255, 214, 90));
	ActivityLabel = LivingMakeLabel(this, RootComponent, TEXT("ActivityLabel"), 104.f, 10.f, FColor(210, 210, 210));
	SpeechLabel = LivingMakeLabel(this, RootComponent, TEXT("SpeechLabel"), 136.f, 16.f, FColor::White);
}

void AMTALivingNPC::BindAgent(AMTALivingWorldManager* InManager, int32 InAgentIndex, FName InId, const FString& InName,
	bool bInKeyNPC, bool bInHasDialogue, float InWalkSpeed, float InScale)
{
	LivingManager = InManager;
	AgentIndex = InAgentIndex;
	NPCId = InId;
	DisplayName = InName;
	bKeyNPC = bInKeyNPC;
	bHasDialogue = bInHasDialogue;
	SpeechText.Reset();
	SpeechTimeLeft = 0.f;

	SetActorScale3D(FVector(InScale));
	SetWalkSpeed(InWalkSpeed);

	if (NameLabel)
	{
		NameLabel->SetText(FText::FromString(DisplayName));
		// Gold = Gespraech moeglich, Hellblau = Schluessel-/Nebenfigur, Grau = Passant
		NameLabel->SetTextRenderColor(bHasDialogue ? FColor(255, 214, 90) : (bKeyNPC ? FColor(170, 210, 255) : FColor(225, 225, 225)));
	}
	if (SpeechLabel)
	{
		SpeechLabel->SetText(FText::GetEmpty());
	}

	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	SetActorTickEnabled(true);
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->SetComponentTickEnabled(true);
		Move->SetMovementMode(MOVE_Walking);
	}
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		Body->SetComponentTickEnabled(true);
	}
	bNameInRange = false;
	bSpeechInRange = false;
	UpdateLabelVisibility();
}

void AMTALivingNPC::UnbindAgent()
{
	AgentIndex = INDEX_NONE;
	NPCId = NAME_None;
	DisplayName.Reset();
	ActivityText.Reset();
	SpeechText.Reset();
	SpeechTimeLeft = 0.f;
	bNameInRange = false;
	bSpeechInRange = false;
	UpdateLabelVisibility();

	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->SetComponentTickEnabled(false);
	}
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		Body->SetComponentTickEnabled(false);
	}
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetActorTickEnabled(false);
}

void AMTALivingNPC::SetWalkSpeed(float Speed)
{
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = FMath::Clamp(Speed, 60.f, 600.f);
	}
}

void AMTALivingNPC::SetNameLabel(const FString& Text)
{
	if (NameLabel)
	{
		NameLabel->SetText(FText::FromString(Text));
	}
}

void AMTALivingNPC::SetActivityText(const FString& Text)
{
	if (ActivityText == Text)
	{
		return;
	}
	ActivityText = Text;
	if (ActivityLabel)
	{
		ActivityLabel->SetText(FText::FromString(ActivityText));
	}
	UpdateLabelVisibility();
}

FString AMTALivingNPC::WrapSpeech(const FString& Text, int32 MaxLineChars)
{
	// UTextRenderComponent bricht nicht selbst um; "<br>" trennt Zeilen.
	TArray<FString> Words;
	Text.ParseIntoArrayWS(Words);
	FString Out;
	int32 LineLen = 0;
	for (const FString& W : Words)
	{
		if (LineLen > 0 && LineLen + 1 + W.Len() > MaxLineChars)
		{
			Out += TEXT("<br>");
			LineLen = 0;
		}
		else if (LineLen > 0)
		{
			Out += TEXT(" ");
			++LineLen;
		}
		Out += W;
		LineLen += W.Len();
	}
	return Out;
}

void AMTALivingNPC::ShowSpeech(const FString& Text, float Duration, FColor Color)
{
	if (Text.IsEmpty())
	{
		return;
	}
	SpeechText = Text;
	SpeechTimeLeft = FMath::Max(0.5f, Duration);
	if (SpeechLabel)
	{
		SpeechLabel->SetTextRenderColor(Color);
		SpeechLabel->SetText(FText::FromString(WrapSpeech(Text, 34)));
	}
	UpdateLabelVisibility();
}

void AMTALivingNPC::ClearSpeech()
{
	SpeechText.Reset();
	SpeechTimeLeft = 0.f;
	UpdateLabelVisibility();
}

void AMTALivingNPC::SetLabelRange(bool bInNameVisible, bool bInSpeechVisible)
{
	if (bNameInRange == bInNameVisible && bSpeechInRange == bInSpeechVisible)
	{
		return;
	}
	bNameInRange = bInNameVisible;
	bSpeechInRange = bInSpeechVisible;
	UpdateLabelVisibility();
}

void AMTALivingNPC::UpdateLabelVisibility()
{
	const bool bBound = AgentIndex != INDEX_NONE;
	if (NameLabel)
	{
		NameLabel->SetVisibility(bBound && bNameInRange);
	}
	if (ActivityLabel)
	{
		ActivityLabel->SetVisibility(bBound && bNameInRange && !ActivityText.IsEmpty());
	}
	if (SpeechLabel)
	{
		SpeechLabel->SetVisibility(bBound && bSpeechInRange && IsSpeaking());
	}
}

void AMTALivingNPC::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (SpeechTimeLeft > 0.f)
	{
		SpeechTimeLeft -= DeltaSeconds;
		if (SpeechTimeLeft <= 0.f)
		{
			SpeechText.Reset();
			UpdateLabelVisibility();
		}
	}

	// Schilder zur Kamera drehen (nur wenn eines sichtbar ist)
	if (!bNameInRange && !(bSpeechInRange && IsSpeaking()))
	{
		return;
	}
	if (const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		const FVector ToCam = Cam->GetCameraLocation() - GetActorLocation();
		const FRotator Facing(0.f, ToCam.Rotation().Yaw, 0.f);
		if (NameLabel) { NameLabel->SetWorldRotation(Facing); }
		if (ActivityLabel) { ActivityLabel->SetWorldRotation(Facing); }
		if (SpeechLabel) { SpeechLabel->SetWorldRotation(Facing); }
	}
}

// --- IMTAInteractable -------------------------------------------------------------------------

FMTAInteractionInfo AMTALivingNPC::GetInteractionInfo_Implementation(AActor* Interactor) const
{
	FMTAInteractionInfo Info;
	Info.Kind = EMTAInteractionKind::Generic;
	if (bHasDialogue)
	{
		Info.Prompt = FText::FromString(FString::Printf(TEXT("Mit %s sprechen"), *DisplayName));
		Info.Priority = 5;
		const UGameInstance* GI = GetGameInstance();
		const UMTANPCSubsystem* Npcs = GI ? GI->GetSubsystem<UMTANPCSubsystem>() : nullptr;
		if (Npcs && !Npcs->IsAvailableForDialogue(NPCId))
		{
			Info.SubText = FText::FromString(TEXT("hat gerade keine Zeit"));
		}
	}
	else
	{
		Info.Prompt = FText::FromString(FString::Printf(TEXT("%s ansprechen"), *DisplayName));
		Info.Priority = 3;
	}
	return Info;
}

bool AMTALivingNPC::CanInteract_Implementation(AActor* Interactor) const
{
	if (AgentIndex == INDEX_NONE)
	{
		return false;
	}
	const UGameInstance* GI = GetGameInstance();
	const UMTADialogueSubsystem* Dialog = GI ? GI->GetSubsystem<UMTADialogueSubsystem>() : nullptr;
	return !Dialog || !Dialog->IsInConversation();
}

void AMTALivingNPC::Interact_Implementation(AActor* Interactor)
{
	if (AMTALivingWorldManager* M = LivingManager.Get())
	{
		M->NotifyPlayerInteract(AgentIndex, Interactor);
	}
}

// --- IMTANPCActor -----------------------------------------------------------------------------

FName AMTALivingNPC::GetNPCId_Implementation() const
{
	return NPCId;
}

void AMTALivingNPC::ApplyAppearance_Implementation(FName ArchetypeId)
{
	AppearanceArchetype = ArchetypeId;
}

void AMTALivingNPC::OnActivityChanged_Implementation(const FMTAScheduleState& NewState)
{
	if (AMTALivingWorldManager* M = LivingManager.Get())
	{
		M->NotifyActivityChanged(AgentIndex);
	}
}

void AMTALivingNPC::PlayBark_Implementation(const FString& Text, FName Category)
{
	if (AMTALivingWorldManager* M = LivingManager.Get())
	{
		M->NotifyBarkShown(AgentIndex, Text, Category);
	}
	else
	{
		ShowSpeech(Text, 3.5f);
	}
}

void AMTALivingNPC::PlayInteraction_Implementation(const FMTANPCInteractionEvent& Event)
{
	if (AMTALivingWorldManager* M = LivingManager.Get())
	{
		M->NotifyInteractionEvent(AgentIndex, Event);
	}
}

void AMTALivingNPC::OnConversationStateChanged_Implementation(bool bInConversation)
{
	if (AMTALivingWorldManager* M = LivingManager.Get())
	{
		M->NotifyConversationState(AgentIndex, bInConversation);
	}
}
