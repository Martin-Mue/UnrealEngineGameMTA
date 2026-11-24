#include "MTAChaseTarget.h"

#include "MTATrafficTypes.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** Trail-Abtastung (s) und maximale Laenge der Spur (Eintraege). */
	constexpr float MTAChaseTrailInterval = 0.1f;
	constexpr int32 MTAChaseTrailMax = 120;

	FVector2D MTAChaseAxisDir(uint8 InAxis, int8 InSign)
	{
		return InAxis == 0 ? FVector2D((double)InSign, 0.0) : FVector2D(0.0, (double)InSign);
	}

	bool MTAChaseLineValid(int32 Index)
	{
		return Index >= 0 && Index <= MTACity::Blocks;
	}
}

AMTAChaseTarget::AMTAChaseTarget()
{
	// Bewegung kommt vom Runner (StepTarget), daher kein eigener Tick.
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	SceneRoot->SetMobility(EComponentMobility::Movable);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(SceneRoot);
	CabinMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CabinMesh"));
	CabinMesh->SetupAttachment(SceneRoot);
	BeaconMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BeaconMesh"));
	BeaconMesh->SetupAttachment(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (CubeFinder.Succeeded())
	{
		BodyMesh->SetStaticMesh(CubeFinder.Object);
		CabinMesh->SetStaticMesh(CubeFinder.Object);
		BeaconMesh->SetStaticMesh(CubeFinder.Object);
	}
	if (MaterialFinder.Succeeded())
	{
		BaseMaterial = MaterialFinder.Object;
	}

	CabinMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BeaconMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BeaconMesh->SetCastShadow(false);
	BodyMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
}

void AMTAChaseTarget::BeginPlay()
{
	Super::BeginPlay();

	HitPoints = MaxHitPoints;

	// Engine-Wuerfel ist 100 cm gross: auf Fahrzeugmasse skalieren
	BodyMesh->SetRelativeScale3D(BodySize / 100.f);
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, 30.f + BodySize.Z * 0.5f));
	CabinMesh->SetRelativeScale3D(CabinSize / 100.f);
	CabinMesh->SetRelativeLocation(FVector(-BodySize.X * 0.08f, 0.f, 30.f + BodySize.Z + CabinSize.Z * 0.5f));
	BeaconMesh->SetRelativeScale3D(FVector(0.5f, 1.2f, 0.18f));
	BeaconMesh->SetRelativeLocation(FVector(-BodySize.X * 0.08f, 0.f, 30.f + BodySize.Z + CabinSize.Z + 10.f));

	if (!bPhysicalCollision)
	{
		BodyMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}

	if (BaseMaterial)
	{
		BodyMID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		CabinMID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		BeaconMID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		if (BodyMID) { BodyMesh->SetMaterial(0, BodyMID); }
		if (CabinMID)
		{
			CabinMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.05f, 0.07f, 0.1f));
			CabinMesh->SetMaterial(0, CabinMID);
		}
		if (BeaconMID)
		{
			BeaconMID->SetVectorParameterValue(TEXT("Color"), BeaconColor);
			BeaconMesh->SetMaterial(0, BeaconMID);
		}
	}
	SetBodyColor(BodyColor);
}

void AMTAChaseTarget::SetBodyColor(const FLinearColor& InColor)
{
	BodyColor = InColor;
	if (BodyMID)
	{
		BodyMID->SetVectorParameterValue(TEXT("Color"), BodyColor);
	}
}

void AMTAChaseTarget::SetDisabled(bool bInDisabled)
{
	bDisabled = bInDisabled;
	if (bDisabled)
	{
		Speed = 0.f;
		SimVelocity = FVector::ZeroVector;
		Mode = EMTAChaseTargetMode::Idle;
	}
	if (BeaconMID)
	{
		BeaconMID->SetVectorParameterValue(TEXT("Color"), bDisabled ? FLinearColor(0.1f, 0.1f, 0.1f) : BeaconColor);
	}
}

// --- Initialisierung ---------------------------------------------------------------------------

