#include "MTAWorldAIManager.h"

#include "MTAHeatSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

// Hinweis: Polizei-Funktionen (UpdatePoliceDispatch, UpdatePoliceUnit, ...) liegen in MTAPoliceLogic.cpp.

AMTAWorldAIManager::AMTAWorldAIManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// Vor der Physik: Treffer-Impulse wirken im selben Frame.
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;

	// Ohne Assets lauffaehig: Engine-Grundformen.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMatFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	if (CubeFinder.Succeeded())
	{
		VehicleBodyMesh = CubeFinder.Object;
		VehicleCabinMesh = CubeFinder.Object;
		SirenMesh = CubeFinder.Object;
	}
	if (CylinderFinder.Succeeded())
	{
		PedestrianMesh = CylinderFinder.Object;
	}
	if (ShapeMatFinder.Succeeded())
	{
		FallbackColorMaterial = ShapeMatFinder.Object;
	}

	// Farben wie im Godot-Prototyp.
	TrafficColors = {
		FLinearColor(0.23f, 0.43f, 0.88f), FLinearColor(0.94f, 0.94f, 0.94f), FLinearColor(0.13f, 0.13f, 0.13f),
		FLinearColor(0.88f, 0.75f, 0.19f), FLinearColor(0.23f, 0.66f, 0.38f), FLinearColor(0.53f, 0.55f, 0.58f) };
	PedestrianColors = {
		FLinearColor(0.8f, 0.25f, 0.25f), FLinearColor(0.25f, 0.45f, 0.8f), FLinearColor(0.25f, 0.7f, 0.45f),
		FLinearColor(0.8f, 0.75f, 0.25f), FLinearColor(0.6f, 0.3f, 0.75f), FLinearColor(0.9f, 0.9f, 0.9f),
		FLinearColor(0.2f, 0.2f, 0.2f) };

	FallbackDistrictNames = {
		FName(TEXT("Hafen")), FName(TEXT("Downtown")), FName(TEXT("Industrie")),
		FName(TEXT("Strand")), FName(TEXT("Wohnviertel")), FName(TEXT("Huegel")) };
}

// =============================================================================================
// Lebenszyklus
// =============================================================================================

void AMTAWorldAIManager::BeginPlay()
{
	Super::BeginPlay();

	Rng.Initialize(RandomSeed);
	if (UWorld* World = GetWorld())
	{
		HeatSubsystem = World->GetSubsystem<UMTAHeatSubsystem>();
	}
	Grid.Init(FMath::Max(1200.f, Traffic.LookAheadDistance + 100.f));

	RefreshPlayer();
	InitAgents();
	InitVisuals();
	UpdateVisuals(0.f);
}

void AMTAWorldAIManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float Step = 1.f / FMath::Max(Traffic.SimulationHz, 1.f);
	const int32 MaxSteps = FMath::Max(Traffic.MaxStepsPerFrame, 1);

	// Fester Schritt; ueberschuessige Zeit wird verworfen (keine Todesspirale).
	Accumulator = FMath::Min(Accumulator + DeltaSeconds, Step * MaxSteps);
	while (Accumulator >= Step)
	{
		SimStep(Step);
		Accumulator -= Step;
	}

	UpdateVisuals(DeltaSeconds);
}

// =============================================================================================
// Initialisierung
// =============================================================================================

void AMTAWorldAIManager::InitAgents()
{
	NumTrafficSlots = FMath::Max(0, Traffic.NumTrafficVehicles);
	NumPoliceSlots = FMath::Max(0, Police.MaxPolice);
	FirstPoliceSlot = NumTrafficSlots;

	Vehicles.Resize(NumTrafficSlots + NumPoliceSlots);
	for (int32 I = 0; I < NumTrafficSlots; ++I)
	{
		SpawnVehicleAt(I, EMTAAgentKind::Traffic);
	}
	for (int32 I = FirstPoliceSlot; I < Vehicles.Num(); ++I)
	{
		Vehicles.Kind[I] = EMTAAgentKind::Police;
		Vehicles.PoliceState[I] = EMTAPoliceState::Inactive;
		Vehicles.bActive[I] = 0;
	}

	Peds.Resize(FMath::Max(0, Traffic.NumPedestrians));
	for (int32 I = 0; I < Peds.Num(); ++I)
	{
		PlacePedestrianRandom(I);
		Peds.Cruise[I] = static_cast<float>(Rng.FRandRange(Traffic.PedestrianSpeedMin, Traffic.PedestrianSpeedMax));
		Peds.Speed[I] = Peds.Cruise[I];
		Peds.Stun[I] = 0.f;
	}
}

void AMTAWorldAIManager::RandomizeLanePlacement(uint8& OutAxis, int8& OutSign, int32& OutLine, float& OutPos)
{
	OutAxis = static_cast<uint8>(Rng.RandRange(0, 1));
	OutLine = Rng.RandRange(0, MTACity::Blocks);
	OutSign = static_cast<int8>(Rng.FRand() < 0.5f ? 1 : -1);
	OutPos = static_cast<float>(Rng.FRandRange(-MTACity::Half + 500.f, MTACity::Half - 500.f));
}

