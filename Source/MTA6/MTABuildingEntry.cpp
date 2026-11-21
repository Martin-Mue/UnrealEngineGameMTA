#include "MTABuildingEntry.h"
#include "MTAInteriorSubsystem.h"
#include "MTAInteriorData.h"
#include "MTAStorySubsystem.h"
#include "Components/SphereComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"

#define LOCTEXT_NAMESPACE "MTABuildingEntry"

AMTABuildingEntry::AMTABuildingEntry()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AMTABuildingEntry::BeginPlay()
{
	Super::BeginPlay();
	if (BuildingKey.IsNone())
	{
		const FVector L = GetActorLocation();
		BuildingKey = FName(*FString::Printf(TEXT("bld_%d_%d"), FMath::RoundToInt(L.X / 100.0), FMath::RoundToInt(L.Y / 100.0)));
	}
	if (Seed == 0) { Seed = (int32)GetTypeHash(BuildingKey); }
	ResolveTemplate();
	if (Entrances.Num() == 0) { AutoLayoutEntrances(GetActorForwardVector()); }
	if (UMTAInteriorSubsystem* Sub = UMTAInteriorSubsystem::Get(this))
	{
		for (int32 i = 0; i < Entrances.Num(); ++i) { Entrances[i].bUnlocked |= Sub->IsUnlocked(UnlockKey(i)); }
		Sub->RegisterEntry(this);
	}
	CreateTriggers();
}

void AMTABuildingEntry::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UMTAInteriorSubsystem* Sub = UMTAInteriorSubsystem::Get(this)) { Sub->UnregisterEntry(this); }
	Super::EndPlay(Reason);
}

void AMTABuildingEntry::ResolveTemplate()
{
	const UMTAInteriorSubsystem* Sub = UMTAInteriorSubsystem::Get(this);
	if (!Sub || !Sub->IsDataLoaded()) { return; }
	if (TemplateId.IsNone() || !Sub->GetDatabase().FindTemplate(TemplateId))
	{
		TemplateId = Sub->PickTemplateId(District, RoomType, Seed, LotSize);
	}
}

const FMTAInteriorTemplate* AMTABuildingEntry::GetTemplate() const
{
	const UMTAInteriorSubsystem* Sub = UMTAInteriorSubsystem::Get(this);
	return Sub ? Sub->GetDatabase().FindTemplate(TemplateId) : nullptr;
}

FText AMTABuildingEntry::GetDisplayName() const
{
	const FMTAInteriorTemplate* T = GetTemplate();
	return T ? FText::FromString(T->Label) : LOCTEXT("Building", "Gebaeude");
}

void AMTABuildingEntry::AutoLayoutEntrances(FVector StreetDir)
{
	StreetDirection = StreetDir.GetSafeNormal2D();
	if (StreetDirection.IsNearlyZero()) { StreetDirection = GetActorForwardVector(); }
	// In Actor-Raum rechnen; Parzelle als achsenparalleles Rechteck um den Ursprung
	const FVector Dir = GetActorTransform().InverseTransformVectorNoScale(StreetDirection).GetSafeNormal2D();
	const FVector Perp(-Dir.Y, Dir.X, 0.f);
	auto HalfAlong = [this](const FVector& D) { return FMath::Abs(D.X) * LotSize.X * 0.5f + FMath::Abs(D.Y) * LotSize.Y * 0.5f; };

	TArray<EMTAEntranceKind> Kinds = { EMTAEntranceKind::Front };
	bool bVehicles = false;
	if (const FMTAInteriorTemplate* T = GetTemplate())
	{
		Kinds = T->GetEntranceKinds();
		bVehicles = T->AllowsVehicles();
		if (Kinds.Num() == 0) { Kinds.Add(EMTAEntranceKind::Front); }
	}

	Entrances.Reset();
	for (const EMTAEntranceKind K : Kinds)
	{
		FMTAEntrance E;
		E.Kind = K;
		FVector Out = Dir;
		float Extra = 120.f;
		float Z = 0.f;
		switch (K)
		{
		case EMTAEntranceKind::Back:      Out = -Dir; break;
		case EMTAEntranceKind::Emergency: Out = Perp; break;
		case EMTAEntranceKind::Garage:    Out = -Perp; Extra = 700.f; E.bAllowsVehicles = bVehicles; E.UseRadius = 400.f; break;
		case EMTAEntranceKind::Roof:      Out = Dir; Extra = -HalfAlong(Dir) * 0.5f; Z = BuildingHeight + 20.f; break;
		default: break;
		}
		const FVector Loc = Out * (HalfAlong(Out) + Extra) + FVector(0.f, 0.f, Z + 100.f);
		E.Exterior = FTransform(Out.Rotation(), Loc);
		Entrances.Add(E);
	}
}

