#include "MTACollectibleComponent.h"
#include "MTADiscoverySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

UMTACollectibleComponent::UMTACollectibleComponent()
{
	InitSphereRadius(150.f);
	SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	SetGenerateOverlapEvents(true);
	ShapeColor = FColor(255, 200, 60);
}

void UMTACollectibleComponent::BeginPlay()
{
	Super::BeginPlay();
	OnComponentBeginOverlap.AddDynamic(this, &UMTACollectibleComponent::HandleBeginOverlap);
	if (IsCollected())
	{
		ApplyCollectedLook();
	}
}

void UMTACollectibleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnComponentBeginOverlap.RemoveDynamic(this, &UMTACollectibleComponent::HandleBeginOverlap);
	Super::EndPlay(EndPlayReason);
}

UMTADiscoverySubsystem* UMTACollectibleComponent::GetDiscovery() const
{
	const UWorld* World = GetWorld();
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UMTADiscoverySubsystem>() : nullptr;
}

bool UMTACollectibleComponent::IsCollected() const
{
	const UMTADiscoverySubsystem* Disc = GetDiscovery();
	return Disc && !CollectionId.IsNone() && Disc->IsItemCollected(CollectionId, ItemIndex);
}

void UMTACollectibleComponent::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || OtherActor == GetOwner()) return;
	if (bOnlyPlayerPawn && OtherActor != UGameplayStatics::GetPlayerPawn(this, 0)) return;
	Collect();
}

bool UMTACollectibleComponent::Collect()
{
	UMTADiscoverySubsystem* Disc = GetDiscovery();
	if (!Disc) return false;

	bool bChanged = false;
	if (!CollectionId.IsNone())
	{
		bChanged = Disc->CollectItem(CollectionId, ItemIndex);
	}
	if (!DiscoveryId.IsNone())
	{
		bChanged |= Disc->ForceDiscover(DiscoveryId, true);
	}
	if (!bChanged) return false;

	const FMTACollectionProgress P = Disc->GetCollectionProgress(CollectionId);
	OnCollected.Broadcast(this, P.Collected, P.Total);
	ApplyCollectedLook();
	return true;
}

void UMTACollectibleComponent::ApplyCollectedLook()
{
	SetGenerateOverlapEvents(false);
	if (bHideOwnerWhenCollected)
	{
		if (AActor* Owner = GetOwner())
		{
			Owner->SetActorHiddenInGame(true);
			Owner->SetActorEnableCollision(false);
		}
	}
}