void AMTAWorldAIManager::SpawnVehicleAt(int32 I, EMTAAgentKind InKind)
{
	RandomizeLanePlacement(Vehicles.Axis[I], Vehicles.Sign[I], Vehicles.Line[I], Vehicles.Pos[I]);
	Vehicles.Kind[I] = InKind;
	if (InKind == EMTAAgentKind::Police)
	{
		Vehicles.Cruise[I] = Police.CalmSpeed;
		Vehicles.Speed[I] = Police.InitialSpeed;
		Vehicles.PoliceState[I] = EMTAPoliceState::Chase;
	}
	else
	{
		Vehicles.Cruise[I] = static_cast<float>(Rng.FRandRange(Traffic.TrafficSpeedMin, Traffic.TrafficSpeedMax));
		Vehicles.Speed[I] = Vehicles.Cruise[I];
		Vehicles.PoliceState[I] = EMTAPoliceState::Inactive;
	}
	Vehicles.Stun[I] = 0.f;
	Vehicles.Wait[I] = 0.f;
	Vehicles.Ghost[I] = 0.f;
	Vehicles.LodAccum[I] = 0.f;
	Vehicles.bActive[I] = 1;
	Vehicles.bSnapVisual[I] = 1;
	SetVehicleOffset(I);
	ComputeVehicleSim(I);
}

void AMTAWorldAIManager::PlacePedestrianRandom(int32 I)
{
	RandomizeLanePlacement(Peds.Axis[I], Peds.Sign[I], Peds.Line[I], Peds.Pos[I]);
	Peds.Side[I] = static_cast<int8>(Rng.FRand() < 0.5f ? 1 : -1);
	Peds.LodAccum[I] = 0.f;
	Peds.bSnapVisual[I] = 1;
	ComputePedestrianSim(I);
}

FTransform AMTAWorldAIManager::HiddenTransform()
{
	// Versteckte Pool-Instanzen: winzig und tief unter der Stadt.
	return FTransform(FQuat::Identity, FVector(0.0, 0.0, -100000.0), FVector(0.001));
}

UInstancedStaticMeshComponent* AMTAWorldAIManager::CreateInstancer(const FName& Name, UStaticMesh* Mesh, const FLinearColor& Color, int32 Count, bool bShadows)
{
	if (!Mesh || Count <= 0)
	{
		return nullptr;
	}

	UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(this, Name);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetCastShadow(bShadows);
	Comp->SetStaticMesh(Mesh);
	Comp->SetupAttachment(GetRootComponent());
	Comp->RegisterComponent();
	AddInstanceComponent(Comp);

	if (AgentMaterial)
	{
		Comp->SetMaterial(0, AgentMaterial);
	}
	else if (FallbackColorMaterial)
	{
		// BasicShapeMaterial besitzt den Vektorparameter "Color".
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(FallbackColorMaterial, this);
		if (MID)
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			Comp->SetMaterial(0, MID);
		}
	}

	// Per-Instance-Custom-Data: 0..2 Farbe, 3 Emissiv.
	Comp->SetNumCustomDataFloats(4);
	const FTransform Hidden = HiddenTransform();
	for (int32 N = 0; N < Count; ++N)
	{
		const int32 Idx = Comp->AddInstance(Hidden, /*bWorldSpace*/ true);
		Comp->SetCustomDataValue(Idx, 0, Color.R, false);
		Comp->SetCustomDataValue(Idx, 1, Color.G, false);
		Comp->SetCustomDataValue(Idx, 2, Color.B, false);
		Comp->SetCustomDataValue(Idx, 3, 0.f, false);
	}
	Comp->MarkRenderStateDirty();
	return Comp;
}

void AMTAWorldAIManager::InitVisuals()
{
	// --- Zivilverkehr: eine Komponente je Farbgruppe -------------------------------------------
	const int32 NumTrafficColors = FMath::Max(1, TrafficColors.Num());
	TArray<int32> GroupCounts;
	GroupCounts.Init(0, NumTrafficColors);
	for (int32 I = 0; I < NumTrafficSlots; ++I)
	{
		const int32 C = I % NumTrafficColors;
		Vehicles.ColorIndex[I] = static_cast<uint8>(C);
		Vehicles.InstanceIndex[I] = GroupCounts[C]++;
	}
	TrafficBodyISMs.SetNum(NumTrafficColors);
	TrafficXfBuffers.SetNum(NumTrafficColors);
	for (int32 C = 0; C < NumTrafficColors; ++C)
	{
		const FLinearColor Col = TrafficColors.IsValidIndex(C) ? TrafficColors[C] : FLinearColor::Gray;
		TrafficBodyISMs[C] = CreateInstancer(*FString::Printf(TEXT("TrafficBody_%d"), C), VehicleBodyMesh, Col, GroupCounts[C], bAgentsCastShadows);
		TrafficXfBuffers[C].SetNum(GroupCounts[C]);
	}

	// --- Polizei ----------------------------------------------------------------------------------
	for (int32 I = FirstPoliceSlot; I < Vehicles.Num(); ++I)
	{
		Vehicles.ColorIndex[I] = 0;
		Vehicles.InstanceIndex[I] = I - FirstPoliceSlot;
	}
	PoliceBodyISM = CreateInstancer(TEXT("PoliceBody"), VehicleBodyMesh, PoliceColor, NumPoliceSlots, bAgentsCastShadows);
	SirenRedISM = CreateInstancer(TEXT("SirenRed"), SirenMesh, FLinearColor(1.f, 0.1f, 0.1f), NumPoliceSlots, false);
	SirenBlueISM = CreateInstancer(TEXT("SirenBlue"), SirenMesh, FLinearColor(0.15f, 0.3f, 1.f), NumPoliceSlots, false);
	PoliceXfBuffer.SetNum(NumPoliceSlots);
	SirenRedXfBuffer.SetNum(NumPoliceSlots);
	SirenBlueXfBuffer.SetNum(NumPoliceSlots);

	// --- Kabine fuer alle Fahrzeug-Slots (Index = Fahrzeugindex) -------------------------------
	CabinISM = CreateInstancer(TEXT("VehicleCabin"), VehicleCabinMesh, CabinColor, Vehicles.Num(), bAgentsCastShadows);
	CabinXfBuffer.SetNum(Vehicles.Num());

	// --- Fussgaenger je Farbgruppe -----------------------------------------------------------------
	const int32 NumPedColors = FMath::Max(1, PedestrianColors.Num());
	TArray<int32> PedCounts;
	PedCounts.Init(0, NumPedColors);
	for (int32 I = 0; I < Peds.Num(); ++I)
	{
		const int32 C = I % NumPedColors;
		Peds.ColorIndex[I] = static_cast<uint8>(C);
		Peds.InstanceIndex[I] = PedCounts[C]++;
	}
	PedISMs.SetNum(NumPedColors);
	PedXfBuffers.SetNum(NumPedColors);
	for (int32 C = 0; C < NumPedColors; ++C)
	{
		const FLinearColor Col = PedestrianColors.IsValidIndex(C) ? PedestrianColors[C] : FLinearColor::White;
		PedISMs[C] = CreateInstancer(*FString::Printf(TEXT("Pedestrian_%d"), C), PedestrianMesh, Col, PedCounts[C], bAgentsCastShadows);
		PedXfBuffers[C].SetNum(PedCounts[C]);
	}
}