void AMTABuildingEntry::CreateTriggers()
{
	for (int32 i = 0; i < Entrances.Num(); ++i)
	{
		USphereComponent* S = NewObject<USphereComponent>(this, FName(*FString::Printf(TEXT("EntranceTrigger_%d"), i)));
		S->SetupAttachment(Root);
		S->SetRelativeTransform(Entrances[i].Exterior);
		S->SetSphereRadius(Entrances[i].UseRadius);
		S->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		S->SetCollisionResponseToAllChannels(ECR_Ignore);
		S->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		S->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Overlap);
		S->SetGenerateOverlapEvents(true);
		S->SetCanEverAffectNavigation(false);
		S->ComponentTags.Add(FName(*FString::Printf(TEXT("entrance_%d"), i)));
		S->OnComponentBeginOverlap.AddDynamic(this, &AMTABuildingEntry::OnTriggerBeginOverlap);
		S->RegisterComponent();
		AddInstanceComponent(S);
		Triggers.Add(S);
	}
}

void AMTABuildingEntry::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// Fahrzeuge fahren in Garagen einfach hinein; Fussgaenger nutzen die Interaktionstaste
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || Pawn->IsA<ACharacter>() || !Pawn->IsPlayerControlled()) { return; }
	const int32 Index = Triggers.IndexOfByKey(Cast<USphereComponent>(OverlappedComp));
	if (!Entrances.IsValidIndex(Index) || !Entrances[Index].bAllowsVehicles) { return; }
	const UMTAInteriorSubsystem* Sub = UMTAInteriorSubsystem::Get(this);
	if (Sub && !Sub->IsTravelCoolingDown(Pawn)) { EnterVia(Pawn, Index); }
}

FName AMTABuildingEntry::UnlockKey(int32 EntranceIndex) const
{
	return FName(*FString::Printf(TEXT("%s#entrance%d"), *BuildingKey.ToString(), EntranceIndex));
}

FMTAAccessRule AMTABuildingEntry::GetEffectiveAccess(int32 EntranceIndex) const
{
	const FMTAEntrance& E = Entrances[EntranceIndex];
	if (E.bOverrideAccess) { return E.Access; }
	const FMTAInteriorTemplate* T = GetTemplate();
	FMTAAccessRule A = T ? T->Access : FMTAAccessRule();
	switch (E.Kind)
	{
	case EMTAEntranceKind::Back:
		A.HackDifficulty = FMath::Max(1, A.HackDifficulty);
		break;
	case EMTAEntranceKind::Roof:
		// Dachzugang: nie "geoeffnet", aber leichter zu knacken (Schleichweg)
		A.bHasOpeningHours = false;
		A.HackDifficulty = FMath::Max(1, A.HackDifficulty - 1);
		break;
	case EMTAEntranceKind::Emergency:
		A.bExitOnly = true;
		A.HackDifficulty = A.HackDifficulty > 0 ? FMath::Min(5, A.HackDifficulty + 1) : 2;
		break;
	default:
		break;
	}
	return A;
}

