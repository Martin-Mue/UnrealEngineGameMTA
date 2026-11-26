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
#include "Materials/MaterialParameters.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/SoftObjectPath.h"

DEFINE_LOG_CATEGORY_STATIC(LogMTAWorldAIModels, Log, All);

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

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	if (CubeFinder.Succeeded())
	{
		VehicleBodyMesh = CubeFinder.Object;
		VehicleCabinMesh = CubeFinder.Object;
		SirenMesh = CubeFinder.Object;
		CubeShapeMesh = CubeFinder.Object;
	}
	if (CylinderFinder.Succeeded())
	{
		PedestrianMesh = CylinderFinder.Object;
	}
	if (SphereFinder.Succeeded())
	{
		PedestrianHeadMesh = SphereFinder.Object;
		VehicleLightMesh = SphereFinder.Object;
	}
	// Scheinwerfer/Ruecklichter: emissives Projekt-Material (tools/ue/build_materials.py, HLSL_LAMP).
	VehicleLightMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/MTA6/Materials/M_MTA_LampEmissive.M_MTA_LampEmissive")));
	if (ShapeMatFinder.Succeeded())
	{
		FallbackColorMaterial = ShapeMatFinder.Object;
	}

	// Fahrzeugmodelle aus den Epic-Vorlagen (weich referenziert: fehlen sie, bleibt der Platzhalter).
	SportsBodyAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Vehicles/SportsCar/SM_SportsCar.SM_SportsCar")));
	SportsGlassAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Vehicles/SportsCar/SM_SportsCar_Glass.SM_SportsCar_Glass")));
	SportsWheelAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Vehicles/SportsCar/SM_SportsCar_Wheel.SM_SportsCar_Wheel")));
	OffroadBodyAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Vehicles/OffroadCar/SM_Offroad_Body.SM_Offroad_Body")));
	OffroadWheelAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Vehicles/OffroadCar/SM_Offroad_Tire.SM_Offroad_Tire")));
	ConceptBodyAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/ConceptCar/Car/SM_AutomotiveTP_Car.SM_AutomotiveTP_Car")));

	// Hauttoene der Massen-Passanten (hell bis dunkel).
	PedestrianSkinColors = {
		FLinearColor(0.80f, 0.60f, 0.48f), FLinearColor(0.64f, 0.45f, 0.33f),
		FLinearColor(0.42f, 0.28f, 0.19f), FLinearColor(0.24f, 0.15f, 0.10f) };

	// Helle, satte Lackfarben (linear), damit die Autos nachts im Laternenlicht erkennbar bleiben:
	// Weiss, Silber, Rot, Blau, Gelb, Orange, Gruen, Tuerkis. Keine fast schwarzen Toene (nur Polizei).
	TrafficColors = {
		FLinearColor(0.92f, 0.92f, 0.90f), FLinearColor(0.62f, 0.64f, 0.68f), FLinearColor(0.85f, 0.06f, 0.05f),
		FLinearColor(0.08f, 0.26f, 0.92f), FLinearColor(0.95f, 0.76f, 0.08f), FLinearColor(0.95f, 0.36f, 0.04f),
		FLinearColor(0.12f, 0.70f, 0.20f), FLinearColor(0.06f, 0.70f, 0.66f) };
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
	// Obergrenze aller Fahrzeuge (Leistung): Polizei behaelt ihre Slots, der Verkehr wird gekuerzt.
	const int32 VehicleCap = MaxTotalVehicles > 0 ? MaxTotalVehicles : TNumericLimits<int32>::Max();
	NumPoliceSlots = FMath::Clamp(Police.MaxPolice, 0, VehicleCap);
	NumTrafficSlots = FMath::Clamp(Traffic.NumTrafficVehicles, 0, FMath::Max(0, VehicleCap - NumPoliceSlots));
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

uint32 AMTAWorldAIManager::HashIndex(uint32 X)
{
	// Ganzzahl-Hash (deterministisch, unabhaengig vom Zufallsstrom der Simulation).
	X ^= X >> 16;
	X *= 0x7feb352dU;
	X ^= X >> 15;
	X *= 0x846ca68bU;
	X ^= X >> 16;
	return X;
}