// =============================================================================================
// Simulationsschritt
// =============================================================================================

void AMTAWorldAIManager::SimStep(float Dt)
{
	SimTime += Dt;
	++StepCounter;
	HitCooldownLeft -= Dt;

	RefreshPlayer();
	CachedHeat = HeatSubsystem ? HeatSubsystem->GetGlobalHeat(PlayerIndex) : 0.f;
	if (HeatSubsystem && bHasPlayer)
	{
		HeatSubsystem->NotifyPlayerDistrict(PlayerIndex, GetDistrictAt(FVector(PlayerLoc2D.X, PlayerLoc2D.Y, 0.0)));
		CachedHeat = HeatSubsystem->GetGlobalHeat(PlayerIndex);
	}

	BuildGrid();
	UpdateVehicles(Dt);
	UpdatePedestrians(Dt);
	UpdatePoliceDispatch(Dt);
	UpdateBubble();
}

void AMTAWorldAIManager::RefreshPlayer()
{
	APawn* Pawn = PlayerPawn;
	APlayerController* PC = nullptr;
	if (!Pawn)
	{
		if (UWorld* World = GetWorld())
		{
			PC = World->GetFirstPlayerController();
			Pawn = PC ? PC->GetPawn() : nullptr;
		}
	}
	else
	{
		PC = Cast<APlayerController>(Pawn->GetController());
	}

	ActivePawn = Pawn;
	bHasPlayer = IsValid(Pawn);
	if (!bHasPlayer)
	{
		PlayerSpeed = 0.f;
		PlayerVelocity = FVector::ZeroVector;
		return;
	}

	const FVector Loc = Pawn->GetActorLocation();
	PlayerLoc2D = FVector2f(static_cast<float>(Loc.X), static_cast<float>(Loc.Y));
	PlayerVelocity = Pawn->GetVelocity();
	PlayerSpeed = static_cast<float>(PlayerVelocity.Size2D());

	FVector ViewLoc = Loc;
	FRotator ViewRot = Pawn->GetActorRotation();
	if (PC)
	{
		PC->GetPlayerViewPoint(ViewLoc, ViewRot);
	}
	ViewLoc2D = FVector2f(static_cast<float>(ViewLoc.X), static_cast<float>(ViewLoc.Y));
	ViewDir2D = MTAAI::DirFromYaw(static_cast<float>(ViewRot.Yaw));
}

void AMTAWorldAIManager::BuildGrid()
{
	Grid.Clear();
	for (int32 I = 0; I < Vehicles.Num(); ++I)
	{
		Vehicles.GridNext[I] = INDEX_NONE;
		if (!Vehicles.bActive[I])
		{
			continue;
		}
		const FVector2f& L = Vehicles.SimLoc[I];
		const int32 Cell = Grid.CellIndex(Grid.CellCoord(L.X), Grid.CellCoord(L.Y));
		Vehicles.GridNext[I] = Grid.Heads[Cell];
		Grid.Heads[Cell] = I;
	}
}

// =============================================================================================
// Spurmodell
// =============================================================================================

void AMTAWorldAIManager::SetVehicleOffset(int32 I)
{
	if (Vehicles.Kind[I] == EMTAAgentKind::Police)
	{
		Vehicles.Off[I] = 0.f; // Polizei faehrt auf der Strassenmitte (Godot)
	}
	else
	{
		const float S = static_cast<float>(Vehicles.Sign[I]);
		Vehicles.Off[I] = Vehicles.Axis[I] == 0 ? MTACity::LaneOffset * S : -MTACity::LaneOffset * S;
	}
}

void AMTAWorldAIManager::ComputeVehicleSim(int32 I)
{
	const float C = MTACity::RoadCenter(Vehicles.Line[I]) + Vehicles.Off[I];
	const float S = static_cast<float>(Vehicles.Sign[I]);
	if (Vehicles.Axis[I] == 0)
	{
		Vehicles.SimLoc[I] = FVector2f(Vehicles.Pos[I], C);
		Vehicles.SimDir[I] = FVector2f(S, 0.f);
	}
	else
	{
		Vehicles.SimLoc[I] = FVector2f(C, Vehicles.Pos[I]);
		Vehicles.SimDir[I] = FVector2f(0.f, S);
	}
}

void AMTAWorldAIManager::ComputePedestrianSim(int32 I)
{
	const float C = MTACity::RoadCenter(Peds.Line[I]) + Peds.Side[I] * MTACity::SidewalkOffset;
	const float S = static_cast<float>(Peds.Sign[I]);
	if (Peds.Axis[I] == 0)
	{
		Peds.SimLoc[I] = FVector2f(Peds.Pos[I], C);
		Peds.SimDir[I] = FVector2f(S, 0.f);
	}
	else
	{
		Peds.SimLoc[I] = FVector2f(C, Peds.Pos[I]);
		Peds.SimDir[I] = FVector2f(0.f, S);
	}
}