void AMTAChaseTarget::InitFlee(const FVector& Near, const FVector& AwayFrom, int32 Seed)
{
	Rng.Initialize(Seed);
	Mode = EMTAChaseTargetMode::Flee;
	HitPoints = MaxHitPoints;
	bDisabled = false;

	// Naechste Strassenlinie: die Achse mit dem kleineren Abstand zur Linienmitte
	const int32 LineX = MTACity::NearestLine(Near.X);
	const int32 LineY = MTACity::NearestLine(Near.Y);
	const float DX = FMath::Abs(Near.X - MTACity::RoadCenter(LineX));
	const float DY = FMath::Abs(Near.Y - MTACity::RoadCenter(LineY));

	FVector2D Away(Near.X - AwayFrom.X, Near.Y - AwayFrom.Y);
	if (Away.IsNearlyZero())
	{
		Away = FVector2D(1.0, 0.0);
	}
	Away.Normalize();

	if (DY <= DX)
	{
		// Liegt auf einer Y-Linie: faehrt entlang X
		Axis = 0;
		Line = LineY;
		Pos = FMath::Clamp((float)Near.X, MTACity::RoadCenter(0), MTACity::RoadCenter(MTACity::Blocks));
		Sign = Away.X >= 0.0 ? 1 : -1;
	}
	else
	{
		Axis = 1;
		Line = LineX;
		Pos = FMath::Clamp((float)Near.Y, MTACity::RoadCenter(0), MTACity::RoadCenter(MTACity::Blocks));
		Sign = Away.Y >= 0.0 ? 1 : -1;
	}
	// Am Stadtrand nicht nach aussen starten
	if ((Sign > 0 && Pos >= MTACity::RoadCenter(MTACity::Blocks) - 10.f) || (Sign < 0 && Pos <= MTACity::RoadCenter(0) + 10.f))
	{
		Sign = (int8)-Sign;
	}
	Speed = CruiseSpeed * 0.5f;
	GroundTimer = 0.f;
	UpdateLaneLocation();
	bVisualInitialized = false;
	ApplyVisual(0.f, true);
}

void AMTAChaseTarget::InitRoute(const TArray<FVector>& Points, float InSpeed, float Lateral)
{
	Mode = EMTAChaseTargetMode::Route;
	RoutePoints = Points;
	RouteCumulative.Reset();
	RouteTotal = 0.f;
	RouteDistance = 0.f;
	RouteSpeed = InSpeed;
	RouteLateral = Lateral;
	bRouteStarted = false;
	HitPoints = MaxHitPoints;
	bDisabled = false;

	for (int32 i = 0; i < RoutePoints.Num(); ++i)
	{
		if (i > 0)
		{
			RouteTotal += FVector::Dist2D(RoutePoints[i - 1], RoutePoints[i]);
		}
		RouteCumulative.Add(RouteTotal);
	}
	if (RoutePoints.Num() > 0)
	{
		SimLocation = RoutePoints[0];
		GroundZ = RoutePoints[0].Z;
		if (RoutePoints.Num() > 1)
		{
			SimForward = (RoutePoints[1] - RoutePoints[0]).GetSafeNormal2D();
		}
		const FVector Right(-SimForward.Y, SimForward.X, 0.f);
		SimLocation += Right * RouteLateral;
	}
	Speed = 0.f;
	bVisualInitialized = false;
	ApplyVisual(0.f, true);
}

void AMTAChaseTarget::InitTrail(AMTAChaseTarget* InLeader, float DelaySeconds)
{
	Mode = EMTAChaseTargetMode::Trail;
	Leader = InLeader;
	TrailDelay = FMath::Max(0.2f, DelaySeconds);
	HitPoints = MaxHitPoints;
	bDisabled = false;
	if (InLeader)
	{
		SimForward = InLeader->GetSimForward();
		SimLocation = InLeader->GetSimLocation() - SimForward * (BodySize.X + 250.f) * (TrailDelay / 1.2f);
		GroundZ = InLeader->GetSimLocation().Z;
	}
	bVisualInitialized = false;
	ApplyVisual(0.f, true);
}

// --- Simulation --------------------------------------------------------------------------------

void AMTAChaseTarget::StepTarget(float DeltaSeconds, const FMTAChasePlayerContext& Player)
{
	const float Dt = FMath::Clamp(DeltaSeconds, 0.f, 0.5f);
	bHitLastStep = false;
	LastHitDamage = 0.f;
	HitCooldownLeft = FMath::Max(0.f, HitCooldownLeft - Dt);

	if (!bDisabled)
	{
		switch (Mode)
		{
		case EMTAChaseTargetMode::Flee:
			StepFlee(Dt, Player);
			break;
		case EMTAChaseTargetMode::Route:
			StepRoute(Dt);
			break;
		case EMTAChaseTargetMode::Trail:
			StepTrail(Dt);
			break;
		default:
			Speed = 0.f;
			SimVelocity = FVector::ZeroVector;
			break;
		}
		HandleContact(Dt, Player);
	}

	UpdateGround(Dt, Player);
	RecordTrail(Dt);
	ApplyVisual(Dt, false);
}