EMTALockState AMTABuildingEntry::EvaluateAccess(int32 EntranceIndex, APawn* Pawn, FText& OutReason) const
{
	if (!Entrances.IsValidIndex(EntranceIndex)) { OutReason = FText::GetEmpty(); return EMTALockState::Sealed; }
	const FMTAEntrance& E = Entrances[EntranceIndex];
	const FMTAAccessRule A = GetEffectiveAccess(EntranceIndex);
	const UMTAInteriorSubsystem* Sub = UMTAInteriorSubsystem::Get(this);

	if (Pawn && !Pawn->IsA<ACharacter>() && !E.bAllowsVehicles)
	{
		OutReason = LOCTEXT("OnFoot", "Nur zu Fuss");
		return EMTALockState::VehicleDenied;
	}
	if (E.bUnlocked || (Sub && Sub->IsUnlocked(UnlockKey(EntranceIndex)))) { OutReason = FText::GetEmpty(); return EMTALockState::Open; }
	if (!A.KeyId.IsNone() && UMTAInteriorSubsystem::PawnHasKey(Pawn, A.KeyId)) { OutReason = FText::GetEmpty(); return EMTALockState::Open; }
	if (A.bExitOnly) { OutReason = LOCTEXT("ExitOnly", "Notausgang - nur von innen"); return EMTALockState::ExitOnly; }
	if (!A.Faction.IsEmpty())
	{
		const UGameInstance* GI = GetGameInstance();
		const UMTAStorySubsystem* Story = GI ? GI->GetSubsystem<UMTAStorySubsystem>() : nullptr;
		const float Standing = Story ? Story->GetStanding(A.Faction) : 0.f;
		if (Standing < A.MinStanding)
		{
			OutReason = FText::Format(LOCTEXT("Faction", "Kein Zutritt ({0})"), FText::FromString(A.Faction));
			return EMTALockState::FactionDenied;
		}
	}
	if (A.bHasOpeningHours)
	{
		const float Hour = Sub ? Sub->GetHourOfDay() : 12.f;
		if (MTAInterior::IsHourInWindow(Hour, A.OpenHour, A.CloseHour)) { OutReason = FText::GetEmpty(); return EMTALockState::Open; }
		OutReason = FText::Format(LOCTEXT("Closed", "Geschlossen - oeffnet um {0}:00"), FMath::FloorToInt(A.OpenHour));
		return EMTALockState::ClosedHours;
	}
	if (!A.KeyId.IsNone()) { OutReason = LOCTEXT("NeedsKey", "Verschlossen - Schluessel noetig"); return EMTALockState::NeedsKey; }
	if (!A.Faction.IsEmpty()) { OutReason = FText::GetEmpty(); return EMTALockState::Open; }
	OutReason = LOCTEXT("Sealed", "Verschlossen");
	return EMTALockState::Sealed;
}

int32 AMTABuildingEntry::FindEntranceFor(const APawn* Pawn) const
{
	if (!Pawn) { return INDEX_NONE; }
	int32 Best = INDEX_NONE;
	double BestD = TNumericLimits<double>::Max();
	const FVector P = Pawn->GetActorLocation();
	for (int32 i = 0; i < Entrances.Num(); ++i)
	{
		const FVector L = GetEntranceExteriorTransform(i).GetLocation();
		const double R = Entrances[i].UseRadius + 150.0;
		const double D = FVector::DistSquared2D(P, L);
		if (D < R * R && FMath::Abs(P.Z - L.Z) < 400.0 && D < BestD) { BestD = D; Best = i; }
	}
	return Best;
}

bool AMTABuildingEntry::CanEnter(APawn* Pawn, FText& OutReason) const
{
	const int32 Index = FindEntranceFor(Pawn);
	if (Index == INDEX_NONE) { OutReason = LOCTEXT("TooFar", "Zu weit weg"); return false; }
	return EvaluateAccess(Index, Pawn, OutReason) == EMTALockState::Open;
}

FText AMTABuildingEntry::GetEnterPrompt(APawn* Pawn) const
{
	const int32 Index = FindEntranceFor(Pawn);
	if (Index == INDEX_NONE) { return FText::GetEmpty(); }
	FText Reason;
	const EMTALockState State = EvaluateAccess(Index, Pawn, Reason);
	if (State == EMTALockState::Open) { return FText::Format(LOCTEXT("Enter", "Betreten: {0}"), GetDisplayName()); }
	const FMTAAccessRule A = GetEffectiveAccess(Index);
	if (A.HackDifficulty > 0 && State != EMTALockState::VehicleDenied)
	{
		return FText::Format(LOCTEXT("Hack", "{0} - Schloss hacken (Stufe {1})"), Reason, A.HackDifficulty);
	}
	return Reason;
}

bool AMTABuildingEntry::Enter(APawn* Pawn)
{
	return EnterVia(Pawn, FindEntranceFor(Pawn));
}