bool AMTAWorldAIManager::HandleIntersection(uint8& Axis, int8& Sign, int32& Line, float& Pos, int32 M, bool bGreedy)
{
	struct FOption
	{
		int8 Choice;     // 0 = geradeaus, +1/-1 = Abbiegen in diese Richtung der neuen Achse
		float Weight;
		FVector2f Target;
	};

	const int32 K = Line;
	const FVector2f Cross = Axis == 0
		? FVector2f(MTACity::RoadCenter(M), MTACity::RoadCenter(K))
		: FVector2f(MTACity::RoadCenter(K), MTACity::RoadCenter(M));
	const float Reach = MTACity::Period * 0.7f;

	FOption Options[3];
	int32 NumOptions = 0;

	const bool bStraightOk = !(M == MTACity::Blocks && Sign > 0) && !(M == 0 && Sign < 0);
	if (bStraightOk)
	{
		const FVector2f SD = Axis == 0 ? FVector2f(Sign, 0.f) : FVector2f(0.f, Sign);
		Options[NumOptions++] = { 0, Traffic.TurnWeightStraight, Cross + SD * Reach };
	}
	const int8 TurnSigns[2] = { 1, -1 };
	for (const int8 S : TurnSigns)
	{
		if ((K == 0 && S < 0) || (K == MTACity::Blocks && S > 0))
		{
			continue; // wuerde die Stadt verlassen
		}
		const FVector2f TD = Axis == 0 ? FVector2f(0.f, S) : FVector2f(S, 0.f);
		Options[NumOptions++] = { S, Traffic.TurnWeightSide, Cross + TD * Reach };
	}
	if (NumOptions == 0)
	{
		return false; // kann im Raster nicht vorkommen, Schutz
	}

	int8 Choice = Options[NumOptions - 1].Choice;
	if (bGreedy && bHasPlayer)
	{
		// Gierig: Option, deren Zielpunkt dem Spieler am naechsten liegt.
		float Best = TNumericLimits<float>::Max();
		for (int32 O = 0; O < NumOptions; ++O)
		{
			const float D = FVector2f::DistSquared(Options[O].Target, PlayerLoc2D);
			if (D < Best)
			{
				Best = D;
				Choice = Options[O].Choice;
			}
		}
	}
	else
	{
		float Total = 0.f;
		for (int32 O = 0; O < NumOptions; ++O)
		{
			Total += Options[O].Weight;
		}
		float R = Rng.FRand() * Total;
		for (int32 O = 0; O < NumOptions; ++O)
		{
			R -= Options[O].Weight;
			if (R <= 0.f)
			{
				Choice = Options[O].Choice;
				break;
			}
		}
	}

	if (Choice == 0)
	{
		return false;
	}
	Axis = static_cast<uint8>(1 - Axis);
	Line = M;
	Sign = Choice;
	Pos = MTACity::RoadCenter(K) + Choice * 100.f;
	return true;
}

void AMTAWorldAIManager::MoveVehicleAlongLane(int32 I, float Dt)
{
	const float Prev = Vehicles.Pos[I];
	Vehicles.Pos[I] += Vehicles.Sign[I] * Vehicles.Speed[I] * Dt;

	const int32 K1 = FMath::FloorToInt((Prev + MTACity::Half) / MTACity::Period);
	const int32 K2 = FMath::FloorToInt((Vehicles.Pos[I] + MTACity::Half) / MTACity::Period);
	if (K1 != K2)
	{
		const int32 M = Vehicles.Sign[I] > 0 ? K2 : K1;
		const bool bGreedy = Vehicles.Kind[I] == EMTAAgentKind::Police
			&& Vehicles.PoliceState[I] == EMTAPoliceState::Chase
			&& Rng.FRand() < Police.GreedyChance;
		if (HandleIntersection(Vehicles.Axis[I], Vehicles.Sign[I], Vehicles.Line[I], Vehicles.Pos[I], M, bGreedy))
		{
			SetVehicleOffset(I);
		}
	}
	ComputeVehicleSim(I);
}

bool AMTAWorldAIManager::IsAhead(const FVector2f& P, const FVector2f& Fwd, const FVector2f& Q, float Dist, float HalfWidth)
{
	const FVector2f D = Q - P;
	const float F = D.X * Fwd.X + D.Y * Fwd.Y;
	if (F <= 0.f || F > Dist)
	{
		return false;
	}
	return FMath::Abs(D.X * Fwd.Y - D.Y * Fwd.X) < HalfWidth;
}

bool AMTAWorldAIManager::IsVehicleBlocked(int32 I) const
{
	const FVector2f& P = Vehicles.SimLoc[I];
	const FVector2f& F = Vehicles.SimDir[I];
	const float Dist = Traffic.LookAheadDistance;
	const float HalfW = Traffic.LookAheadHalfWidth;

	const int32 CX = Grid.CellCoord(P.X);
	const int32 CY = Grid.CellCoord(P.Y);
	for (int32 Y = FMath::Max(CY - 1, 0); Y <= FMath::Min(CY + 1, Grid.Dim - 1); ++Y)
	{
		for (int32 X = FMath::Max(CX - 1, 0); X <= FMath::Min(CX + 1, Grid.Dim - 1); ++X)
		{
			for (int32 J = Grid.Heads[Grid.CellIndex(X, Y)]; J != INDEX_NONE; J = Vehicles.GridNext[J])
			{
				if (J != I && IsAhead(P, F, Vehicles.SimLoc[J], Dist, HalfW))
				{
					return true;
				}
			}
		}
	}
	// Zivilverkehr haelt auch vor dem Spieler (Polizei nicht).
	return Vehicles.Kind[I] == EMTAAgentKind::Traffic && bHasPlayer && IsAhead(P, F, PlayerLoc2D, Dist, HalfW);
}