void AMTAChaseTarget::ApproachSpeed(float Desired, float Dt)
{
	if (Speed < Desired)
	{
		Speed = FMath::Min(Desired, Speed + Acceleration * Dt);
	}
	else
	{
		Speed = FMath::Max(Desired, Speed - Braking * Dt);
	}
}

void AMTAChaseTarget::StepFlee(float Dt, const FMTAChasePlayerContext& Player)
{
	const FVector2D Fwd = MTAChaseAxisDir(Axis, Sign);
	const FVector2D Loc2(SimLocation.X, SimLocation.Y);
	const FVector2D PlayerLoc2(Player.Location.X, Player.Location.Y);
	const float Dist = Player.bValid ? (float)FVector2D::Distance(Loc2, PlayerLoc2) : 1.0e7f;

	float Desired = CruiseSpeed * (1.f + 0.4f * EscapeSkill);
	if (Dist < PanicDistance)
	{
		Desired = MaxSpeed;
	}
	else if (Dist > CatchUpDistance)
	{
		Desired *= CatchUpFactor;
	}

	// Blockade durch den Spieler (Korridor vor dem Ziel)
	bool bBlocked = false;
	if (Player.bValid)
	{
		const FVector2D Delta = PlayerLoc2 - Loc2;
		const float Ahead = (float)FVector2D::DotProduct(Delta, Fwd);
		const float Lateral = FMath::Abs((float)FVector2D::CrossProduct(Fwd, Delta));
		const float PlayerSpeed = (float)FVector2D(Player.Velocity.X, Player.Velocity.Y).Size();
		bBlocked = Ahead > 0.f && Ahead < BlockLookAhead && Lateral < BlockHalfWidth && PlayerSpeed < 600.f;
	}
	if (bBlocked)
	{
		Desired = 0.f;
		BlockedTime += Dt;
	}
	else
	{
		BlockedTime = FMath::Max(0.f, BlockedTime - Dt * 0.5f);
	}

	ApproachSpeed(Desired, Dt);

	const float OldPos = Pos;
	Pos += (float)Sign * Speed * Dt;

	// Kreuzung ueberfahren? (Wechsel des Blockindex wie in der Welt-KI)
	const int32 OldIdx = FMath::FloorToInt((OldPos + MTACity::Half) / MTACity::Period);
	const int32 NewIdx = FMath::FloorToInt((Pos + MTACity::Half) / MTACity::Period);
	if (OldIdx != NewIdx)
	{
		const int32 Crossed = Sign > 0 ? NewIdx : OldIdx;
		HandleCrossing(Crossed, Player);
	}

	// Sicherheitsnetz am Stadtrand
	const float MinPos = MTACity::RoadCenter(0);
	const float MaxPos = MTACity::RoadCenter(MTACity::Blocks);
	if (Pos > MaxPos || Pos < MinPos)
	{
		Pos = FMath::Clamp(Pos, MinPos, MaxPos);
		Sign = (int8)-Sign;
		Speed *= 0.3f;
	}

	UpdateLaneLocation();
	SimVelocity = SimForward * Speed;
}

