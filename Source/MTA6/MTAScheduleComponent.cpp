#include "MTAScheduleComponent.h"
#include "MTANPCSubsystem.h"
#include "MTANPCActor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UMTAScheduleComponent::UMTAScheduleComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

UMTANPCSubsystem* UMTAScheduleComponent::GetNPCSubsystem() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UMTANPCSubsystem>() : nullptr;
}

void UMTAScheduleComponent::BeginPlay()
{
	Super::BeginPlay();
	// NPC-ID aus der Schnittstelle uebernehmen, wenn nicht im Editor gesetzt.
	AActor* Owner = GetOwner();
	if (NPCId.IsNone() && Owner && Owner->GetClass()->ImplementsInterface(UMTANPCActor::StaticClass()))
	{
		NPCId = IMTANPCActor::Execute_GetNPCId(Owner);
	}
	if (UMTANPCSubsystem* N = GetNPCSubsystem())
	{
		N->RegisterNPCActor(NPCId, Owner);
	}
	Refresh();
}

void UMTAScheduleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMTANPCSubsystem* N = GetNPCSubsystem())
	{
		if (N->GetNPCActor(NPCId) == GetOwner()) { N->UnregisterNPCActor(NPCId); }
	}
	Super::EndPlay(EndPlayReason);
}

void UMTAScheduleComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Accumulator += DeltaTime;
	if (Accumulator >= PollInterval)
	{
		Accumulator = 0.f;
		Refresh();
	}
}

void UMTAScheduleComponent::Refresh()
{
	const UMTANPCSubsystem* N = GetNPCSubsystem();
	const FMTANPCRuntime* R = N ? N->FindRuntime(NPCId) : nullptr;
	if (!R) { return; }
	const FMTAScheduleState& S = R->Current;
	const bool bChanged = !bHasState || S.Activity != Current.Activity || S.Location != Current.Location
		|| S.bTalkable != Current.bTalkable || S.bTrading != Current.bTrading || S.OverrideReason != Current.OverrideReason;
	Current = S;
	bHasState = true;
	if (bChanged)
	{
		// Das Subsystem ruft IMTANPCActor::OnActivityChanged bereits; hier nur das Komponenten-Delegate.
		OnScheduleChanged.Broadcast(Current);
	}
}

bool UMTAScheduleComponent::IsAvailableForDialogue() const
{
	const UMTANPCSubsystem* N = GetNPCSubsystem();
	return N && N->IsAvailableForDialogue(NPCId);
}

bool UMTAScheduleComponent::IsTrading() const
{
	const UMTANPCSubsystem* N = GetNPCSubsystem();
	return N && N->IsTrading(NPCId);
}

TArray<FMTAScheduleState> UMTAScheduleComponent::PreviewDay(int32 Day) const
{
	TArray<FMTAScheduleState> Out;
	if (const UMTANPCSubsystem* N = GetNPCSubsystem())
	{
		for (int32 H = 0; H < 24; ++H)
		{
			Out.Add(N->EvaluateSchedule(NPCId, (float)(Day * 24 + H) + 0.5f));
		}
	}
	return Out;
}
