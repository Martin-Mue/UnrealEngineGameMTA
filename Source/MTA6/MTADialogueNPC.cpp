#include "MTADialogueNPC.h"

#include "MTADialogueSubsystem.h"

#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

AMTADialogueNPC::AMTADialogueNPC()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCapsuleComponent()->InitCapsuleSize(38.f, 92.f);
	GetCharacterMovement()->MaxWalkSpeed = 0.f;

	// Figur: Mannequin aus den Epic-Vorlagen (Content/Mannequin)
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		static ConstructorHelpers::FObjectFinderOptional<USkeletalMesh> NpcMesh(TEXT("/Game/Mannequin/Character/Mesh/SK_Mannequin.SK_Mannequin"));
		static ConstructorHelpers::FClassFinder<UAnimInstance> NpcAnim(TEXT("/Game/Mannequin/Animations/ThirdPerson_AnimBP"));
		if (NpcMesh.Succeeded())
		{
			Body->SetSkeletalMesh(NpcMesh.Get());
		}
		if (NpcAnim.Succeeded())
		{
			Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
			Body->SetAnimInstanceClass(NpcAnim.Class);
		}
		Body->SetRelativeLocation(FVector(0.f, 0.f, -92.f));
		Body->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	}

	NameLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NameLabel"));
	NameLabel->SetupAttachment(RootComponent);
	NameLabel->SetRelativeLocation(FVector(0.f, 0.f, 125.f));
	NameLabel->SetHorizontalAlignment(EHTA_Center);
	NameLabel->SetWorldSize(30.f);
	NameLabel->SetTextRenderColor(FColor(255, 214, 90));
	NameLabel->SetText(FText::FromString(TEXT("NPC")));
}

void AMTADialogueNPC::SetupNPC(FName InNPCId, const FString& InDisplayName)
{
	NPCId = InNPCId;
	DisplayName = InDisplayName;
	if (NameLabel)
	{
		NameLabel->SetText(FText::FromString(DisplayName));
	}
}

void AMTADialogueNPC::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Namensschild zur Kamera drehen
	if (NameLabel)
	{
		if (const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
		{
			const FVector ToCam = Cam->GetCameraLocation() - NameLabel->GetComponentLocation();
			NameLabel->SetWorldRotation(FRotator(0.f, ToCam.Rotation().Yaw, 0.f));
		}
	}
}

FMTAInteractionInfo AMTADialogueNPC::GetInteractionInfo_Implementation(AActor* Interactor) const
{
	FMTAInteractionInfo Info;
	Info.Prompt = FText::FromString(FString::Printf(TEXT("Mit %s sprechen"), *DisplayName));
	Info.Kind = EMTAInteractionKind::Generic;
	Info.Priority = 5;
	return Info;
}

bool AMTADialogueNPC::CanInteract_Implementation(AActor* Interactor) const
{
	const UGameInstance* GI = GetGameInstance();
	const UMTADialogueSubsystem* Dialog = GI ? GI->GetSubsystem<UMTADialogueSubsystem>() : nullptr;
	return Dialog && !Dialog->IsInConversation();
}

void AMTADialogueNPC::Interact_Implementation(AActor* Interactor)
{
	UGameInstance* GI = GetGameInstance();
	UMTADialogueSubsystem* Dialog = GI ? GI->GetSubsystem<UMTADialogueSubsystem>() : nullptr;
	if (!Dialog)
	{
		return;
	}
	// Dem Spieler zuwenden
	if (Interactor)
	{
		const FVector To = Interactor->GetActorLocation() - GetActorLocation();
		SetActorRotation(FRotator(0.f, To.Rotation().Yaw, 0.f));
	}
	Dialog->StartConversation(NPCId);
}