UInstancedStaticMeshComponent* AMTAWorldAIManager::CreateInstancer(const FName& InName, UStaticMesh* Mesh, const FLinearColor& Color, int32 Count, bool bShadows)
{
	if (!Mesh || Count <= 0)
	{
		return nullptr;
	}

	UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(this, InName);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetCastShadow(bShadows);
	Comp->bCastFarShadow = false;
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

UInstancedStaticMeshComponent* AMTAWorldAIManager::CreateModelInstancer(const FName& InName, UStaticMesh* Mesh, int32 Count, bool bShadows, const FIntPoint& Cull)
{
	if (!Mesh || Count <= 0)
	{
		return nullptr;
	}

	UInstancedStaticMeshComponent* Comp = NewObject<UInstancedStaticMeshComponent>(this, InName);
	Comp->SetMobility(EComponentMobility::Movable);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->SetCanEverAffectNavigation(false);
	Comp->SetCastShadow(bShadows);
	// Keine fernen Kaskaden: Fahrzeugschatten nur im Nahbereich.
	Comp->bCastFarShadow = false;
	Comp->SetStaticMesh(Mesh);
	Comp->SetupAttachment(GetRootComponent());
	Comp->RegisterComponent();
	AddInstanceComponent(Comp);
	Comp->SetCullDistances(Cull.X, Cull.Y);

	const FTransform Hidden = HiddenTransform();
	for (int32 N = 0; N < Count; ++N)
	{
		Comp->AddInstance(Hidden, /*bWorldSpace*/ true);
	}
	Comp->MarkRenderStateDirty();
	return Comp;
}

// =============================================================================================
// Fahrzeugmodelle (Epic-Vorlagen)
// =============================================================================================

bool AMTAWorldAIManager::FindPaintSlot(const UStaticMesh* Mesh, int32& OutSlot, FName& OutParam)
{
	OutSlot = INDEX_NONE;
	OutParam = NAME_None;
	if (!Mesh)
	{
		return false;
	}

	// Lack-Slot ueber den Materialnamen (MI_SportsCarBody, MI_OffroadCar_Main, MI_CarPaint), dann den
	// Farbparameter im Material suchen (M_SportsCarBase: "Paint Tint", M_CarPaint: "PaintColor").
	static const TCHAR* SlotHints[] = { TEXT("CarPaint"), TEXT("SportsCarBody"), TEXT("OffroadCar_Main"), TEXT("Body"), TEXT("Paint"), TEXT("Main") };
	static const TCHAR* ParamNames[] = { TEXT("Paint Tint"), TEXT("PaintColor"), TEXT("Paint Color"), TEXT("BodyColor"), TEXT("Color"), TEXT("BaseColor"), TEXT("Tint") };

	const TArray<FStaticMaterial>& Mats = Mesh->GetStaticMaterials();
	for (const TCHAR* Hint : SlotHints)
	{
		for (int32 S = 0; S < Mats.Num(); ++S)
		{
			const UMaterialInterface* Mat = Mats[S].MaterialInterface;
			if (!Mat)
			{
				continue;
			}
			if (!Mat->GetName().Contains(Hint) && !Mats[S].MaterialSlotName.ToString().Contains(Hint))
			{
				continue;
			}
			for (const TCHAR* Param : ParamNames)
			{
				FLinearColor Value;
				if (Mat->GetVectorParameterValue(FHashedMaterialParameterInfo(FName(Param)), Value))
				{
					OutSlot = S;
					OutParam = FName(Param);
					return true;
				}
			}
		}
	}
	return false;
}

bool AMTAWorldAIManager::ComputeModelFit(EMTATrafficBodyModel Model, UStaticMesh* Body, UStaticMesh* Wheel, FMTATrafficModelFit& Out) const
{
	Out = FMTATrafficModelFit();
	if (!Body)
	{
		return false;
	}
	const FBox BodyBox = Body->GetBoundingBox();
	const FVector BodySize = BodyBox.GetSize();
	if (!BodyBox.IsValid || BodySize.X < 1.0 || BodySize.Y < 1.0 || BodySize.Z < 1.0)
	{
		return false;
	}

	const bool bSports = Model == EMTATrafficBodyModel::Sports;
	const bool bOffroad = Model == EMTATrafficBodyModel::Offroad;
	const FVector2D Target = bSports ? SportsTargetSize : (bOffroad ? OffroadTargetSize : ConceptTargetSize);

	// Vorlagen schauen in +X: Laenge entlang X, Breite entlang Y; Hoehe proportional mitskaliert.
	const double Sx = Target.X / BodySize.X;
	const double Sy = Target.Y / BodySize.Y;
	const double Sz = 0.5 * (Sx + Sy);
	Out.BodyScale = FVector(Sx, Sy, Sz);
	Out.Length = static_cast<float>(Target.X);
	Out.Width = static_cast<float>(Target.Y);
	Out.RawLength = static_cast<float>(BodySize.X);

	double GroundRefZ = BodyBox.Min.Z;   // Mesh-Z, das auf dem Boden (+ Bodenfreiheit) liegen soll
	double Clearance = 0.0;              // Concept Car: Raeder im Mesh -> Unterkante = Reifen

	if (Wheel)
	{
		const FBox WheelBox = Wheel->GetBoundingBox();
		const FVector WheelSize = WheelBox.GetSize();
		if (WheelBox.IsValid && WheelSize.Z > 1.0)
		{
			if (WheelSize.X > 150.0 || WheelSize.Y > 150.0)
			{
				// Ein Mesh mit allen vier Raedern: gleicher Pivot wie die Karosserie.
				Out.bWheelSet = true;
				GroundRefZ = FMath::Min(GroundRefZ, WheelBox.Min.Z);
			}
			else
			{
				// Einzelrad: 4 Instanzen. Achse = schmalste horizontale Ausdehnung.
				Out.bSeparateWheels = true;
				Out.bWheelAxleAlongX = WheelSize.X < WheelSize.Y;
				const double MeshDiameter = FMath::Max(Out.bWheelAxleAlongX ? WheelSize.Y : WheelSize.X, WheelSize.Z);
				const double MeshWidth = FMath::Min(WheelSize.X, WheelSize.Y);
				const double Diameter = bSports ? WheelDiameter.X : WheelDiameter.Y;
				const double Ws = Diameter / FMath::Max(MeshDiameter, 1.0);
				Out.WheelScale = FVector(Ws);
				Out.WheelMeshCenter = WheelBox.GetCenter();
				Out.WheelRadius = static_cast<float>(Diameter * 0.5);
				Clearance = bSports ? BodyClearance.X : BodyClearance.Y;

				const FVector2D Axle = bSports ? SportsAxleFactor : OffroadAxleFactor;
				const double Lat = Target.Y * 0.5 - MeshWidth * Ws * 0.5 - 3.0;
				const double FrontX = Axle.X * Target.X;
				const double RearX = -Axle.Y * Target.X;
				const double R = Diameter * 0.5;
				Out.WheelCenters[0] = FVector(FrontX, -Lat, R);  // vorn links
				Out.WheelCenters[1] = FVector(FrontX, Lat, R);   // vorn rechts
				Out.WheelCenters[2] = FVector(RearX, -Lat, R);   // hinten links
				Out.WheelCenters[3] = FVector(RearX, Lat, R);    // hinten rechts
			}
		}
	}

	// Pivot: Mitte in X/Y, Bezugs-Unterkante auf Bodenfreiheit.
	const FVector C = BodyBox.GetCenter();
	Out.BodyOffset = FVector(-C.X * Sx, -C.Y * Sy, -GroundRefZ * Sz + Clearance);
	Out.RoofZ = static_cast<float>(Out.BodyOffset.Z + BodyBox.Max.Z * Sz);

	// Lichter: Unterkante der Karosserie (nach Einpassung) bis Dach.
	const double BottomZ = Out.BodyOffset.Z + BodyBox.Min.Z * Sz;
	ComputeLightPlacement(Out, BottomZ, FMath::Max(10.0, static_cast<double>(Out.RoofZ) - BottomZ), false);

	FindPaintSlot(Body, Out.PaintSlot, Out.PaintParam);
	Out.bValid = true;
	return true;
}

void AMTAWorldAIManager::LoadVehicleModels()
{
	const int32 NumModels = static_cast<int32>(EMTATrafficBodyModel::Count);
	ModelBodyMeshes.Init(nullptr, NumModels);
	ModelWheelMeshes.Init(nullptr, NumModels);
	LoadedSportsGlass = nullptr;
	for (int32 M = 0; M < NumModels; ++M)
	{
		ModelFits[M] = FMTATrafficModelFit();
	}
	// Platzhalter ist immer verfuegbar.
	ModelFits[static_cast<int32>(EMTATrafficBodyModel::Box)].bValid = VehicleBodyMesh != nullptr;
	ModelFits[static_cast<int32>(EMTATrafficBodyModel::Box)].Length = static_cast<float>(VehicleBodySize.X);
	ModelFits[static_cast<int32>(EMTATrafficBodyModel::Box)].Width = static_cast<float>(VehicleBodySize.Y);
	// Platzhalter-Wuerfel: Unterkante 20 cm (wie in UpdateVisuals), Hoehe = Karosseriehoehe, glatte Enden.
	ComputeLightPlacement(ModelFits[static_cast<int32>(EMTATrafficBodyModel::Box)], 20.0, VehicleBodySize.Z, true);

	if (!bUseVehicleModels)
	{
		return;
	}

	auto LoadMesh = [](const TSoftObjectPtr<UStaticMesh>& Ref) -> UStaticMesh*
	{
		return Ref.IsNull() ? nullptr : Ref.LoadSynchronous();
	};

	const int32 SportsIdx = static_cast<int32>(EMTATrafficBodyModel::Sports);
	const int32 OffroadIdx = static_cast<int32>(EMTATrafficBodyModel::Offroad);
	const int32 ConceptIdx = static_cast<int32>(EMTATrafficBodyModel::Concept);

	ModelBodyMeshes[SportsIdx] = LoadMesh(SportsBodyAsset);
	ModelWheelMeshes[SportsIdx] = LoadMesh(SportsWheelAsset);
	ModelBodyMeshes[OffroadIdx] = LoadMesh(OffroadBodyAsset);
	ModelWheelMeshes[OffroadIdx] = LoadMesh(OffroadWheelAsset);
	ModelBodyMeshes[ConceptIdx] = LoadMesh(ConceptBodyAsset);
	LoadedSportsGlass = ModelBodyMeshes[SportsIdx] ? LoadMesh(SportsGlassAsset) : nullptr;

	for (int32 M = 0; M < static_cast<int32>(EMTATrafficBodyModel::Box); ++M)
	{
		const EMTATrafficBodyModel Model = static_cast<EMTATrafficBodyModel>(M);
		if (!ComputeModelFit(Model, ModelBodyMeshes[M], ModelWheelMeshes[M], ModelFits[M]))
		{
			UE_LOG(LogMTAWorldAIModels, Warning, TEXT("Welt-KI: Fahrzeugmodell %d nicht verfuegbar (Asset fehlt? tools/setup_template_assets.ps1)"), M);
			ModelBodyMeshes[M] = nullptr;
			ModelWheelMeshes[M] = nullptr;
			continue;
		}
		if (ModelFits[M].PaintSlot == INDEX_NONE)
		{
			UE_LOG(LogMTAWorldAIModels, Warning, TEXT("Welt-KI: Fahrzeugmodell %d ohne Lack-Parameter, Farben fehlen"), M);
		}
		UE_LOG(LogMTAWorldAIModels, Log, TEXT("Welt-KI: Modell %d roh %.0f cm -> %.0f x %.0f cm, Dach %.0f cm, Lack-Slot %d (%s), Raeder %s"),
			M, ModelFits[M].RawLength, ModelFits[M].Length, ModelFits[M].Width, ModelFits[M].RoofZ,
			ModelFits[M].PaintSlot, *ModelFits[M].PaintParam.ToString(),
			ModelFits[M].bSeparateWheels ? TEXT("einzeln") : (ModelFits[M].bWheelSet ? TEXT("Satz") : TEXT("im Mesh")));
	}
}

void AMTAWorldAIManager::ComputeLightPlacement(FMTATrafficModelFit& Fit, double BottomZ, double Height, bool bFlatEnds) const
{
	// Fahrtrichtung +X: Scheinwerfer vorn, Ruecklichter hinten; linke Seite (Y negativ) wird gespeichert.
	const double HalfL = Fit.Length * 0.5;
	const double HalfW = Fit.Width * 0.5;
	// Abgerundete Vorlagen-Karosserien: Einzug, damit das Licht in der Ecke sitzt und nicht davor schwebt.
	// Wuerfel: Licht ragt knapp aus der flachen Stirnseite.
	const double HeadInset = bFlatEnds ? FMath::Min(HeadLightPlacement.X, HeadLightSize.X * 0.3) : HeadLightPlacement.X;
	const double TailInset = bFlatEnds ? FMath::Min(TailLightPlacement.X, TailLightSize.X * 0.3) : TailLightPlacement.X;
	Fit.HeadLightLocal = FVector(HalfL - HeadInset, -HalfW * HeadLightPlacement.Y, BottomZ + Height * HeadLightPlacement.Z);
	Fit.TailLightLocal = FVector(-HalfL + TailInset, -HalfW * TailLightPlacement.Y, BottomZ + Height * TailLightPlacement.Z);
}

UMaterialInstanceDynamic* AMTAWorldAIManager::MakePaintMID(EMTATrafficBodyModel Model, const FLinearColor& Color)
{
	const int32 M = static_cast<int32>(Model);
	if (!ModelBodyMeshes.IsValidIndex(M) || !ModelBodyMeshes[M] || ModelFits[M].PaintSlot == INDEX_NONE)
	{
		return nullptr;
	}
	UMaterialInterface* BaseMat = ModelBodyMeshes[M]->GetMaterial(ModelFits[M].PaintSlot);
	UMaterialInstanceDynamic* MID = BaseMat ? UMaterialInstanceDynamic::Create(BaseMat, this) : nullptr;
	if (MID)
	{
		MID->SetVectorParameterValue(ModelFits[M].PaintParam, Color);
	}
	return MID;
}

EMTATrafficBodyModel AMTAWorldAIManager::PickModelForSlot(int32 Slot) const
{
	const float Weights[3] = {
		static_cast<float>(ModelWeights.X), static_cast<float>(ModelWeights.Y), static_cast<float>(ModelWeights.Z) };
	TArray<int32, TInlineAllocator<3>> Avail;
	float Total = 0.f;
	for (int32 M = 0; M < 3; ++M)
	{
		if (ModelBodyMeshes.IsValidIndex(M) && ModelBodyMeshes[M] && ModelFits[M].bValid && Weights[M] > 0.f)
		{
			Avail.Add(M);
			Total += Weights[M];
		}
	}
	if (Avail.Num() == 0 || Total <= 0.f)
	{
		return EMTATrafficBodyModel::Box;
	}
	// Jede verfuegbare Sorte kommt mindestens einmal vor, danach gewichtet.
	if (Slot < Avail.Num())
	{
		return static_cast<EMTATrafficBodyModel>(Avail[Slot]);
	}
	float R = static_cast<float>(HashIndex(static_cast<uint32>(Slot) * 2654435761U + 17U) % 10000U) / 10000.f * Total;
	for (const int32 M : Avail)
	{
		R -= Weights[M];
		if (R <= 0.f)
		{
			return static_cast<EMTATrafficBodyModel>(M);
		}
	}
	return static_cast<EMTATrafficBodyModel>(Avail.Last());
}

void AMTAWorldAIManager::InitVisuals()
{
	LoadVehicleModels();

	const int32 NumModels = static_cast<int32>(EMTATrafficBodyModel::Count);
	const int32 BoxIdx = static_cast<int32>(EMTATrafficBodyModel::Box);
	const int32 SportsIdx = static_cast<int32>(EMTATrafficBodyModel::Sports);
	const int32 NumVeh = Vehicles.Num();
	// Gruppenindex muss in uint8 (ColorIndex) passen.
	NumPaintColors = FMath::Clamp(TrafficColors.Num(), 1, 255 / NumModels);

	VehicleModel.Init(static_cast<uint8>(BoxIdx), NumVeh);
	WheelInstanceBase.Init(INDEX_NONE, NumVeh);
	GlassInstance.Init(INDEX_NONE, NumVeh);
	WheelSpin.Init(0.f, NumVeh);

	TArray<int32> WheelCounts;
	WheelCounts.Init(0, NumModels);
	int32 GlassCount = 0;
	bool bAnyBox = false;

	auto AssignParts = [&](int32 I, int32 M)
	{
		VehicleModel[I] = static_cast<uint8>(M);
		const FMTATrafficModelFit& Fit = ModelFits[M];
		if (ModelWheelMeshes.IsValidIndex(M) && ModelWheelMeshes[M] && (Fit.bSeparateWheels || Fit.bWheelSet))
		{
			WheelInstanceBase[I] = WheelCounts[M];
			WheelCounts[M] += Fit.bSeparateWheels ? 4 : 1;
		}
		if (M == SportsIdx && LoadedSportsGlass)
		{
			GlassInstance[I] = GlassCount++;
		}
		bAnyBox |= (M == BoxIdx);
	};

	// --- Zivilverkehr: eine Komponente je (Sorte, Lackfarbe) -----------------------------------
	const int32 NumGroups = NumModels * NumPaintColors;
	TArray<int32> GroupCounts;
	GroupCounts.Init(0, NumGroups);
	for (int32 I = 0; I < NumTrafficSlots; ++I)
	{
		const int32 M = static_cast<int32>(PickModelForSlot(I));
		const int32 Color = static_cast<int32>(HashIndex(static_cast<uint32>(I) * 31U + 7U) % static_cast<uint32>(NumPaintColors));
		const int32 G = M * NumPaintColors + Color;
		Vehicles.ColorIndex[I] = static_cast<uint8>(G);
		Vehicles.InstanceIndex[I] = GroupCounts[G]++;
		AssignParts(I, M);
	}
	TrafficBodyISMs.Reset();
	TrafficBodyISMs.SetNum(NumGroups);
	TrafficXfBuffers.Reset();
	TrafficXfBuffers.SetNum(NumGroups);
	for (int32 G = 0; G < NumGroups; ++G)
	{
		if (GroupCounts[G] <= 0)
		{
			continue;
		}
		const int32 M = G / NumPaintColors;
		const int32 C = G % NumPaintColors;
		const FLinearColor BaseCol = TrafficColors.IsValidIndex(C) ? TrafficColors[C] : FLinearColor::Gray;
		// Lack aufhellen ("Paint Tint" multipliziert die Textur, Werte > 1 erlaubt).
		const float Bright = FMath::Clamp(PaintBrightness, 0.1f, 4.f);
		const FLinearColor Col(BaseCol.R * Bright, BaseCol.G * Bright, BaseCol.B * Bright, 1.f);
		const FName CompName = *FString::Printf(TEXT("TrafficBody_%d_%d"), M, C);
		UInstancedStaticMeshComponent* Comp = nullptr;
		if (M == BoxIdx)
		{
			Comp = CreateInstancer(CompName, VehicleBodyMesh, Col, GroupCounts[G], bAgentsCastShadows);
			if (Comp)
			{
				Comp->SetCullDistances(VehicleCullDistance.X, VehicleCullDistance.Y);
			}
		}
		else
		{
			Comp = CreateModelInstancer(CompName, ModelBodyMeshes[M], GroupCounts[G], bAgentsCastShadows, VehicleCullDistance);
			UMaterialInstanceDynamic* Paint = Comp ? MakePaintMID(static_cast<EMTATrafficBodyModel>(M), Col) : nullptr;
			if (Paint)
			{
				Comp->SetMaterial(ModelFits[M].PaintSlot, Paint);
			}
		}
		TrafficBodyISMs[G] = Comp;
		TrafficXfBuffers[G].SetNum(GroupCounts[G]);
	}

	// --- Polizei: Sportwagen, dunkler Lack, weisser Streifen, Sirenenbalken ------------------------
	const bool bPoliceModel = ModelBodyMeshes.IsValidIndex(SportsIdx) && ModelBodyMeshes[SportsIdx] != nullptr;
	for (int32 I = FirstPoliceSlot; I < NumVeh; ++I)
	{
		Vehicles.ColorIndex[I] = 0;
		Vehicles.InstanceIndex[I] = I - FirstPoliceSlot;
		AssignParts(I, bPoliceModel ? SportsIdx : BoxIdx);
	}
	PolicePaintMID = nullptr;
	PoliceStripeMID = nullptr;
	if (bPoliceModel)
	{
		PoliceBodyISM = CreateModelInstancer(TEXT("PoliceBody"), ModelBodyMeshes[SportsIdx], NumPoliceSlots, bAgentsCastShadows, VehicleCullDistance);
		PolicePaintMID = PoliceBodyISM ? MakePaintMID(EMTATrafficBodyModel::Sports, PoliceColor) : nullptr;
		if (PolicePaintMID)
		{
			PoliceBodyISM->SetMaterial(ModelFits[SportsIdx].PaintSlot, PolicePaintMID);
		}
	}
	else
	{
		PoliceBodyISM = CreateInstancer(TEXT("PoliceBody"), VehicleBodyMesh, PoliceColor, NumPoliceSlots, bAgentsCastShadows);
		if (PoliceBodyISM)
		{
			PoliceBodyISM->SetCullDistances(VehicleCullDistance.X, VehicleCullDistance.Y);
		}
	}
	// Streifen: zweite Materialinstanz (BasicShapeMaterial "Color" weiss) auf einem flachen Quader.
	PoliceStripeISM = CreateInstancer(TEXT("PoliceStripe"), CubeShapeMesh, PoliceStripeColor, NumPoliceSlots, false);
	if (PoliceStripeISM)
	{
		PoliceStripeISM->SetCullDistances(VehicleCullDistance.X, VehicleCullDistance.Y);
		PoliceStripeMID = Cast<UMaterialInstanceDynamic>(PoliceStripeISM->GetMaterial(0));
	}
	SirenRedISM = CreateInstancer(TEXT("SirenRed"), SirenMesh, FLinearColor(1.f, 0.1f, 0.1f), NumPoliceSlots, false);
	SirenBlueISM = CreateInstancer(TEXT("SirenBlue"), SirenMesh, FLinearColor(0.15f, 0.3f, 1.f), NumPoliceSlots, false);
	for (UInstancedStaticMeshComponent* Siren : { SirenRedISM.Get(), SirenBlueISM.Get() })
	{
		if (Siren)
		{
			Siren->SetCullDistances(VehicleCullDistance.X, VehicleCullDistance.Y);
		}
	}
	PoliceXfBuffer.SetNum(NumPoliceSlots);
	StripeXfBuffer.SetNum(NumPoliceSlots);
	SirenRedXfBuffer.SetNum(NumPoliceSlots);
	SirenBlueXfBuffer.SetNum(NumPoliceSlots);

	// --- Raeder je Sorte, Glas des Sportwagens (ohne Schatten) -----------------------------------
	WheelISMs.Reset();
	WheelISMs.SetNum(NumModels);
	WheelXfBuffers.Reset();
	WheelXfBuffers.SetNum(NumModels);
	for (int32 M = 0; M < NumModels; ++M)
	{
		if (WheelCounts[M] > 0 && ModelWheelMeshes.IsValidIndex(M) && ModelWheelMeshes[M])
		{
			WheelISMs[M] = CreateModelInstancer(*FString::Printf(TEXT("TrafficWheels_%d"), M), ModelWheelMeshes[M], WheelCounts[M], false, VehicleCullDistance);
			WheelXfBuffers[M].SetNum(WheelCounts[M]);
		}
	}
	SportsGlassISM = GlassCount > 0 ? CreateModelInstancer(TEXT("SportsGlass"), LoadedSportsGlass, GlassCount, false, VehicleCullDistance) : nullptr;
	GlassXfBuffer.SetNum(SportsGlassISM ? GlassCount : 0);

	// --- Kabine nur fuer Platzhalter-Fahrzeuge (Index = Fahrzeugindex) ---------------------------
	CabinISM = bAnyBox ? CreateInstancer(TEXT("VehicleCabin"), VehicleCabinMesh, CabinColor, NumVeh, bAgentsCastShadows) : nullptr;
	if (CabinISM)
	{
		CabinISM->SetCullDistances(VehicleCullDistance.X, VehicleCullDistance.Y);
	}
	CabinXfBuffer.SetNum(CabinISM ? NumVeh : 0);

	// Schatten nur nah: je Karosserie-Komponente (Verkehr + Polizei als letzter Eintrag).
	BodyNearestDistSq.Init(TNumericLimits<float>::Max(), TrafficBodyISMs.Num() + 1);

	// --- Scheinwerfer und Ruecklichter (je 2 pro Fahrzeug) ---------------------------------------
	InitVehicleLights();

	// --- Fussgaenger: Koerper je Kleidungsfarbe, Kopf je Hautton -----------------------------------
	const int32 NumPedColors = FMath::Clamp(PedestrianColors.Num(), 1, 255);
	TArray<int32> PedCounts;
	PedCounts.Init(0, NumPedColors);
	for (int32 I = 0; I < Peds.Num(); ++I)
	{
		const int32 C = I % NumPedColors;
		Peds.ColorIndex[I] = static_cast<uint8>(C);
		Peds.InstanceIndex[I] = PedCounts[C]++;
	}
	PedISMs.Reset();
	PedISMs.SetNum(NumPedColors);
	PedXfBuffers.Reset();
	PedXfBuffers.SetNum(NumPedColors);
	for (int32 C = 0; C < NumPedColors; ++C)
	{
		const FLinearColor Col = PedestrianColors.IsValidIndex(C) ? PedestrianColors[C] : FLinearColor::White;
		PedISMs[C] = CreateInstancer(*FString::Printf(TEXT("Pedestrian_%d"), C), PedestrianMesh, Col, PedCounts[C], bAgentsCastShadows);
		if (PedISMs[C])
		{
			PedISMs[C]->SetCullDistances(PedestrianCullDistance.X, PedestrianCullDistance.Y);
		}
		PedXfBuffers[C].SetNum(PedCounts[C]);
	}

	const int32 NumSkins = FMath::Clamp(PedestrianSkinColors.Num(), 1, 255);
	PedHeadGroup.Init(0, Peds.Num());
	PedHeadInstance.Init(INDEX_NONE, Peds.Num());
	TArray<int32> HeadCounts;
	HeadCounts.Init(0, NumSkins);
	for (int32 I = 0; I < Peds.Num(); ++I)
	{
		const int32 S = static_cast<int32>(HashIndex(static_cast<uint32>(I) * 13U + 5U) % static_cast<uint32>(NumSkins));
		PedHeadGroup[I] = static_cast<uint8>(S);
		PedHeadInstance[I] = HeadCounts[S]++;
	}
	PedHeadISMs.Reset();
	PedHeadISMs.SetNum(NumSkins);
	PedHeadXfBuffers.Reset();
	PedHeadXfBuffers.SetNum(NumSkins);
	for (int32 S = 0; S < NumSkins; ++S)
	{
		const FLinearColor Col = PedestrianSkinColors.IsValidIndex(S) ? PedestrianSkinColors[S] : FLinearColor(0.7f, 0.5f, 0.4f);
		// Koepfe ohne Schatten (klein, viele): der Koerper wirft den Schatten.
		PedHeadISMs[S] = CreateInstancer(*FString::Printf(TEXT("PedestrianHead_%d"), S), PedestrianHeadMesh, Col, HeadCounts[S], false);
		if (PedHeadISMs[S])
		{
			PedHeadISMs[S]->SetCullDistances(PedestrianCullDistance.X, PedestrianCullDistance.Y);
		}
		PedHeadXfBuffers[S].SetNum(HeadCounts[S]);
	}

	bVisualsReady = true;
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

void AMTAWorldAIManager::WriteWheelTransforms(int32 I, const FQuat& Rot, const FVector& Base, float FrameDt)
{
	if (!VehicleModel.IsValidIndex(I) || !WheelInstanceBase.IsValidIndex(I))
	{
		return;
	}
	const int32 M = VehicleModel[I];
	const int32 B = WheelInstanceBase[I];
	if (B == INDEX_NONE || !WheelXfBuffers.IsValidIndex(M))
	{
		return;
	}
	TArray<FTransform>& Buf = WheelXfBuffers[M];
	const FMTATrafficModelFit& Fit = ModelFits[M];

	if (Fit.bWheelSet)
	{
		// Radsatz teilt den Pivot mit der Karosserie.
		if (Buf.IsValidIndex(B))
		{
			Buf[B] = FTransform(Rot, Base + Rot.RotateVector(Fit.BodyOffset), Fit.BodyScale);
		}
		return;
	}
	if (!Buf.IsValidIndex(B + 3))
	{
		return;
	}

	// Raddrehung aus dem Tempo (Grad), nur Darstellung.
	if (Fit.WheelRadius > 1.f && WheelSpin.IsValidIndex(I))
	{
		const float DegPerCm = 180.f / (UE_PI * Fit.WheelRadius);
		WheelSpin[I] = FMath::Fmod(WheelSpin[I] + Vehicles.Speed[I] * FrameDt * DegPerCm, 360.f);
	}
	const float SpinDeg = WheelSpin.IsValidIndex(I) ? WheelSpin[I] : 0.f;
	const FQuat Spin = FRotator(-SpinDeg, 0.f, 0.f).Quaternion();
	const FQuat AxleFix = Fit.bWheelAxleAlongX ? FRotator(0.f, 90.f, 0.f).Quaternion() : FQuat::Identity;
	const FQuat Mirror = FRotator(0.f, 180.f, 0.f).Quaternion();
	const FVector ScaledCenter = Fit.WheelMeshCenter * Fit.WheelScale;

	for (int32 W = 0; W < 4; ++W)
	{
		// Mesh ist fuer links modelliert (sonst bFlipWheelSides): rechte Seite gespiegelt per 180 Grad.
		const bool bRight = (W % 2) == 1;
		const bool bMirror = bFlipWheelSides ? !bRight : bRight;
		const FQuat WheelRot = Rot * Spin * (bMirror ? Mirror : FQuat::Identity) * AxleFix;
		const FVector Center = Base + Rot.RotateVector(Fit.WheelCenters[W]);
		Buf[B + W] = FTransform(WheelRot, Center - WheelRot.RotateVector(ScaledCenter), Fit.WheelScale);
	}
}

namespace MTAVehicleLights
{
	/** Custom-Data einer Licht-Instanz: [0..2] Farbe (M_MTA_LampEmissive: LampColor), [3] Emissiv (AgentMaterial). */
	static void WriteLightData(UInstancedStaticMeshComponent* Comp, int32 Index, const FLinearColor& Color, float Emissive)
	{
		if (!Comp || Index < 0 || Index >= Comp->GetInstanceCount())
		{
			return;
		}
		const float Data[4] = { Color.R, Color.G, Color.B, Emissive };
		Comp->SetCustomData(Index, MakeArrayView(Data, 4), /*bMarkRenderStateDirty*/ false);
	}

	static FLinearColor ScaleRGB(const FLinearColor& Color, float Factor)
	{
		return FLinearColor(Color.R * Factor, Color.G * Factor, Color.B * Factor, 1.f);
	}
}

void AMTAWorldAIManager::InitVehicleLights()
{
	const int32 NumVeh = Vehicles.Num();
	const int32 NumLights = NumVeh * 2;

	LightPrevSpeed.SetNum(NumVeh);
	for (int32 I = 0; I < NumVeh; ++I)
	{
		LightPrevSpeed[I] = Vehicles.Speed[I];
	}
	BrakeHold.Init(0.f, NumVeh);
	TailLightState.Init(0, NumVeh);
	HeadLightState.Init(0, NumVeh);

	// Emissives Projekt-Material; Leuchtstaerke ueber "LampLuminance" (Laternen: 600, Autos dezenter).
	VehicleLightMID = nullptr;
	UMaterialInterface* LightMat = VehicleLightMaterialAsset.IsNull() ? nullptr : VehicleLightMaterialAsset.LoadSynchronous();
	if (LightMat)
	{
		VehicleLightMID = UMaterialInstanceDynamic::Create(LightMat, this);
		if (VehicleLightMID)
		{
			VehicleLightMID->SetScalarParameterValue(FName(TEXT("LampLuminance")), VehicleLightLuminance);
		}
	}
	else
	{
		UE_LOG(LogMTAWorldAIModels, Warning, TEXT("Welt-KI: Lichtmaterial %s fehlt (tools/ue/build_materials.py), Fahrzeuglichter nicht emissiv"),
			*VehicleLightMaterialAsset.ToString());
	}

	HeadLightISM = NumLights > 0 ? CreateInstancer(TEXT("VehicleHeadLights"), VehicleLightMesh, HeadLightColor, NumLights, false) : nullptr;
	TailLightISM = NumLights > 0 ? CreateInstancer(TEXT("VehicleTailLights"), VehicleLightMesh, TailLightColor, NumLights, false) : nullptr;
	for (UInstancedStaticMeshComponent* Comp : { HeadLightISM.Get(), TailLightISM.Get() })
	{
		if (!Comp)
		{
			continue;
		}
		Comp->SetCullDistances(VehicleCullDistance.X, VehicleCullDistance.Y);
		if (VehicleLightMID)
		{
			Comp->SetMaterial(0, VehicleLightMID);
		}
	}

	// Grundzustand: Scheinwerfer warmweiss (Polizei kaltweiss), Ruecklichter gedimmt rot.
	for (int32 I = 0; I < NumVeh; ++I)
	{
		const bool bPolice = Vehicles.Kind[I] == EMTAAgentKind::Police;
		const FLinearColor Head = bPolice ? PoliceHeadLightColor : HeadLightColor;
		for (int32 S = 0; S < 2; ++S)
		{
			MTAVehicleLights::WriteLightData(HeadLightISM, I * 2 + S, Head, 2.f);
			MTAVehicleLights::WriteLightData(TailLightISM, I * 2 + S, TailLightColor, 1.f);
		}
	}
	for (UInstancedStaticMeshComponent* Comp : { HeadLightISM.Get(), TailLightISM.Get() })
	{
		if (Comp)
		{
			Comp->MarkRenderStateDirty();
		}
	}

	const FTransform Hidden = HiddenTransform();
	HeadLightXfBuffer.Init(Hidden, HeadLightISM ? NumLights : 0);
	TailLightXfBuffer.Init(Hidden, TailLightISM ? NumLights : 0);
}

void AMTAWorldAIManager::HideVehicleLights(int32 I)
{
	const FTransform Hidden = HiddenTransform();
	for (int32 S = 0; S < 2; ++S)
	{
		if (HeadLightXfBuffer.IsValidIndex(I * 2 + S))
		{
			HeadLightXfBuffer[I * 2 + S] = Hidden;
		}
		if (TailLightXfBuffer.IsValidIndex(I * 2 + S))
		{
			TailLightXfBuffer[I * 2 + S] = Hidden;
		}
	}
}

void AMTAWorldAIManager::WriteLightTransforms(int32 I, const FQuat& Rot, const FVector& Base, const FMTATrafficModelFit& Fit)
{
	// Gleiche Basis/Drehung wie Karosserie und Raeder; Kugel (100 cm) auf Lichtgroesse abgeflacht.
	const FVector HeadScale = HeadLightSize / 100.0;
	const FVector TailScale = TailLightSize / 100.0;
	for (int32 S = 0; S < 2; ++S)
	{
		const double SideSign = S == 0 ? 1.0 : -1.0;
		if (HeadLightXfBuffer.IsValidIndex(I * 2 + S))
		{
			const FVector Local(Fit.HeadLightLocal.X, Fit.HeadLightLocal.Y * SideSign, Fit.HeadLightLocal.Z);
			HeadLightXfBuffer[I * 2 + S] = FTransform(Rot, Base + Rot.RotateVector(Local), HeadScale);
		}
		if (TailLightXfBuffer.IsValidIndex(I * 2 + S))
		{
			const FVector Local(Fit.TailLightLocal.X, Fit.TailLightLocal.Y * SideSign, Fit.TailLightLocal.Z);
			TailLightXfBuffer[I * 2 + S] = FTransform(Rot, Base + Rot.RotateVector(Local), TailScale);
		}
	}
}

void AMTAWorldAIManager::UpdateLightState(int32 I, float FrameDt, bool bPolice, bool bSirenOn, bool bRedPhase)
{
	if (!LightPrevSpeed.IsValidIndex(I) || !BrakeHold.IsValidIndex(I))
	{
		return;
	}

	// Bremslicht: Tempo sinkt (Sim mit fester Rate -> Haltezeit gegen Flackern), steht oder wartet.
	const float Spd = Vehicles.Speed[I];
	if (Spd < LightPrevSpeed[I] - 5.f)
	{
		BrakeHold[I] = 0.4f;
	}
	else
	{
		BrakeHold[I] = FMath::Max(0.f, BrakeHold[I] - FrameDt);
	}
	LightPrevSpeed[I] = Spd;
	const bool bBrake = BrakeHold[I] > 0.f || FMath::Abs(Spd) < 50.f || Vehicles.Wait[I] > 0.f;

	const uint8 Tail = bBrake ? 1 : 0;
	if (TailLightISM && TailLightState[I] != Tail)
	{
		TailLightState[I] = Tail;
		const FLinearColor Col = bBrake ? BrakeLightColor : TailLightColor;
		for (int32 S = 0; S < 2; ++S)
		{
			MTAVehicleLights::WriteLightData(TailLightISM, I * 2 + S, Col, bBrake ? 3.f : 1.f);
		}
	}

	// Polizei im Einsatz: Frontlichter wechselseitig (links in der roten, rechts in der blauen Phase).
	const bool bFlash = bPolice && bSirenOn && Vehicles.PoliceState[I] != EMTAPoliceState::Calm;
	const uint8 Head = bFlash ? (bRedPhase ? 1 : 2) : 0;
	if (HeadLightISM && HeadLightState[I] != Head)
	{
		HeadLightState[I] = Head;
		const FLinearColor Base = bPolice ? PoliceHeadLightColor : HeadLightColor;
		for (int32 S = 0; S < 2; ++S)
		{
			const bool bDim = (Head == 1 && S == 1) || (Head == 2 && S == 0);
			const bool bBright = (Head == 1 && S == 0) || (Head == 2 && S == 1);
			const float Factor = bDim ? 0.15f : (bBright ? 1.8f : 1.f);
			MTAVehicleLights::WriteLightData(HeadLightISM, I * 2 + S, MTAVehicleLights::ScaleRGB(Base, Factor), 2.f * Factor);
		}
	}
}

void AMTAWorldAIManager::UpdateVisuals(float FrameDt)
{
	if (!bVisualsReady)
	{
		return;
	}
	const float Alpha = 1.f - FMath::Exp(-FrameDt * Traffic.VisualSmoothing);
	const float LodDistSq = FMath::Square(Traffic.LodDistance);
	const FTransform Hidden = HiddenTransform();

	// Platzhalter-Masse (Sorte Box, Engine-Wuerfel = 100 cm)
	const FVector BoxBodyScale = VehicleBodySize / 100.0;
	const FVector BoxCabinScale = VehicleCabinSize / 100.0;
	const double BoxClearance = 20.0;
	const double BoxBodyZ = BoxClearance + VehicleBodySize.Z * 0.5;
	const double BoxCabinZ = BoxClearance + VehicleBodySize.Z + VehicleCabinSize.Z * 0.5;
	const double BoxRoofZ = BoxClearance + VehicleBodySize.Z + VehicleCabinSize.Z;

	const bool bSirenOn = CachedHeat > 0.f;
	const bool bRedPhase = IsSirenPhaseRed();
	const int32 BoxIdx = static_cast<int32>(EMTATrafficBodyModel::Box);
	const int32 PoliceShadowSlot = TrafficBodyISMs.Num();

	for (float& D : BodyNearestDistSq)
	{
		D = TNumericLimits<float>::Max();
	}

	auto HideVehicleParts = [this, &Hidden](int32 I)
	{
		HideVehicleLights(I);
		if (CabinXfBuffer.IsValidIndex(I))
		{
			CabinXfBuffer[I] = Hidden;
		}
		if (GlassInstance.IsValidIndex(I) && GlassXfBuffer.IsValidIndex(GlassInstance[I]))
		{
			GlassXfBuffer[GlassInstance[I]] = Hidden;
		}
		if (VehicleModel.IsValidIndex(I) && WheelInstanceBase.IsValidIndex(I) && WheelInstanceBase[I] != INDEX_NONE
			&& WheelXfBuffers.IsValidIndex(VehicleModel[I]))
		{
			TArray<FTransform>& Buf = WheelXfBuffers[VehicleModel[I]];
			const int32 N = ModelFits[VehicleModel[I]].bSeparateWheels ? 4 : 1;
			for (int32 W = 0; W < N; ++W)
			{
				if (Buf.IsValidIndex(WheelInstanceBase[I] + W))
				{
					Buf[WheelInstanceBase[I] + W] = Hidden;
				}
			}
		}
	};

	// --- Fahrzeuge ------------------------------------------------------------------------------
	for (int32 I = 0; I < Vehicles.Num(); ++I)
	{
		const bool bPolice = Vehicles.Kind[I] == EMTAAgentKind::Police;
		const int32 Inst = Vehicles.InstanceIndex[I];
		const int32 M = VehicleModel.IsValidIndex(I) ? VehicleModel[I] : BoxIdx;
		const FMTATrafficModelFit& Fit = ModelFits[M];
		const bool bBox = M == BoxIdx;

		if (!Vehicles.bActive[I])
		{
			if (bPolice && PoliceXfBuffer.IsValidIndex(Inst))
			{
				PoliceXfBuffer[Inst] = Hidden;
				StripeXfBuffer[Inst] = Hidden;
				SirenRedXfBuffer[Inst] = Hidden;
				SirenBlueXfBuffer[Inst] = Hidden;
			}
			else if (!bPolice)
			{
				const int32 G = Vehicles.ColorIndex[I];
				if (TrafficXfBuffers.IsValidIndex(G) && TrafficXfBuffers[G].IsValidIndex(Inst))
				{
					TrafficXfBuffers[G][Inst] = Hidden;
				}
			}
			HideVehicleParts(I);
			continue;
		}

		const FVector Target(Vehicles.SimLoc[I].X, Vehicles.SimLoc[I].Y, GroundZ);
		const float TargetYaw = MTAAI::YawFromDir(Vehicles.SimDir[I]);
		const float PlayerDistSq = bHasPlayer ? FVector2f::DistSquared(Vehicles.SimLoc[I], PlayerLoc2D) : 0.f;
		const bool bFar = bHasPlayer && PlayerDistSq > LodDistSq;
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

		// Karosserie: Vorlagen-Mesh mit Pivot-Korrektur oder Platzhalter-Wuerfel mit Kabine.
		FTransform BodyXf;
		double RoofZ = BoxRoofZ;
		double RoofX = -10.0;
		double BodyLength = VehicleBodySize.X;
		double BodyWidth = VehicleBodySize.Y;
		if (bBox)
		{
			BodyXf = FTransform(Rot, Base + FVector(0.0, 0.0, BoxBodyZ), BoxBodyScale);
			if (CabinXfBuffer.IsValidIndex(I))
			{
				CabinXfBuffer[I] = FTransform(Rot, Base + Rot.RotateVector(FVector(-20.0, 0.0, BoxCabinZ)), BoxCabinScale);
			}
		}
		else
		{
			BodyXf = FTransform(Rot, Base + Rot.RotateVector(Fit.BodyOffset), Fit.BodyScale);
			RoofZ = Fit.RoofZ;
			RoofX = PoliceRoofXFactor * Fit.Length;
			BodyLength = Fit.Length;
			BodyWidth = Fit.Width;
			if (CabinXfBuffer.IsValidIndex(I))
			{
				CabinXfBuffer[I] = Hidden;
			}
			if (GlassInstance.IsValidIndex(I) && GlassXfBuffer.IsValidIndex(GlassInstance[I]))
			{
				GlassXfBuffer[GlassInstance[I]] = BodyXf; // gleicher Pivot wie SM_SportsCar
			}
			WriteWheelTransforms(I, Rot, Base, FrameDt);
		}

		// Scheinwerfer/Ruecklichter mit der Karosserie mitbewegen, Brems-/Blinkzustand nachfuehren.
		WriteLightTransforms(I, Rot, Base, Fit);
		UpdateLightState(I, FrameDt, bPolice, bSirenOn, bRedPhase);

		if (bPolice)
		{
			if (!PoliceXfBuffer.IsValidIndex(Inst))
			{
				continue;
			}
			PoliceXfBuffer[Inst] = BodyXf;
			if (BodyNearestDistSq.IsValidIndex(PoliceShadowSlot))
			{
				BodyNearestDistSq[PoliceShadowSlot] = FMath::Min(BodyNearestDistSq[PoliceShadowSlot], PlayerDistSq);
			}

			// Weisser Seitenstreifen: flacher Quader in Guertelhoehe, minimal breiter als die Flanke.
			const double StripeZ = bBox ? BoxClearance + VehicleBodySize.Z * 0.6 : RoofZ * PoliceStripePlacement.X;
			const FVector StripeScale(BodyLength * 0.6 / 100.0, BodyWidth * (bBox ? 1.01 : PoliceStripePlacement.Y) / 100.0, 0.09);
			StripeXfBuffer[Inst] = FTransform(Rot, Base + Rot.RotateVector(FVector(0.0, 0.0, StripeZ)), StripeScale);

			// Sirenenbalken auf dem Dach: rot links, blau rechts. Blinken: die inaktive Phase wird
			// ausgeblendet, zusaetzlich Emissiv in Custom-Data[3] fuer Materialien mit PerInstanceCustomData.
			const FVector SirenScale(0.22, 0.5, 0.09);
			const double SirenZ = RoofZ + 4.5;
			const FTransform RedXf(Rot, Base + Rot.RotateVector(FVector(RoofX, -26.0, SirenZ)), SirenScale);
			const FTransform BlueXf(Rot, Base + Rot.RotateVector(FVector(RoofX, 26.0, SirenZ)), SirenScale);
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
			const int32 G = Vehicles.ColorIndex[I];
			if (TrafficXfBuffers.IsValidIndex(G) && TrafficXfBuffers[G].IsValidIndex(Inst))
			{
				TrafficXfBuffers[G][Inst] = BodyXf;
			}
			if (BodyNearestDistSq.IsValidIndex(G))
			{
				BodyNearestDistSq[G] = FMath::Min(BodyNearestDistSq[G], PlayerDistSq);
			}
		}
	}

	// --- Fussgaenger: schlanker Koerper + Kopf, Proportion je Index variiert ------------------------
	const double MeanDiameter = PedestrianSize.X;
	const double MeanHeight = PedestrianSize.Y;
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

		// Deterministische Proportion: Groesse 0.93-1.07, Statur 0.86-1.14, Kopf 0.95-1.05.
		const uint32 H = HashIndex(static_cast<uint32>(I) + 1000U);
		const double HeightScale = 0.93 + static_cast<double>(H % 15U) / 100.0;
		const double BuildScale = 0.86 + static_cast<double>((H >> 8) % 29U) / 100.0;
		const double HeadD = PedestrianHeadSize * (0.95 + static_cast<double>((H >> 16) % 11U) / 100.0);
		const double TotalH = MeanHeight * HeightScale;
		const double BodyH = FMath::Max(40.0, TotalH - HeadD * 0.92);
		const double BodyW = MeanDiameter * BuildScale;          // Schulterbreite (Y)
		const double BodyDepth = BodyW * 0.72;                     // flacher von vorn nach hinten (X)

		const bool bDown = Peds.Stun[I] > 0.f;
		const FQuat Rot = FRotator(bDown ? 90.f : 0.f, Peds.VisYaw[I], 0.f).Quaternion();
		const FVector Base = Peds.VisLoc[I] + FVector(0.0, 0.0, bDown ? BodyDepth * 0.5 : 0.0);
		const FTransform BodyXf(Rot, Base + Rot.RotateVector(FVector(0.0, 0.0, BodyH * 0.5)),
			FVector(BodyDepth / 100.0, BodyW / 100.0, BodyH / 100.0));
		const FTransform HeadXf(Rot, Base + Rot.RotateVector(FVector(0.0, 0.0, BodyH + HeadD * 0.42)), FVector(HeadD / 100.0));

		const int32 C = Peds.ColorIndex[I];
		const int32 Inst = Peds.InstanceIndex[I];
		if (PedXfBuffers.IsValidIndex(C) && PedXfBuffers[C].IsValidIndex(Inst))
		{
			PedXfBuffers[C][Inst] = BodyXf;
		}
		if (PedHeadGroup.IsValidIndex(I))
		{
			const int32 S = PedHeadGroup[I];
			const int32 HeadInst = PedHeadInstance[I];
			if (PedHeadXfBuffers.IsValidIndex(S) && PedHeadXfBuffers[S].IsValidIndex(HeadInst))
			{
				PedHeadXfBuffers[S][HeadInst] = HeadXf;
			}
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
	for (int32 G = 0; G < TrafficBodyISMs.Num(); ++G)
	{
		Upload(TrafficBodyISMs[G], TrafficXfBuffers[G]);
	}
	for (int32 M = 0; M < WheelISMs.Num(); ++M)
	{
		Upload(WheelISMs[M], WheelXfBuffers[M]);
	}
	for (int32 C = 0; C < PedISMs.Num(); ++C)
	{
		Upload(PedISMs[C], PedXfBuffers[C]);
	}
	for (int32 S = 0; S < PedHeadISMs.Num(); ++S)
	{
		Upload(PedHeadISMs[S], PedHeadXfBuffers[S]);
	}
	Upload(SportsGlassISM, GlassXfBuffer);
	Upload(CabinISM, CabinXfBuffer);
	Upload(PoliceBodyISM, PoliceXfBuffer);
	Upload(PoliceStripeISM, StripeXfBuffer);
	Upload(SirenRedISM, SirenRedXfBuffer);
	Upload(SirenBlueISM, SirenBlueXfBuffer);
	Upload(HeadLightISM, HeadLightXfBuffer);
	Upload(TailLightISM, TailLightXfBuffer);

	// --- Schatten nur nah: Karosserie-Komponenten ohne nahes Fahrzeug werfen keinen Schatten -------
	if (bAgentsCastShadows)
	{
		const float ShadowSq = FMath::Square(FMath::Max(0.f, VehicleShadowDistance));
		auto UpdateShadow = [](UInstancedStaticMeshComponent* Comp, bool bWant)
		{
			if (Comp && static_cast<bool>(Comp->CastShadow) != bWant)
			{
				Comp->SetCastShadow(bWant);
			}
		};
		for (int32 G = 0; G < TrafficBodyISMs.Num(); ++G)
		{
			const bool bNear = !bHasPlayer || (BodyNearestDistSq.IsValidIndex(G) && BodyNearestDistSq[G] <= ShadowSq);
			UpdateShadow(TrafficBodyISMs[G], bNear);
		}
		const bool bPoliceNear = !bHasPlayer || (BodyNearestDistSq.IsValidIndex(PoliceShadowSlot) && BodyNearestDistSq[PoliceShadowSlot] <= ShadowSq);
		UpdateShadow(PoliceBodyISM, bPoliceNear);
	}
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

// =============================================================================================
// Modelltest
// =============================================================================================

FString AMTAWorldAIManager::RunModelSelfTest()
{
	TArray<FString> Lines;
	int32 Errors = 0;
	auto Check = [&Lines, &Errors](bool bOk, const FString& What)
	{
		Lines.Add(FString::Printf(TEXT("%s %s"), bOk ? TEXT("OK    ") : TEXT("FEHLER"), *What));
		if (!bOk)
		{
			++Errors;
		}
	};
	Lines.Add(TEXT("MODELLTEST Welt-KI: Fahrzeugmodelle und Passanten"));

	// --- 1) Assets laden, Materialien vorhanden (unabhaengig vom Spielzustand) ----------------------
	struct FModelAssetCheck
	{
		const TCHAR* Label;
		const TSoftObjectPtr<UStaticMesh>* Ref;
	};
	const FModelAssetCheck AssetChecks[] = {
		{ TEXT("Sportwagen Karosserie"), &SportsBodyAsset },
		{ TEXT("Sportwagen Glas"), &SportsGlassAsset },
		{ TEXT("Sportwagen Rad"), &SportsWheelAsset },
		{ TEXT("Gelaendewagen Karosserie"), &OffroadBodyAsset },
		{ TEXT("Gelaendewagen Rad"), &OffroadWheelAsset },
		{ TEXT("Concept Car"), &ConceptBodyAsset } };
	for (const FModelAssetCheck& AC : AssetChecks)
	{
		UStaticMesh* Mesh = AC.Ref->IsNull() ? nullptr : AC.Ref->LoadSynchronous();
		Check(Mesh != nullptr, FString::Printf(TEXT("%s laedt (%s)"), AC.Label, *AC.Ref->ToString()));
		if (!Mesh)
		{
			continue;
		}
		int32 MissingMats = 0;
		const TArray<FStaticMaterial>& Mats = Mesh->GetStaticMaterials();
		for (const FStaticMaterial& SM : Mats)
		{
			MissingMats += SM.MaterialInterface ? 0 : 1;
		}
		Check(Mats.Num() > 0 && MissingMats == 0,
			FString::Printf(TEXT("%s: %d Materialslots, %d ohne Material"), AC.Label, Mats.Num(), MissingMats));
	}

	// --- 2) Einpassung je Sorte: Lack-Parameter, Masse, Raeder -------------------------------------
	const TCHAR* ModelNames[] = { TEXT("Sportwagen"), TEXT("Gelaendewagen"), TEXT("Concept Car") };
	const TSoftObjectPtr<UStaticMesh>* BodyRefs[] = { &SportsBodyAsset, &OffroadBodyAsset, &ConceptBodyAsset };
	const TSoftObjectPtr<UStaticMesh>* WheelRefs[] = { &SportsWheelAsset, &OffroadWheelAsset, nullptr };
	for (int32 M = 0; M < 3; ++M)
	{
		UStaticMesh* Body = BodyRefs[M]->IsNull() ? nullptr : BodyRefs[M]->LoadSynchronous();
		UStaticMesh* Wheel = (WheelRefs[M] && !WheelRefs[M]->IsNull()) ? WheelRefs[M]->LoadSynchronous() : nullptr;
		FMTATrafficModelFit Fit;
		const bool bFit = ComputeModelFit(static_cast<EMTATrafficBodyModel>(M), Body, Wheel, Fit);
		Check(bFit, FString::Printf(TEXT("%s: Einpassung aus Bounds"), ModelNames[M]));
		if (!bFit)
		{
			continue;
		}
		Check(Fit.PaintSlot != INDEX_NONE, FString::Printf(TEXT("%s: Lack-Slot %d, Parameter '%s'"),
			ModelNames[M], Fit.PaintSlot, *Fit.PaintParam.ToString()));
		const float ScaledLength = static_cast<float>(Fit.RawLength * Fit.BodyScale.X);
		Check(ScaledLength >= 380.f && ScaledLength <= 520.f, FString::Printf(TEXT("%s: Laenge %.0f cm (Mesh roh %.0f cm)"),
			ModelNames[M], ScaledLength, Fit.RawLength));
		Check(Fit.Width >= 160.f && Fit.Width <= 220.f, FString::Printf(TEXT("%s: Breite %.0f cm"), ModelNames[M], Fit.Width));
		Check(Fit.RoofZ >= 90.f && Fit.RoofZ <= 240.f, FString::Printf(TEXT("%s: Dachhoehe %.0f cm"), ModelNames[M], Fit.RoofZ));
		if (WheelRefs[M])
		{
			Check(Fit.bSeparateWheels || Fit.bWheelSet, FString::Printf(TEXT("%s: Raeder (%s)"), ModelNames[M],
				Fit.bSeparateWheels ? TEXT("4 Instanzen") : (Fit.bWheelSet ? TEXT("Satz") : TEXT("fehlen"))));
		}
	}

	// --- 3) Instanzen (erst nach BeginPlay) -----------------------------------------------------------
	Check(bVisualsReady, TEXT("Darstellung initialisiert (BeginPlay gelaufen)"));
	if (bVisualsReady)
	{
		const int32 NumModels = static_cast<int32>(EMTATrafficBodyModel::Count);
		const int32 SportsIdx = static_cast<int32>(EMTATrafficBodyModel::Sports);
		const int32 BoxIdx = static_cast<int32>(EMTATrafficBodyModel::Box);
		TArray<int32> InstCounts;
		InstCounts.Init(0, NumModels);
		TArray<bool> LengthChecked;
		LengthChecked.Init(false, NumModels);
		int32 NumComponents = 0;
		for (int32 G = 0; G < TrafficBodyISMs.Num(); ++G)
		{
			UInstancedStaticMeshComponent* Comp = TrafficBodyISMs[G];
			if (!Comp)
			{
				continue;
			}
			++NumComponents;
			const int32 M = G / FMath::Max(1, NumPaintColors);
			const int32 N = Comp->GetInstanceCount();
			if (InstCounts.IsValidIndex(M))
			{
				InstCounts[M] += N;
			}
			// Laenge einer echten Instanz: Mesh-Bounds x Instanz-Skalierung.
			FTransform Xf;
			if (M != BoxIdx && LengthChecked.IsValidIndex(M) && !LengthChecked[M] && N > 0 && Comp->GetStaticMesh()
				&& Comp->GetInstanceTransform(0, Xf, true))
			{
				LengthChecked[M] = true;
				const float InstLength = static_cast<float>(Comp->GetStaticMesh()->GetBoundingBox().GetSize().X * Xf.GetScale3D().X);
				Check(InstLength >= 380.f && InstLength <= 520.f, FString::Printf(TEXT("%s: Instanzlaenge %.0f cm"), ModelNames[M], InstLength));
			}
		}
		for (int32 M = 0; M < 3; ++M)
		{
			Check(InstCounts[M] > 0, FString::Printf(TEXT("%s: %d Verkehrs-Instanzen"), ModelNames[M], InstCounts[M]));
		}
		Check(InstCounts[BoxIdx] == 0, FString::Printf(TEXT("keine Platzhalter-Wuerfel im Verkehr (%d)"), InstCounts[BoxIdx]));
		Check(MaxTotalVehicles <= 0 || Vehicles.Num() <= MaxTotalVehicles,
			FString::Printf(TEXT("Fahrzeuge gesamt %d (Grenze %d), Karosserie-Komponenten %d"), Vehicles.Num(), MaxTotalVehicles, NumComponents));
		for (int32 M = 0; M < 2; ++M)
		{
			const bool bHasWheels = WheelISMs.IsValidIndex(M) && WheelISMs[M] && WheelISMs[M]->GetInstanceCount() > 0;
			Check(bHasWheels, FString::Printf(TEXT("%s: Rad-Instanzen %d"), ModelNames[M], bHasWheels ? WheelISMs[M]->GetInstanceCount() : 0));
		}
		Check(SportsGlassISM != nullptr && SportsGlassISM->GetInstanceCount() > 0, TEXT("Sportwagen: Glas-Instanzen"));

		// Polizei: Sportwagen-Mesh, dunkle Lack-MID, weisse Streifen-MID, Sirenen
		const bool bPoliceBody = PoliceBodyISM && PoliceBodyISM->GetInstanceCount() == NumPoliceSlots;
		Check(bPoliceBody, FString::Printf(TEXT("Polizei: %d Karosserie-Instanzen"), PoliceBodyISM ? PoliceBodyISM->GetInstanceCount() : 0));
		if (PoliceBodyISM)
		{
			Check(ModelBodyMeshes.IsValidIndex(SportsIdx) && ModelBodyMeshes[SportsIdx] && PoliceBodyISM->GetStaticMesh() == ModelBodyMeshes[SportsIdx],
				TEXT("Polizei: Sportwagen-Mesh"));
			const int32 PaintSlot = ModelFits[SportsIdx].PaintSlot;
			const bool bPaintSet = PolicePaintMID && PaintSlot != INDEX_NONE && PoliceBodyISM->GetMaterial(PaintSlot) == PolicePaintMID.Get();
			Check(bPaintSet, TEXT("Polizei: Lack-Materialinstanz gesetzt"));
			if (bPaintSet)
			{
				FLinearColor Paint = FLinearColor::White;
				PolicePaintMID->GetVectorParameterValue(FHashedMaterialParameterInfo(ModelFits[SportsIdx].PaintParam), Paint);
				Check(Paint.GetLuminance() < 0.25f, FString::Printf(TEXT("Polizei: Lack dunkel (Luminanz %.2f)"), Paint.GetLuminance()));
			}
		}
		const bool bStripe = PoliceStripeISM && PoliceStripeISM->GetInstanceCount() == NumPoliceSlots
			&& (PoliceStripeMID ? PoliceStripeISM->GetMaterial(0) == PoliceStripeMID.Get() : AgentMaterial != nullptr);
		Check(bStripe, TEXT("Polizei: Streifen mit zweiter Materialinstanz"));
		if (bStripe && PoliceStripeMID)
		{
			FLinearColor Stripe = FLinearColor::Black;
			PoliceStripeMID->GetVectorParameterValue(FHashedMaterialParameterInfo(FName(TEXT("Color"))), Stripe);
			Check(Stripe.GetLuminance() > 0.6f, FString::Printf(TEXT("Polizei: Streifen hell (Luminanz %.2f)"), Stripe.GetLuminance()));
		}
		Check(SirenRedISM && SirenBlueISM && SirenRedISM->GetInstanceCount() == NumPoliceSlots && SirenBlueISM->GetInstanceCount() == NumPoliceSlots,
			TEXT("Polizei: Sirenenbalken-Instanzen"));

		// Lack: keine fast schwarzen Verkehrsfarben (nachts sonst unsichtbar).
		{
			const float Bright = FMath::Clamp(PaintBrightness, 0.1f, 4.f);
			int32 DarkColors = 0;
			float MinLum = TNumericLimits<float>::Max();
			for (const FLinearColor& C : TrafficColors)
			{
				const float Lum = C.GetLuminance() * Bright;
				MinLum = FMath::Min(MinLum, Lum);
				DarkColors += Lum < 0.15f ? 1 : 0;
			}
			Check(TrafficColors.Num() > 0 && DarkColors == 0, FString::Printf(TEXT("Verkehr: %d Lackfarben x %.2f, dunkelste Luminanz %.2f, %d zu dunkel"),
				TrafficColors.Num(), Bright, TrafficColors.Num() > 0 ? MinLum : 0.f, DarkColors));
		}

		// Fahrzeuglichter: je Fahrzeug 2 Scheinwerfer + 2 Ruecklichter, emissives Material, Lage vorn/hinten.
		{
			const int32 NumVehAll = Vehicles.Num();
			const int32 HeadN = HeadLightISM ? HeadLightISM->GetInstanceCount() : 0;
			const int32 TailN = TailLightISM ? TailLightISM->GetInstanceCount() : 0;
			Check(HeadLightISM != nullptr && TailLightISM != nullptr, TEXT("Fahrzeuglichter: Scheinwerfer- und Ruecklicht-Komponenten vorhanden"));
			Check(HeadN == 2 * NumVehAll && TailN == 2 * NumVehAll && HeadN + TailN == 4 * NumVehAll,
				FString::Printf(TEXT("Fahrzeuglichter: %d Scheinwerfer + %d Ruecklichter = %d (erwartet 4 x %d Fahrzeuge = %d)"),
					HeadN, TailN, HeadN + TailN, NumVehAll, 4 * NumVehAll));
			Check(VehicleLightMID != nullptr && HeadLightISM && TailLightISM
				&& HeadLightISM->GetMaterial(0) == VehicleLightMID.Get() && TailLightISM->GetMaterial(0) == VehicleLightMID.Get(),
				FString::Printf(TEXT("Fahrzeuglichter: emissives Material (%s)"), *VehicleLightMaterialAsset.ToString()));

			// Erstes aktives Fahrzeug: Scheinwerfer vor (+X), Ruecklichter hinter der Fahrzeugmitte.
			int32 Probe = INDEX_NONE;
			for (int32 I = 0; I < NumVehAll; ++I)
			{
				if (Vehicles.bActive[I])
				{
					Probe = I;
					break;
				}
			}
			FTransform HeadXf;
			FTransform TailXf;
			if (Probe != INDEX_NONE && HeadN == 2 * NumVehAll && TailN == 2 * NumVehAll
				&& HeadLightISM->GetInstanceTransform(Probe * 2, HeadXf, true) && TailLightISM->GetInstanceTransform(Probe * 2, TailXf, true))
			{
				const FQuat VehRot = FRotator(0.f, Vehicles.VisYaw[Probe], 0.f).Quaternion();
				const FVector HeadLocal = VehRot.UnrotateVector(HeadXf.GetLocation() - Vehicles.VisLoc[Probe]);
				const FVector TailLocal = VehRot.UnrotateVector(TailXf.GetLocation() - Vehicles.VisLoc[Probe]);
				Check(HeadLocal.X > 100.0 && TailLocal.X < -100.0 && HeadLocal.Z > 10.0 && TailLocal.Z > 10.0,
					FString::Printf(TEXT("Fahrzeuglichter: Lage Fahrzeug %d vorn (%.0f, %.0f, %.0f), hinten (%.0f, %.0f, %.0f)"), Probe,
						HeadLocal.X, HeadLocal.Y, HeadLocal.Z, TailLocal.X, TailLocal.Y, TailLocal.Z));
			}
			else
			{
				Check(false, TEXT("Fahrzeuglichter: Lage pruefbar (aktives Fahrzeug mit Licht-Instanzen)"));
			}
		}

		// Passanten: Koerper + Kopf je Figur
		int32 BodyInst = 0;
		int32 HeadInst = 0;
		for (const TObjectPtr<UInstancedStaticMeshComponent>& Comp : PedISMs)
		{
			BodyInst += Comp ? Comp->GetInstanceCount() : 0;
		}
		for (const TObjectPtr<UInstancedStaticMeshComponent>& Comp : PedHeadISMs)
		{
			HeadInst += Comp ? Comp->GetInstanceCount() : 0;
		}
		Check(BodyInst == Peds.Num() && HeadInst == Peds.Num(),
			FString::Printf(TEXT("Passanten: %d Koerper, %d Koepfe fuer %d Figuren"), BodyInst, HeadInst, Peds.Num()));
	}

	Lines.Add(FString::Printf(TEXT("MODELLTEST ERGEBNIS: %d Fehler"), Errors));
	const FString Report = FString::Join(Lines, TEXT("\n"));
	UE_LOG(LogMTAWorldAIModels, Display, TEXT("%s"), *Report);
	return Report;
}
