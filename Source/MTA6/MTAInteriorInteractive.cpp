#include "MTAInteriorInteractive.h"
#include "MTAInteriorInstance.h"
#include "MTAInteriorSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Materials/MaterialInterface.h"

#define LOCTEXT_NAMESPACE "MTAInterior"

AMTAInteriorInteractive::AMTAInteriorInteractive()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(Root);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Hinge);
	Mesh->SetCollisionProfileName(TEXT("NoCollision"));
	Mesh->SetVisibility(false);

	UseVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("UseVolume"));
	UseVolume->SetupAttachment(Root);
	UseVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	UseVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	UseVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	UseVolume->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Overlap);
	UseVolume->SetGenerateOverlapEvents(true);
	UseVolume->SetCanEverAffectNavigation(false);
}

void AMTAInteriorInteractive::ApplyVisual(const FVector& Size, const FVector& Offset, const FLinearColor& Color, float Emissive, int32 ShapeIndex)
{
	AMTAInteriorInstance* Inst = Interior.Get();
	if (!Inst || !Inst->ShapeMeshes.IsValidIndex(ShapeIndex)) { return; }
	Mesh->SetStaticMesh(Inst->ShapeMeshes[ShapeIndex]);
	if (Inst->InteractiveMaterial) { Mesh->SetMaterial(0, Inst->InteractiveMaterial); }
	Mesh->SetRelativeLocation(Offset);
	Mesh->SetRelativeScale3D(Size / 100.f);
	Mesh->SetCustomPrimitiveDataVector4(0, FVector4(Color.R, Color.G, Color.B, Emissive));
	Mesh->SetVisibility(true);
}

void AMTAInteriorInteractive::Setup(const FMTAInteractiveSpec& Spec, AMTAInteriorInstance* InInterior)
{
	Interior = InInterior;
	Kind = Spec.Kind;
	InteractiveId = Spec.Id;
	Links = Spec.Links;
	KeyId = Spec.KeyId;
	HackDifficulty = Spec.HackDifficulty;
	EntranceKind = Spec.Entrance;
	RoomIndex = Spec.RoomIndex;
	LocalSecretId = Spec.SecretId;
	SecretKind = Spec.SecretKind;
	GlobalSecretId = (InInterior && !Spec.SecretId.IsNone()) ? InInterior->MakeGlobalSecretId(Spec.SecretId) : NAME_None;
	bConcealed = Spec.bHidden;
	bLocked = Kind == EMTAInteractiveKind::Door && (!KeyId.IsNone() || HackDifficulty > 0);
	if (bLocked)
	{
		// Frueher gehackte/aufgeschlossene Tueren bleiben offen
		if (const UMTAInteriorSubsystem* Sub = UMTAInteriorSubsystem::Get(this)) { bLocked = !Sub->IsUnlocked(GlobalUnlockKey()); }
	}

	const float W = Spec.Size.X, H = Spec.Size.Y;
	switch (Kind)
	{
	case EMTAInteractiveKind::Door:
	{
		// Scharnier an einer Zargenseite; das Blatt schwenkt nach innen (+X)
		Hinge->SetRelativeLocation(FVector(0.f, -W * 0.5f + 4.f, 0.f));
		ApplyVisual(FVector(5.f, W - 8.f, H - 3.f), FVector(0.f, (W - 8.f) * 0.5f, H * 0.5f),
			bLocked ? FLinearColor(0.25f, 0.12f, 0.1f) : FLinearColor(0.3f, 0.22f, 0.15f), 0.f, (int32)EMTAPrimShape::Cube);
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		UseVolume->SetRelativeLocation(FVector(0.f, 0.f, H * 0.5f));
		UseVolume->SetBoxExtent(FVector(90.f, W * 0.5f, H * 0.5f));
		break;
	}
	case EMTAInteractiveKind::Secret:
	{
		const FLinearColor C = SecretKind == FName(TEXT("note")) ? FLinearColor(0.9f, 0.9f, 0.85f)
			: SecretKind == FName(TEXT("stash")) ? FLinearColor(0.2f, 0.9f, 0.4f) : FLinearColor(1.f, 0.75f, 0.2f);
		ApplyVisual(FVector(22.f), FVector::ZeroVector, C, 3.f, (int32)EMTAPrimShape::Sphere);
		UseVolume->SetBoxExtent(FVector(70.f, 70.f, 80.f));
		SetActorTickEnabled(true);
		break;
	}
	case EMTAInteractiveKind::Exit:
		UseVolume->SetRelativeLocation(FVector(0.f, 0.f, H * 0.5f));
		UseVolume->SetBoxExtent(FVector(EntranceKind == EMTAEntranceKind::Garage ? 150.f : 70.f, W * 0.5f, H * 0.5f));
		break;
	default:
		// Schalter, Terminal, Versteck: Optik kommt vom Moebelstueck, hier nur das Nutzvolumen
		UseVolume->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
		UseVolume->SetBoxExtent(FVector(W * 0.5f + 60.f, W * 0.5f + 60.f, 100.f));
		break;
	}
	RefreshVisibility();
}