void AMTAWorldAIManager::UpdateLaneSpeed(int32 I, float Dt, float TargetSpeed)
{
	if (Vehicles.Stun[I] > 0.f)
	{
		Vehicles.Stun[I] -= Dt;
		Vehicles.Speed[I] = MTAAI::MoveTowards(Vehicles.Speed[I], 0.f, Traffic.StunBraking * Dt);
		return;
	}

	float Target = TargetSpeed;
	if (Vehicles.Ghost[I] > 0.f)
	{
		Vehicles.Ghost[I] -= Dt; // Geist: Abstandsregel aus (Deadlock-Aufloesung)
	}
	else if (IsVehicleBlocked(I))
	{
		Target = 0.f;
		Vehicles.Wait[I] += Dt;
		if (Vehicles.Wait[I] > Traffic.WaitBeforeGhost)
		{
			Vehicles.Ghost[I] = Traffic.GhostDuration;
			Vehicles.Wait[I] = 0.f;
		}
	}
	else
	{
		Vehicles.Wait[I] = 0.f;
	}
	const float Rate = Target > Vehicles.Speed[I] ? Traffic.Acceleration : Traffic.Braking;
	Vehicles.Speed[I] = MTAAI::MoveTowards(Vehicles.Speed[I], Target, Rate * Dt);
}

// =============================================================================================
// Fahrzeuge, Fussgaenger
// =============================================================================================

void AMTAWorldAIManager::UpdateVehicles(float Dt)
{
	const float LodDistSq = FMath::Square(Traffic.LodDistance);
	const int32 Divider = FMath::Max(1, Traffic.LodStepDivider);

	for (int32 I = 0; I < Vehicles.Num(); ++I)
	{
		if (!Vehicles.bActive[I])
		{
			continue;
		}

		// Sim-LOD: ferne Fahrzeuge seltener, mit aufsummiertem dt.
		float StepDt = Dt + Vehicles.LodAccum[I];
		const bool bFar = bHasPlayer && FVector2f::DistSquared(Vehicles.SimLoc[I], PlayerLoc2D) > LodDistSq;
		if (bFar && Divider > 1 && ((I + StepCounter) % Divider) != 0)
		{
			Vehicles.LodAccum[I] += Dt;
			continue;
		}
		Vehicles.LodAccum[I] = 0.f;

		if (Vehicles.Kind[I] == EMTAAgentKind::Police)
		{
			UpdatePoliceUnit(I, StepDt, CachedHeat);
			if (!Vehicles.bActive[I])
			{
				continue;
			}
		}
		else
		{
			UpdateLaneSpeed(I, StepDt, Vehicles.Cruise[I]);
			MoveVehicleAlongLane(I, StepDt);
		}

		// Kontakt mit dem Spieler (rein abstandsbasiert, wie Godot).
		if (bHasPlayer && !bFar)
		{
			const FVector2f Delta = PlayerLoc2D - Vehicles.SimLoc[I];
			const float Dist = Delta.Size();
			if (Dist < Traffic.VehicleHitRadius)
			{
				HandleVehiclePlayerContact(I, Dist, Delta);
			}
		}
	}
}

void AMTAWorldAIManager::HandleVehiclePlayerContact(int32 I, float Dist, const FVector2f& Delta)
{
	if (HitCooldownLeft > 0.f)
	{
		return;
	}
	HitCooldownLeft = Traffic.HitCooldown;

	const bool bPolice = Vehicles.Kind[I] == EMTAAgentKind::Police;
	FVector2f Dir2 = Dist > KINDA_SMALL_NUMBER ? Delta / Dist : Vehicles.SimDir[I];
	const FVector Dir(Dir2.X, Dir2.Y, 0.0);

	// Schaden aus Relativgeschwindigkeit.
	const FVector AIVel(Vehicles.SimDir[I].X * Vehicles.Speed[I], Vehicles.SimDir[I].Y * Vehicles.Speed[I], 0.0);
	const float RelMps = static_cast<float>((PlayerVelocity - AIVel).Size2D()) / 100.f;
	float Damage = Traffic.HitBaseDamage + Traffic.HitDamagePerMps * RelMps;
	if (bPolice)
	{
		Damage *= Police.RamDamageMultiplier;
	}
	const float Impulse = Traffic.HitImpulse;

	// Impuls am simulierenden Root des Pawns (Chaos-Fahrzeug: Skeletal-Mesh-Root).
	if (bApplyImpulseToPlayer)
	{
		if (APawn* Pawn = ActivePawn.Get())
		{
			UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Pawn->GetRootComponent());
			if (Prim && Prim->IsSimulatingPhysics())
			{
				Prim->AddImpulse(Dir * Impulse, NAME_None, /*bVelChange*/ false);
				Prim->SetPhysicsLinearVelocity(Prim->GetPhysicsLinearVelocity() * Traffic.HitVelocityKeep);
			}
		}
	}

	Vehicles.Stun[I] = bPolice ? Police.StunTime : Traffic.TrafficStunTime;

	if (!bPolice && PlayerSpeed > Traffic.VehicleHitMinSpeedForHeat && HeatSubsystem)
	{
		HeatSubsystem->AddPlayerHeat(PlayerIndex, Traffic.VehicleHitHeat, NAME_None);
		CachedHeat = HeatSubsystem->GetGlobalHeat(PlayerIndex);
	}

	OnPlayerHitByAI.Broadcast(Damage, Impulse, Dir);
}