void AMTAChaseTarget::HandleCrossing(int32 CrossLine, const FMTAChasePlayerContext& Player)
{
	if (!MTAChaseLineValid(CrossLine))
	{
		return;
	}

	FVector2D Away(SimLocation.X - Player.Location.X, SimLocation.Y - Player.Location.Y);
	if (!Player.bValid || Away.IsNearlyZero())
	{
		Away = MTAChaseAxisDir(Axis, Sign);
	}
	Away.Normalize();

	struct FOption
	{
		uint8 OptAxis;
		int8 OptSign;
		bool bStraight;
	};
	TArray<FOption, TInlineAllocator<3>> Options;
	if (MTAChaseLineValid(CrossLine + Sign))
	{
		Options.Add({ Axis, Sign, true });
	}
	for (int8 Turn : { (int8)-1, (int8)1 })
	{
		if (MTAChaseLineValid(Line + Turn))
		{
			Options.Add({ (uint8)(1 - Axis), Turn, false });
		}
	}

	if (Options.Num() == 0)
	{
		Sign = (int8)-Sign;
		Speed *= 0.4f;
		return;
	}

	int32 BestIndex = 0;
	float BestScore = -1.0e9f;
	for (int32 i = 0; i < Options.Num(); ++i)
	{
		const FVector2D Dir = MTAChaseAxisDir(Options[i].OptAxis, Options[i].OptSign);
		const float Score = Rng.FRand() * (1.2f - EscapeSkill)
			+ EscapeSkill * 1.5f * (float)FVector2D::DotProduct(Dir, Away)
			+ (Options[i].bStraight ? 0.3f : 0.f);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestIndex = i;
		}
	}

	const FOption& Choice = Options[BestIndex];
	if (!Choice.bStraight)
	{
		// Abbiegen: Ueberhang auf die neue Achse uebertragen
		const float Overshoot = FMath::Abs(Pos - MTACity::RoadCenter(CrossLine));
		const int32 OldLine = Line;
		Axis = Choice.OptAxis;
		Sign = Choice.OptSign;
		Line = CrossLine;
		Pos = MTACity::RoadCenter(OldLine) + (float)Sign * Overshoot;
		Speed *= 0.65f;
	}
}

void AMTAChaseTarget::UpdateLaneLocation()
{
	// Rechtsverkehr: rechts von +X ist +Y, rechts von +Y ist -X (UE-Koordinaten)
	if (Axis == 0)
	{
		SimLocation.X = Pos;
		SimLocation.Y = MTACity::RoadCenter(Line) + (float)Sign * LaneOffset;
		SimForward = FVector((float)Sign, 0.f, 0.f);
	}
	else
	{
		SimLocation.X = MTACity::RoadCenter(Line) - (float)Sign * LaneOffset;
		SimLocation.Y = Pos;
		SimForward = FVector(0.f, (float)Sign, 0.f);
	}
	SimLocation.Z = GroundZ;
}

void AMTAChaseTarget::StepRoute(float Dt)
{
	if (!bRouteStarted || RoutePoints.Num() < 2)
	{
		ApproachSpeed(0.f, Dt);
		SimVelocity = FVector::ZeroVector;
		return;
	}

	// Segment suchen
	int32 Seg = 0;
	while (Seg < RouteCumulative.Num() - 2 && RouteDistance >= RouteCumulative[Seg + 1])
	{
		++Seg;
	}
	const float SegEnd = RouteCumulative[Seg + 1];
	const float ToCorner = SegEnd - RouteDistance;

	float Desired = RouteSpeed * SpeedScale;
	if (ToCorner < 1500.f && Seg + 2 < RoutePoints.Num())
	{
		Desired *= 0.6f;
	}
	ApproachSpeed(Desired, Dt);
	RouteDistance = FMath::Min(RouteTotal, RouteDistance + Speed * Dt);

	// Position auf der Strecke
	while (Seg < RouteCumulative.Num() - 2 && RouteDistance >= RouteCumulative[Seg + 1])
	{
		++Seg;
	}
	const FVector A = RoutePoints[Seg];
	const FVector B = RoutePoints[Seg + 1];
	const float SegLen = FMath::Max(1.f, RouteCumulative[Seg + 1] - RouteCumulative[Seg]);
	const float Alpha = FMath::Clamp((RouteDistance - RouteCumulative[Seg]) / SegLen, 0.f, 1.f);
	SimForward = (B - A).GetSafeNormal2D();
	const FVector Right(-SimForward.Y, SimForward.X, 0.f);
	SimLocation = FMath::Lerp(A, B, Alpha) + Right * RouteLateral;
	SimLocation.Z = GroundZ;
	SimVelocity = SimForward * Speed;

	if (IsRouteFinished())
	{
		Speed = 0.f;
		SimVelocity = FVector::ZeroVector;
	}
}

