#include "MTAWildernessRegion.h"
#include "MTADiscoverySubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

AMTAWildernessRegion::AMTAWildernessRegion()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	RegionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("RegionBounds"));
	RegionBounds->SetupAttachment(Root);
	RegionBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RegionBounds->ShapeColor = FColor(60, 200, 90);

	WaterfallAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("WaterfallAnchor"));
	WaterfallAnchor->SetupAttachment(Root);

	SecretEntranceTrigger = CreateDefaultSubobject<UBoxComponent>(TEXT("SecretEntranceTrigger"));
	SecretEntranceTrigger->SetupAttachment(Root);
	SecretEntranceTrigger->InitBoxExtent(FVector(200.f, 200.f, 200.f));
	SecretEntranceTrigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	SecretEntranceTrigger->SetGenerateOverlapEvents(true);
	SecretEntranceTrigger->ShapeColor = FColor(255, 140, 40);

	// Standard-Biome gemaess wilderness.json (Planungswerte).
	auto AddBiome = [this](const TCHAR* Id, float Trees, float Under, float Rocks, float Lianas)
	{
		FMTABiomeFoliageParams B;
		B.BiomeId = Id;
		B.TreesPerHectare = Trees;
		B.Undergrowth = Under;
		B.Rocks = Rocks;
		B.Lianas = Lianas;
		Biomes.Add(B);
	};
	AddBiome(TEXT("auwald"), 220.f, 0.6f, 0.1f, 0.f);
	AddBiome(TEXT("nebelwald"), 420.f, 0.95f, 0.25f, 0.7f);
	AddBiome(TEXT("klamm"), 80.f, 0.3f, 0.9f, 0.f);
	AddBiome(TEXT("kammwald"), 150.f, 0.4f, 0.6f, 0.f);

	Landscape.HeightControlPointsM = {
		FVector(0, 0, 0), FVector(300, 400, 14), FVector(700, 900, 36), FVector(1000, 600, 48), FVector(1250, 1150, 78),
		FVector(1500, 1100, 90), FVector(1530, 1110, 128), FVector(1800, 1300, 150), FVector(2100, 1500, 185), FVector(2350, 1700, 215) };
}

void AMTAWildernessRegion::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyLayout();
}

void AMTAWildernessRegion::ApplyLayout()
{
	// Ursprung = Suedwestecke am Talboden; Box mittig ueber der Flaeche.
	const FVector HalfCm(SizeM.X * 50.0, SizeM.Y * 50.0, 15000.0);
	RegionBounds->SetBoxExtent(HalfCm, false);
	RegionBounds->SetRelativeLocation(FVector(HalfCm.X, HalfCm.Y, HalfCm.Z - 2000.0));
	WaterfallAnchor->SetRelativeLocation(WaterfallTopM * 100.0);
	SecretEntranceTrigger->SetRelativeLocation(SecretDoorM * 100.0);
}

FName AMTAWildernessRegion::StateKey() const
{
	return FName(*(RegionId.ToString() + TEXT(".entrance")));
}

FName AMTAWildernessRegion::SluiceKey() const
{
	return FName(*(RegionId.ToString() + TEXT(".sluice")));
}

UMTADiscoverySubsystem* AMTAWildernessRegion::GetDiscovery() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UMTADiscoverySubsystem>() : nullptr;
}

void AMTAWildernessRegion::BeginPlay()
{
	Super::BeginPlay();
	ApplyLayout();
	if (bLoadRulesFromJson) { LoadRulesFromJson(); }

	if (UMTADiscoverySubsystem* Disc = GetDiscovery())
	{
		Disc->RegisterRegion(RegionId, GetActorTransform());
		EntranceState = static_cast<EMTASecretEntranceState>(FMath::Clamp(Disc->GetRegionState(StateKey()), 0, 2));
	}

	// Soft-Assets synchron laden (kleine Zahl, einmalig); null, wenn nicht gesetzt.
	UObject* Fall = WaterfallSystem.IsNull() ? nullptr : WaterfallSystem.LoadSynchronous();
	UObject* Mist = MistSystem.IsNull() ? nullptr : MistSystem.LoadSynchronous();
	UObject* Sound = WaterfallSound.IsNull() ? nullptr : WaterfallSound.LoadSynchronous();
	OnSpawnWaterfallFX(Fall, Mist, Sound, WaterfallAnchor);
	OnWaterflowChanged(GetWaterflowScale());
	OnEntranceStateChanged(EntranceState, EntranceState);
}

void AMTAWildernessRegion::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMTADiscoverySubsystem* Disc = GetDiscovery())
	{
		Disc->UnregisterRegion(RegionId);
	}
	Super::EndPlay(EndPlayReason);
}