void AMTAWorldAIManager::UpdatePedestrians(float Dt)
{
	const float LodDistSq = FMath::Square(Traffic.LodDistance);
	const int32 Divider = FMath::Max(1, Traffic.LodStepDivider);
	const float HitRadiusSq = FMath::Square(Traffic.PedestrianHitRadius);

	for (int32 I = 0; I < Peds.Num(); ++I)
	{
		float StepDt = Dt + Peds.LodAccum[I];
		const bool bFar = bHasPlayer && FVector2f::DistSquared(Peds.SimLoc[I], PlayerLoc2D) > LodDistSq;
		if (bFar && Divider > 1 && ((I + StepCounter) % Divider) != 0)
		{
			Peds.LodAccum[I] += Dt;
			continue;
		}
		Peds.LodAccum[I] = 0.f;

		// Umgefallen: liegen bleiben, dann aufstehen.
		if (Peds.Stun[I] > 0.f)
		{
			Peds.Stun[I] -= StepDt;
			Peds.Speed[I] = 0.f;
			continue;
		}

		Peds.Speed[I] = MTAAI::MoveTowards(Peds.Speed[I], Peds.Cruise[I], Traffic.Acceleration * StepDt);

		const float Prev = Peds.Pos[I];
		Peds.Pos[I] += Peds.Sign[I] * Peds.Speed[I] * StepDt;
		const int32 K1 = FMath::FloorToInt((Prev + MTACity::Half) / MTACity::Period);
		const int32 K2 = FMath::FloorToInt((Peds.Pos[I] + MTACity::Half) / MTACity::Period);
		if (K1 != K2)
		{
			const int32 M = Peds.Sign[I] > 0 ? K2 : K1;
			if (HandleIntersection(Peds.Axis[I], Peds.Sign[I], Peds.Line[I], Peds.Pos[I], M, false))
			{
				Peds.Side[I] = static_cast<int8>(Rng.FRand() < 0.5f ? 1 : -1);
			}
		}
		ComputePedestrianSim(I);

		// Vom Spieler angefahren?
		if (bHasPlayer && !bFar && PlayerSpeed > Traffic.PedestrianHitMinSpeed
			&& FVector2f::DistSquared(Peds.SimLoc[I], PlayerLoc2D) < HitRadiusSq)
		{
			Peds.Stun[I] = Traffic.PedestrianDownTime;
			Peds.Speed[I] = 0.f;
			const FVector HitLoc(Peds.SimLoc[I].X, Peds.SimLoc[I].Y, GroundZ);
			if (HeatSubsystem)
			{
				HeatSubsystem->AddPlayerHeat(PlayerIndex, Traffic.PedestrianHitHeat, GetDistrictAt(HitLoc));
				CachedHeat = HeatSubsystem->GetGlobalHeat(PlayerIndex);
			}
			OnPedestrianHit.Broadcast(HitLoc);
		}
	}
}

bool AMTAWorldAIManager::IsOutOfView(const FVector2f& Loc) const
{
	const FVector2f To = Loc - ViewLoc2D;
	const float D = To.Size();
	if (D < KINDA_SMALL_NUMBER)
	{
		return false;
	}
	// Hinter bzw. seitlich der Kamera (Sichtkegel ca. +-72 Grad) oder sehr weit entfernt.
	const float Dot = (To.X * ViewDir2D.X + To.Y * ViewDir2D.Y) / D;
	return Dot < 0.3f || D > 20000.f;
}

void AMTAWorldAIManager::UpdateBubble()
{
	if (!bHasPlayer || Traffic.BubbleRadius <= 0.f || Traffic.BubbleRespawnsPerStep <= 0)
	{
		return;
	}
	const float RadiusSq = FMath::Square(Traffic.BubbleRadius);
	const float MinSq = FMath::Square(FMath::Min(Traffic.BubbleRespawnMinDistance, Traffic.BubbleRadius * 0.9f));
	int32 Budget = Traffic.BubbleRespawnsPerStep;

	auto TryPlace = [&](uint8& Axis, int8& Sign, int32& Line, float& Pos, float LateralOffsetForSide, FVector2f& OutLoc) -> bool
	{
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			uint8 A; int8 S; int32 L; float P;
			RandomizeLanePlacement(A, S, L, P);
			const float C = MTACity::RoadCenter(L) + LateralOffsetForSide;
			const FVector2f Loc = A == 0 ? FVector2f(P, C) : FVector2f(C, P);
			const float DSq = FVector2f::DistSquared(Loc, PlayerLoc2D);
			if (DSq >= MinSq && DSq <= RadiusSq && IsOutOfView(Loc))
			{
				Axis = A; Sign = S; Line = L; Pos = P; OutLoc = Loc;
				return true;
			}
		}
		return false;
	};

	// Zivilverkehr (Polizei wird ueber den Dispatch verwaltet).
	for (int32 I = 0; I < NumTrafficSlots && Budget > 0; ++I)
	{
		if (FVector2f::DistSquared(Vehicles.SimLoc[I], PlayerLoc2D) <= RadiusSq)
		{
			continue;
		}
		FVector2f Loc;
		if (TryPlace(Vehicles.Axis[I], Vehicles.Sign[I], Vehicles.Line[I], Vehicles.Pos[I], 0.f, Loc))
		{
			Vehicles.Speed[I] = Vehicles.Cruise[I];
			Vehicles.Stun[I] = Vehicles.Wait[I] = Vehicles.Ghost[I] = 0.f;
			SetVehicleOffset(I);
			ComputeVehicleSim(I);
			Vehicles.bSnapVisual[I] = 1;
		}
		--Budget; // auch Fehlversuche kosten Budget
	}

	Budget = Traffic.BubbleRespawnsPerStep;
	for (int32 I = 0; I < Peds.Num() && Budget > 0; ++I)
	{
		if (FVector2f::DistSquared(Peds.SimLoc[I], PlayerLoc2D) <= RadiusSq)
		{
			continue;
		}
		FVector2f Loc;
		const int8 NewSide = static_cast<int8>(Rng.FRand() < 0.5f ? 1 : -1);
		if (TryPlace(Peds.Axis[I], Peds.Sign[I], Peds.Line[I], Peds.Pos[I], NewSide * MTACity::SidewalkOffset, Loc))
		{
			Peds.Side[I] = NewSide;
			Peds.Stun[I] = 0.f;
			ComputePedestrianSim(I);
			Peds.bSnapVisual[I] = 1;
		}
		--Budget;
	}
}