void AMTAChaseTarget::StepTrail(float Dt)
{
	AMTAChaseTarget* L = Leader.Get();
	if (!L)
	{
		ApproachSpeed(0.f, Dt);
		SimVelocity = FVector::ZeroVector;
		return;
	}
	FVector NewLoc;
	FVector NewFwd;
	if (!L->GetTrailSample(TrailDelay, NewLoc, NewFwd))
	{
		NewFwd = L->GetSimForward();
		NewLoc = L->GetSimLocation() - NewFwd * (BodySize.X + 250.f) * (TrailDelay / 1.2f);
	}
	const FVector Delta = NewLoc - SimLocation;
	Speed = Dt > 0.f ? (float)Delta.Size2D() / Dt : 0.f;
	SimVelocity = Dt > 0.f ? Delta / Dt : FVector::ZeroVector;
	SimLocation = NewLoc;
	SimLocation.Z = GroundZ;
	if (!NewFwd.IsNearlyZero())
	{
		SimForward = NewFwd;
	}
}

void AMTAChaseTarget::HandleContact(float Dt, const FMTAChasePlayerContext& Player)
{
	if (!Player.bValid || HitCooldownLeft > 0.f)
	{
		return;
	}
	const FVector D = SimLocation - Player.Location;
	const float Dist = (float)D.Size2D();
	if (Dist > ContactRadius || Dist < KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FVector Normal = D.GetSafeNormal2D();
	const FVector RelVel = Player.Velocity - SimVelocity;
	const float Closing = (float)FVector::DotProduct(FVector(RelVel.X, RelVel.Y, 0.f), Normal);
	if (Closing < RamMinClosingSpeed)
	{
		return;
	}
	const float Damage = 8.f + Closing / 100.f * 1.6f;
	HitPoints -= Damage;
	Speed *= 0.5f;
	HitCooldownLeft = RamCooldown;
	bHitLastStep = true;
	LastHitDamage = Damage;
}

void AMTAChaseTarget::RecordTrail(float Dt)
{
	TrailTimer += Dt;
	if (TrailTimer < MTAChaseTrailInterval && TrailLocations.Num() > 0)
	{
		return;
	}
	TrailTimer = 0.f;
	TrailLocations.Add(SimLocation);
	TrailForwards.Add(SimForward);
	if (TrailLocations.Num() > MTAChaseTrailMax)
	{
		TrailLocations.RemoveAt(0);
		TrailForwards.RemoveAt(0);
	}
}

bool AMTAChaseTarget::GetTrailSample(float DelaySeconds, FVector& OutLocation, FVector& OutForward) const
{
	const int32 Back = FMath::RoundToInt(DelaySeconds / MTAChaseTrailInterval);
	const int32 Index = TrailLocations.Num() - 1 - Back;
	if (Index < 0 || !TrailLocations.IsValidIndex(Index))
	{
		return false;
	}
	OutLocation = TrailLocations[Index];
	OutForward = TrailForwards[Index];
	return true;
}

void AMTAChaseTarget::UpdateGround(float Dt, const FMTAChasePlayerContext& Player)
{
	GroundTimer -= Dt;
	if (GroundTimer > 0.f)
	{
		return;
	}
	GroundTimer = 0.3f;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MTAChaseGround), false, this);
	if (AActor* PlayerActor = Player.PlayerActor.Get())
	{
		Params.AddIgnoredActor(PlayerActor);
	}
	const FVector Start(SimLocation.X, SimLocation.Y, 50000.f);
	const FVector End(SimLocation.X, SimLocation.Y, -50000.f);
	if (World->LineTraceSingleByObjectType(Hit, Start, End, FCollisionObjectQueryParams(ECC_WorldStatic), Params))
	{
		GroundZ = (float)Hit.ImpactPoint.Z;
		SimLocation.Z = GroundZ;
	}
}

void AMTAChaseTarget::ApplyVisual(float Dt, bool bSnap)
{
	const FRotator TargetRot = SimForward.IsNearlyZero() ? GetActorRotation() : SimForward.Rotation();
	if (bSnap || !bVisualInitialized)
	{
		bVisualInitialized = true;
		SetActorLocationAndRotation(SimLocation, FRotator(0.f, TargetRot.Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}
	const float Alpha = 1.f - FMath::Exp(-Dt * VisualSmoothing);
	const FVector NewLoc = FMath::Lerp(GetActorLocation(), SimLocation, Alpha);
	const FRotator NewRot = FMath::RInterpTo(GetActorRotation(), FRotator(0.f, TargetRot.Yaw, 0.f), Dt, VisualSmoothing);
	SetActorLocationAndRotation(NewLoc, NewRot, false, nullptr, ETeleportType::None);
}