void AMTAWildernessRegion::LoadRulesFromJson()
{
	const FString File = FPaths::ProjectContentDir() / TEXT("Data/wilderness.json");
	FString Text;
	TSharedPtr<FJsonObject> JsonRoot;
	if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), JsonRoot) || !JsonRoot.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("MTA6: wilderness.json fuer Geheimeingang nicht lesbar"));
		return;
	}
	const TSharedPtr<FJsonObject>* Secret = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Transitions = nullptr;
	if (!JsonRoot->TryGetObjectField(TEXT("secretEntrance"), Secret) || !(*Secret)->TryGetArrayField(TEXT("transitions"), Transitions)) return;

	for (const TSharedPtr<FJsonValue>& V : *Transitions)
	{
		const TSharedPtr<FJsonObject> T = V->AsObject();
		if (!T.IsValid()) continue;
		double From = -1.0, To = -1.0, Radius = 0.0;
		T->TryGetNumberField(TEXT("from"), From);
		T->TryGetNumberField(TEXT("to"), To);
		const TArray<TSharedPtr<FJsonValue>>* When = nullptr;
		FString WhenJson = TEXT("[]");
		if (T->TryGetArrayField(TEXT("when"), When))
		{
			WhenJson.Reset();
			FJsonSerializer::Serialize(*When, TJsonWriterFactory<>::Create(&WhenJson));
		}
		if (FMath::IsNearlyEqual(From, 0.0) && FMath::IsNearlyEqual(To, 1.0))
		{
			if (HiddenToHintedWhen.IsEmpty()) { HiddenToHintedWhen = WhenJson; }
			if (T->TryGetNumberField(TEXT("radiusM"), Radius) && Radius > 0.0) { HintRadiusM = (float)Radius; }
		}
		else if (FMath::IsNearlyEqual(From, 1.0) && FMath::IsNearlyEqual(To, 2.0))
		{
			if (HintedToOpenWhen.IsEmpty()) { HintedToOpenWhen = WhenJson; }
		}
	}
}

FVector AMTAWildernessRegion::RegionMetersToWorld(const FVector& LocalM) const
{
	return GetActorTransform().TransformPosition(LocalM * 100.0);
}

bool AMTAWildernessRegion::IsPlayerNear(const FVector& WorldLocation, float RadiusCm, FVector* OutPlayerLocation) const
{
	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn) return false;
	if (OutPlayerLocation) { *OutPlayerLocation = Pawn->GetActorLocation(); }
	return FVector::Dist(Pawn->GetActorLocation(), WorldLocation) <= RadiusCm;
}

void AMTAWildernessRegion::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (EntranceState != EMTASecretEntranceState::Hidden) return;
	UMTADiscoverySubsystem* Disc = GetDiscovery();
	if (!Disc) return;
	if (IsPlayerNear(RegionMetersToWorld(SecretDoorM), HintRadiusM * 100.f) && Disc->EvaluateConditionsJson(HiddenToHintedWhen))
	{
		SetEntranceState(EMTASecretEntranceState::Hinted);
	}
}

bool AMTAWildernessRegion::TryOpenEntrance()
{
	if (EntranceState == EMTASecretEntranceState::Open) return true;
	if (EntranceState != EMTASecretEntranceState::Hinted) return false;
	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn || !SecretEntranceTrigger->IsOverlappingActor(Pawn)) return false;
	UMTADiscoverySubsystem* Disc = GetDiscovery();
	if (!Disc || !Disc->EvaluateConditionsJson(HintedToOpenWhen)) return false;
	SetEntranceState(EMTASecretEntranceState::Open);
	return true;
}

void AMTAWildernessRegion::SetEntranceState(EMTASecretEntranceState NewState)
{
	if (NewState == EntranceState) return;
	const EMTASecretEntranceState Old = EntranceState;
	EntranceState = NewState;
	if (UMTADiscoverySubsystem* Disc = GetDiscovery())
	{
		Disc->SetRegionState(StateKey(), static_cast<int32>(NewState));
	}
	OnEntranceStateChanged(Old, NewState);
	OnEntranceStateChangedEvent.Broadcast(Old, NewState);
}

void AMTAWildernessRegion::SetSluiceClosed(bool bClosed)
{
	if (UMTADiscoverySubsystem* Disc = GetDiscovery())
	{
		Disc->SetRegionState(SluiceKey(), bClosed ? 1 : 0);
	}
	OnWaterflowChanged(GetWaterflowScale());
}

float AMTAWildernessRegion::GetWaterflowScale() const
{
	const UMTADiscoverySubsystem* Disc = GetDiscovery();
	if (!Disc) return 1.f;
	if (Disc->GetRegionState(SluiceKey()) >= 1) return 0.5f;
	const FName W = Disc->GetWeather();
	return (W == TEXT("rain") || W == TEXT("storm")) ? 2.f : 1.f;
}

FMTABiomeFoliageParams AMTAWildernessRegion::GetBiomeParams(FName BiomeId) const
{
	for (const FMTABiomeFoliageParams& B : Biomes)
	{
		if (B.BiomeId == BiomeId)
		{
			FMTABiomeFoliageParams Scaled = B;
			Scaled.TreesPerHectare *= FoliageDensityScale;
			Scaled.Undergrowth = FMath::Clamp(B.Undergrowth * FoliageDensityScale, 0.f, 1.f);
			return Scaled;
		}
	}
	return FMTABiomeFoliageParams();
}
