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
#include "Misc/Crc.h"
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

	// Aussehen aus Kennung und Fraktion (Fraktion steht im Agenten-Datensatz des Managers).
	const FMTALivingAgent* AgentData = InManager ? InManager->GetAgent(InAgentIndex) : nullptr;
	ApplyLookFromId(InId, AgentData ? AgentData->Faction : NAME_None, InScale);
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

bool AMTALivingNPC::GetFactionLook(FName InFaction, FLinearColor& OutAccent, FLinearColor& OutBodyTint, float& OutBodyBlend)
{
	// Fraktions-IDs wie in story.json. Akzent = Logo/Brust, BodyBlend = Anteil der Koerpertoenung.
	const FString Key = InFaction.ToString().ToLower();
	if (Key == TEXT("aurelia"))
	{
		OutAccent = FLinearColor(0.62f, 0.65f, 0.70f);    // silber
		OutBodyTint = FLinearColor(0.02f, 0.04f, 0.14f);  // dunkelblau
		OutBodyBlend = 0.8f;
		return true;
	}
	if (Key == TEXT("kcg"))
	{
		OutAccent = FLinearColor(0.08f, 0.08f, 0.09f);    // schwarz/anthrazit
		OutBodyTint = FLinearColor(0.012f, 0.012f, 0.014f); // schwarz
		OutBodyBlend = 0.85f;
		return true;
	}
	if (Key == TEXT("union"))
	{
		OutAccent = FLinearColor(1.0f, 0.32f, 0.02f);     // orange
		OutBodyTint = FLinearColor(0.75f, 0.24f, 0.02f);
		OutBodyBlend = 0.3f;
		return true;
	}
	if (Key == TEXT("collective"))
	{
		OutAccent = FLinearColor(0.0f, 0.62f, 0.56f);     // tuerkis
		OutBodyTint = FLinearColor(0.0f, 0.42f, 0.38f);
		OutBodyBlend = 0.3f;
		return true;
	}
	if (Key == TEXT("wrenchers"))
	{
		OutAccent = FLinearColor(0.72f, 0.02f, 0.02f);    // rot
		OutBodyTint = FLinearColor(0.45f, 0.02f, 0.02f);
		OutBodyBlend = 0.3f;
		return true;
	}
	return false;
}

void AMTALivingNPC::ApplyLookFromId(FName InId, FName InFaction, float InScale)
{
	// Deterministisch: CRC der kleingeschriebenen Kennung (FName-Hashes sind nicht sitzungsstabil).
	const FString Key = InId.ToString().ToLower();
	const uint32 Hash = FCrc::StrCrc32(*Key);

	// Kleidungs-/Koerperfarben (lineare Werte, gedeckt, keine Rollenzuordnung).
	static const FLinearColor BodyPalette[] = {
		FLinearColor(0.42f, 0.36f, 0.28f), FLinearColor(0.10f, 0.12f, 0.16f), FLinearColor(0.30f, 0.30f, 0.32f),
		FLinearColor(0.05f, 0.10f, 0.25f), FLinearColor(0.22f, 0.27f, 0.12f), FLinearColor(0.35f, 0.08f, 0.08f),
		FLinearColor(0.60f, 0.55f, 0.45f), FLinearColor(0.15f, 0.25f, 0.35f), FLinearColor(0.45f, 0.20f, 0.30f),
		FLinearColor(0.70f, 0.70f, 0.72f), FLinearColor(0.08f, 0.08f, 0.08f), FLinearColor(0.40f, 0.30f, 0.10f) };
	static const FLinearColor AccentPalette[] = {
		FLinearColor(0.80f, 0.80f, 0.80f), FLinearColor(0.70f, 0.55f, 0.10f), FLinearColor(0.10f, 0.35f, 0.70f),
		FLinearColor(0.15f, 0.55f, 0.25f), FLinearColor(0.60f, 0.15f, 0.40f), FLinearColor(0.05f, 0.05f, 0.05f) };

	FLinearColor BodyCol = BodyPalette[Hash % UE_ARRAY_COUNT(BodyPalette)];
	FLinearColor AccentCol = AccentPalette[(Hash >> 8) % UE_ARRAY_COUNT(AccentPalette)];

	FLinearColor FactionAccent;
	FLinearColor FactionTint;
	float FactionBlend = 0.f;
	if (GetFactionLook(InFaction, FactionAccent, FactionTint, FactionBlend))
	{
		AccentCol = FactionAccent;
		// Leichte Streuung, damit Fraktionsmitglieder nicht uniform wirken.
		const float Jitter = static_cast<float>((Hash >> 20) % 21u) / 100.f - 0.1f;
		BodyCol = FMath::Lerp(BodyCol, FactionTint, FMath::Clamp(FactionBlend + Jitter, 0.f, 1.f));
		LookFaction = InFaction;
	}
	else
	{
		LookFaction = NAME_None;
	}
	LookBodyColor = BodyCol;
	LookAccentColor = AccentCol;

	// Groesse 0.93-1.07 (vom Manager vorgegeben, hier nur begrenzt); Statur nur am Mesh (Kapsel bleibt).
	SetActorScale3D(FVector(FMath::Clamp(InScale, 0.93f, 1.07f)));

	if (USkeletalMeshComponent* Body = GetMesh())
	{
		const float Build = 0.94f + static_cast<float>((Hash >> 12) % 13u) / 100.f;
		Body->SetRelativeScale3D(FVector(Build, Build, 1.f));

		// Mannequin-Material (M_UE4Man_Body / M_UE4Man_ChestLogo): BodyColor bzw. PlasticColor
		// (Grundfarbe), LogoColor (Brustlogo = Akzent). Fehlende Parameter werden ignoriert.
		const FVector BodyVec(BodyCol.R, BodyCol.G, BodyCol.B);
		const FVector AccentVec(AccentCol.R, AccentCol.G, AccentCol.B);
		Body->SetVectorParameterValueOnMaterials(TEXT("BodyColor"), BodyVec);
		Body->SetVectorParameterValueOnMaterials(TEXT("PlasticColor"), BodyVec);
		Body->SetVectorParameterValueOnMaterials(TEXT("LogoColor"), AccentVec);
	}
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