void AMTAInteriorInteractive::RefreshVisibility()
{
	const bool bShowMesh = (Kind == EMTAInteractiveKind::Door || Kind == EMTAInteractiveKind::Secret) && !bDormantState && !bConcealed;
	Mesh->SetVisibility(bShowMesh);
	UseVolume->SetCollisionEnabled((bDormantState || bConcealed) ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
}

void AMTAInteriorInteractive::SetDormant(bool bDormant)
{
	bDormantState = bDormant;
	SetActorHiddenInGame(bDormant);
	SetActorEnableCollision(!bDormant);
	RefreshVisibility();
}

bool AMTAInteriorInteractive::PawnHasKey(const APawn* Pawn) const
{
	return UMTAInteriorSubsystem::PawnHasKey(Pawn, KeyId);
}

FText AMTAInteriorInteractive::GetUsePrompt(APawn* Pawn) const
{
	switch (Kind)
	{
	case EMTAInteractiveKind::Door:
		if (bLocked)
		{
			if (PawnHasKey(Pawn)) return LOCTEXT("DoorUnlock", "Aufschliessen");
			if (HackDifficulty > 0) return FText::Format(LOCTEXT("DoorHack", "Schloss hacken (Stufe {0})"), HackDifficulty);
			return LOCTEXT("DoorLocked", "Verschlossen");
		}
		return bOpen ? LOCTEXT("DoorClose", "Tuer schliessen") : LOCTEXT("DoorOpen", "Tuer oeffnen");
	case EMTAInteractiveKind::Switch:   return LOCTEXT("Switch", "Licht schalten");
	case EMTAInteractiveKind::Terminal:
		return (HackDifficulty > 0 && !bHacked) ? FText::Format(LOCTEXT("TermHack", "Terminal hacken (Stufe {0})"), HackDifficulty) : LOCTEXT("TermUse", "Terminal benutzen");
	case EMTAInteractiveKind::HidingSpot:
		return HiddenPawn.IsValid() ? LOCTEXT("Unhide", "Versteck verlassen") : LOCTEXT("Hide", "Verstecken");
	case EMTAInteractiveKind::Secret:
		return SecretKind == FName(TEXT("note")) ? LOCTEXT("ReadNote", "Notiz lesen") : LOCTEXT("TakeSecret", "Aufheben");
	case EMTAInteractiveKind::Exit:     return LOCTEXT("Exit", "Hinausgehen");
	default:                            return FText::GetEmpty();
	}
}

bool AMTAInteriorInteractive::CanUse(APawn* Pawn) const
{
	if (!Pawn || bDormantState || bConcealed) { return false; }
	if (Kind == EMTAInteractiveKind::Door && bLocked) { return PawnHasKey(Pawn) || HackDifficulty > 0; }
	if (Kind == EMTAInteractiveKind::HidingSpot && HiddenPawn.IsValid()) { return HiddenPawn.Get() == Pawn; }
	return true;
}

bool AMTAInteriorInteractive::Use(APawn* Pawn)
{
	if (!CanUse(Pawn)) { return false; }
	UMTAInteriorSubsystem* Sub = UMTAInteriorSubsystem::Get(this);
	AMTAInteriorInstance* Inst = Interior.Get();

	switch (Kind)
	{
	case EMTAInteractiveKind::Door:
		if (bLocked)
		{
			if (PawnHasKey(Pawn)) { bLocked = false; }
			else if (Sub)
			{
				PendingHackPawn = Pawn;
				Sub->RequestHack(this, HackDifficulty, InteractiveId);
				return true;
			}
			else { return false; }
		}
		bOpen = !bOpen;
		DoorTarget = bOpen ? -95.f : 0.f;
		SetActorTickEnabled(true);
		return true;

	case EMTAInteractiveKind::Switch:
		if (Inst)
		{
			Inst->ToggleRoomLights(RoomIndex);
			Inst->ActivateLink(Links);
		}
		return true;

	case EMTAInteractiveKind::Terminal:
		if (HackDifficulty > 0 && !bHacked && Sub)
		{
			PendingHackPawn = Pawn;
			Sub->RequestHack(this, HackDifficulty, InteractiveId);
			return true;
		}
		if (Inst) { Inst->ActivateLink(Links); }
		return true;

	case EMTAInteractiveKind::HidingSpot:
	{
		const bool bHide = !HiddenPawn.IsValid();
		HiddenPawn = bHide ? Pawn : nullptr;
		Pawn->SetActorHiddenInGame(bHide);
		if (Sub) { Sub->NotifyHidingChanged(Pawn, bHide, this); }
		return true;
	}

	case EMTAInteractiveKind::Secret:
		if (Sub) { Sub->ReportSecretFound(GlobalSecretId.IsNone() ? InteractiveId : GlobalSecretId); }
		Destroy();
		return true;

	case EMTAInteractiveKind::Exit:
		return Sub && Inst && Sub->ExitInterior(Pawn, Inst, EntranceKind);

	default:
		return false;
	}
}

void AMTAInteriorInteractive::OnHackResolved(bool bSuccess, FName ContextId)
{
	APawn* Pawn = PendingHackPawn.Get();
	PendingHackPawn = nullptr;
	if (!bSuccess) { return; }
	bHacked = true;
	if (Kind == EMTAInteractiveKind::Door)
	{
		bLocked = false;
		if (UMTAInteriorSubsystem* Sub = UMTAInteriorSubsystem::Get(this)) { Sub->MarkUnlocked(GlobalUnlockKey()); }
		if (Pawn) { Use(Pawn); }
	}
	else if (Kind == EMTAInteractiveKind::Terminal)
	{
		if (AMTAInteriorInstance* Inst = Interior.Get()) { Inst->ActivateLink(Links); }
	}
}

FName AMTAInteriorInteractive::GlobalUnlockKey() const
{
	const AMTAInteriorInstance* Inst = Interior.Get();
	return FName(*FString::Printf(TEXT("%s#%s"), Inst ? *Inst->EntryKey.ToString() : TEXT("?"), *InteractiveId.ToString()));
}

void AMTAInteriorInteractive::ActivateFromLink()
{
	if (Kind == EMTAInteractiveKind::Door)
	{
		bLocked = false;
		bOpen = true;
		DoorTarget = -95.f;
		SetActorTickEnabled(true);
	}
	else if (Kind == EMTAInteractiveKind::Secret)
	{
		bConcealed = false;
		RefreshVisibility();
	}
}

void AMTAInteriorInteractive::NotifyActorBeginOverlap(AActor* OtherActor)
{
	Super::NotifyActorBeginOverlap(OtherActor);
	// Fahrzeuge verlassen Garagen durch Hineinfahren in das Ausgangsvolumen
	if (Kind != EMTAInteractiveKind::Exit || EntranceKind != EMTAEntranceKind::Garage) { return; }
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || Pawn->IsA<ACharacter>() || !Pawn->IsPlayerControlled()) { return; }
	UMTAInteriorSubsystem* Sub = UMTAInteriorSubsystem::Get(this);
	if (Sub && Interior.IsValid() && !Sub->IsTravelCoolingDown(Pawn)) { Sub->ExitInterior(Pawn, Interior.Get(), EntranceKind); }
}

void AMTAInteriorInteractive::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Kind == EMTAInteractiveKind::Door)
	{
		DoorAngle = FMath::FInterpConstantTo(DoorAngle, DoorTarget, DeltaSeconds, 240.f);
		Hinge->SetRelativeRotation(FRotator(0.f, DoorAngle, 0.f));
		if (FMath::IsNearlyEqual(DoorAngle, DoorTarget, 0.1f)) { SetActorTickEnabled(false); }
	}
	else if (Kind == EMTAInteractiveKind::Secret)
	{
		// Sanftes Schweben und Drehen, damit Sammelobjekte auffallen
		const float T = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
		Hinge->SetRelativeLocationAndRotation(FVector(0.f, 0.f, 8.f * FMath::Sin(T * 2.f)), FRotator(0.f, T * 60.f, 0.f));
	}
}

#undef LOCTEXT_NAMESPACE