// =============================================================================================
// Darstellung
// =============================================================================================

void AMTAWorldAIManager::UpdateVisuals(float FrameDt)
{
	const float Alpha = 1.f - FMath::Exp(-FrameDt * Traffic.VisualSmoothing);
	const float LodDistSq = FMath::Square(Traffic.LodDistance);
	const FTransform Hidden = HiddenTransform();

	const FVector BodyScale = VehicleBodySize / 100.0;   // Engine-Wuerfel = 100 cm
	const FVector CabinScale = VehicleCabinSize / 100.0;
	const double Clearance = 20.0;
	const double BodyZ = Clearance + VehicleBodySize.Z * 0.5;
	const double CabinZ = Clearance + VehicleBodySize.Z + VehicleCabinSize.Z * 0.5;
	const double SirenZ = Clearance + VehicleBodySize.Z + VehicleCabinSize.Z + 10.0;
	const FVector SirenScale(0.4, 0.7, 0.18);

	const bool bSirenOn = CachedHeat > 0.f;
	const bool bRedPhase = IsSirenPhaseRed();

	// --- Fahrzeuge ------------------------------------------------------------------------------
	for (int32 I = 0; I < Vehicles.Num(); ++I)
	{
		const bool bPolice = Vehicles.Kind[I] == EMTAAgentKind::Police;
		const int32 Inst = Vehicles.InstanceIndex[I];

		if (!Vehicles.bActive[I])
		{
			if (bPolice && PoliceXfBuffer.IsValidIndex(Inst))
			{
				PoliceXfBuffer[Inst] = Hidden;
				SirenRedXfBuffer[Inst] = Hidden;
				SirenBlueXfBuffer[Inst] = Hidden;
			}
			if (CabinXfBuffer.IsValidIndex(I))
			{
				CabinXfBuffer[I] = Hidden;
			}
			continue;
		}

		const FVector Target(Vehicles.SimLoc[I].X, Vehicles.SimLoc[I].Y, GroundZ);
		const float TargetYaw = MTAAI::YawFromDir(Vehicles.SimDir[I]);
		const bool bFar = bHasPlayer && FVector2f::DistSquared(Vehicles.SimLoc[I], PlayerLoc2D) > LodDistSq;
		if (Vehicles.bSnapVisual[I] || bFar)
		{
			Vehicles.VisLoc[I] = Target;
			Vehicles.VisYaw[I] = TargetYaw;
			Vehicles.bSnapVisual[I] = 0;
		}
		else
		{
			Vehicles.VisLoc[I] += (Target - Vehicles.VisLoc[I]) * Alpha;
			Vehicles.VisYaw[I] += FMath::FindDeltaAngleDegrees(Vehicles.VisYaw[I], TargetYaw) * Alpha;
		}

		const FQuat Rot = FRotator(0.f, Vehicles.VisYaw[I], 0.f).Quaternion();
		const FVector Base = Vehicles.VisLoc[I];
		const FTransform BodyXf(Rot, Base + FVector(0.0, 0.0, BodyZ), BodyScale);
		const FTransform CabinXf(Rot, Base + Rot.RotateVector(FVector(-20.0, 0.0, CabinZ)), CabinScale);

		if (CabinXfBuffer.IsValidIndex(I))
		{
			CabinXfBuffer[I] = CabinXf;
		}

		if (bPolice)
		{
			if (!PoliceXfBuffer.IsValidIndex(Inst))
			{
				continue;
			}
			PoliceXfBuffer[Inst] = BodyXf;
			// Sirenenbalken: rot links, blau rechts. Blinken: die inaktive Phase wird ausgeblendet,
			// zusaetzlich Emissiv in Custom-Data[3] fuer Materialien mit PerInstanceCustomData.
			const FTransform RedXf(Rot, Base + Rot.RotateVector(FVector(-10.0, -45.0, SirenZ)), SirenScale);
			const FTransform BlueXf(Rot, Base + Rot.RotateVector(FVector(-10.0, 45.0, SirenZ)), SirenScale);
			const bool bShowRed = !bSirenOn || bRedPhase;
			const bool bShowBlue = !bSirenOn || !bRedPhase;
			SirenRedXfBuffer[Inst] = bShowRed ? RedXf : Hidden;
			SirenBlueXfBuffer[Inst] = bShowBlue ? BlueXf : Hidden;
			if (SirenRedISM)
			{
				SirenRedISM->SetCustomDataValue(Inst, 3, (bSirenOn && bRedPhase) ? 4.f : 0.2f, false);
			}
			if (SirenBlueISM)
			{
				SirenBlueISM->SetCustomDataValue(Inst, 3, (bSirenOn && !bRedPhase) ? 4.f : 0.2f, false);
			}
		}
		else
		{
			const int32 C = Vehicles.ColorIndex[I];
			if (TrafficXfBuffers.IsValidIndex(C) && TrafficXfBuffers[C].IsValidIndex(Inst))
			{
				TrafficXfBuffers[C][Inst] = BodyXf;
			}
		}
	}

	// --- Fussgaenger ----------------------------------------------------------------------------
	const FVector PedScale(PedestrianSize.X / 100.0, PedestrianSize.X / 100.0, PedestrianSize.Y / 100.0);
	for (int32 I = 0; I < Peds.Num(); ++I)
	{
		const FVector Target(Peds.SimLoc[I].X, Peds.SimLoc[I].Y, GroundZ);
		const float TargetYaw = MTAAI::YawFromDir(Peds.SimDir[I]);
		const bool bFar = bHasPlayer && FVector2f::DistSquared(Peds.SimLoc[I], PlayerLoc2D) > LodDistSq;
		if (Peds.bSnapVisual[I] || bFar)
		{
			Peds.VisLoc[I] = Target;
			Peds.VisYaw[I] = TargetYaw;
			Peds.bSnapVisual[I] = 0;
		}
		else
		{
			Peds.VisLoc[I] += (Target - Peds.VisLoc[I]) * Alpha;
			Peds.VisYaw[I] += FMath::FindDeltaAngleDegrees(Peds.VisYaw[I], TargetYaw) * Alpha;
		}

		const bool bDown = Peds.Stun[I] > 0.f;
		const FRotator Rot(bDown ? 90.f : 0.f, Peds.VisYaw[I], 0.f);
		const double Z = bDown ? PedestrianSize.X * 0.5 : PedestrianSize.Y * 0.5;
		const FTransform Xf(Rot.Quaternion(), Peds.VisLoc[I] + FVector(0.0, 0.0, Z), PedScale);

		const int32 C = Peds.ColorIndex[I];
		const int32 Inst = Peds.InstanceIndex[I];
		if (PedXfBuffers.IsValidIndex(C) && PedXfBuffers[C].IsValidIndex(Inst))
		{
			PedXfBuffers[C][Inst] = Xf;
		}
	}

	// --- Batch-Upload (ein Aufruf je Komponente) --------------------------------------------------
	auto Upload = [](UInstancedStaticMeshComponent* Comp, const TArray<FTransform>& Xfs)
	{
		if (Comp && Xfs.Num() > 0 && Comp->GetInstanceCount() == Xfs.Num())
		{
			Comp->BatchUpdateInstancesTransforms(0, Xfs, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
		}
	};
	for (int32 C = 0; C < TrafficBodyISMs.Num(); ++C)
	{
		Upload(TrafficBodyISMs[C], TrafficXfBuffers[C]);
	}
	for (int32 C = 0; C < PedISMs.Num(); ++C)
	{
		Upload(PedISMs[C], PedXfBuffers[C]);
	}
	Upload(CabinISM, CabinXfBuffer);
	Upload(PoliceBodyISM, PoliceXfBuffer);
	Upload(SirenRedISM, SirenRedXfBuffer);
	Upload(SirenBlueISM, SirenBlueXfBuffer);
}

// =============================================================================================
// Schnittstellen
// =============================================================================================

void AMTAWorldAIManager::GetMinimapAgents(TArray<FMTAMinimapAgent>& Out, bool bIncludePedestrians) const
{
	Out.Reset();
	const bool bRed = IsSirenPhaseRed();
	const bool bSirenOn = CachedHeat > 0.f;

	for (int32 I = 0; I < Vehicles.Num(); ++I)
	{
		if (!Vehicles.bActive[I])
		{
			continue;
		}
		FMTAMinimapAgent& A = Out.AddDefaulted_GetRef();
		A.Location = Vehicles.VisLoc[I];
		A.YawDegrees = Vehicles.VisYaw[I];
		A.Kind = Vehicles.Kind[I];
		if (A.Kind == EMTAAgentKind::Police)
		{
			A.bSirenActive = bSirenOn && Vehicles.PoliceState[I] != EMTAPoliceState::Calm;
			A.bLightPhaseRed = A.bSirenActive && bRed;
		}
	}

	if (bIncludePedestrians)
	{
		for (int32 I = 0; I < Peds.Num(); ++I)
		{
			FMTAMinimapAgent& A = Out.AddDefaulted_GetRef();
			A.Location = Peds.VisLoc[I];
			A.YawDegrees = Peds.VisYaw[I];
			A.Kind = EMTAAgentKind::Pedestrian;
		}
	}
}

void AMTAWorldAIManager::ReportCrime(float HeatAmount, FVector Location)
{
	if (HeatSubsystem)
	{
		HeatSubsystem->AddPlayerHeat(PlayerIndex, HeatAmount, GetDistrictAt(Location));
		CachedHeat = HeatSubsystem->GetGlobalHeat(PlayerIndex);
	}
}

FName AMTAWorldAIManager::GetDistrictAt(const FVector& Location) const
{
	if (DistrictResolver)
	{
		return DistrictResolver(Location);
	}
	if (FallbackDistrictNames.Num() == 0)
	{
		return NAME_None;
	}
	// Ersatzraster: 3 Spalten (X) x 2 Reihen (Y), bis die echte Bezirkskarte (M3) existiert.
	const float ColWidth = (2.f * MTACity::Half) / 3.f;
	const int32 Col = FMath::Clamp(FMath::FloorToInt((static_cast<float>(Location.X) + MTACity::Half) / ColWidth), 0, 2);
	const int32 Row = Location.Y < 0.0 ? 0 : 1;
	const int32 Idx = FMath::Min(Row * 3 + Col, FallbackDistrictNames.Num() - 1);
	return FallbackDistrictNames[Idx];
}