bool AMTABuildingEntry::EnterVia(APawn* Pawn, int32 EntranceIndex)
{
	if (!Pawn || !Entrances.IsValidIndex(EntranceIndex)) { return false; }
	UMTAInteriorSubsystem* Sub = UMTAInteriorSubsystem::Get(this);
	if (!Sub) { return false; }
	FText Reason;
	const EMTALockState State = EvaluateAccess(EntranceIndex, Pawn, Reason);
	if (State == EMTALockState::Open)
	{
		if (!Sub->EnterInterior(Pawn, this, EntranceIndex)) { return false; }
		OnEntered.Broadcast(Pawn, EntranceIndex);
		return true;
	}
	const FMTAAccessRule A = GetEffectiveAccess(EntranceIndex);
	if (A.HackDifficulty > 0 && State != EMTALockState::VehicleDenied)
	{
		PendingHackPawn = Pawn;
		PendingHackEntrance = EntranceIndex;
		Sub->RequestHack(this, A.HackDifficulty, UnlockKey(EntranceIndex));
		return true;
	}
	OnDenied.Broadcast(Pawn, EntranceIndex, State);
	return false;
}

void AMTABuildingEntry::OnHackResolved(bool bSuccess, FName ContextId)
{
	APawn* Pawn = PendingHackPawn.Get();
	const int32 Index = PendingHackEntrance;
	PendingHackPawn = nullptr;
	PendingHackEntrance = INDEX_NONE;
	if (!bSuccess || !Entrances.IsValidIndex(Index))
	{
		if (Pawn) { OnDenied.Broadcast(Pawn, Index, EMTALockState::Sealed); }
		return;
	}
	UnlockEntrance(Index);
	if (Pawn) { EnterVia(Pawn, Index); }
}

void AMTABuildingEntry::UnlockEntrance(int32 EntranceIndex)
{
	if (!Entrances.IsValidIndex(EntranceIndex)) { return; }
	Entrances[EntranceIndex].bUnlocked = true;
	if (UMTAInteriorSubsystem* Sub = UMTAInteriorSubsystem::Get(this)) { Sub->MarkUnlocked(UnlockKey(EntranceIndex)); }
}

int32 AMTABuildingEntry::FindEntranceIndexByKind(EMTAEntranceKind Kind) const
{
	return Entrances.IndexOfByPredicate([Kind](const FMTAEntrance& E) { return E.Kind == Kind; });
}

FTransform AMTABuildingEntry::GetEntranceExteriorTransform(int32 EntranceIndex) const
{
	return Entrances.IsValidIndex(EntranceIndex) ? Entrances[EntranceIndex].Exterior * GetActorTransform() : GetActorTransform();
}

FMTAInteriorRequest AMTABuildingEntry::MakeInteriorRequest() const
{
	FMTAInteriorRequest R;
	R.EntryKey = BuildingKey;
	R.TemplateId = TemplateId;
	R.District = District;
	R.Seed = Seed;
	R.StyleOverride = StyleOverride;
	R.Anchor = GetActorLocation();
	return R;
}

AMTABuildingEntry* AMTABuildingEntry::SpawnForLot(UWorld* World, const FVector& LotCenter, const FVector2D& LotSizeCm, float HeightCm,
	const FVector& StreetDir, EMTADistrict InDistrict, int32 InSeed, FName InRoomType)
{
	if (!World) { return nullptr; }
	const FVector Dir = StreetDir.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : StreetDir.GetSafeNormal2D();
	const FTransform Xf(Dir.Rotation(), LotCenter);
	AMTABuildingEntry* E = World->SpawnActorDeferred<AMTABuildingEntry>(AMTABuildingEntry::StaticClass(), Xf, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!E) { return nullptr; }
	// Parzelle im Actor-Raum: X zeigt zur Strasse, daher Breite/Tiefe passend tauschen
	const bool bAlongX = FMath::Abs(Dir.X) >= FMath::Abs(Dir.Y);
	E->LotSize = bAlongX ? LotSizeCm : FVector2D(LotSizeCm.Y, LotSizeCm.X);
	E->BuildingHeight = HeightCm;
	E->District = InDistrict;
	E->Seed = InSeed;
	E->RoomType = InRoomType;
	E->FinishSpawning(Xf);
	return E;
}

#undef LOCTEXT_NAMESPACE
