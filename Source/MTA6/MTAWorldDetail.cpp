#include "MTAWorldDetail.h"

#include "MTABuildingEntry.h"
#include "MTACityGenerator.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogMTAWorldDetail, Log, All);

namespace
{
	// Materialplaetze der Ebenen (Reihenfolge = DetailMaterialPaths).
	namespace MTADetailSlot
	{
		constexpr int32 Water = 0;
		constexpr int32 Concrete = 1;
		constexpr int32 Metal = 2;
		constexpr int32 Neon = 3;
		constexpr int32 Sign = 4;
		constexpr int32 Mountain = 5;
		constexpr int32 Road = 6;
		constexpr int32 Trunk = 7;
		constexpr int32 Foliage = 8;
		constexpr int32 Count = 9;
	}

	// Meshes der Ebenen.
	namespace MTADetailMesh
	{
		constexpr int32 Cube = 0;
		constexpr int32 Cylinder = 1;
		constexpr int32 Sphere = 2;
		constexpr int32 Cone = 3;
		constexpr int32 Plane = 4;
	}

	/** Standardpfade der von tools/ue/build_materials.py erzeugten Materialien. */
	const TCHAR* const DetailMaterialPaths[MTADetailSlot::Count] = {
		TEXT("/Game/MTA6/Materials/M_MTA_Water.M_MTA_Water"),
		TEXT("/Game/MTA6/Materials/M_MTA_Concrete.M_MTA_Concrete"),
		TEXT("/Game/MTA6/Materials/M_MTA_PaintedMetal.M_MTA_PaintedMetal"),
		TEXT("/Game/MTA6/Materials/M_MTA_Neon.M_MTA_Neon"),
		TEXT("/Game/MTA6/Materials/M_MTA_Signage.M_MTA_Signage"),
		TEXT("/Game/MTA6/Materials/M_MTA_Mountain.M_MTA_Mountain"),
		TEXT("/Game/MTA6/Materials/M_MTA_Road.M_MTA_Road"),
		TEXT("/Game/MTA6/Materials/M_MTA_TreeTrunk.M_MTA_TreeTrunk"),
		TEXT("/Game/MTA6/Materials/M_MTA_Foliage.M_MTA_Foliage"),
	};

	/** Schild-Arten fuer M_MTA_Signage (Custom-Data P0). */
	namespace MTASignKind
	{
		constexpr float Poster = 0.f;
		constexpr float Graffiti = 1.f;
		constexpr float Billboard = 2.f;
		constexpr float AegisLogo = 3.f;
		constexpr float ShopSign = 4.f;
		constexpr float Door = 5.f;
		constexpr float StreetName = 6.f;
		constexpr float Crosswalk = 7.f;
		constexpr float Stop = 8.f;
		constexpr float Warning = 9.f;
		constexpr float Cloth = 10.f;
	}

	/** Muster fuer M_MTA_Concrete (Custom-Data P1). */
	namespace MTAConcretePattern
	{
		constexpr float Plain = 0.f;
		constexpr float Sand = 1.f;
		constexpr float Paving = 2.f;
		constexpr float Planks = 3.f;
		constexpr float Salt = 4.f;
		constexpr float Earth = 6.f;
	}

	/** Streifen fuer M_MTA_PaintedMetal (Custom-Data P2). */
	namespace MTAMetalStripes
	{
		constexpr float None = 0.f;
		constexpr float Corrugated = 1.f;
		constexpr float Hazard = 2.f;
		constexpr float Bands = 3.f;
		constexpr float Boom = 4.f;
	}

	/** Custom-Data einer Instanz (MTADetailData::Num Werte). */
	struct FDetailData
	{
		float V[MTADetailData::Num] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
	};

	FDetailData MakeConcrete(const FLinearColor& C, float Wear, float Pattern)
	{
		FDetailData D;
		D.V[0] = C.R; D.V[1] = C.G; D.V[2] = C.B; D.V[3] = Wear; D.V[4] = Pattern;
		return D;
	}

	FDetailData MakePaint(const FLinearColor& C, float Wear, float Metallic, float Stripes)
	{
		FDetailData D;
		D.V[0] = C.R; D.V[1] = C.G; D.V[2] = C.B; D.V[3] = Wear; D.V[4] = Metallic; D.V[5] = Stripes;
		return D;
	}

	/** Neon: Intensitaet, Flackern (0..1, 2 = Blinken), Tagesanteil (0 = nur nachts). */
	FDetailData MakeNeon(const FLinearColor& C, float Intensity, float Flicker, float Day)
	{
		FDetailData D;
		D.V[0] = C.R; D.V[1] = C.G; D.V[2] = C.B; D.V[3] = Intensity; D.V[4] = Flicker; D.V[5] = Day;
		return D;
	}

	FDetailData MakeSign(const FLinearColor& C, float Kind, float SignSeed, float Emissive)
	{
		FDetailData D;
		D.V[0] = C.R; D.V[1] = C.G; D.V[2] = C.B; D.V[3] = Kind; D.V[4] = SignSeed; D.V[5] = Emissive;
		return D;
	}

	FDetailData MakeRock(const FLinearColor& Tint, float SnowLineMeters, float Haze)
	{
		FDetailData D;
		D.V[0] = Tint.R; D.V[1] = Tint.G; D.V[2] = Tint.B; D.V[3] = SnowLineMeters; D.V[4] = Haze;
		return D;
	}

	FDetailData MakeRoad(float Length, float Width, float Kind)
	{
		FDetailData D;
		D.V[3] = Length; D.V[4] = Width; D.V[5] = Kind;
		return D;
	}

	FDetailData MakeColor(const FLinearColor& C)
	{
		FDetailData D;
		D.V[0] = C.R; D.V[1] = C.G; D.V[2] = C.B;
		return D;
	}

	FLinearColor HexColor(uint32 RGB)
	{
		return FLinearColor(FColor((RGB >> 16) & 0xFF, (RGB >> 8) & 0xFF, RGB & 0xFF, 0xFF));
	}

	FRotator YawFromDir(const FVector2D& Dir)
	{
		return FRotator(0.f, FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Dir.Y), static_cast<float>(Dir.X))), 0.f);
	}

	int32 HashSeed(int32 A, int32 B, int32 C)
	{
		uint32 H = HashCombine(GetTypeHash(A), GetTypeHash(B * 73856093));
		H = HashCombine(H, GetTypeHash(C * 19349663));
		return static_cast<int32>(H & 0x7FFFFFFF);
	}

	/** Neon-Farben fuer Leuchtschilder (Pink, Tuerkis, Amber, Gruen, Violett, Kaltweiss). */
	const FLinearColor NeonPalette[] = {
		FLinearColor(1.f, 0.15f, 0.6f), FLinearColor(0.1f, 0.9f, 1.f), FLinearColor(1.f, 0.6f, 0.1f),
		FLinearColor(0.3f, 1.f, 0.35f), FLinearColor(0.6f, 0.25f, 1.f), FLinearColor(0.85f, 0.9f, 1.f)
	};

	constexpr int32 NumNeonColors = 6;

	/** Containerfarben (verblasstes Rot, Blau, Gruen, Orange, Grau, Weiss). */
	const FLinearColor ContainerPalette[] = {
		FLinearColor(0.45f, 0.08f, 0.05f), FLinearColor(0.08f, 0.18f, 0.42f), FLinearColor(0.1f, 0.3f, 0.15f),
		FLinearColor(0.75f, 0.32f, 0.06f), FLinearColor(0.35f, 0.36f, 0.37f), FLinearColor(0.75f, 0.75f, 0.72f),
		FLinearColor(0.55f, 0.45f, 0.1f)
	};
	constexpr int32 NumContainerColors = 7;

	/** Raumtyp -> Schildtext und Farbe der Tuermarkierung. */
	void RoomTypeSign(FName RoomType, FString& OutLabel, FLinearColor& OutColor)
	{
		const FString T = RoomType.ToString();
		if (T == TEXT("bar_club"))             { OutLabel = TEXT("BAR");        OutColor = FLinearColor(1.f, 0.1f, 0.6f); }
		else if (T == TEXT("diner"))           { OutLabel = TEXT("IMBISS");     OutColor = FLinearColor(1.f, 0.5f, 0.1f); }
		else if (T == TEXT("shop_kiosk"))      { OutLabel = TEXT("KIOSK");      OutColor = FLinearColor(0.2f, 1.f, 0.4f); }
		else if (T == TEXT("workshop_garage")) { OutLabel = TEXT("WERKSTATT");  OutColor = FLinearColor(1.f, 0.85f, 0.1f); }
		else if (T == TEXT("clinic"))          { OutLabel = TEXT("KLINIK");     OutColor = FLinearColor(0.9f, 0.95f, 1.f); }
		else if (T == TEXT("warehouse"))       { OutLabel = TEXT("LAGER");      OutColor = FLinearColor(0.5f, 0.6f, 0.7f); }
		else if (T == TEXT("office_lobby"))    { OutLabel = TEXT("BUERO");      OutColor = FLinearColor(0.3f, 0.7f, 1.f); }
		else if (T == TEXT("police_station"))  { OutLabel = TEXT("WACHE");      OutColor = FLinearColor(0.15f, 0.3f, 1.f); }
		else if (T == TEXT("apartment_small")) { OutLabel = TEXT("WOHNUNGEN");  OutColor = FLinearColor(1.f, 0.85f, 0.6f); }
		else if (T == TEXT("apartment_large")) { OutLabel = TEXT("WOHNHAUS");   OutColor = FLinearColor(1.f, 0.8f, 0.55f); }
		else if (T == TEXT("slum_room"))       { OutLabel = TEXT("ZIMMER FREI"); OutColor = FLinearColor(0.8f, 0.6f, 0.3f); }
		else if (T == TEXT("hackerspace"))     { OutLabel = TEXT("COMPUTER");   OutColor = FLinearColor(0.2f, 1.f, 1.f); }
		else if (T == TEXT("community_hall"))  { OutLabel = TEXT("HALLE");      OutColor = FLinearColor(1.f, 0.8f, 0.3f); }
		else                                   { OutLabel = TEXT("EINGANG");    OutColor = FLinearColor(0.9f, 0.9f, 0.9f); }
	}

	/** Raumtypen je Bezirk (zyklisch); Industrieparzellen nutzen die zweite Liste. */
	void RoomTypesFor(EMTADistrict District, bool bIndustrial, TArray<FName>& Out)
	{
		auto Set = [&Out](std::initializer_list<const TCHAR*> List)
		{
			Out.Reset();
			for (const TCHAR* Item : List) { Out.Add(FName(Item)); }
		};
		if (bIndustrial)
		{
			switch (District)
			{
			case EMTADistrict::Sunstrip:   Set({ TEXT("warehouse") }); return;
			case EMTADistrict::Saltmarket: Set({ TEXT("warehouse"), TEXT("workshop_garage") }); return;
			default:                       Set({ TEXT("workshop_garage"), TEXT("warehouse"), TEXT("workshop_garage") }); return;
			}
		}
		switch (District)
		{
		case EMTADistrict::Core:
			Set({ TEXT("shop_kiosk"), TEXT("diner"), TEXT("bar_club"), TEXT("office_lobby"), TEXT("clinic"), TEXT("apartment_small"), TEXT("police_station") });
			return;
		case EMTADistrict::Saltmarket:
			Set({ TEXT("bar_club"), TEXT("diner"), TEXT("shop_kiosk"), TEXT("apartment_small"), TEXT("community_hall"), TEXT("slum_room"), TEXT("apartment_large") });
			return;
		case EMTADistrict::LanternHill:
			Set({ TEXT("bar_club"), TEXT("diner"), TEXT("shop_kiosk"), TEXT("hackerspace"), TEXT("clinic"), TEXT("apartment_large"), TEXT("apartment_small") });
			return;
		case EMTADistrict::Greenmoor:
			Set({ TEXT("apartment_large"), TEXT("shop_kiosk"), TEXT("diner"), TEXT("police_station"), TEXT("office_lobby") });
			return;
		case EMTADistrict::Rustline:
			Set({ TEXT("bar_club"), TEXT("clinic"), TEXT("slum_room"), TEXT("diner"), TEXT("hackerspace"), TEXT("apartment_small"), TEXT("workshop_garage") });
			return;
		default:
			Set({ TEXT("bar_club"), TEXT("diner"), TEXT("shop_kiosk"), TEXT("apartment_small"), TEXT("hackerspace"), TEXT("police_station"), TEXT("apartment_large") });
			return;
		}
	}

	bool IsUniqueRoomType(FName RoomType)
	{
		return RoomType == FName(TEXT("police_station")) || RoomType == FName(TEXT("community_hall")) || RoomType == FName(TEXT("office_lobby"));
	}
}

// ============================================================================ Ebenen

const AMTAWorldDetail::FLayerSpec& AMTAWorldDetail::GetLayerSpec(int32 Layer)
{
	using namespace MTADetailMesh;
	// Name, Mesh, Material, Kollision (0/1 Abfrage/2 voll), Schatten, Kleinteil, Sichtweite, Distance Field, beweglich
	static const FLayerSpec Specs[] = {
		{ TEXT("Water"),          Plane,    MTADetailSlot::Water,    0, false, false, 0,     false, false },
		{ TEXT("Seabed"),         Cube,     MTADetailSlot::Concrete, 2, false, false, 0,     false, false },
		{ TEXT("Quay"),           Cube,     MTADetailSlot::Concrete, 2, true,  false, 0,     true,  false },
		{ TEXT("Piles"),          Cylinder, MTADetailSlot::Concrete, 2, true,  false, 40000, false, false },
		{ TEXT("Roads"),          Cube,     MTADetailSlot::Road,     2, false, false, 0,     false, false },
		{ TEXT("Mountains"),      Cone,     MTADetailSlot::Mountain, 2, true,  false, 0,     false, false },
		{ TEXT("Hills"),          Sphere,   MTADetailSlot::Mountain, 2, true,  false, 0,     false, false },
		{ TEXT("StructConcrete"), Cube,     MTADetailSlot::Concrete, 2, true,  false, 0,     true,  false },
		{ TEXT("StructMetal"),    Cube,     MTADetailSlot::Metal,    2, true,  false, 0,     false, false },
		{ TEXT("StructMetalCyl"), Cylinder, MTADetailSlot::Metal,    2, true,  false, 0,     true,  false },
		{ TEXT("StructCone"),     Cone,     MTADetailSlot::Metal,    2, true,  false, 0,     false, false },
		{ TEXT("Containers"),     Cube,     MTADetailSlot::Metal,    2, true,  false, 60000, true,  false },
		{ TEXT("NeonLandmark"),   Cube,     MTADetailSlot::Neon,     0, false, false, 0,     false, false },
		{ TEXT("NeonGlobe"),      Sphere,   MTADetailSlot::Neon,     0, false, false, 0,     false, false },
		{ TEXT("NeonSmall"),      Cube,     MTADetailSlot::Neon,     0, false, false, 25000, false, false },
		{ TEXT("Poles"),          Cylinder, MTADetailSlot::Metal,    2, true,  true,  15000, false, false },
		{ TEXT("FurnitureBox"),   Cube,     MTADetailSlot::Metal,    1, true,  true,  12000, false, false },
		{ TEXT("FurnitureCyl"),   Cylinder, MTADetailSlot::Metal,    1, true,  true,  9000,  false, false },
		{ TEXT("RoofAntennas"),   Cylinder, MTADetailSlot::Metal,    0, false, false, 40000, false, false },
		{ TEXT("Lines"),          Cylinder, MTADetailSlot::Metal,    0, false, false, 12000, false, false },
		{ TEXT("Cloth"),          Cube,     MTADetailSlot::Sign,     0, false, false, 12000, false, false },
		{ TEXT("Signage"),        Cube,     MTADetailSlot::Sign,     0, false, false, 15000, false, false },
		{ TEXT("SignageFar"),     Cube,     MTADetailSlot::Sign,     0, false, false, 0,     false, false },
		{ TEXT("Scrap"),          Cube,     MTADetailSlot::Metal,    2, true,  false, 30000, false, false },
		{ TEXT("PalmTrunks"),     Cylinder, MTADetailSlot::Trunk,    2, true,  false, 30000, false, false },
		{ TEXT("PalmCrowns"),     Sphere,   MTADetailSlot::Foliage,  0, true,  false, 30000, false, false },
		{ TEXT("WheelFrame"),     Cube,     MTADetailSlot::Metal,    0, true,  false, 0,     false, true  },
		{ TEXT("WheelNeon"),      Cube,     MTADetailSlot::Neon,     0, false, false, 0,     false, true  },
		{ TEXT("WheelCabins"),    Cylinder, MTADetailSlot::Metal,    0, true,  false, 0,     false, true  },
		{ TEXT("DoorSigns"),      Cube,     MTADetailSlot::Sign,     0, false, false, 15000, false, false },
	};
	static_assert(static_cast<int32>(UE_ARRAY_COUNT(Specs)) == static_cast<int32>(EMTADetailLayer::Count),"Ebenentabelle passt nicht zu EMTADetailLayer");
	return Specs[FMath::Clamp(Layer, 0, static_cast<int32>(EMTADetailLayer::Count) - 1)];
}

// ============================================================================ Konstruktion

AMTAWorldDetail::AMTAWorldDetail()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	RootComponent = SceneRoot;

	WheelPivot = CreateDefaultSubobject<USceneComponent>(TEXT("WheelPivot"));
	WheelPivot->SetupAttachment(SceneRoot);
	WheelPivot->SetMobility(EComponentMobility::Movable);

	for (int32 L = 0; L < static_cast<int32>(EMTADetailLayer::Count); ++L)
	{
		const FLayerSpec& Spec = GetLayerSpec(L);
		UHierarchicalInstancedStaticMeshComponent* Comp = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(Spec.Name);
		Comp->SetupAttachment(Spec.bMovable ? WheelPivot.Get() : SceneRoot.Get());
		Comp->SetMobility(Spec.bMovable ? EComponentMobility::Movable : EComponentMobility::Static);
		DetailLayers.Add(Comp);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	CubeMesh = CubeFinder.Object;
	CylinderMesh = CylinderFinder.Object;
	SphereMesh = SphereFinder.Object;
	ConeMesh = ConeFinder.Object;
	PlaneMesh = PlaneFinder.Object;
}

void AMTAWorldDetail::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UWorld* World = GetWorld();
	// In der Spielwelt erzeugt BeginPlay (sonst doppelt bei Laufzeit-Spawn).
	if (bGenerateOnConstruction && World && !World->IsGameWorld())
	{
		Generate();
	}
}

void AMTAWorldDetail::BeginPlay()
{
	Super::BeginPlay();
	const AMTACityGenerator* Gen = ResolveCity();
	const bool bStale = Gen && BuiltForCityHash != ComputeCityHash(Gen);
	if (!IsBuilt() || (bRegenerateIfStale && bStale))
	{
		Generate(); // ruft SetupRuntime in der Spielwelt
	}
	else
	{
		SetupRuntime();
	}
}

void AMTAWorldDetail::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearRuntimeTexts();
	for (UTextRenderComponent* T : DoorTexts)
	{
		if (IsValid(T)) { T->DestroyComponent(); }
	}
	DoorTexts.Reset();
	SpawnedEntries.Reset();
	Super::EndPlay(EndPlayReason);
}

void AMTAWorldDetail::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (WheelPivot && bAnimateWheel)
	{
		// Drehung um die lokale Y-Achse (Radscheibe liegt in der X-Z-Ebene).
		WheelPivot->AddLocalRotation(FRotator(WheelDegreesPerSecond * DeltaSeconds, 0.f, 0.f));
	}
}

// ============================================================================ Hilfen

AMTACityGenerator* AMTAWorldDetail::ResolveCity() const
{
	if (City)
	{
		return City.Get();
	}
	return AMTACityGenerator::FindCityGenerator(this);
}

int32 AMTAWorldDetail::ComputeCityHash(const AMTACityGenerator* Gen) const
{
	if (!Gen)
	{
		return 0;
	}
	const FVector O = Gen->GetActorLocation();
	uint32 H = HashCombine(GetTypeHash(Gen->Seed), GetTypeHash(Gen->GetCityLots().Num()));
	H = HashCombine(H, GetTypeHash(Gen->BlocksX * 131 + Gen->BlocksY));
	H = HashCombine(H, GetTypeHash(FMath::RoundToInt(O.X / 10.0) * 7 + FMath::RoundToInt(O.Y / 10.0) * 13));
	H = HashCombine(H, GetTypeHash(Seed));
	return static_cast<int32>(H | 1u); // nie 0 (0 = nicht gebaut)
}

bool AMTAWorldDetail::IsBuilt() const
{
	return BuiltForCityHash != 0 && GetTotalInstanceCount() > 0;
}

UMaterialInterface* AMTAWorldDetail::ResolveLayerMaterial(int32 Slot) const
{
	UMaterialInterface* Assigned = nullptr;
	switch (Slot)
	{
	case MTADetailSlot::Water:    Assigned = WaterMaterial; break;
	case MTADetailSlot::Concrete: Assigned = ConcreteMaterial; break;
	case MTADetailSlot::Metal:    Assigned = PaintedMetalMaterial; break;
	case MTADetailSlot::Neon:     Assigned = NeonMaterial; break;
	case MTADetailSlot::Sign:     Assigned = SignageMaterial; break;
	case MTADetailSlot::Mountain: Assigned = MountainMaterial; break;
	case MTADetailSlot::Road:     Assigned = RoadMaterial; break;
	case MTADetailSlot::Trunk:    Assigned = TrunkMaterial; break;
	case MTADetailSlot::Foliage:  Assigned = FoliageMaterial; break;
	default: break;
	}
	if (Assigned)
	{
		return Assigned;
	}
	if (Slot < 0 || Slot >= MTADetailSlot::Count)
	{
		return nullptr;
	}
	return LoadObject<UMaterialInterface>(nullptr, DetailMaterialPaths[Slot], nullptr, LOAD_NoWarn | LOAD_Quiet);
}

UStaticMesh* AMTAWorldDetail::ResolveLayerMesh(int32 Mesh) const
{
	switch (Mesh)
	{
	case MTADetailMesh::Cylinder: return CylinderMesh;
	case MTADetailMesh::Sphere:   return SphereMesh;
	case MTADetailMesh::Cone:     return ConeMesh;
	case MTADetailMesh::Plane:    return PlaneMesh;
	default:                      return CubeMesh;
	}
}

FVector AMTAWorldDetail::CityToWorld(const FDetailContext& Ctx, float LX, float LY, float Z) const
{
	return Ctx.Origin + FVector(LX, LY, Z);
}

float AMTAWorldDetail::CoastLocal(const FDetailContext& Ctx, float LX, bool bStepped) const
{
	return Ctx.CityGen ? Ctx.CityGen->GetCoastLocalY(LX, bStepped) : -(Ctx.HalfY + 8000.f);
}

EMTADistrict AMTAWorldDetail::DistrictAtLocal(const FDetailContext& Ctx, float LX, float LY) const
{
	return Ctx.CityGen ? Ctx.CityGen->GetDistrictAtLocation(CityToWorld(Ctx, LX, LY, 0.f)) : EMTADistrict::Core;
}

EMTADistrict AMTAWorldDetail::IntersectionDistrict(const FDetailContext& Ctx, int32 IX, int32 IY) const
{
	return Ctx.CityGen ? Ctx.CityGen->GetBlockDistrict(IX, IY) : EMTADistrict::Core;
}

const FMTADistrictProfile& AMTAWorldDetail::ProfileOf(const FDetailContext& Ctx, EMTADistrict District) const
{
	const int32 D = FMath::Clamp(static_cast<int32>(District), 0, Ctx.Profiles.Num() - 1);
	return Ctx.Profiles[D];
}

FString AMTAWorldDetail::PickSlogan(EMTADistrict District, int32 Salt) const
{
	static const TCHAR* const CoreS[] = { TEXT("AEGIS SIEHT DICH. AEGIS SCHUETZT DICH."), TEXT("AURELIA HOLDINGS - ORDNUNG IST RENDITE"),
		TEXT("DEIN PROFIL. DEINE FREIHEIT."), TEXT("SAUBERES PROFIL - SAUBERE STRASSE") };
	static const TCHAR* const SaltS[] = { TEXT("DOCKERS UNION - SCHICHT IST SCHICHT"), TEXT("FISCH FRISCH VOM KAI"),
		TEXT("STREIK? FRAG DEINEN VERTRAUENSMANN"), TEXT("SALTMARKET LAGER - 24H") };
	static const TCHAR* const LanternS[] = { TEXT("DIE STADT GEHOERT UNS"), TEXT("KELLERLICHT - LIVE JEDE NACHT"),
		TEXT("KEINE SANIERUNG OHNE UNS"), TEXT("ST. MERRIN LAEUTET FUER ALLE") };
	static const TCHAR* const GreenS[] = { TEXT("GREENMOOR - SICHER WOHNEN"), TEXT("PRIVATSICHERHEIT 24/7"), TEXT("RASEN IST VERTRAUEN") };
	static const TCHAR* const RustS[] = { TEXT("WRENCHERS - WIR SCHRAUBEN ALLES"), TEXT("BIKERBAR ROSTFREI"),
		TEXT("RAFFINERIE KESTREL - WIR BRENNEN FUER DICH"), TEXT("SCHROTT? GELD!") };
	static const TCHAR* const SunS[] = { TEXT("LAECHELN! AEGIS PERSONALISIERT DEIN GLUECK"), TEXT("KESTREL WHEEL - DIE STADT VON OBEN"),
		TEXT("CLUB WHEELHOUSE - HEUTE NACHT"), TEXT("SONNE. STRAND. SCORE.") };
	const int32 Pick = FMath::Abs(Salt);
	switch (District)
	{
	case EMTADistrict::Core:        return CoreS[Pick % static_cast<int32>(UE_ARRAY_COUNT(CoreS))];
	case EMTADistrict::Saltmarket:  return SaltS[Pick % static_cast<int32>(UE_ARRAY_COUNT(SaltS))];
	case EMTADistrict::LanternHill: return LanternS[Pick % static_cast<int32>(UE_ARRAY_COUNT(LanternS))];
	case EMTADistrict::Greenmoor:   return GreenS[Pick % static_cast<int32>(UE_ARRAY_COUNT(GreenS))];
	case EMTADistrict::Rustline:    return RustS[Pick % static_cast<int32>(UE_ARRAY_COUNT(RustS))];
	default:                        return SunS[Pick % static_cast<int32>(UE_ARRAY_COUNT(SunS))];
	}
}

// ============================================================================ Bausteine

void AMTAWorldDetail::AddInst(FDetailContext& Ctx, EMTADetailLayer Layer, const FTransform& T, const float* Data) const
{
	FBatch& Batch = Ctx.Batches[static_cast<int32>(Layer)];
	Batch.Transforms.Add(T);
	const int32 Start = Batch.Data.AddZeroed(MTADetailData::Num);
	if (Data)
	{
		for (int32 I = 0; I < MTADetailData::Num; ++I)
		{
			Batch.Data[Start + I] = Data[I];
		}
	}
}

void AMTAWorldDetail::AddBox(FDetailContext& Ctx, EMTADetailLayer Layer, const FVector& Center, const FVector& Size, const FRotator& Rot, const float* Data) const
{
	// Engine-Grundkoerper: 100 cm, mittig gepivotet.
	AddInst(Ctx, Layer, FTransform(Rot, Center, Size / 100.0), Data);
}

void AMTAWorldDetail::AddColumn(FDetailContext& Ctx, EMTADetailLayer Layer, const FVector& Base, float Diameter, float Height, const float* Data) const
{
	AddInst(Ctx, Layer, FTransform(FRotator::ZeroRotator, Base + FVector(0.f, 0.f, Height * 0.5f), FVector(Diameter, Diameter, Height) / 100.0), Data);
}

void AMTAWorldDetail::AddBeam(FDetailContext& Ctx, EMTADetailLayer Layer, const FVector& A, const FVector& B, float Thick, float Width, const float* Data) const
{
	const FVector D = B - A;
	const double Len = D.Size();
	if (Len < 1.0)
	{
		return;
	}
	AddInst(Ctx, Layer, FTransform(D.Rotation(), (A + B) * 0.5, FVector(Len, Width, Thick) / 100.0), Data);
}

void AMTAWorldDetail::AddRod(FDetailContext& Ctx, EMTADetailLayer Layer, const FVector& A, const FVector& B, float Diameter, const float* Data) const
{
	const FVector D = B - A;
	const double Len = D.Size();
	if (Len < 1.0)
	{
		return;
	}
	const FRotator Rot = FRotationMatrix::MakeFromZ(D / Len).Rotator();
	AddInst(Ctx, Layer, FTransform(Rot, (A + B) * 0.5, FVector(Diameter, Diameter, Len) / 100.0), Data);
}

void AMTAWorldDetail::AddLandmark(FName Id, const FString& DisplayName, EMTADistrict District, const FVector& Location, float Height)
{
	FMTALandmarkInfo Info;
	Info.Id = Id;
	Info.DisplayName = DisplayName;
	Info.District = District;
	Info.Location = Location;
	Info.Height = Height;
	Landmarks.Add(Info);
}

void AMTAWorldDetail::AddAegisCamera(FDetailContext& Ctx, const FVector& Foot, const FVector& LookAt, EMTADistrict District, const FIntPoint& Crossing, FName Kind, bool bHacked)
{
	const float MastH = 850.f;
	const FDetailData Mast = MakePaint(FLinearColor(0.12f, 0.13f, 0.14f), 0.2f, 0.7f, MTAMetalStripes::None);
	AddColumn(Ctx, EMTADetailLayer::Poles, Foot, 22.f, MastH, Mast.V);

	FVector Dir = LookAt - Foot;
	Dir.Z = 0.0;
	Dir = Dir.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector::ForwardVector;
	}
	const FVector ArmStart = Foot + FVector(0.f, 0.f, MastH - 20.f);
	const FVector ArmEnd = ArmStart + Dir * 120.f;
	AddBeam(Ctx, EMTADetailLayer::FurnitureBox, ArmStart, ArmEnd, 10.f, 10.f, Mast.V);

	// Gehaeuse leicht nach unten geneigt, Linse vorne.
	FRotator Look = Dir.Rotation();
	Look.Pitch = -15.f;
	const FVector Body = ArmEnd + FVector(0.f, 0.f, -25.f);
	AddBox(Ctx, EMTADetailLayer::FurnitureBox, Body, FVector(60.f, 25.f, 25.f), Look,
		MakePaint(FLinearColor(0.82f, 0.84f, 0.86f), 0.1f, 0.3f, MTAMetalStripes::None).V);
	const FVector LensPos = Body + Look.Vector() * 32.f;
	const FLinearColor LensCol = bHacked ? FLinearColor(0.03f, 0.03f, 0.03f) : FLinearColor(1.f, 0.02f, 0.02f);
	AddBox(Ctx, EMTADetailLayer::NeonSmall, LensPos, FVector(6.f, 13.f, 13.f), Look, MakeNeon(LensCol, bHacked ? 0.f : 6.f, 0.f, 0.6f).V);

	FMTAAegisCameraPoint P;
	P.Location = LensPos;
	P.Facing = Look;
	P.District = District;
	P.Intersection = Crossing;
	P.bHacked = bHacked;
	P.Kind = Kind;
	P.Range = District == EMTADistrict::Core ? 6000.f : 4500.f;
	AegisCameras.Add(P);
	++Stats.AegisCameras;
}

void AMTAWorldDetail::AddBillboard(FDetailContext& Ctx, const FVector& PanelCenter, const FVector& Facing, float Width, float Height, EMTADistrict District, bool bOnPosts, float PostBaseZ)
{
	FVector N = Facing;
	N.Z = 0.0;
	N = N.GetSafeNormal();
	const FRotator Rot = N.Rotation();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, N);
	const int32 BSeed = Ctx.Rng.RandRange(0, 9999);
	const FLinearColor AdCol = NeonPalette[BSeed % NumNeonColors];

	AddBox(Ctx, EMTADetailLayer::SignageFar, PanelCenter, FVector(30.f, Width, Height), Rot, MakeSign(AdCol, MTASignKind::Billboard, static_cast<float>(BSeed), 1.f).V);
	// Neonrahmen vorne.
	const FDetailData Frame = MakeNeon(ProfileOf(Ctx, District).AccentColor, 5.f, District == EMTADistrict::Rustline ? 0.6f : 0.f, 0.1f);
	const FVector Front = PanelCenter + N * 18.f;
	AddBox(Ctx, EMTADetailLayer::NeonSmall, Front + FVector(0.f, 0.f, Height * 0.5f), FVector(8.f, Width, 10.f), Rot, Frame.V);
	AddBox(Ctx, EMTADetailLayer::NeonSmall, Front - FVector(0.f, 0.f, Height * 0.5f), FVector(8.f, Width, 10.f), Rot, Frame.V);
	AddBox(Ctx, EMTADetailLayer::NeonSmall, Front + Right * (Width * 0.5f), FVector(8.f, 10.f, Height), Rot, Frame.V);
	AddBox(Ctx, EMTADetailLayer::NeonSmall, Front - Right * (Width * 0.5f), FVector(8.f, 10.f, Height), Rot, Frame.V);

	if (bOnPosts)
	{
		const FDetailData PostData = MakePaint(FLinearColor(0.2f, 0.2f, 0.21f), 0.5f, 0.6f, MTAMetalStripes::None);
		const float Bottom = static_cast<float>(PanelCenter.Z) - Height * 0.5f;
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const FVector PostFoot = PanelCenter - N * 40.f + Right * (Side * Width * 0.35f);
			AddColumn(Ctx, EMTADetailLayer::Poles, FVector(PostFoot.X, PostFoot.Y, PostBaseZ), 30.f, FMath::Max(50.f, Bottom - PostBaseZ + 20.f), PostData.V);
		}
	}

	FMTABillboardSpot Spot;
	Spot.Location = PanelCenter + N * 25.f;
	Spot.Facing = N;
	Spot.PanelSize = FVector2D(Width, Height);
	Spot.Slogan = PickSlogan(District, BSeed);
	Spot.TextColor = FLinearColor::White;
	Spot.District = District;
	Billboards.Add(Spot);
	++Stats.Billboards;
}

void AMTAWorldDetail::AddGuardrail(FDetailContext& Ctx, const FVector& A, const FVector& B)
{
	// Leitplanke mit voller Kollision (StructMetal), Pfosten an beiden Enden.
	const FDetailData Rail = MakePaint(FLinearColor(0.6f, 0.62f, 0.63f), 0.35f, 0.9f, MTAMetalStripes::None);
	AddBeam(Ctx, EMTADetailLayer::StructMetal, A + FVector(0.f, 0.f, 60.f), B + FVector(0.f, 0.f, 60.f), 32.f, 8.f, Rail.V);
	AddBox(Ctx, EMTADetailLayer::StructMetal, A + FVector(0.f, 0.f, 37.f), FVector(12.f, 12.f, 75.f), FRotator::ZeroRotator, Rail.V);
	++Stats.GuardrailSegments;
}

void AMTAWorldDetail::AddScrapHeap(FDetailContext& Ctx, const FVector& Center, float Radius, int32 Pieces)
{
	for (int32 P = 0; P < Pieces; ++P)
	{
		const float R = Radius * FMath::Sqrt(Ctx.Rng.FRand());
		const float A = Ctx.Rng.FRandRange(0.f, UE_TWO_PI);
		const float Z = (1.f - R / FMath::Max(1.f, Radius)) * Radius * 0.45f * Ctx.Rng.FRandRange(0.5f, 1.f);
		const FVector Pos = Center + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, Z);
		const FVector Size(Ctx.Rng.FRandRange(100.f, 420.f), Ctx.Rng.FRandRange(80.f, 250.f), Ctx.Rng.FRandRange(50.f, 200.f));
		const FRotator Rot(Ctx.Rng.FRandRange(-35.f, 35.f), Ctx.Rng.FRandRange(0.f, 360.f), Ctx.Rng.FRandRange(-35.f, 35.f));
		const FLinearColor Col = ContainerPalette[Ctx.Rng.RandRange(0, NumContainerColors - 1)] * 0.7f;
		AddBox(Ctx, EMTADetailLayer::Scrap, Pos, Size, Rot, MakePaint(Col, Ctx.Rng.FRandRange(0.6f, 1.f), 0.5f, MTAMetalStripes::None).V);
	}
}

void AMTAWorldDetail::AddPalm(FDetailContext& Ctx, const FVector& Foot, float Scale)
{
	const float TrunkH = 800.f * Scale;
	AddColumn(Ctx, EMTADetailLayer::PalmTrunks, Foot, 35.f * Scale, TrunkH, nullptr);
	AddBox(Ctx, EMTADetailLayer::PalmCrowns, Foot + FVector(0.f, 0.f, TrunkH + 50.f * Scale), FVector(480.f, 480.f, 160.f) * Scale,
		FRotator(0.f, Ctx.Rng.FRandRange(0.f, 360.f), 0.f), MakeColor(HexColor(0x5E7F22)).V);
}

// ============================================================================ Generierung

void AMTAWorldDetail::Clear()
{
	for (UHierarchicalInstancedStaticMeshComponent* Comp : DetailLayers)
	{
		if (Comp)
		{
			Comp->ClearInstances();
		}
	}
	ClearRuntimeTexts();
	AegisCameras.Reset();
	Landmarks.Reset();
	Billboards.Reset();
	SignTexts.Reset();
	EnterableSites.Reset();
	Stats = FMTAFurnitureStats();
	BuiltForCityHash = 0;
}

void AMTAWorldDetail::ConfigureLayers()
{
	for (int32 L = 0; L < DetailLayers.Num(); ++L)
	{
		UHierarchicalInstancedStaticMeshComponent* Comp = DetailLayers[L];
		if (!Comp)
		{
			continue;
		}
		const FLayerSpec& Spec = GetLayerSpec(L);
		Comp->SetStaticMesh(ResolveLayerMesh(Spec.Mesh));
		if (UMaterialInterface* Mat = ResolveLayerMaterial(Spec.Material))
		{
			Comp->SetMaterial(0, Mat);
		}
		Comp->SetNumCustomDataFloats(MTADetailData::Num);
		if (Spec.Collision > 0)
		{
			Comp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			// 1 = nur Abfragen: Fussgaenger stossen an, Chaos-Fahrzeuge fahren durch (keine festen Hydranten-Crashs).
			Comp->SetCollisionEnabled(Spec.Collision == 1 ? ECollisionEnabled::QueryOnly : ECollisionEnabled::QueryAndPhysics);
		}
		else
		{
			Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Comp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		}
		Comp->SetCanEverAffectNavigation(Spec.Collision == 2 && !Spec.bMovable);
		Comp->SetCastShadow(Spec.bSmallProp ? bSmallPropsCastShadow : Spec.bShadow);
		Comp->SetAffectDistanceFieldLighting(Spec.bDistanceField);
		const int32 CullEnd = Spec.CullEnd > 0 ? FMath::RoundToInt(Spec.CullEnd * CullDistanceScale) : 0;
		Comp->SetCullDistances(CullEnd > 0 ? FMath::RoundToInt(CullEnd * 0.8f) : 0, CullEnd);
	}
}

void AMTAWorldDetail::ApplyBatches(FDetailContext& Ctx)
{
	for (int32 L = 0; L < DetailLayers.Num(); ++L)
	{
		UHierarchicalInstancedStaticMeshComponent* Comp = DetailLayers[L];
		const FBatch& Batch = Ctx.Batches[L];
		if (!Comp || Batch.Transforms.Num() == 0)
		{
			continue;
		}
		const FLayerSpec& Spec = GetLayerSpec(L);
		// Riesenrad-Ebenen liegen im Raum des WheelPivot, alles andere in Weltkoordinaten.
		Comp->AddInstances(Batch.Transforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ !Spec.bMovable);
		if (Batch.Data.Num() == Batch.Transforms.Num() * MTADetailData::Num)
		{
			// HISM verdeckt die Bereichs-Variante: explizit ueber die Basisklasse.
			Comp->UInstancedStaticMeshComponent::SetCustomData(0, Batch.Transforms.Num() - 1, TConstArrayView<float>(Batch.Data), false);
		}
		Comp->MarkRenderStateDirty();
	}
}

void AMTAWorldDetail::Generate()
{
	AMTACityGenerator* Gen = ResolveCity();
	if (!Gen)
	{
		UE_LOG(LogMTAWorldDetail, Warning, TEXT("%s: kein AMTACityGenerator gefunden, nichts erzeugt."), *GetName());
		return;
	}
	if (!CubeMesh || !CylinderMesh || !SphereMesh || !ConeMesh || !PlaneMesh)
	{
		UE_LOG(LogMTAWorldDetail, Error, TEXT("%s: Grundkoerper (/Engine/BasicShapes) fehlen."), *GetName());
		return;
	}
	if (Gen->GetCityLots().Num() == 0)
	{
		// Altes Level oder noch nicht generiert: Parzellen werden gebraucht.
		Gen->Generate();
	}

	const double StartTime = FPlatformTime::Seconds();
	Clear();

	FDetailContext Ctx;
	Ctx.CityGen = Gen;
	Ctx.Rng.Initialize(Seed ^ 0x2A17);
	Ctx.Origin = Gen->GetActorLocation();
	{ const FVector2D HalfExt = Gen->GetCityHalfExtent(); Ctx.HalfX = static_cast<float>(HalfExt.X); Ctx.HalfY = static_cast<float>(HalfExt.Y); }
	Ctx.RoadW = Gen->RoadWidth;
	Ctx.Curb = Gen->CurbHeight;
	Ctx.Period = Gen->BlockPeriod;
	Ctx.BlocksX = Gen->BlocksX;
	Ctx.BlocksY = Gen->BlocksY;
	Ctx.GroundHalf = Gen->GetGroundHalfSize();
	for (int32 D = 0; D < MTADistrict::Num(); ++D)
	{
		Ctx.Profiles.Add(Gen->GetDistrictProfile(static_cast<EMTADistrict>(D)));
	}

	// Kuestenabschnitte aus der westlichsten Blockspalte (BY = 0) ableiten.
	{
		float SaltMin = TNumericLimits<float>::Max(), SaltMax = -TNumericLimits<float>::Max();
		float SunMin = TNumericLimits<float>::Max(), SunMax = -TNumericLimits<float>::Max();
		for (int32 BX = 0; BX < Ctx.BlocksX; ++BX)
		{
			const float X0 = BX * Ctx.Period - Ctx.HalfX;
			const EMTADistrict D = Gen->GetBlockDistrict(BX, 0);
			if (D == EMTADistrict::Saltmarket) { SaltMin = FMath::Min(SaltMin, X0); SaltMax = FMath::Max(SaltMax, X0 + Ctx.Period); }
			if (D == EMTADistrict::Sunstrip)   { SunMin = FMath::Min(SunMin, X0); SunMax = FMath::Max(SunMax, X0 + Ctx.Period); }
		}
		if (SaltMax > SaltMin && SaltMax - SaltMin >= 12000.f) { Ctx.SaltX0 = SaltMin; Ctx.SaltX1 = SaltMax; }
		if (SunMax > SunMin && SunMax - SunMin >= 8000.f)      { Ctx.SunX0 = SunMin; Ctx.SunX1 = SunMax; }
		const int32 MidRoad = Ctx.BlocksX / 2;
		Ctx.Route9X = MidRoad * Ctx.Period - Ctx.HalfX;
	}

	ConfigureLayers();
	if (WheelPivot)
	{
		WheelPivot->SetRelativeRotation(FRotator::ZeroRotator);
	}

	if (bBuildMountains)
	{
		BuildMountains(Ctx);
	}
	if (bBuildSea)
	{
		BuildSea(Ctx);
		BuildCoastRoads(Ctx);
	}
	if (bBuildLandmarks)
	{
		FVector TowerBase;
		if (Gen->GetLandmarkTowerLocation(TowerBase))
		{
			AddLandmark(TEXT("aurelia_tower"), TEXT("Aurelia Tower"), EMTADistrict::Core, TowerBase, 19000.f);
		}
		if (bBuildSea)
		{
			BuildHarbor(Ctx);
			BuildSunstrip(Ctx);
		}
		BuildCoreTowers(Ctx);
		BuildLanternHill(Ctx);
		BuildGreenmoor(Ctx);
		BuildRustline(Ctx);
	}
	if (bBuildFurniture)
	{
		BuildIntersections(Ctx);
		BuildSidewalkFurniture(Ctx);
	}
	if (bBuildFacadeDetails)
	{
		BuildFacadeDetails(Ctx);
	}
	PlanEnterableSites(Ctx);

	ApplyBatches(Ctx);
	Stats.WaterTiles = GetLayerInstanceCount(EMTADetailLayer::Water);
	Stats.Containers = GetLayerInstanceCount(EMTADetailLayer::Containers);
	BuiltForCityHash = ComputeCityHash(Gen);

	UE_LOG(LogMTAWorldDetail, Log, TEXT("%s: Welt-Details erzeugt: %d Instanzen (davon %d Wasser), %d Landmarken, %d Aegis-Kameras, %d Werbetafeln, %d Eingangsstandorte, %.1f ms."),
		*GetName(), GetTotalInstanceCount(), Stats.WaterTiles, Landmarks.Num(), AegisCameras.Num(), Billboards.Num(), EnterableSites.Num(),
		(FPlatformTime::Seconds() - StartTime) * 1000.0);

	UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		SetupRuntime();
	}
}

// ---------------------------------------------------------------------------- Meer, Kueste, Boden

void AMTAWorldDetail::BuildSea(FDetailContext& Ctx)
{
	AMTACityGenerator* Gen = Ctx.CityGen;
	if (!Gen->bGenerateCoast)
	{
		UE_LOG(LogMTAWorldDetail, Warning, TEXT("%s: Generator schneidet keine Kueste (bGenerateCoast aus) - Meer uebersprungen."), *GetName());
		return;
	}
	const float GH = Ctx.GroundHalf;
	const float Coarse = 20000.f;
	const float Coast0 = CoastLocal(Ctx, 0.f, false);
	const float WestLimit = FMath::FloorToFloat((Coast0 - FineWaterReach) / Coarse) * Coarse;
	const float Margin = 3000.f; // Wasser reicht versteckt unter den Boden (Wasserspiegel liegt unter Z = 0)

	// Wasser-Custom-Data: Grundfarbe + Kuestenlinie in Weltkoordinaten (Schaum im Material).
	FDetailData W;
	W.V[0] = 0.02f; W.V[1] = 0.07f; W.V[2] = 0.08f;
	W.V[MTADetailData::P0] = Coast0 + static_cast<float>(Ctx.Origin.Y);
	W.V[MTADetailData::P1] = static_cast<float>(Ctx.Origin.X);
	W.V[MTADetailData::P2] = Gen->BayHeadlandStart;
	W.V[MTADetailData::P3] = Gen->BayHeadlandSlope;

	// Feine Kacheln (WPO-Wellen brauchen Eckpunkte): Spalten entlang X, Zeilen im globalen Raster.
	const float T = WaterTileSize;
	const int32 Cols = FMath::CeilToInt(2.f * GH / T);
	for (int32 Col = 0; Col < Cols; ++Col)
	{
		const float CX = -GH + (Col + 0.5f) * T;
		const float East = CoastLocal(Ctx, FMath::Clamp(0.f, CX - T * 0.5f, CX + T * 0.5f), false) + Margin;
		const int32 K0 = FMath::FloorToInt(WestLimit / T);
		const int32 K1 = FMath::CeilToInt(East / T);
		for (int32 K = K0; K < K1; ++K)
		{
			AddBox(Ctx, EMTADetailLayer::Water, CityToWorld(Ctx, CX, (K + 0.5f) * T, WaterLevel), FVector(T, T, 100.f), FRotator::ZeroRotator, W.V);
		}
	}
	// Grobe Kacheln bis zum Horizont (Wellenamplitude ist dort im Material bereits 0).
	const float YCg = FMath::FloorToFloat(CoastLocal(Ctx, GH, false) / Coarse) * Coarse;
	const int32 CoarseN = FMath::CeilToInt(2.f * SeaExtent / Coarse);
	for (int32 IX = 0; IX < CoarseN; ++IX)
	{
		const float CX = -SeaExtent + (IX + 0.5f) * Coarse;
		const float YLimit = FMath::Abs(CX) < GH ? WestLimit : YCg;
		for (int32 IY = 0; IY < CoarseN; ++IY)
		{
			const float CY = -SeaExtent + (IY + 0.5f) * Coarse;
			if (CY + Coarse * 0.5f > YLimit + 1.f)
			{
				break;
			}
			AddBox(Ctx, EMTADetailLayer::Water, CityToWorld(Ctx, CX, CY, WaterLevel), FVector(Coarse, Coarse, 100.f), FRotator::ZeroRotator, W.V);
		}
	}

	// Meeresgrund (nur Kollision, opakes Wasser verdeckt ihn).
	const FDetailData Bed = MakeConcrete(FLinearColor(0.25f, 0.23f, 0.18f), 0.5f, MTAConcretePattern::Sand);
	for (float BX = -GH; BX < GH; BX += 40000.f)
	{
		for (float BY = WestLimit; BY < Coast0 + Margin; BY += 40000.f)
		{
			const float SY = FMath::Min(40000.f, Coast0 + Margin - BY);
			AddBox(Ctx, EMTADetailLayer::Seabed, CityToWorld(Ctx, BX + 20000.f, BY + SY * 0.5f, SeabedLevel - 100.f), FVector(40000.f, SY, 200.f), FRotator::ZeroRotator, Bed.V);
		}
	}

	// Ferne Landplatten ausserhalb der Bodenflaeche (Osten, Norden, Sueden), damit dort kein Loch klafft.
	const FDetailData Earth = MakeConcrete(FLinearColor(0.2f, 0.24f, 0.14f), 0.3f, MTAConcretePattern::Earth);
	const float SE = SeaExtent;
	AddBox(Ctx, EMTADetailLayer::Quay, CityToWorld(Ctx, 0.f, (GH + SE) * 0.5f, -102.f), FVector(2.f * SE, SE - GH, 200.f), FRotator::ZeroRotator, Earth.V);
	AddBox(Ctx, EMTADetailLayer::Quay, CityToWorld(Ctx, (GH + SE) * 0.5f, (YCg + GH) * 0.5f, -102.f), FVector(SE - GH, GH - YCg, 200.f), FRotator::ZeroRotator, Earth.V);
	AddBox(Ctx, EMTADetailLayer::Quay, CityToWorld(Ctx, -(GH + SE) * 0.5f, (YCg + GH) * 0.5f, -102.f), FVector(SE - GH, GH - YCg, 200.f), FRotator::ZeroRotator, Earth.V);

	// Kaimauern (Stadt) bzw. Felsbrocken (Landzungen) entlang der gestuften Kueste.
	const float StripW = Gen->GroundTileSize / FMath::Max(1, FMath::RoundToInt(Gen->GroundTileSize / FMath::Max(500.f, Gen->CoastStripWidth)));
	const int32 Strips = FMath::RoundToInt(2.f * GH / StripW);
	const float WallTop = 40.f;
	const FDetailData Wall = MakeConcrete(FLinearColor(0.5f, 0.49f, 0.46f), 0.55f, MTAConcretePattern::Plain);
	const FDetailData RockData = MakeRock(FLinearColor(0.85f, 0.82f, 0.78f), 9999.f, 0.2f);
	float PrevC = 0.f;
	for (int32 S = 0; S < Strips; ++S)
	{
		const float SX = -GH + (S + 0.5f) * StripW;
		const float C = CoastLocal(Ctx, SX, true);
		const bool bBeach = SX > Ctx.SunX0 && SX < Ctx.SunX1 + 3000.f;
		const bool bUrban = FMath::Abs(SX) < Gen->BayHeadlandStart;
		if (!bBeach)
		{
			if (bUrban)
			{
				AddBox(Ctx, EMTADetailLayer::Quay, CityToWorld(Ctx, SX, C - 60.f, (SeabedLevel + WallTop) * 0.5f),
					FVector(StripW + 4.f, 120.f, WallTop - SeabedLevel), FRotator::ZeroRotator, Wall.V);
			}
			else
			{
				AddBox(Ctx, EMTADetailLayer::Hills, CityToWorld(Ctx, SX + Ctx.Rng.FRandRange(-300.f, 300.f), C - 250.f, -150.f),
					FVector(StripW * Ctx.Rng.FRandRange(0.9f, 1.4f), Ctx.Rng.FRandRange(600.f, 1000.f), Ctx.Rng.FRandRange(500.f, 900.f)),
					FRotator(0.f, Ctx.Rng.FRandRange(-20.f, 20.f), 0.f), RockData.V);
				if (S > 0 && FMath::Abs(C - PrevC) > 1.f)
				{
					// Stufe zwischen zwei Streifen mit einem Brocken schliessen.
					AddBox(Ctx, EMTADetailLayer::Hills, CityToWorld(Ctx, SX - StripW * 0.5f, (C + PrevC) * 0.5f, -100.f),
						FVector(700.f, FMath::Abs(C - PrevC) + 600.f, 800.f), FRotator::ZeroRotator, RockData.V);
				}
			}
			++Stats.QuayPieces;
		}
		PrevC = C;
	}

	// Promenade/Kai zwischen Stadtrand und Kaimauer (Pflaster), am Strand von Sunstrip schmaler.
	const float EdgeY = -Ctx.HalfY - Ctx.RoadW * 0.5f;
	const float EdgeX = Ctx.HalfX + Ctx.RoadW * 0.5f;
	const FDetailData Paving = MakeConcrete(FLinearColor(0.56f, 0.54f, 0.5f), 0.4f, MTAConcretePattern::Paving);
	auto AddApron = [this, &Ctx, &Paving](float X0, float X1, float Y0, float Y1)
	{
		if (X1 - X0 < 10.f || Y1 - Y0 < 10.f)
		{
			return;
		}
		AddBox(Ctx, EMTADetailLayer::Quay, CityToWorld(Ctx, (X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f, -8.f), FVector(X1 - X0, Y1 - Y0, 40.f), FRotator::ZeroRotator, Paving.V);
	};
	const float BeachEdge = Coast0 + 2000.f;
	AddApron(-EdgeX, Ctx.SunX0, Coast0, EdgeY);
	AddApron(Ctx.SunX0, EdgeX, BeachEdge, EdgeY);
	AddApron(EdgeX, Ctx.SunX1 + 3000.f, BeachEdge, EdgeY + 0.f);
	++Stats.QuayPieces;

	// Strand Sunstrip: flacher Sand auf dem Kaistreifen, dann geneigte Sandplatte ins Wasser.
	const FDetailData SandData = MakeConcrete(FLinearColor(0.78f, 0.69f, 0.5f), 0.2f, MTAConcretePattern::Sand);
	const float BX0 = Ctx.SunX0;
	const float BX1 = Ctx.SunX1 + 3000.f;
	AddBox(Ctx, EMTADetailLayer::Quay, CityToWorld(Ctx, (BX0 + BX1) * 0.5f, (Coast0 + BeachEdge) * 0.5f, -8.f), FVector(BX1 - BX0, BeachEdge - Coast0, 40.f), FRotator::ZeroRotator, SandData.V);
	const float SlopeLen = 12000.f;
	const float Drop = 492.f;
	const float Theta = FMath::RadiansToDegrees(FMath::Atan2(Drop, SlopeLen));
	// Yaw 90: lokales X zeigt nach +Y (Osten); positiver Pitch hebt die Ostkante.
	AddBox(Ctx, EMTADetailLayer::Quay, CityToWorld(Ctx, (BX0 + BX1) * 0.5f, Coast0 - SlopeLen * 0.5f, 12.f - Drop * 0.5f - 30.f),
		FVector(SlopeLen / FMath::Cos(FMath::DegreesToRadians(Theta)) + 100.f, BX1 - BX0, 60.f), FRotator(Theta, 90.f, 0.f), SandData.V);
}

void AMTAWorldDetail::BuildCoastRoads(FDetailContext& Ctx)
{
	const float GH = Ctx.GroundHalf;
	const float EdgeX = Ctx.HalfX + Ctx.RoadW * 0.5f;
	auto AddRoadSeg = [this, &Ctx](const FVector2D& A, const FVector2D& B, float Width, float Kind)
	{
		const FVector2D D = B - A;
		const float Len = static_cast<float>(D.Size()) + 60.f;
		const FVector2D M = (A + B) * 0.5;
		AddBox(Ctx, EMTADetailLayer::Roads, CityToWorld(Ctx, static_cast<float>(M.X), static_cast<float>(M.Y), 0.f), FVector(Len, Width, 40.f),
			YawFromDir(D), MakeRoad(Len, Width, Kind).V);
	};

	// Kuestenstrasse "Coast Road": verlaengert die westliche Rasterstrasse nach Norden und Sueden entlang der Bucht.
	if (Ctx.CityGen->bGenerateCoast)
	{
		for (int32 Dir = -1; Dir <= 1; Dir += 2)
		{
			FVector2D Prev(Dir * EdgeX, -Ctx.HalfY);
			for (float X = EdgeX + 4000.f; X < GH - 2000.f; X += 4000.f)
			{
				const float SX = Dir * X;
				const float Blend = FMath::SmoothStep(EdgeX, EdgeX + 30000.f, X);
				const float RY = FMath::Lerp(-Ctx.HalfY, CoastLocal(Ctx, SX, false) + 4500.f, Blend);
				const FVector2D Cur(SX, RY);
				AddRoadSeg(Prev, Cur, 1400.f, 0.f);
				// Leitplanke auf der Meerseite (Westen).
				const FVector2D Dn = (Cur - Prev).GetSafeNormal();
				FVector2D Side(-Dn.Y, Dn.X);
				if (Side.Y > 0.0)
				{
					Side = -Side;
				}
				const FVector2D GA = Prev + Side * 850.0;
				const FVector2D GB = Cur + Side * 850.0;
				AddGuardrail(Ctx, CityToWorld(Ctx, static_cast<float>(GA.X), static_cast<float>(GA.Y), 20.f), CityToWorld(Ctx, static_cast<float>(GB.X), static_cast<float>(GB.Y), 20.f));
				Prev = Cur;
			}
		}
		AddLandmark(TEXT("coast_road"), TEXT("Coast Road"), EMTADistrict::Count, CityToWorld(Ctx, EdgeX + 40000.f, -Ctx.HalfY, 0.f), 0.f);
	}

	// Route 9: vom Ostrand der Stadt bis zum (gesperrten) Tunnelportal der Ashridge Tube.
	const float StartY = Ctx.HalfY + Ctx.RoadW * 0.5f;
	FVector2D Prev(Ctx.Route9X, StartY);
	for (float Y = StartY + 4000.f; Y <= Ctx.TunnelY + 1.f; Y += 4000.f)
	{
		const FVector2D Cur(Ctx.Route9X, FMath::Min(Y, Ctx.TunnelY));
		AddRoadSeg(Prev, Cur, 2400.f, 1.f);
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			AddGuardrail(Ctx, CityToWorld(Ctx, Ctx.Route9X + Side * 1350.f, static_cast<float>(Prev.Y), 20.f),
				CityToWorld(Ctx, Ctx.Route9X + Side * 1350.f, static_cast<float>(Cur.Y), 20.f));
		}
		Prev = Cur;
	}
	// Aegis-Checkpoint am Stadteingang (Prolog).
	AddAegisCamera(Ctx, CityToWorld(Ctx, Ctx.Route9X + 1500.f, StartY + 2500.f, 20.f), CityToWorld(Ctx, Ctx.Route9X, StartY + 6000.f, 0.f),
		DistrictAtLocal(Ctx, Ctx.Route9X, Ctx.HalfY - 100.f), FIntPoint(-1, -1), TEXT("checkpoint"), false);

	// Tunnelportal + Sperre.
	const FDetailData PortalData = MakeConcrete(FLinearColor(0.45f, 0.44f, 0.42f), 0.6f, MTAConcretePattern::Plain);
	const float PY = Ctx.TunnelY;
	const float RX = Ctx.Route9X;
	AddBox(Ctx, EMTADetailLayer::StructConcrete, CityToWorld(Ctx, RX - 1600.f, PY + 400.f, 650.f), FVector(700.f, 1200.f, 1300.f), FRotator::ZeroRotator, PortalData.V);
	AddBox(Ctx, EMTADetailLayer::StructConcrete, CityToWorld(Ctx, RX + 1600.f, PY + 400.f, 650.f), FVector(700.f, 1200.f, 1300.f), FRotator::ZeroRotator, PortalData.V);
	AddBox(Ctx, EMTADetailLayer::StructConcrete, CityToWorld(Ctx, RX, PY + 400.f, 1550.f), FVector(3900.f, 1200.f, 500.f), FRotator::ZeroRotator, PortalData.V);
	AddBox(Ctx, EMTADetailLayer::StructConcrete, CityToWorld(Ctx, RX, PY + 800.f, 2300.f), FVector(4800.f, 500.f, 1000.f), FRotator::ZeroRotator, PortalData.V);
	AddBox(Ctx, EMTADetailLayer::StructConcrete, CityToWorld(Ctx, RX, PY + 950.f, 650.f), FVector(2600.f, 100.f, 1300.f), FRotator::ZeroRotator,
		MakeConcrete(FLinearColor(0.015f, 0.015f, 0.02f), 0.f, MTAConcretePattern::Plain).V);
	const FDetailData BoomData = MakePaint(FLinearColor(0.8f, 0.05f, 0.05f), 0.3f, 0.2f, MTAMetalStripes::Boom);
	AddBox(Ctx, EMTADetailLayer::StructMetal, CityToWorld(Ctx, RX, PY - 1500.f, 110.f), FVector(20.f, 2400.f, 20.f), FRotator(0.f, 90.f, 0.f), BoomData.V);
	AddBox(Ctx, EMTADetailLayer::StructMetal, CityToWorld(Ctx, RX - 1250.f, PY - 1500.f, 70.f), FVector(30.f, 30.f, 140.f), FRotator::ZeroRotator, BoomData.V);
	AddBox(Ctx, EMTADetailLayer::StructMetal, CityToWorld(Ctx, RX + 1250.f, PY - 1500.f, 70.f), FVector(30.f, 30.f, 140.f), FRotator::ZeroRotator, BoomData.V);
	AddBox(Ctx, EMTADetailLayer::SignageFar, CityToWorld(Ctx, RX, PY - 1400.f, 320.f), FVector(20.f, 900.f, 160.f), FRotator(0.f, -90.f, 0.f),
		MakeSign(FLinearColor(1.f, 0.8f, 0.05f), MTASignKind::Warning, 9.f, 0.5f).V);
	FMTABillboardSpot Tunnel;
	Tunnel.Location = CityToWorld(Ctx, RX, PY - 1420.f, 320.f);
	Tunnel.Facing = FVector(0.f, -1.f, 0.f);
	Tunnel.PanelSize = FVector2D(900.f, 160.f);
	Tunnel.Slogan = TEXT("ASHRIDGE TUBE - GESPERRT");
	Tunnel.TextColor = FLinearColor::Black;
	Tunnel.District = EMTADistrict::Count;
	SignTexts.Add(Tunnel);
	AddLandmark(TEXT("ashridge_tube"), TEXT("Ashridge Tube (gesperrt)"), EMTADistrict::Count, CityToWorld(Ctx, RX, PY, 0.f), 2800.f);
}

void AMTAWorldDetail::BuildMountains(FDetailContext& Ctx)
{
	FRandomStream R(Seed ^ 0x4D4F);
	const float GH = Ctx.GroundHalf;
	auto AddPeak = [this, &Ctx](float X, float Y, float D, float H, const FLinearColor& Tint, float SnowM, float Haze)
	{
		// Kegel leicht eingesenkt (3 %), damit der Fuss nicht schwebt.
		AddBox(Ctx, EMTADetailLayer::Mountains, CityToWorld(Ctx, X, Y, H * 0.5f - H * 0.03f), FVector(D, D, H), FRotator(0.f, X * 0.01f, 0.f), MakeRock(Tint, SnowM, Haze).V);
		++Stats.MountainPeaks;
	};

	// Hauptgipfel der Ashridge-Berge mit dem Portal der Route 9.
	const float MainY = FMath::Max(Ctx.HalfY + 120000.f, GH - 24000.f);
	const float MainD = 64000.f;
	const float MainH = 42000.f;
	AddPeak(Ctx.Route9X, MainY, MainD, MainH, FLinearColor(0.9f, 0.95f, 0.85f), 380.f, 0.6f);
	// Hoehe 0 der Kegelflanke liegt bei r = R * 0.97 (Einsenkung); dort steht das Portal.
	Ctx.TunnelY = MainY - MainD * 0.5f * 0.97f - 300.f;
	AddLandmark(TEXT("ashridge"), TEXT("Ashridge-Berge"), EMTADistrict::Count, CityToWorld(Ctx, Ctx.Route9X, MainY, 0.f), MainH);

	// Ostkamm (Ashridge).
	TArray<FVector> East; // X, Y, H
	const int32 EastN = 14;
	for (int32 I = 0; I < EastN; ++I)
	{
		const float X = -GH * 0.85f + I * (1.7f * GH / (EastN - 1)) + R.FRandRange(-8000.f, 8000.f);
		const float D = R.FRandRange(36000.f, 70000.f);
		const float H = R.FRandRange(22000.f, 52000.f);
		const float Y = R.FRandRange(FMath::Max(Ctx.HalfY + 85000.f, GH * 0.6f), GH * 0.92f);
		if (FMath::Abs(X - Ctx.Route9X) < D * 0.5f + 9000.f)
		{
			continue; // Route 9 freihalten
		}
		AddPeak(X, Y, D, H, FLinearColor(0.9f, 0.95f, 0.85f), 380.f, 0.6f);
		East.Add(FVector(X, Y, H));
	}
	// Rueckenlinien zwischen benachbarten Gipfeln (abgeflachte Kugeln).
	for (int32 I = 1; I < East.Num(); ++I)
	{
		const FVector& A = East[I - 1];
		const FVector& B = East[I];
		const FVector2D Delta(B.X - A.X, B.Y - A.Y);
		const float Dist = static_cast<float>(Delta.Size());
		const float H = static_cast<float>(FMath::Min(A.Z, B.Z)) * 0.45f;
		AddBox(Ctx, EMTADetailLayer::Hills, CityToWorld(Ctx, static_cast<float>(A.X + B.X) * 0.5f, static_cast<float>(A.Y + B.Y) * 0.5f, 0.f),
			FVector(Dist * 1.1f, 26000.f, H * 2.f), YawFromDir(Delta), MakeRock(FLinearColor(0.95f, 1.f, 0.85f), 9999.f, 0.6f).V);
	}

	// Nordkamm und Suedhuegel (Hoehenzuege, nicht auf der Kuestenstrasse).
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		const int32 N = 10;
		for (int32 I = 0; I < N; ++I)
		{
			const float X = Side * R.FRandRange(GH * 0.62f, GH * 0.95f);
			const float D = Side > 0 ? R.FRandRange(30000.f, 60000.f) : R.FRandRange(26000.f, 46000.f);
			const float H = Side > 0 ? R.FRandRange(15000.f, 38000.f) : R.FRandRange(9000.f, 22000.f);
			const float YMin = CoastLocal(Ctx, X, false) + 4500.f + D * 0.5f + 2000.f;
			const float Y = FMath::Max(YMin, -GH * 0.5f + I * (1.3f * GH / N) + R.FRandRange(-6000.f, 6000.f));
			if (Y > GH * 0.95f)
			{
				continue;
			}
			const FLinearColor Tint = Side > 0 ? FLinearColor(0.85f, 0.9f, 0.8f) : FLinearColor(1.05f, 0.95f, 0.75f);
			AddPeak(X, Y, D, H, Tint, Side > 0 ? 380.f : 9999.f, 0.6f);
		}
	}
	// Landzungen-Huegel an der Bucht.
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		for (int32 I = 0; I < 3; ++I)
		{
			const float X = Side * (GH * 0.4f + I * GH * 0.18f);
			const float Y = CoastLocal(Ctx, X, false) + 9000.f;
			AddBox(Ctx, EMTADetailLayer::Hills, CityToWorld(Ctx, X, Y, 0.f), FVector(36000.f, 16000.f, R.FRandRange(8000.f, 16000.f)),
				FRotator(0.f, Side * 35.f, 0.f), MakeRock(FLinearColor(0.95f, 0.95f, 0.8f), 9999.f, 0.4f).V);
		}
	}
	// Ferner Ring (Dunst): nur Silhouette, nicht im westlichen Meer.
	for (int32 I = 0; I < 18; ++I)
	{
		const float A = FMath::DegreesToRadians(-150.f + I * (300.f / 17.f));
		const float Rad = R.FRandRange(300000.f, 400000.f);
		const float X = Rad * FMath::Sin(A);
		const float Y = Rad * FMath::Cos(A);
		if (Y < -60000.f)
		{
			continue;
		}
		AddPeak(X, Y, R.FRandRange(120000.f, 180000.f), R.FRandRange(60000.f, 110000.f), FLinearColor(0.8f, 0.85f, 0.9f), 600.f, 1.f);
	}
}

// ---------------------------------------------------------------------------- Landmarken

void AMTAWorldDetail::BuildHarbor(FDetailContext& Ctx)
{
	const float Coast0 = CoastLocal(Ctx, 0.f, false);
	const float X0 = Ctx.SaltX0;
	const float X1 = Ctx.SaltX1;
	const float PierLen = 24000.f;
	const float DeckTop = 30.f;
	const float BaseH = DeckTop - SeabedLevel;
	const FDetailData Mole = MakeConcrete(FLinearColor(0.52f, 0.5f, 0.47f), 0.65f, MTAConcretePattern::Plain);

	// Containerterminal (suedliche Mole, 80 m breit) und schmale Nordmole; dazwischen das Hafenbecken.
	const float TX0 = X0 + 1000.f;
	const float TX1 = X0 + 9000.f;
	AddBox(Ctx, EMTADetailLayer::Quay, CityToWorld(Ctx, (TX0 + TX1) * 0.5f, Coast0 - PierLen * 0.5f, SeabedLevel + BaseH * 0.5f), FVector(TX1 - TX0, PierLen, BaseH), FRotator::ZeroRotator, Mole.V);
	const float NX0 = X1 - 4000.f;
	const float NX1 = X1 - 2400.f;
	AddBox(Ctx, EMTADetailLayer::Quay, CityToWorld(Ctx, (NX0 + NX1) * 0.5f, Coast0 - PierLen * 0.5f, SeabedLevel + BaseH * 0.5f), FVector(NX1 - NX0, PierLen, BaseH), FRotator::ZeroRotator, Mole.V);
	Stats.QuayPieces += 2;

	// Molenfeuer: Leuchtturm am Ende der Nordmole (rot-weiss, gruenes Blinklicht).
	const FVector LightFoot = CityToWorld(Ctx, (NX0 + NX1) * 0.5f, Coast0 - PierLen + 500.f, DeckTop);
	AddColumn(Ctx, EMTADetailLayer::StructMetalCyl, LightFoot, 260.f, 1800.f, MakePaint(FLinearColor(0.75f, 0.08f, 0.06f), 0.4f, 0.1f, MTAMetalStripes::Bands).V);
	AddBox(Ctx, EMTADetailLayer::NeonGlobe, LightFoot + FVector(0.f, 0.f, 1900.f), FVector(160.f, 160.f, 160.f), FRotator::ZeroRotator, MakeNeon(FLinearColor(0.1f, 1.f, 0.2f), 30.f, 2.f, 0.4f).V);
	AddBox(Ctx, EMTADetailLayer::NeonGlobe, CityToWorld(Ctx, TX1 - 300.f, Coast0 - PierLen + 300.f, DeckTop + 250.f), FVector(80.f, 80.f, 80.f), FRotator::ZeroRotator, MakeNeon(FLinearColor(1.f, 0.05f, 0.05f), 25.f, 2.f, 0.4f).V);
	AddLandmark(TEXT("harbor_light"), TEXT("Molenfeuer"), EMTADistrict::Saltmarket, LightFoot, 2000.f);

	// Kranschienen und Poller entlang der Beckenkante.
	const float EdgeX = TX1 - 60.f;
	const FDetailData RailData = MakePaint(FLinearColor(0.15f, 0.14f, 0.13f), 0.8f, 0.9f, MTAMetalStripes::None);
	const float XL = X0 + 5800.f;
	const float XR = X0 + 8600.f;
	for (const float RailX : { XL, XR })
	{
		AddBox(Ctx, EMTADetailLayer::StructMetal, CityToWorld(Ctx, RailX, Coast0 - PierLen * 0.5f, DeckTop + 4.f), FVector(30.f, PierLen - 400.f, 8.f), FRotator::ZeroRotator, RailData.V);
	}
	for (float Y = Coast0 - 800.f; Y > Coast0 - PierLen + 400.f; Y -= 1500.f)
	{
		AddColumn(Ctx, EMTADetailLayer::FurnitureCyl, CityToWorld(Ctx, EdgeX, Y, DeckTop), 45.f, 70.f, MakePaint(FLinearColor(0.1f, 0.1f, 0.1f), 0.7f, 0.6f, MTAMetalStripes::None).V);
		++Stats.Bollards;
	}

	// Die Kranreihe: drei rote Containerbruecken mit Warnlichtern, Ausleger ueber dem Becken.
	const FDetailData Red = MakePaint(FLinearColor(0.62f, 0.07f, 0.04f), 0.5f, 0.5f, MTAMetalStripes::None);
	const FDetailData Hazard = MakePaint(FLinearColor(0.9f, 0.7f, 0.05f), 0.5f, 0.3f, MTAMetalStripes::Hazard);
	const FDetailData Warn = MakeNeon(FLinearColor(1.f, 0.05f, 0.02f), 40.f, 2.f, 0.5f);
	const float XC = (XL + XR) * 0.5f;
	for (int32 K = 0; K < 3; ++K)
	{
		const float Y0 = Coast0 - 6000.f - K * 6000.f;
		auto P = [&Ctx, this](float X, float Y, float Z) { return CityToWorld(Ctx, X, Y, Z); };
		for (const float LX : { XL, XR })
		{
			for (int32 S = -1; S <= 1; S += 2)
			{
				const FVector Foot = P(LX, Y0 + S * 700.f, DeckTop);
				AddBox(Ctx, EMTADetailLayer::StructMetal, Foot + FVector(0.f, 0.f, 1750.f), FVector(120.f, 120.f, 3200.f), FRotator::ZeroRotator, Red.V);
				AddBox(Ctx, EMTADetailLayer::StructMetal, Foot + FVector(0.f, 0.f, 150.f), FVector(130.f, 130.f, 300.f), FRotator::ZeroRotator, Hazard.V);
				// A-Rahmen zum Turmkopf.
				AddBeam(Ctx, EMTADetailLayer::StructMetal, Foot + FVector(0.f, 0.f, 3300.f), P(XC, Y0 + S * 450.f, DeckTop + 5600.f), 90.f, 90.f, Red.V);
			}
		}
		for (int32 S = -1; S <= 1; S += 2)
		{
			AddBeam(Ctx, EMTADetailLayer::StructMetal, P(XL, Y0 + S * 700.f, DeckTop + 3300.f), P(XR, Y0 + S * 700.f, DeckTop + 3300.f), 140.f, 140.f, Red.V);
		}
		// Ausleger: Rueckausleger landseitig, Hauptausleger 45 m ueber das Becken.
		const FVector BoomA = P(XL - 3000.f, Y0, DeckTop + 4300.f);
		const FVector BoomB = P(XR + 4500.f, Y0, DeckTop + 4300.f);
		AddBeam(Ctx, EMTADetailLayer::StructMetal, BoomA, BoomB, 260.f, 420.f, Red.V);
		const FVector Apex = P(XC, Y0, DeckTop + 5700.f);
		AddBeam(Ctx, EMTADetailLayer::StructMetal, Apex, BoomB + FVector(0.f, 0.f, 120.f), 50.f, 50.f, Red.V);
		AddBeam(Ctx, EMTADetailLayer::StructMetal, Apex, BoomA + FVector(0.f, 0.f, 120.f), 50.f, 50.f, Red.V);
		AddBox(Ctx, EMTADetailLayer::StructMetal, P(XC - 600.f, Y0, DeckTop + 4700.f), FVector(900.f, 700.f, 500.f), FRotator::ZeroRotator,
			MakePaint(FLinearColor(0.85f, 0.85f, 0.82f), 0.4f, 0.3f, MTAMetalStripes::None).V);
		// Laufkatze mit Spreader und Seilen.
		const float TrolleyX = XR + 2000.f + K * 700.f;
		AddBox(Ctx, EMTADetailLayer::StructMetal, P(TrolleyX, Y0, DeckTop + 4050.f), FVector(400.f, 500.f, 200.f), FRotator::ZeroRotator, Red.V);
		AddRod(Ctx, EMTADetailLayer::Lines, P(TrolleyX, Y0 - 150.f, DeckTop + 3950.f), P(TrolleyX, Y0 - 150.f, DeckTop + 1600.f), 4.f, RailData.V);
		AddRod(Ctx, EMTADetailLayer::Lines, P(TrolleyX, Y0 + 150.f, DeckTop + 3950.f), P(TrolleyX, Y0 + 150.f, DeckTop + 1600.f), 4.f, RailData.V);
		AddBox(Ctx, EMTADetailLayer::StructMetal, P(TrolleyX, Y0, DeckTop + 1550.f), FVector(260.f, 1250.f, 80.f), FRotator::ZeroRotator, Hazard.V);
		// Warnlichter (blinkend) an Auslegerspitze und Turm.
		AddBox(Ctx, EMTADetailLayer::NeonGlobe, BoomB + FVector(0.f, 0.f, 200.f), FVector(90.f, 90.f, 90.f), FRotator::ZeroRotator, Warn.V);
		AddBox(Ctx, EMTADetailLayer::NeonGlobe, Apex + FVector(0.f, 0.f, 90.f), FVector(90.f, 90.f, 90.f), FRotator::ZeroRotator, Warn.V);
	}
	AddLandmark(TEXT("crane_row"), TEXT("Die Kranreihe"), EMTADistrict::Saltmarket, CityToWorld(Ctx, XC, Coast0 - 12000.f, DeckTop), 5700.f);

	// Containerstapel: Terminal (laengs Y) und Kai (laengs X).
	auto AddStack = [this, &Ctx](float X, float Y, bool bAlongY, int32 Levels, float Z0)
	{
		for (int32 Lv = 0; Lv < Levels; ++Lv)
		{
			const FLinearColor Col = ContainerPalette[Ctx.Rng.RandRange(0, NumContainerColors - 1)];
			const FVector Size = bAlongY ? FVector(244.f, 1220.f, 259.f) : FVector(1220.f, 244.f, 259.f);
			AddBox(Ctx, EMTADetailLayer::Containers, CityToWorld(Ctx, X, Y, Z0 + 259.f * Lv + 129.5f), Size, FRotator(0.f, Ctx.Rng.FRandRange(-1.5f, 1.5f), 0.f),
				MakePaint(Col, Ctx.Rng.FRandRange(0.3f, 0.85f), 0.4f, MTAMetalStripes::Corrugated).V);
		}
	};
	for (int32 Row = 0; Row < 15; ++Row)
	{
		const float Y = Coast0 - 1500.f - Row * 1400.f;
		for (int32 C = 0; C < 10; ++C)
		{
			const float X = C < 6 ? X0 + 1600.f + C * 290.f : X0 + 3700.f + (C - 6) * 290.f;
			const int32 Levels = FMath::Clamp(Ctx.Rng.RandRange(-1, 4), 0, 4);
			AddStack(X, Y, true, Levels, DeckTop);
		}
	}
	const float KaiX0 = X0 + 10000.f;
	const float KaiX1 = X1 - 6000.f;
	for (int32 Row = 0; Row < 3; ++Row)
	{
		const float Y = Coast0 + 700.f + Row * 270.f;
		for (float X = KaiX0 + 700.f; X < KaiX1 - 600.f; X += 1320.f)
		{
			AddStack(X, Y, false, Ctx.Rng.RandRange(1, 3), 12.f);
		}
	}
	AddLandmark(TEXT("container_terminal"), TEXT("Containerterminal"), EMTADistrict::Saltmarket, CityToWorld(Ctx, X0 + 3000.f, Coast0 - 12000.f, DeckTop), 1100.f);

	// Hafentor mit Aegis-Kamera ("Kameras nur an Toren").
	const float GateY = -Ctx.HalfY - Ctx.RoadW * 0.5f - 1700.f;
	const float GateX = (TX0 + TX1) * 0.5f;
	const FDetailData Pillar = MakeConcrete(FLinearColor(0.6f, 0.58f, 0.55f), 0.6f, MTAConcretePattern::Plain);
	AddBox(Ctx, EMTADetailLayer::StructConcrete, CityToWorld(Ctx, GateX - 900.f, GateY, 240.f), FVector(100.f, 100.f, 450.f), FRotator::ZeroRotator, Pillar.V);
	AddBox(Ctx, EMTADetailLayer::StructConcrete, CityToWorld(Ctx, GateX + 900.f, GateY, 240.f), FVector(100.f, 100.f, 450.f), FRotator::ZeroRotator, Pillar.V);
	AddBox(Ctx, EMTADetailLayer::StructMetal, CityToWorld(Ctx, GateX - 450.f, GateY, 120.f), FVector(850.f, 15.f, 15.f), FRotator(60.f, 0.f, 0.f),
		MakePaint(FLinearColor(0.8f, 0.05f, 0.05f), 0.4f, 0.2f, MTAMetalStripes::Boom).V);
	AddAegisCamera(Ctx, CityToWorld(Ctx, GateX + 1200.f, GateY + 300.f, 12.f), CityToWorld(Ctx, GateX, GateY - 2000.f, 0.f), EMTADistrict::Saltmarket,
		FIntPoint(-1, -1), TEXT("harbor"), false);
}

void AMTAWorldDetail::BuildSunstrip(FDetailContext& Ctx)
{
	const float Coast0 = CoastLocal(Ctx, 0.f, false);
	const float XP = (Ctx.SunX0 + Ctx.SunX1) * 0.5f;
	const float DeckTop = 40.f;
	const float PierStart = Coast0 + 2000.f;
	const float PierEnd = Coast0 - 36000.f;
	const FDetailData Wood = MakeConcrete(FLinearColor(0.45f, 0.32f, 0.2f), 0.45f, MTAConcretePattern::Planks);
	const FDetailData PileData = MakeConcrete(FLinearColor(0.35f, 0.3f, 0.25f), 0.8f, MTAConcretePattern::Plain);

	// Pier auf Pfaehlen.
	AddBox(Ctx, EMTADetailLayer::Quay, CityToWorld(Ctx, XP, (PierStart + PierEnd) * 0.5f, DeckTop - 25.f), FVector(1200.f, PierStart - PierEnd, 50.f), FRotator::ZeroRotator, Wood.V);
	const float WheelY = Coast0 - 19000.f;
	AddBox(Ctx, EMTADetailLayer::Quay, CityToWorld(Ctx, XP, WheelY, DeckTop - 25.f), FVector(6400.f, 6000.f, 50.f), FRotator::ZeroRotator, Wood.V);
	AddBox(Ctx, EMTADetailLayer::Quay, CityToWorld(Ctx, XP, PierEnd + 2000.f, DeckTop - 25.f), FVector(4000.f, 4000.f, 50.f), FRotator::ZeroRotator, Wood.V);
	for (float Y = Coast0 - 400.f; Y > PierEnd + 200.f; Y -= 800.f)
	{
		for (int32 S = -1; S <= 1; S += 2)
		{
			AddColumn(Ctx, EMTADetailLayer::Piles, CityToWorld(Ctx, XP + S * 520.f, Y, SeabedLevel), 45.f, -10.f - SeabedLevel, PileData.V);
		}
	}
	for (int32 GX = -1; GX <= 1; GX += 2)
	{
		for (int32 GY = -1; GY <= 1; GY += 2)
		{
			AddColumn(Ctx, EMTADetailLayer::Piles, CityToWorld(Ctx, XP + GX * 2800.f, WheelY + GY * 2600.f, SeabedLevel), 60.f, -10.f - SeabedLevel, PileData.V);
		}
	}
	// Gelaender und Lampen.
	const FDetailData RailData = MakePaint(FLinearColor(0.9f, 0.9f, 0.88f), 0.3f, 0.6f, MTAMetalStripes::None);
	const FDetailData LampGlow = MakeNeon(FLinearColor(1.f, 0.85f, 0.6f), 12.f, 0.f, 0.f);
	for (int32 S = -1; S <= 1; S += 2)
	{
		const float RX = XP + S * 580.f;
		for (float Y = Coast0; Y > PierEnd; Y -= 4000.f)
		{
			const float YEnd = FMath::Max(Y - 4000.f, PierEnd);
			AddRod(Ctx, EMTADetailLayer::Lines, CityToWorld(Ctx, RX, Y, DeckTop + 100.f), CityToWorld(Ctx, RX, YEnd, DeckTop + 100.f), 6.f, RailData.V);
		}
		for (float Y = Coast0 - 1200.f; Y > PierEnd; Y -= 2400.f)
		{
			AddColumn(Ctx, EMTADetailLayer::Poles, CityToWorld(Ctx, RX, Y, DeckTop), 14.f, 420.f, RailData.V);
			AddBox(Ctx, EMTADetailLayer::NeonGlobe, CityToWorld(Ctx, RX, Y, DeckTop + 440.f), FVector(50.f, 50.f, 50.f), FRotator::ZeroRotator, LampGlow.V);
		}
	}
	AddLandmark(TEXT("sunstrip_pier"), TEXT("Pier von Sunstrip"), EMTADistrict::Sunstrip, CityToWorld(Ctx, XP, PierStart, DeckTop), 500.f);

	// Kestrel Wheel: statische A-Boecke, drehender Teil am WheelPivot (lokale Koordinaten, Achse = Y).
	const float Radius = 3000.f;
	const FVector Hub = CityToWorld(Ctx, XP, WheelY, DeckTop + Radius + 350.f);
	WheelHub = Hub;
	if (WheelPivot)
	{
		WheelPivot->SetWorldLocation(Hub);
	}
	const FDetailData Steel = MakePaint(FLinearColor(0.92f, 0.92f, 0.95f), 0.15f, 0.7f, MTAMetalStripes::None);
	for (int32 SX = -1; SX <= 1; SX += 2)
	{
		for (int32 SY = -1; SY <= 1; SY += 2)
		{
			AddBeam(Ctx, EMTADetailLayer::StructMetal, CityToWorld(Ctx, XP + SX * 2300.f, WheelY + SY * 700.f, DeckTop), Hub + FVector(0.f, SY * 380.f, 0.f), 160.f, 160.f, Steel.V);
		}
	}
	AddRod(Ctx, EMTADetailLayer::StructMetalCyl, Hub - FVector(0.f, 650.f, 0.f), Hub + FVector(0.f, 650.f, 0.f), 220.f, Steel.V);

	const int32 RimSeg = 36;
	const int32 Cabins = 18;
	const FLinearColor Pink(1.f, 0.15f, 0.6f);
	const FLinearColor Cyan(0.1f, 0.9f, 1.f);
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		const float YOff = Side * 300.f;
		for (int32 K = 0; K < RimSeg; ++K)
		{
			const float A0 = K * UE_TWO_PI / RimSeg;
			const float A1 = (K + 1) * UE_TWO_PI / RimSeg;
			const FVector P0(Radius * FMath::Cos(A0), YOff, Radius * FMath::Sin(A0));
			const FVector P1(Radius * FMath::Cos(A1), YOff, Radius * FMath::Sin(A1));
			AddBeam(Ctx, EMTADetailLayer::WheelFrame, P0, P1, 60.f, 60.f, Steel.V);
			const FVector N0 = P0 * ((Radius + 45.f) / Radius);
			const FVector N1 = P1 * ((Radius + 45.f) / Radius);
			AddBeam(Ctx, EMTADetailLayer::WheelNeon, FVector(N0.X, YOff, N0.Z), FVector(N1.X, YOff, N1.Z), 18.f, 18.f, MakeNeon((K % 2) ? Pink : Cyan, 8.f, 0.f, 0.15f).V);
		}
		for (int32 K = 0; K < Cabins; ++K)
		{
			const float A = K * UE_TWO_PI / Cabins;
			const FVector Rim(Radius * FMath::Cos(A), YOff, Radius * FMath::Sin(A));
			AddBeam(Ctx, EMTADetailLayer::WheelFrame, FVector(0.f, YOff * 0.8f, 0.f), Rim, 40.f, 40.f, Steel.V);
			if (Side > 0)
			{
				// Speichen-Neon auf der Stadtseite (Osten).
				AddBeam(Ctx, EMTADetailLayer::WheelNeon, FVector(0.f, YOff + 30.f, 0.f), Rim + FVector(0.f, 30.f, 0.f), 14.f, 14.f, MakeNeon((K % 2) ? Cyan : Pink, 6.f, 0.f, 0.1f).V);
			}
		}
	}
	// Gondeln: Zylinder mit Achse parallel zur Radachse (sehen in jeder Drehlage gleich aus).
	for (int32 K = 0; K < Cabins; ++K)
	{
		const float A = (K + 0.5f) * UE_TWO_PI / Cabins;
		const FVector C(Radius * FMath::Cos(A), 0.f, Radius * FMath::Sin(A));
		const FLinearColor Col = NeonPalette[K % NumNeonColors] * 0.6f + FLinearColor(0.35f, 0.35f, 0.35f);
		AddRod(Ctx, EMTADetailLayer::WheelCabins, C - FVector(0.f, 230.f, 0.f), C + FVector(0.f, 230.f, 0.f), 300.f, MakePaint(Col, 0.15f, 0.2f, MTAMetalStripes::None).V);
	}
	AddLandmark(TEXT("kestrel_wheel"), TEXT("Kestrel Wheel"), EMTADistrict::Sunstrip, Hub - FVector(0.f, 0.f, Radius + 350.f), Radius * 2.f + 350.f);
	AddAegisCamera(Ctx, CityToWorld(Ctx, XP + 900.f, PierStart + 800.f, 12.f), CityToWorld(Ctx, XP, PierStart - 3000.f, 0.f), EMTADistrict::Sunstrip,
		FIntPoint(-1, -1), TEXT("landmark"), false);

	// Neon-Promenade: Boegen ueber dem Kaistreifen, Leuchtband an der Strandkante, Palmen.
	const float PromY0 = -Ctx.HalfY - Ctx.RoadW * 0.5f - 500.f;
	const float PromY1 = Coast0 + 2300.f;
	int32 ArchIndex = 0;
	for (float X = Ctx.SunX0 + 1500.f; X < Ctx.SunX1 + 2500.f; X += 3000.f, ++ArchIndex)
	{
		const FLinearColor Col = (ArchIndex % 2) ? Pink : Cyan;
		AddColumn(Ctx, EMTADetailLayer::Poles, CityToWorld(Ctx, X, PromY0, 12.f), 22.f, 520.f, RailData.V);
		AddColumn(Ctx, EMTADetailLayer::Poles, CityToWorld(Ctx, X, PromY1, 12.f), 22.f, 520.f, RailData.V);
		AddBox(Ctx, EMTADetailLayer::NeonLandmark, CityToWorld(Ctx, X, (PromY0 + PromY1) * 0.5f, 540.f), FVector(30.f, PromY0 - PromY1, 30.f), FRotator::ZeroRotator, MakeNeon(Col, 10.f, 0.f, 0.2f).V);
		AddBox(Ctx, EMTADetailLayer::NeonSmall, CityToWorld(Ctx, X + 1500.f, PromY1 - 150.f, 40.f), FVector(3000.f, 12.f, 10.f), FRotator::ZeroRotator, MakeNeon(Col, 6.f, 0.f, 0.f).V);
		AddPalm(Ctx, CityToWorld(Ctx, X + 750.f, (PromY0 + PromY1) * 0.5f + 600.f, 12.f), Ctx.Rng.FRandRange(0.85f, 1.2f));
		AddPalm(Ctx, CityToWorld(Ctx, X + 2250.f, (PromY0 + PromY1) * 0.5f - 600.f, 12.f), Ctx.Rng.FRandRange(0.85f, 1.2f));
		++Stats.NeonSigns;
	}
	AddLandmark(TEXT("neon_promenade"), TEXT("Neon-Promenade"), EMTADistrict::Sunstrip, CityToWorld(Ctx, XP, (PromY0 + PromY1) * 0.5f, 12.f), 540.f);
	// Grosse Strandwerbung (Werbung allgegenwaertig).
	for (int32 I = 0; I < 2; ++I)
	{
		const float X = Ctx.SunX0 + 4000.f + I * (Ctx.SunX1 - Ctx.SunX0 - 8000.f);
		AddBillboard(Ctx, CityToWorld(Ctx, X, PromY1 + 600.f, 900.f), FVector(0.f, -1.f, 0.f), 1600.f, 600.f, EMTADistrict::Sunstrip, true, static_cast<float>(Ctx.Origin.Z) + 12.f);
	}
}

void AMTAWorldDetail::BuildCoreTowers(FDetailContext& Ctx)
{
	// Die hoechsten Core-Tuerme (ohne Aurelia-Block) bekommen Aegis-Logoflaechen und Flugwarnlichter.
	const TArray<FMTACityLot>& Lots = Ctx.CityGen->GetCityLots();
	const int32 LandmarkBlock = Ctx.CityGen->LandmarkBlockIndex;
	TArray<int32> Towers;
	for (int32 I = 0; I < Lots.Num(); ++I)
	{
		const FMTACityLot& L = Lots[I];
		if (L.District == EMTADistrict::Core && L.bTower && !L.bEmpty && (L.Block.X + L.Block.Y * Ctx.BlocksX) != LandmarkBlock)
		{
			Towers.Add(I);
		}
	}
	Towers.Sort([&Lots](int32 A, int32 B) { return Lots[A].Height > Lots[B].Height; });
	const int32 Count = FMath::Min(5, Towers.Num());
	const FLinearColor AegisCyan(0.2f, 0.85f, 1.f);
	for (int32 K = 0; K < Count; ++K)
	{
		const FMTACityLot& L = Lots[Towers[K]];
		const float MainTop = L.BaseZ + L.Height;
		for (int32 Face = 0; Face < 2; ++Face)
		{
			FVector2D Dir = Face == 0 ? L.StreetDir : (L.SecondStreetDir.IsNearlyZero() ? -L.StreetDir : L.SecondStreetDir);
			const float HalfDepth = static_cast<float>(FMath::Abs(Dir.X) * L.Size.X + FMath::Abs(Dir.Y) * L.Size.Y) * 0.5f;
			const float FaceWidth = static_cast<float>(FMath::Abs(Dir.Y) * L.Size.X + FMath::Abs(Dir.X) * L.Size.Y);
			const float LogoSize = FMath::Min(1400.f, FaceWidth * 0.6f);
			const FVector2D P = L.Center + Dir * (HalfDepth + 15.f);
			AddBox(Ctx, EMTADetailLayer::SignageFar, FVector(P.X, P.Y, MainTop - LogoSize * 0.5f - 300.f), FVector(30.f, LogoSize, LogoSize), YawFromDir(Dir),
				MakeSign(AegisCyan, MTASignKind::AegisLogo, static_cast<float>(L.Seed % 1000), 1.f).V);
		}
		AddBox(Ctx, EMTADetailLayer::NeonGlobe, FVector(L.Center.X, L.Center.Y, L.RoofTopZ + 60.f), FVector(70.f, 70.f, 70.f), FRotator::ZeroRotator,
			MakeNeon(FLinearColor(1.f, 0.03f, 0.03f), 40.f, 2.f, 0.5f).V);
		AddLandmark(*FString::Printf(TEXT("aegis_tower_%d"), K), FString::Printf(TEXT("Aegis-Turm %d"), K + 1), EMTADistrict::Core,
			FVector(L.Center.X, L.Center.Y, L.BaseZ), L.RoofTopZ - L.BaseZ);
	}
}

void AMTAWorldDetail::BuildLanternHill(FDetailContext& Ctx)
{
	FVector C;
	float Inner = 0.f;
	const FMTADistrictProfile& LP = ProfileOf(Ctx, EMTADistrict::LanternHill);
	if (Ctx.CityGen->GetDistrictPlaza(EMTADistrict::LanternHill, C, Inner))
	{
		// Glockenturm St. Merrin auf einer Terrasse mit beleuchteter Treppe.
		const float Z0 = static_cast<float>(C.Z);
		const FDetailData Stone = MakeConcrete(FLinearColor(0.66f, 0.52f, 0.34f), 0.45f, MTAConcretePattern::Plain);
		const FDetailData Ochre = MakeConcrete(FLinearColor(0.78f, 0.58f, 0.36f), 0.4f, MTAConcretePattern::Plain);
		const float TerrH = 300.f;
		const FVector TC = C + FVector(500.f, 0.f, 0.f);
		AddBox(Ctx, EMTADetailLayer::StructConcrete, FVector(TC.X, TC.Y, Z0 + TerrH * 0.5f), FVector(3800.f, 3000.f, TerrH), FRotator::ZeroRotator, Stone.V);
		const float StairX0 = static_cast<float>(TC.X) - 1900.f;
		for (int32 Step = 0; Step < 10; ++Step)
		{
			const float H = 30.f * (Step + 1);
			const float X = StairX0 - (10 - Step) * 45.f + 22.5f;
			AddBox(Ctx, EMTADetailLayer::StructConcrete, FVector(X, TC.Y, Z0 + H * 0.5f), FVector(45.f, 1600.f, H), FRotator::ZeroRotator, Stone.V);
			AddBox(Ctx, EMTADetailLayer::NeonLandmark, FVector(X - 23.f, TC.Y, Z0 + H - 4.f), FVector(4.f, 1560.f, 5.f), FRotator::ZeroRotator,
				MakeNeon(FLinearColor(1.f, 0.7f, 0.35f), 8.f, 0.f, 0.f).V);
		}
		// Kirchenschiff mit Satteldach.
		const float ZT = Z0 + TerrH;
		AddBox(Ctx, EMTADetailLayer::StructConcrete, FVector(TC.X + 500.f, TC.Y, ZT + 650.f), FVector(2600.f, 1200.f, 1300.f), FRotator::ZeroRotator, Ochre.V);
		const float RoofS = 1200.f / 1.41421356f * 1.06f;
		AddBox(Ctx, EMTADetailLayer::StructConcrete, FVector(TC.X + 500.f, TC.Y, ZT + 1300.f), FVector(2650.f, RoofS, RoofS), FRotator(0.f, 0.f, 45.f),
			MakeConcrete(FLinearColor(0.5f, 0.2f, 0.12f), 0.5f, MTAConcretePattern::Plain).V);
		// Turm: Schaft, Glockenstube mit warmem Licht, Gesims, Spitze, Uhren.
		const FVector TB = FVector(TC.X - 1200.f, TC.Y, ZT);
		AddBox(Ctx, EMTADetailLayer::StructConcrete, TB + FVector(0.f, 0.f, 1250.f), FVector(700.f, 700.f, 2500.f), FRotator::ZeroRotator, Ochre.V);
		for (int32 SX = -1; SX <= 1; SX += 2)
		{
			for (int32 SY = -1; SY <= 1; SY += 2)
			{
				AddBox(Ctx, EMTADetailLayer::StructConcrete, TB + FVector(SX * 260.f, SY * 260.f, 2850.f), FVector(180.f, 180.f, 700.f), FRotator::ZeroRotator, Ochre.V);
			}
		}
		AddBox(Ctx, EMTADetailLayer::NeonLandmark, TB + FVector(0.f, 0.f, 2850.f), FVector(420.f, 420.f, 520.f), FRotator::ZeroRotator, MakeNeon(FLinearColor(1.f, 0.65f, 0.3f), 6.f, 0.f, 0.f).V);
		AddColumn(Ctx, EMTADetailLayer::StructCone, TB + FVector(0.f, 0.f, 2700.f), 260.f, 260.f, MakePaint(FLinearColor(0.55f, 0.4f, 0.15f), 0.6f, 0.9f, MTAMetalStripes::None).V);
		AddBox(Ctx, EMTADetailLayer::StructConcrete, TB + FVector(0.f, 0.f, 3300.f), FVector(760.f, 760.f, 200.f), FRotator::ZeroRotator, Stone.V);
		AddColumn(Ctx, EMTADetailLayer::StructCone, TB + FVector(0.f, 0.f, 3400.f), 760.f, 1800.f, MakePaint(FLinearColor(0.2f, 0.24f, 0.26f), 0.4f, 0.4f, MTAMetalStripes::None).V);
		for (int32 F = 0; F < 4; ++F)
		{
			const FVector2D N(FMath::Cos(F * UE_HALF_PI), FMath::Sin(F * UE_HALF_PI));
			AddBox(Ctx, EMTADetailLayer::NeonLandmark, TB + FVector(N.X * 355.f, N.Y * 355.f, 2150.f), FVector(8.f, 260.f, 260.f), YawFromDir(N),
				MakeNeon(FLinearColor(1.f, 0.95f, 0.8f), 4.f, 0.f, 0.f).V);
		}
		AddLandmark(TEXT("st_merrin"), TEXT("Glockenturm St. Merrin"), EMTADistrict::LanternHill, TB, TerrH + 5200.f);
		AddAegisCamera(Ctx, C + FVector(-Inner * 0.5f + 300.f, Inner * 0.5f - 300.f, 0.f), TB, EMTADistrict::LanternHill, FIntPoint(-1, -1), TEXT("landmark"),
			Ctx.Rng.FRand() < 0.6f);
	}

	// Antennenwald und Waescheleinen auf/zwischen den Altstadthaeusern.
	const TArray<FMTACityLot>& Lots = Ctx.CityGen->GetCityLots();
	const FDetailData AntennaData = MakePaint(FLinearColor(0.3f, 0.3f, 0.32f), 0.6f, 0.8f, MTAMetalStripes::None);
	const FDetailData LineData = MakePaint(FLinearColor(0.85f, 0.85f, 0.8f), 0.3f, 0.f, MTAMetalStripes::None);
	FVector Sum = FVector::ZeroVector;
	int32 Antennas = 0;
	int32 LineCount = 0;
	for (int32 I = 0; I < Lots.Num(); ++I)
	{
		const FMTACityLot& L = Lots[I];
		if (L.District != EMTADistrict::LanternHill || L.bEmpty || L.Height < 500.f)
		{
			continue;
		}
		FRandomStream LR(L.Seed);
		const float Top = L.BaseZ + L.Height;
		const int32 N = FMath::RoundToInt(LR.FRandRange(2.f, 7.f) * LP.RooftopClutter);
		for (int32 A = 0; A < N; ++A)
		{
			const float X = static_cast<float>(L.Center.X + L.Size.X * LR.FRandRange(-0.4f, 0.4f));
			const float Y = static_cast<float>(L.Center.Y + L.Size.Y * LR.FRandRange(-0.4f, 0.4f));
			const float AH = LR.FRandRange(400.f, 1100.f);
			const float Base = L.bFlatRoof ? Top : Top + 150.f;
			AddColumn(Ctx, EMTADetailLayer::RoofAntennas, FVector(X, Y, Base), LR.FRandRange(8.f, 14.f), AH, AntennaData.V);
			const int32 Bars = LR.RandRange(1, 3);
			for (int32 Bar = 0; Bar < Bars; ++Bar)
			{
				const float BZ = Base + AH - 60.f - Bar * 90.f;
				const float BYaw = LR.FRandRange(0.f, 180.f);
				const FVector Off = FRotator(0.f, BYaw, 0.f).Vector() * 90.f;
				AddRod(Ctx, EMTADetailLayer::RoofAntennas, FVector(X, Y, BZ) - Off, FVector(X, Y, BZ) + Off, 5.f, AntennaData.V);
			}
			if (LR.FRand() < 0.3f)
			{
				// Satellitenschuessel: flacher, gekippter Zylinder.
				AddInst(Ctx, EMTADetailLayer::RoofAntennas, FTransform(FRotator(60.f, LR.FRandRange(0.f, 360.f), 0.f), FVector(X + 80.f, Y, Base + 90.f), FVector(0.9f, 0.9f, 0.1f)),
					MakePaint(FLinearColor(0.8f, 0.8f, 0.78f), 0.4f, 0.3f, MTAMetalStripes::None).V);
			}
			++Antennas;
		}
		Sum += FVector(L.Center.X, L.Center.Y, Top);

		// Waescheleinen zum naechsten Nachbarn in +X und +Y (Gasse 0,6-9 m).
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			int32 Best = INDEX_NONE;
			double BestD = 2200.0;
			for (int32 J = 0; J < Lots.Num(); ++J)
			{
				const FMTACityLot& O = Lots[J];
				if (J == I || O.District != EMTADistrict::LanternHill || O.bEmpty || O.Block != L.Block)
				{
					continue;
				}
				const double Along = Axis == 0 ? O.Center.X - L.Center.X : O.Center.Y - L.Center.Y;
				const double Perp = Axis == 0 ? FMath::Abs(O.Center.Y - L.Center.Y) : FMath::Abs(O.Center.X - L.Center.X);
				if (Along > 1.0 && Along < BestD && Perp < 300.0)
				{
					BestD = Along;
					Best = J;
				}
			}
			if (Best == INDEX_NONE)
			{
				continue;
			}
			const FMTACityLot& O = Lots[Best];
			const double HalfA = Axis == 0 ? L.Size.X * 0.5 : L.Size.Y * 0.5;
			const double HalfB = Axis == 0 ? O.Size.X * 0.5 : O.Size.Y * 0.5;
			const double Gap = BestD - HalfA - HalfB;
			if (Gap < 60.0 || Gap > 900.0)
			{
				continue;
			}
			const float MaxZ = FMath::Min(L.Height, O.Height) - 150.f;
			for (float LZ = 450.f; LZ <= FMath::Min(MaxZ, 800.f); LZ += 350.f)
			{
				const FVector2D AxisDir = Axis == 0 ? FVector2D(1.0, 0.0) : FVector2D(0.0, 1.0);
				const FVector2D PerpDir(-AxisDir.Y, AxisDir.X);
				const double PerpOff = LR.FRandRange(-0.25f, 0.25f) * (Axis == 0 ? L.Size.Y : L.Size.X);
				const FVector2D A2 = L.Center + AxisDir * (HalfA + 5.0) + PerpDir * PerpOff;
				const FVector2D B2 = L.Center + AxisDir * (BestD - HalfB - 5.0) + PerpDir * PerpOff;
				const FVector PA(A2.X, A2.Y, L.BaseZ + LZ);
				const FVector PB(B2.X, B2.Y, L.BaseZ + LZ);
				const FVector PM = (PA + PB) * 0.5 - FVector(0.f, 0.f, 30.f);
				AddRod(Ctx, EMTADetailLayer::Lines, PA, PM, 2.5f, LineData.V);
				AddRod(Ctx, EMTADetailLayer::Lines, PM, PB, 2.5f, LineData.V);
				const int32 Pieces = LR.RandRange(2, 5);
				for (int32 Pc = 0; Pc < Pieces; ++Pc)
				{
					const float T01 = (Pc + 0.5f) / Pieces;
					const FVector P = FMath::Lerp(PA, PB, static_cast<double>(T01)) - FVector(0.f, 0.f, 30.f * (1.f - FMath::Abs(T01 * 2.f - 1.f)) + 45.f);
					const FLinearColor Col = NeonPalette[LR.RandRange(0, NumNeonColors - 1)] * 0.5f + FLinearColor(0.4f, 0.4f, 0.4f);
					AddBox(Ctx, EMTADetailLayer::Cloth, P, FVector(LR.FRandRange(40.f, 80.f), 3.f, LR.FRandRange(50.f, 90.f)), YawFromDir(AxisDir),
						MakeSign(Col, MTASignKind::Cloth, static_cast<float>(Pc), 0.f).V);
				}
				++LineCount;
			}
		}
	}
	if (Antennas > 0)
	{
		AddLandmark(TEXT("antenna_forest"), TEXT("Antennenwald"), EMTADistrict::LanternHill, Sum / FMath::Max(1, Antennas), 800.f);
	}
	if (LineCount > 0)
	{
		AddLandmark(TEXT("wash_lines"), TEXT("Waescheleinen der Altstadt"), EMTADistrict::LanternHill, Sum / FMath::Max(1, Antennas), 600.f);
	}
}

void AMTAWorldDetail::BuildGreenmoor(FDetailContext& Ctx)
{
	FVector C;
	float Inner = 0.f;
	if (Ctx.CityGen->GetDistrictPlaza(EMTADistrict::Greenmoor, C, Inner))
	{
		// Country-Club-Wasserturm.
		const FDetailData Leg = MakePaint(FLinearColor(0.75f, 0.78f, 0.76f), 0.15f, 0.6f, MTAMetalStripes::None);
		const FDetailData Tank = MakePaint(FLinearColor(0.72f, 0.86f, 0.78f), 0.1f, 0.4f, MTAMetalStripes::None);
		for (int32 SX = -1; SX <= 1; SX += 2)
		{
			for (int32 SY = -1; SY <= 1; SY += 2)
			{
				AddColumn(Ctx, EMTADetailLayer::StructMetalCyl, C + FVector(SX * 450.f, SY * 450.f, 0.f), 60.f, 2600.f, Leg.V);
			}
		}
		for (int32 Lv = 0; Lv < 2; ++Lv)
		{
			const float Z0 = 300.f + Lv * 1100.f;
			const float Z1 = Z0 + 1000.f;
			AddBeam(Ctx, EMTADetailLayer::StructMetal, C + FVector(-450.f, -450.f, Z0), C + FVector(450.f, -450.f, Z1), 20.f, 20.f, Leg.V);
			AddBeam(Ctx, EMTADetailLayer::StructMetal, C + FVector(450.f, 450.f, Z0), C + FVector(-450.f, 450.f, Z1), 20.f, 20.f, Leg.V);
			AddBeam(Ctx, EMTADetailLayer::StructMetal, C + FVector(-450.f, 450.f, Z0), C + FVector(-450.f, -450.f, Z1), 20.f, 20.f, Leg.V);
			AddBeam(Ctx, EMTADetailLayer::StructMetal, C + FVector(450.f, -450.f, Z0), C + FVector(450.f, 450.f, Z1), 20.f, 20.f, Leg.V);
		}
		AddColumn(Ctx, EMTADetailLayer::StructMetalCyl, C, 80.f, 2600.f, Leg.V);
		AddColumn(Ctx, EMTADetailLayer::StructMetalCyl, C + FVector(0.f, 0.f, 2580.f), 1600.f, 20.f, Leg.V);
		AddColumn(Ctx, EMTADetailLayer::StructMetalCyl, C + FVector(0.f, 0.f, 2600.f), 1400.f, 900.f, Tank.V);
		AddColumn(Ctx, EMTADetailLayer::StructCone, C + FVector(0.f, 0.f, 3500.f), 1450.f, 450.f, MakePaint(FLinearColor(0.25f, 0.4f, 0.3f), 0.1f, 0.5f, MTAMetalStripes::None).V);
		const FDetailData Band = MakeNeon(FLinearColor(0.3f, 1.f, 0.45f), 6.f, 0.f, 0.f);
		for (int32 K = 0; K < 12; ++K)
		{
			const float A = K * UE_TWO_PI / 12.f;
			const FVector2D N(FMath::Cos(A), FMath::Sin(A));
			AddBox(Ctx, EMTADetailLayer::NeonLandmark, C + FVector(N.X * 706.f, N.Y * 706.f, 3100.f), FVector(6.f, 380.f, 50.f), YawFromDir(N), Band.V);
		}
		AddBox(Ctx, EMTADetailLayer::NeonGlobe, C + FVector(0.f, 0.f, 3990.f), FVector(60.f, 60.f, 60.f), FRotator::ZeroRotator, MakeNeon(FLinearColor(1.f, 0.03f, 0.03f), 40.f, 2.f, 0.5f).V);
		AddLandmark(TEXT("greenmoor_water_tower"), TEXT("Country-Club-Wasserturm"), EMTADistrict::Greenmoor, C, 3990.f);
	}

	// Tore mit Kameramast dort, wo eine Strasse in Greenmoor hineinfuehrt.
	auto BlockIsGM = [&Ctx](int32 BX, int32 BY) -> int32
	{
		if (BX < 0 || BY < 0 || BX >= Ctx.BlocksX || BY >= Ctx.BlocksY)
		{
			return -1; // ausserhalb
		}
		return Ctx.CityGen->GetBlockDistrict(BX, BY) == EMTADistrict::Greenmoor ? 1 : 0;
	};
	// Segment "innen", wenn alle angrenzenden Bloecke Greenmoor sind (mindestens einer existiert).
	auto SegmentInside = [&BlockIsGM](int32 BA_X, int32 BA_Y, int32 BB_X, int32 BB_Y) -> bool
	{
		const int32 A = BlockIsGM(BA_X, BA_Y);
		const int32 B = BlockIsGM(BB_X, BB_Y);
		return (A != 0 && B != 0) && (A == 1 || B == 1);
	};
	struct FGate { FVector2D Pos; FVector2D Dir; };
	TArray<FGate> Gates;
	const float HalfRoad = Ctx.RoadW * 0.5f;
	for (int32 K = 0; K <= Ctx.BlocksX; ++K)
	{
		// Strasse mit konstantem X (verlaeuft entlang Y), Segment J zwischen Bloecken (K-1, J) und (K, J).
		const float RX = K * Ctx.Period - Ctx.HalfX;
		for (int32 J = 0; J < Ctx.BlocksY; ++J)
		{
			if (!SegmentInside(K - 1, J, K, J))
			{
				continue;
			}
			const float Y0 = J * Ctx.Period - Ctx.HalfY;
			if (J == 0 || !SegmentInside(K - 1, J - 1, K, J - 1))
			{
				Gates.Add({ FVector2D(RX, Y0 + HalfRoad + 1400.f), FVector2D(0.0, 1.0) });
			}
			if (J == Ctx.BlocksY - 1 || !SegmentInside(K - 1, J + 1, K, J + 1))
			{
				Gates.Add({ FVector2D(RX, Y0 + Ctx.Period - HalfRoad - 1400.f), FVector2D(0.0, -1.0) });
			}
		}
	}
	for (int32 K = 0; K <= Ctx.BlocksY; ++K)
	{
		const float RY = K * Ctx.Period - Ctx.HalfY;
		for (int32 I = 0; I < Ctx.BlocksX; ++I)
		{
			if (!SegmentInside(I, K - 1, I, K))
			{
				continue;
			}
			const float X0 = I * Ctx.Period - Ctx.HalfX;
			if (I == 0 || !SegmentInside(I - 1, K - 1, I - 1, K))
			{
				Gates.Add({ FVector2D(X0 + HalfRoad + 1400.f, RY), FVector2D(1.0, 0.0) });
			}
			if (I == Ctx.BlocksX - 1 || !SegmentInside(I + 1, K - 1, I + 1, K))
			{
				Gates.Add({ FVector2D(X0 + Ctx.Period - HalfRoad - 1400.f, RY), FVector2D(-1.0, 0.0) });
			}
		}
	}
	const int32 MaxGates = 6;
	const FDetailData Cream = MakeConcrete(FLinearColor(0.85f, 0.82f, 0.74f), 0.1f, MTAConcretePattern::Plain);
	const FDetailData BoomData = MakePaint(FLinearColor(0.8f, 0.05f, 0.05f), 0.1f, 0.2f, MTAMetalStripes::Boom);
	for (int32 G = 0; G < FMath::Min(MaxGates, Gates.Num()); ++G)
	{
		const FGate& Gate = Gates[G];
		const FVector2D Lat(-Gate.Dir.Y, Gate.Dir.X);
		const float Z0 = Ctx.Curb;
		for (int32 S = -1; S <= 1; S += 2)
		{
			const FVector2D P = Gate.Pos + Lat * (S * (HalfRoad + 150.f));
			AddBox(Ctx, EMTADetailLayer::StructConcrete, CityToWorld(Ctx, static_cast<float>(P.X), static_cast<float>(P.Y), Z0 + 225.f), FVector(100.f, 100.f, 450.f), FRotator::ZeroRotator, Cream.V);
			// Geoeffnete Schranke (60 Grad hoch).
			const FVector Pivot = CityToWorld(Ctx, static_cast<float>(P.X), static_cast<float>(P.Y), Z0 + 120.f);
			const FVector Tip = Pivot + FVector(-Lat.X * S, -Lat.Y * S, 0.0) * (HalfRoad * 0.5) + FVector(0.f, 0.f, HalfRoad * 0.85f);
			AddBeam(Ctx, EMTADetailLayer::FurnitureBox, Pivot, Tip, 15.f, 15.f, BoomData.V);
		}
		const FVector2D House = Gate.Pos + Lat * (HalfRoad + 420.f) - Gate.Dir * 300.0;
		AddBox(Ctx, EMTADetailLayer::StructConcrete, CityToWorld(Ctx, static_cast<float>(House.X), static_cast<float>(House.Y), Z0 + 160.f), FVector(300.f, 300.f, 320.f), YawFromDir(Gate.Dir), Cream.V);
		AddBox(Ctx, EMTADetailLayer::Signage, CityToWorld(Ctx, static_cast<float>(House.X - Gate.Dir.X * 155.0), static_cast<float>(House.Y - Gate.Dir.Y * 155.0), Z0 + 260.f),
			FVector(8.f, 260.f, 60.f), YawFromDir(-Gate.Dir), MakeSign(FLinearColor(0.2f, 0.55f, 0.3f), MTASignKind::ShopSign, static_cast<float>(G), 0.6f).V);
		const FVector2D Cam = Gate.Pos - Lat * (HalfRoad + 250.f) - Gate.Dir * 300.0;
		AddAegisCamera(Ctx, CityToWorld(Ctx, static_cast<float>(Cam.X), static_cast<float>(Cam.Y), Z0), CityToWorld(Ctx, static_cast<float>(Gate.Pos.X - Gate.Dir.X * 2500.0), static_cast<float>(Gate.Pos.Y - Gate.Dir.Y * 2500.0), 0.f),
			EMTADistrict::Greenmoor, FIntPoint(-1, -1), TEXT("gate"), false);
		FMTABillboardSpot Txt;
		Txt.Location = CityToWorld(Ctx, static_cast<float>(House.X - Gate.Dir.X * 162.0), static_cast<float>(House.Y - Gate.Dir.Y * 162.0), Z0 + 260.f);
		Txt.Facing = FVector(-Gate.Dir.X, -Gate.Dir.Y, 0.0);
		Txt.PanelSize = FVector2D(260.f, 60.f);
		Txt.Slogan = TEXT("GREENMOOR - PRIVAT");
		Txt.TextColor = FLinearColor::White;
		Txt.District = EMTADistrict::Greenmoor;
		SignTexts.Add(Txt);
		if (G == 0)
		{
			AddLandmark(TEXT("greenmoor_gate"), TEXT("Tor von Greenmoor"), EMTADistrict::Greenmoor, CityToWorld(Ctx, static_cast<float>(Gate.Pos.X), static_cast<float>(Gate.Pos.Y), Z0), 450.f);
		}
	}
}

void AMTAWorldDetail::BuildRustline(FDetailContext& Ctx)
{
	// Raffinerie suedlich der Stadt (zwischen Rustline und Salzebene).
	const float RCX = -Ctx.HalfX - 16000.f;
	const float RCY = 0.f;
	auto P = [this, &Ctx, RCX, RCY](float DX, float DY, float Z) { return CityToWorld(Ctx, RCX + DX, RCY + DY, Z); };
	AddBox(Ctx, EMTADetailLayer::Quay, P(0.f, 0.f, -12.f), FVector(20000.f, 44000.f, 40.f), FRotator::ZeroRotator,
		MakeConcrete(FLinearColor(0.32f, 0.31f, 0.29f), 0.9f, MTAConcretePattern::Plain).V);
	// Salzebene weiter suedlich.
	AddBox(Ctx, EMTADetailLayer::Quay, CityToWorld(Ctx, -Ctx.HalfX - 62000.f, 25000.f, -16.f), FVector(60000.f, 110000.f, 40.f), FRotator::ZeroRotator,
		MakeConcrete(FLinearColor(0.88f, 0.87f, 0.84f), 0.1f, MTAConcretePattern::Salt).V);
	AddLandmark(TEXT("salt_flats"), TEXT("Salzebene"), EMTADistrict::Count, CityToWorld(Ctx, -Ctx.HalfX - 62000.f, 25000.f, 0.f), 0.f);

	const FDetailData Bands = MakePaint(FLinearColor(0.7f, 0.1f, 0.06f), 0.7f, 0.2f, MTAMetalStripes::Bands);
	const FDetailData Steel = MakePaint(FLinearColor(0.55f, 0.55f, 0.53f), 0.65f, 0.8f, MTAMetalStripes::None);
	const FDetailData White = MakePaint(FLinearColor(0.8f, 0.8f, 0.78f), 0.55f, 0.4f, MTAMetalStripes::None);
	const FDetailData Warn = MakeNeon(FLinearColor(1.f, 0.05f, 0.02f), 30.f, 2.f, 0.4f);

	// Fackel mit Flamme (emissiv, flackernd, auch am Tag sichtbar).
	const FVector FlareFoot = P(-6000.f, -15000.f, 8.f);
	AddColumn(Ctx, EMTADetailLayer::StructMetalCyl, FlareFoot, 180.f, 7500.f, Bands.V);
	AddBox(Ctx, EMTADetailLayer::NeonGlobe, FlareFoot + FVector(0.f, 0.f, 7500.f + 380.f), FVector(320.f, 320.f, 760.f), FRotator::ZeroRotator,
		MakeNeon(FLinearColor(1.f, 0.42f, 0.08f), 60.f, 1.f, 0.9f).V);
	AddBox(Ctx, EMTADetailLayer::NeonGlobe, FlareFoot + FVector(0.f, 0.f, 7500.f + 250.f), FVector(180.f, 180.f, 420.f), FRotator::ZeroRotator,
		MakeNeon(FLinearColor(1.f, 0.85f, 0.3f), 80.f, 1.f, 1.f).V);
	AddLandmark(TEXT("refinery_flare"), TEXT("Raffinerie-Fackel"), EMTADistrict::Rustline, FlareFoot, 8300.f);

	// Schornsteine.
	const float StackH[3] = { 6000.f, 5200.f, 4500.f };
	for (int32 K = 0; K < 3; ++K)
	{
		const FVector Foot = P(2000.f + K * 1800.f, -9000.f + K * 900.f, 8.f);
		AddColumn(Ctx, EMTADetailLayer::StructMetalCyl, Foot, 450.f, StackH[K], Bands.V);
		AddBox(Ctx, EMTADetailLayer::NeonGlobe, Foot + FVector(0.f, 0.f, StackH[K] + 40.f), FVector(80.f, 80.f, 80.f), FRotator::ZeroRotator, Warn.V);
	}
	// Destillationskolonnen mit Buehnenringen und Arbeitslichtern.
	for (int32 K = 0; K < 4; ++K)
	{
		const float D = 500.f + K * 90.f;
		const float H = 3000.f + K * 450.f;
		const FVector Foot = P(-2500.f + K * 1400.f, -2000.f, 8.f);
		AddColumn(Ctx, EMTADetailLayer::StructMetalCyl, Foot, D, H, Steel.V);
		for (float Z = 900.f; Z < H; Z += 900.f)
		{
			AddColumn(Ctx, EMTADetailLayer::StructMetalCyl, Foot + FVector(0.f, 0.f, Z), D + 140.f, 15.f, Steel.V);
			AddBox(Ctx, EMTADetailLayer::NeonSmall, Foot + FVector(D * 0.5f + 75.f, 0.f, Z + 40.f), FVector(20.f, 20.f, 20.f), FRotator::ZeroRotator,
				MakeNeon(FLinearColor(1.f, 0.75f, 0.4f), 12.f, 0.f, 0.f).V);
		}
	}
	// Tanklager.
	for (int32 K = 0; K < 6; ++K)
	{
		const FVector Foot = P(-4500.f + (K % 3) * 4500.f, 6000.f + (K / 3) * 5000.f, 8.f);
		const float D = 2200.f + (K % 2) * 600.f;
		AddColumn(Ctx, EMTADetailLayer::StructMetalCyl, Foot, D, 1100.f + (K % 3) * 200.f, White.V);
	}
	// Rohrbruecken.
	const FLinearColor PipeCols[3] = { FLinearColor(0.75f, 0.6f, 0.1f), FLinearColor(0.45f, 0.45f, 0.45f), FLinearColor(0.6f, 0.15f, 0.08f) };
	for (float Y = -18000.f; Y <= 18000.f; Y += 1500.f)
	{
		AddBox(Ctx, EMTADetailLayer::StructMetal, P(4500.f, Y, 358.f), FVector(60.f, 60.f, 700.f), FRotator::ZeroRotator, Steel.V);
		AddBox(Ctx, EMTADetailLayer::StructMetal, P(4500.f, Y, 700.f), FVector(500.f, 60.f, 40.f), FRotator::ZeroRotator, Steel.V);
	}
	for (int32 K = 0; K < 3; ++K)
	{
		AddRod(Ctx, EMTADetailLayer::StructMetalCyl, P(4350.f + K * 150.f, -18000.f, 770.f), P(4350.f + K * 150.f, 18000.f, 770.f), 70.f,
			MakePaint(PipeCols[K], 0.6f, 0.6f, MTAMetalStripes::None).V);
	}
	AddLandmark(TEXT("refinery"), TEXT("Raffinerie Kestrel"), EMTADistrict::Rustline, P(0.f, 0.f, 0.f), 6000.f);

	// Schrotthaufen: grosser Schrottberg an der Raffinerie und auf Rustline-Brachen.
	AddScrapHeap(Ctx, P(7000.f, 14000.f, 8.f), 2000.f, 60);
	AddScrapHeap(Ctx, P(7500.f, -14500.f, 8.f), 1600.f, 45);
	AddLandmark(TEXT("scrap_mountain"), TEXT("Schrottberg"), EMTADistrict::Rustline, P(7000.f, 14000.f, 0.f), 900.f);
	int32 Heaps = 0;
	for (const FMTACityLot& L : Ctx.CityGen->GetCityLots())
	{
		if (Heaps >= 8 || L.District != EMTADistrict::Rustline || !L.bEmpty)
		{
			continue;
		}
		const float Rad = static_cast<float>(FMath::Min(L.Size.X, L.Size.Y)) * 0.4f;
		AddScrapHeap(Ctx, FVector(L.Center.X, L.Center.Y, L.BaseZ), Rad, 25);
		++Heaps;
	}
}

// ---------------------------------------------------------------------------- Strassenmoeblierung

void AMTAWorldDetail::BuildIntersections(FDetailContext& Ctx)
{
	const float HR = Ctx.RoadW * 0.5f;
	const FDetailData PoleData = MakePaint(FLinearColor(0.16f, 0.17f, 0.18f), 0.3f, 0.7f, MTAMetalStripes::None);
	const FDetailData HousingData = MakePaint(FLinearColor(0.08f, 0.08f, 0.08f), 0.3f, 0.4f, MTAMetalStripes::None);
	auto HasBlock = [&Ctx](int32 BX, int32 BY) { return BX >= 0 && BY >= 0 && BX < Ctx.BlocksX && BY < Ctx.BlocksY; };

	for (int32 IX = 0; IX <= Ctx.BlocksX; ++IX)
	{
		for (int32 IY = 0; IY <= Ctx.BlocksY; ++IY)
		{
			FRandomStream R(HashSeed(Seed, IX * 31 + 7, IY * 977 + 3));
			const float CX = IX * Ctx.Period - Ctx.HalfX;
			const float CY = IY * Ctx.Period - Ctx.HalfY;
			const EMTADistrict D = IntersectionDistrict(Ctx, IX, IY);
			const FMTADistrictProfile& Prof = ProfileOf(Ctx, D);
			const float Z0 = Ctx.Curb;
			// Quadrant (SX,SY) hat Gehweg, wenn dort ein Block liegt.
			auto Corner = [&](int32 SX, int32 SY) { return HasBlock(IX + (SX > 0 ? 0 : -1), IY + (SY > 0 ? 0 : -1)); };

			// Ampeln an zwei diagonalen Ecken bzw. Stoppschilder.
			const bool bLights = R.FRand() < Prof.TrafficLightShare;
			const int32 Phase = (IX + IY) % 3;
			for (int32 S = -1; S <= 1; S += 2)
			{
				if (!Corner(S, S))
				{
					continue;
				}
				const FVector Foot = CityToWorld(Ctx, CX + S * (HR + 90.f), CY + S * (HR + 420.f), Z0);
				if (bLights)
				{
					AddColumn(Ctx, EMTADetailLayer::Poles, Foot, 18.f, 560.f, PoleData.V);
					const FVector ArmA = Foot + FVector(0.f, 0.f, 540.f);
					const FVector ArmB = ArmA - FVector(S * 450.f, 0.f, 0.f);
					AddBeam(Ctx, EMTADetailLayer::FurnitureBox, ArmA, ArmB, 12.f, 12.f, PoleData.V);
					const FVector Housing = ArmB + FVector(S * 20.f, 0.f, -60.f);
					AddBox(Ctx, EMTADetailLayer::FurnitureBox, Housing, FVector(35.f, 35.f, 100.f), FRotator::ZeroRotator, HousingData.V);
					const FLinearColor Lamp[3] = { FLinearColor(1.f, 0.05f, 0.03f), FLinearColor(1.f, 0.6f, 0.05f), FLinearColor(0.1f, 1.f, 0.3f) };
					for (int32 Li = 0; Li < 3; ++Li)
					{
						const float Inten = Li == Phase ? 8.f : 0.4f;
						AddBox(Ctx, EMTADetailLayer::NeonSmall, Housing + FVector(0.f, S * 19.f, 30.f - Li * 30.f), FVector(16.f, 4.f, 16.f), FRotator::ZeroRotator,
							MakeNeon(Lamp[Li], Inten, 0.f, 0.5f).V);
					}
					++Stats.TrafficLights;
				}
				else
				{
					AddColumn(Ctx, EMTADetailLayer::Poles, Foot, 8.f, 260.f, PoleData.V);
					AddBox(Ctx, EMTADetailLayer::Signage, Foot + FVector(0.f, S * 6.f, 240.f), FVector(60.f, 4.f, 60.f), FRotator(0.f, 90.f, 0.f),
						MakeSign(FLinearColor(0.75f, 0.05f, 0.05f), MTASignKind::Stop, 0.f, 0.f).V);
					++Stats.StopSigns;
				}
			}
			// Strassennamensschild (NO-Ecke).
			if (Corner(1, -1))
			{
				const FVector Foot = CityToWorld(Ctx, CX + HR + 250.f, CY - HR - 250.f, Z0);
				AddColumn(Ctx, EMTADetailLayer::Poles, Foot, 8.f, 320.f, PoleData.V);
				const FDetailData NameSign = MakeSign(FLinearColor(0.05f, 0.35f, 0.15f), MTASignKind::StreetName, static_cast<float>(IX * 10 + IY), 0.f);
				AddBox(Ctx, EMTADetailLayer::Signage, Foot + FVector(0.f, 0.f, 305.f), FVector(4.f, 90.f, 22.f), FRotator::ZeroRotator, NameSign.V);
				AddBox(Ctx, EMTADetailLayer::Signage, Foot + FVector(0.f, 0.f, 280.f), FVector(4.f, 90.f, 22.f), FRotator(0.f, 90.f, 0.f), NameSign.V);
				++Stats.StreetSigns;
			}
			// Zebrastreifen-Schilder an beiden Enden des +X-Uebergangs.
			if (Corner(1, 1) && Corner(1, -1) && R.FRand() < FMath::Min(1.f, Prof.StreetFurnitureDensity * 1.3f))
			{
				for (int32 S = -1; S <= 1; S += 2)
				{
					const FVector Foot = CityToWorld(Ctx, CX + HR + 150.f, CY + S * (HR + 60.f), Z0);
					AddColumn(Ctx, EMTADetailLayer::Poles, Foot, 8.f, 280.f, PoleData.V);
					AddBox(Ctx, EMTADetailLayer::Signage, Foot + FVector(0.f, 0.f, 255.f), FVector(4.f, 60.f, 60.f), FRotator::ZeroRotator,
						MakeSign(FLinearColor(0.05f, 0.25f, 0.8f), MTASignKind::Crosswalk, 0.f, 0.2f).V);
					++Stats.CrosswalkSigns;
				}
			}
			// Aegis-Kameramast (Dichte je Bezirk); Core zusaetzlich an einer zweiten Ecke.
			const FIntPoint Crossing(IX, IY);
			const FVector LookAt = CityToWorld(Ctx, CX, CY, 0.f);
			if (Corner(-1, 1) && R.FRand() < Prof.AegisCameraDensity)
			{
				const bool bHacked = D == EMTADistrict::LanternHill && R.FRand() < 0.6f;
				AddAegisCamera(Ctx, CityToWorld(Ctx, CX - HR - 300.f, CY + HR + 600.f, Z0), LookAt, D, Crossing, TEXT("crossing"), bHacked);
			}
			if (D == EMTADistrict::Core && Corner(1, 1) && R.FRand() < 0.5f)
			{
				AddAegisCamera(Ctx, CityToWorld(Ctx, CX + HR + 600.f, CY + HR + 300.f, Z0), LookAt, D, Crossing, TEXT("crossing"), false);
			}
		}
	}
}

void AMTAWorldDetail::BuildSidewalkFurniture(FDetailContext& Ctx)
{
	const float Curb = (Ctx.Period - Ctx.RoadW) * 0.5f; // Bordsteinlinie ab Blockmitte
	const FDetailData Hydrant = MakePaint(FLinearColor(0.7f, 0.08f, 0.05f), 0.4f, 0.4f, MTAMetalStripes::None);
	const FDetailData BollardData = MakePaint(FLinearColor(0.15f, 0.15f, 0.16f), 0.3f, 0.7f, MTAMetalStripes::None);
	for (int32 BY = 0; BY < Ctx.BlocksY; ++BY)
	{
		for (int32 BX = 0; BX < Ctx.BlocksX; ++BX)
		{
			const EMTADistrict D = Ctx.CityGen->GetBlockDistrict(BX, BY);
			const FMTADistrictProfile& Prof = ProfileOf(Ctx, D);
			const FVector2D BC(BX * Ctx.Period - Ctx.HalfX + Ctx.Period * 0.5f, BY * Ctx.Period - Ctx.HalfY + Ctx.Period * 0.5f);
			FLinearColor BinCol(0.12f, 0.14f, 0.15f);
			switch (D)
			{
			case EMTADistrict::Sunstrip:    BinCol = FLinearColor(0.1f, 0.6f, 0.6f); break;
			case EMTADistrict::Rustline:    BinCol = FLinearColor(0.2f, 0.25f, 0.12f); break;
			case EMTADistrict::LanternHill: BinCol = FLinearColor(0.08f, 0.22f, 0.12f); break;
			case EMTADistrict::Greenmoor:   BinCol = FLinearColor(0.15f, 0.35f, 0.18f); break;
			case EMTADistrict::Saltmarket:  BinCol = FLinearColor(0.55f, 0.25f, 0.06f); break;
			default: break;
			}
			const FDetailData Bin = MakePaint(BinCol, Prof.Wear, 0.3f, MTAMetalStripes::None);
			const FDetailData Wood = MakePaint(FLinearColor(0.4f, 0.28f, 0.16f), Prof.Wear, 0.f, MTAMetalStripes::None);
			for (int32 Side = 0; Side < 4; ++Side)
			{
				FRandomStream R(HashSeed(Seed + 17, BX * 4 + Side, BY));
				const FVector2D N = Side == 0 ? FVector2D(1.0, 0.0) : Side == 1 ? FVector2D(-1.0, 0.0) : Side == 2 ? FVector2D(0.0, 1.0) : FVector2D(0.0, -1.0);
				const FVector2D T(-N.Y, N.X);
				auto At = [&](float Along, float FromCurb, float Z) { const FVector2D P = BC + N * (Curb - FromCurb) + T * Along; return CityToWorld(Ctx, static_cast<float>(P.X), static_cast<float>(P.Y), Ctx.Curb + Z); };

				if (R.FRand() < 0.8f)
				{
					AddColumn(Ctx, EMTADetailLayer::FurnitureCyl, At(-2200.f, 70.f, 0.f), 30.f, 75.f, Hydrant.V);
					AddColumn(Ctx, EMTADetailLayer::FurnitureCyl, At(-2200.f, 70.f, 75.f), 22.f, 12.f, Hydrant.V);
					++Stats.Hydrants;
				}
				const int32 Bins = FMath::RoundToInt(Prof.StreetFurnitureDensity * 2.f + R.FRandRange(-0.4f, 0.4f));
				for (int32 B = 0; B < FMath::Clamp(Bins, 0, 2); ++B)
				{
					AddColumn(Ctx, EMTADetailLayer::FurnitureCyl, At(B == 0 ? 1000.f : -1000.f, 80.f, 0.f), 55.f, 95.f, Bin.V);
					++Stats.TrashBins;
				}
				if (R.FRand() < Prof.StreetFurnitureDensity * 0.8f)
				{
					const FRotator Yaw = YawFromDir(T);
					AddBox(Ctx, EMTADetailLayer::FurnitureBox, At(2000.f, 260.f, 45.f), FVector(160.f, 45.f, 8.f), Yaw, Wood.V);
					AddBox(Ctx, EMTADetailLayer::FurnitureBox, At(2000.f, 285.f, 72.f), FVector(160.f, 6.f, 40.f), Yaw, Wood.V);
					AddBox(Ctx, EMTADetailLayer::FurnitureBox, At(1940.f, 260.f, 22.f), FVector(8.f, 40.f, 45.f), Yaw, BollardData.V);
					AddBox(Ctx, EMTADetailLayer::FurnitureBox, At(2060.f, 260.f, 22.f), FVector(8.f, 40.f, 45.f), Yaw, BollardData.V);
					++Stats.Benches;
				}
				if (Prof.StreetFurnitureDensity >= 0.6f)
				{
					for (int32 End = -1; End <= 1; End += 2)
					{
						for (int32 K = 0; K < 3; ++K)
						{
							AddColumn(Ctx, EMTADetailLayer::FurnitureCyl, At(End * (2600.f + K * 150.f), 40.f, 0.f), 18.f, 90.f, BollardData.V);
							++Stats.Bollards;
						}
					}
				}
			}
		}
	}
}

void AMTAWorldDetail::BuildFacadeDetails(FDetailContext& Ctx)
{
	for (const FMTACityLot& L : Ctx.CityGen->GetCityLots())
	{
		if (L.bEmpty || !L.bHasFrontage || L.Height < 600.f)
		{
			continue;
		}
		const FMTADistrictProfile& Prof = ProfileOf(Ctx, L.District);
		FRandomStream R(L.Seed ^ 0x5151);
		const FVector2D Dir = L.StreetDir;
		const FVector2D Perp(-Dir.Y, Dir.X);
		const float HalfDepth = static_cast<float>(FMath::Abs(Dir.X) * L.Size.X + FMath::Abs(Dir.Y) * L.Size.Y) * 0.5f;
		const float FaceW = static_cast<float>(FMath::Abs(Dir.Y) * L.Size.X + FMath::Abs(Dir.X) * L.Size.Y);
		const FVector2D Facade = L.Center + Dir * HalfDepth;
		const FRotator Yaw = YawFromDir(Dir);

		// Neon-Leuchtschilder (Ausleger) im 1. Obergeschoss.
		if (R.FRand() < Prof.NeonShare)
		{
			const int32 Signs = R.RandRange(1, 2);
			for (int32 K = 0; K < Signs; ++K)
			{
				const FVector2D P = Facade + Dir * 45.0 + Perp * (R.FRandRange(-0.35f, 0.35f) * FaceW);
				const FLinearColor Col = R.FRand() < 0.5f ? Prof.AccentColor : NeonPalette[R.RandRange(0, NumNeonColors - 1)];
				const float Flicker = (L.District == EMTADistrict::Rustline || L.District == EMTADistrict::Saltmarket) ? 0.6f : (R.FRand() < 0.15f ? 0.4f : 0.f);
				AddBox(Ctx, EMTADetailLayer::NeonSmall, FVector(P.X, P.Y, L.BaseZ + R.FRandRange(450.f, 600.f)), FVector(70.f, 25.f, 280.f), Yaw,
					MakeNeon(Col, R.FRandRange(4.f, 8.f), Flicker, 0.25f).V);
				++Stats.NeonSigns;
			}
		}
		// Plakate (Core/Sunstrip) bzw. Graffiti (Rustline, Lantern Hill, Saltmarket).
		if (R.FRand() < Prof.GraffitiShare)
		{
			const int32 Pieces = R.RandRange(1, 3);
			for (int32 K = 0; K < Pieces; ++K)
			{
				const float W = R.FRandRange(300.f, 700.f);
				const float H = R.FRandRange(180.f, 300.f);
				if (FaceW < W + 200.f)
				{
					continue;
				}
				const bool bPoster = L.District == EMTADistrict::Core || L.District == EMTADistrict::Sunstrip || R.FRand() < 0.25f;
				const FVector2D P = Facade + Dir * 3.0 + Perp * (R.FRandRange(-0.5f, 0.5f) * (FaceW - W));
				const FLinearColor Col = NeonPalette[R.RandRange(0, NumNeonColors - 1)];
				AddBox(Ctx, EMTADetailLayer::Signage, FVector(P.X, P.Y, L.BaseZ + 40.f + H * 0.5f), FVector(4.f, W, H), Yaw,
					MakeSign(Col, bPoster ? MTASignKind::Poster : MTASignKind::Graffiti, static_cast<float>(R.RandRange(0, 9999)), 0.f).V);
				++Stats.PostersAndGraffiti;
			}
		}
		// Werbetafel auf niedrigen Flachdaechern.
		if (L.bFlatRoof && !L.bTower && L.Height <= 2600.f && FaceW >= 1400.f && R.FRand() < Prof.BillboardShare)
		{
			const float W = FMath::Min(1200.f, FaceW * 0.7f);
			const float H = W * 0.42f;
			const FVector2D P = Facade - Dir * 300.0;
			const float RoofZ = L.BaseZ + L.Height;
			AddBillboard(Ctx, FVector(P.X, P.Y, RoofZ + 160.f + H * 0.5f), FVector(Dir.X, Dir.Y, 0.0), W, H, L.District, true, RoofZ);
		}
	}
}

// ---------------------------------------------------------------------------- Begehbare Gebaeude

void AMTAWorldDetail::PlanEnterableSites(FDetailContext& Ctx)
{
	struct FCandidate { int32 Lot; int32 Part; uint32 Order; };
	const TArray<FMTACityLot>& Lots = Ctx.CityGen->GetCityLots();
	const int32 LandmarkBlock = Ctx.CityGen->LandmarkBlockIndex;
	TArray<FCandidate> Buckets[2][MTADistrict::Num()];
	for (int32 I = 0; I < Lots.Num(); ++I)
	{
		const FMTACityLot& L = Lots[I];
		if (L.bEmpty || !L.bHasFrontage || L.Height < 600.f || (L.Block.X + L.Block.Y * Ctx.BlocksX) == LandmarkBlock)
		{
			continue;
		}
		const int32 D = static_cast<int32>(L.District);
		if (D < 0 || D >= MTADistrict::Num())
		{
			continue;
		}
		const float FaceW = static_cast<float>(FMath::Abs(L.StreetDir.Y) * L.Size.X + FMath::Abs(L.StreetDir.X) * L.Size.Y);
		const uint32 Order = static_cast<uint32>(HashSeed(L.Seed, Seed, 77));
		if (FaceW >= 3200.f)
		{
			// Breite Fassade: zwei Ladeneinheiten (zweite erst im zweiten Durchgang).
			Buckets[0][D].Add({ I, 1, Order });
			Buckets[1][D].Add({ I, 2, Order });
		}
		else
		{
			Buckets[0][D].Add({ I, 0, Order });
		}
	}
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (int32 D = 0; D < MTADistrict::Num(); ++D)
		{
			Buckets[Pass][D].Sort([](const FCandidate& A, const FCandidate& B) { return A.Order < B.Order; });
		}
	}

	int32 TypeCounter[MTADistrict::Num()] = {};
	TSet<FString> UsedUnique;
	TArray<FName> Types;
	for (int32 Pass = 0; Pass < 2 && EnterableSites.Num() < PlannedEnterableSites; ++Pass)
	{
		int32 Cursor[MTADistrict::Num()] = {};
		bool bAny = true;
		while (bAny && EnterableSites.Num() < PlannedEnterableSites)
		{
			bAny = false;
			// Reihum ueber alle Bezirke: gleichmaessige Verteilung.
			for (int32 D = 0; D < MTADistrict::Num() && EnterableSites.Num() < PlannedEnterableSites; ++D)
			{
				TArray<FCandidate>& Bucket = Buckets[Pass][D];
				if (Cursor[D] >= Bucket.Num())
				{
					continue;
				}
				bAny = true;
				const FCandidate Cand = Bucket[Cursor[D]++];
				const FMTACityLot& L = Lots[Cand.Lot];
				const FVector2D Dir = L.StreetDir;
				const FVector2D Perp(-Dir.Y, Dir.X);
				const double FaceW = FMath::Abs(Dir.Y) * L.Size.X + FMath::Abs(Dir.X) * L.Size.Y;
				FVector2D Center = L.Center;
				FVector2D Size = L.Size;
				if (Cand.Part > 0)
				{
					const double Sign = Cand.Part == 1 ? -1.0 : 1.0;
					Center += Perp * (Sign * FaceW * 0.25);
					// Halbe Breite entlang der Fassade.
					if (FMath::Abs(Perp.X) > 0.5) { Size.X *= 0.5; } else { Size.Y *= 0.5; }
				}
				RoomTypesFor(L.District, L.bIndustrial, Types);
				FName RoomType = NAME_None;
				for (int32 Try = 0; Try < Types.Num(); ++Try)
				{
					const FName Candidate = Types[(TypeCounter[D] + Try) % Types.Num()];
					const FString Key = FString::Printf(TEXT("%d_%s"), D, *Candidate.ToString());
					if (IsUniqueRoomType(Candidate) && UsedUnique.Contains(Key))
					{
						continue;
					}
					if (IsUniqueRoomType(Candidate))
					{
						UsedUnique.Add(Key);
					}
					RoomType = Candidate;
					TypeCounter[D] += Try + 1;
					break;
				}
				if (RoomType.IsNone())
				{
					RoomType = Types.Num() > 0 ? Types[0] : FName(TEXT("shop_kiosk"));
				}
				FMTAEnterableSite Site;
				Site.LotCenter = FVector(Center.X, Center.Y, L.BaseZ + 15.f);
				Site.LotSize = Size;
				Site.Height = L.Height;
				Site.StreetDir = FVector(Dir.X, Dir.Y, 0.0);
				Site.District = L.District;
				Site.RoomType = RoomType;
				Site.Seed = L.Seed + Cand.Part * 7919;
				RoomTypeSign(RoomType, Site.SignLabel, Site.SignColor);
				EnterableSites.Add(Site);
			}
		}
	}
}

int32 AMTAWorldDetail::SpawnEnterableBuildings(int32 MaxCount)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		UE_LOG(LogMTAWorldDetail, Warning, TEXT("%s: SpawnEnterableBuildings nur in der Spielwelt."), *GetName());
		return 0;
	}
	if (!IsBuilt() || EnterableSites.Num() == 0)
	{
		Generate();
	}
	DestroySpawnedEntries();

	UHierarchicalInstancedStaticMeshComponent* Doors = DetailLayers.IsValidIndex(static_cast<int32>(EMTADetailLayer::DoorSigns))
		? DetailLayers[static_cast<int32>(EMTADetailLayer::DoorSigns)].Get() : nullptr;
	TArray<FTransform> DoorXf;
	TArray<float> DoorData;
	auto PushDoor = [&DoorXf, &DoorData](const FVector& Center, const FVector& Size, const FRotator& Rot, const FDetailData& Data)
	{
		DoorXf.Add(FTransform(Rot, Center, Size / 100.0));
		DoorData.Append(Data.V, MTADetailData::Num);
	};

	const int32 Count = FMath::Min(FMath::Max(0, MaxCount), EnterableSites.Num());
	for (int32 I = 0; I < Count; ++I)
	{
		const FMTAEnterableSite& Site = EnterableSites[I];
		AMTABuildingEntry* Entry = AMTABuildingEntry::SpawnForLot(World, Site.LotCenter, Site.LotSize, Site.Height, Site.StreetDir, Site.District, Site.Seed, Site.RoomType);
		if (!Entry)
		{
			continue;
		}
		SpawnedEntries.Add(Entry);

		// Tuermarkierung an der Fassade: Tuer, Schild mit Typ, Lichtleiste.
		const FVector Dir = Site.StreetDir.GetSafeNormal2D();
		const float HalfDepth = static_cast<float>(FMath::Abs(Dir.X) * Site.LotSize.X + FMath::Abs(Dir.Y) * Site.LotSize.Y) * 0.5f;
		const FVector Facade = FVector(Site.LotCenter.X, Site.LotCenter.Y, Site.LotCenter.Z - 15.f) + Dir * HalfDepth;
		const FRotator Rot = Dir.Rotation();
		PushDoor(Facade + Dir * 4.f + FVector(0.f, 0.f, 125.f), FVector(8.f, 180.f, 250.f), Rot, MakeSign(FLinearColor(0.1f, 0.1f, 0.12f), MTASignKind::Door, static_cast<float>(I), 0.5f));
		PushDoor(Facade + Dir * 6.f + FVector(0.f, 0.f, 310.f), FVector(10.f, 300.f, 70.f), Rot, MakeSign(Site.SignColor, MTASignKind::ShopSign, static_cast<float>(I), 1.f));
		PushDoor(Facade + Dir * 8.f + FVector(0.f, 0.f, 357.f), FVector(6.f, 260.f, 8.f), Rot, MakeSign(Site.SignColor, MTASignKind::ShopSign, 0.f, 3.f));
		const float Luma = Site.SignColor.R * 0.3f + Site.SignColor.G * 0.59f + Site.SignColor.B * 0.11f;
		UTextRenderComponent* Text = AddRuntimeText(Site.SignLabel, Facade + Dir * 12.f + FVector(0.f, 0.f, 310.f), Rot, 42.f,
			Luma > 0.6f ? FColor(20, 20, 20) : FColor::White);
		if (Text)
		{
			Text->SetCullDistance(FMath::Max(1500.f, TextCullDistance * 0.5f));
			RuntimeTexts.Remove(Text);
			DoorTexts.Add(Text);
		}
	}
	if (Doors && DoorXf.Num() > 0)
	{
		Doors->AddInstances(DoorXf, false, true);
		Doors->UInstancedStaticMeshComponent::SetCustomData(0, DoorXf.Num() - 1, TConstArrayView<float>(DoorData), false);
		Doors->MarkRenderStateDirty();
	}
	UE_LOG(LogMTAWorldDetail, Display, TEXT("MTA6: %d begehbare Gebaeude erzeugt (%d geplant)."), SpawnedEntries.Num(), EnterableSites.Num());
	return SpawnedEntries.Num();
}

void AMTAWorldDetail::DestroySpawnedEntries()
{
	for (AMTABuildingEntry* Entry : SpawnedEntries)
	{
		if (IsValid(Entry))
		{
			Entry->Destroy();
		}
	}
	SpawnedEntries.Reset();
	for (UTextRenderComponent* T : DoorTexts)
	{
		if (IsValid(T))
		{
			T->DestroyComponent();
		}
	}
	DoorTexts.Reset();
	if (DetailLayers.IsValidIndex(static_cast<int32>(EMTADetailLayer::DoorSigns)) && DetailLayers[static_cast<int32>(EMTADetailLayer::DoorSigns)])
	{
		DetailLayers[static_cast<int32>(EMTADetailLayer::DoorSigns)]->ClearInstances();
	}
}

// ============================================================================ Laufzeit

void AMTAWorldDetail::SetupRuntime()
{
	ClearRuntimeTexts();
	if (WheelPivot && !WheelHub.IsNearlyZero())
	{
		WheelPivot->SetWorldLocation(WheelHub);
	}
	for (const FMTABillboardSpot& Spot : Billboards)
	{
		AddRuntimeText(Spot.Slogan, Spot.Location, Spot.Facing.Rotation(), FMath::Clamp(Spot.PanelSize.Y * 0.16f, 30.f, 140.f), Spot.TextColor.ToFColor(true));
	}
	for (const FMTABillboardSpot& Spot : SignTexts)
	{
		AddRuntimeText(Spot.Slogan, Spot.Location, Spot.Facing.Rotation(), FMath::Clamp(Spot.PanelSize.Y * 0.4f, 20.f, 90.f), Spot.TextColor.ToFColor(true));
	}
	const bool bWheel = bAnimateWheel && GetLayerInstanceCount(EMTADetailLayer::WheelFrame) > 0;
	SetActorTickEnabled(bWheel);
}

void AMTAWorldDetail::ClearRuntimeTexts()
{
	for (UTextRenderComponent* T : RuntimeTexts)
	{
		if (IsValid(T))
		{
			T->DestroyComponent();
		}
	}
	RuntimeTexts.Reset();
}

UTextRenderComponent* AMTAWorldDetail::AddRuntimeText(const FString& Text, const FVector& Location, const FRotator& Rotation, float WorldSize, const FColor& Color)
{
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(this, NAME_None, RF_Transient);
	if (!T)
	{
		return nullptr;
	}
	T->SetMobility(EComponentMobility::Movable);
	T->SetupAttachment(SceneRoot);
	T->SetUsingAbsoluteLocation(true);
	T->SetUsingAbsoluteRotation(true);
	T->RegisterComponent();
	// Textflaeche zeigt nach +X der Komponente.
	T->SetWorldLocationAndRotation(Location, Rotation);
	T->SetText(FText::FromString(Text));
	T->SetWorldSize(WorldSize);
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetVerticalAlignment(EVRTA_TextCenter);
	T->SetTextRenderColor(Color);
	T->SetCastShadow(false);
	T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Nur nahe sichtbar (jede Textkomponente ist ein eigener Draw).
	T->SetCullDistance(TextCullDistance);
	RuntimeTexts.Add(T);
	return T;
}

// ============================================================================ Abfragen

AMTAWorldDetail* AMTAWorldDetail::FindWorldDetail(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<AMTAWorldDetail> It(World);
	return It ? *It : nullptr;
}

AMTAWorldDetail* AMTAWorldDetail::FindOrSpawnWorldDetail(UObject* WorldContextObject)
{
	if (AMTAWorldDetail* Found = FindWorldDetail(WorldContextObject))
	{
		return Found;
	}
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || !World->IsGameWorld())
	{
		return nullptr;
	}
	AMTACityGenerator* Gen = AMTACityGenerator::FindCityGenerator(WorldContextObject);
	const FVector Loc = Gen ? Gen->GetActorLocation() : FVector::ZeroVector;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMTAWorldDetail* Spawned = World->SpawnActor<AMTAWorldDetail>(AMTAWorldDetail::StaticClass(), Loc, FRotator::ZeroRotator, Params);
	if (Spawned)
	{
		Spawned->City = Gen;
		if (!Spawned->IsBuilt())
		{
			Spawned->Generate();
		}
	}
	return Spawned;
}

TArray<FMTAAegisCameraPoint> AMTAWorldDetail::GetAegisCamerasNear(const FVector& Location, float Radius, bool bIncludeHacked) const
{
	TArray<FMTAAegisCameraPoint> Out;
	const double R2 = static_cast<double>(Radius) * Radius;
	for (const FMTAAegisCameraPoint& P : AegisCameras)
	{
		if ((bIncludeHacked || !P.bHacked) && FVector::DistSquared(P.Location, Location) <= R2)
		{
			Out.Add(P);
		}
	}
	return Out;
}

int32 AMTAWorldDetail::GetLandmarkCount(EMTADistrict District) const
{
	int32 N = 0;
	for (const FMTALandmarkInfo& L : Landmarks)
	{
		N += L.District == District ? 1 : 0;
	}
	return N;
}

int32 AMTAWorldDetail::GetSpawnedEntryCount() const
{
	int32 N = 0;
	for (const AMTABuildingEntry* Entry : SpawnedEntries)
	{
		N += IsValid(Entry) ? 1 : 0;
	}
	return N;
}

int32 AMTAWorldDetail::GetLayerInstanceCount(EMTADetailLayer Layer) const
{
	const int32 L = static_cast<int32>(Layer);
	return DetailLayers.IsValidIndex(L) && DetailLayers[L] ? DetailLayers[L]->GetInstanceCount() : 0;
}

int32 AMTAWorldDetail::GetTotalInstanceCount() const
{
	int32 N = 0;
	for (const UHierarchicalInstancedStaticMeshComponent* Comp : DetailLayers)
	{
		N += Comp ? Comp->GetInstanceCount() : 0;
	}
	return N;
}

float AMTAWorldDetail::GetWaterLevel() const
{
	const AMTACityGenerator* Gen = ResolveCity();
	return (Gen ? static_cast<float>(Gen->GetActorLocation().Z) : 0.f) + WaterLevel;
}

// ============================================================================ Selbsttest

FString AMTAWorldDetail::RunWorldSelfTest() const
{
	TArray<FString> Lines;
	int32 Failures = 0;
	auto Check = [&Lines, &Failures](const FString& Name, bool bOk, const FString& Detail)
	{
		Lines.Add(FString::Printf(TEXT("WORLDTEST %s: %s (%s)"), *Name, bOk ? TEXT("OK") : TEXT("FEHLER"), *Detail));
		if (!bOk)
		{
			++Failures;
		}
	};
	auto Num = [](int32 V) { return FString::FromInt(V); };

	// Kueste und Meer.
	const int32 Water = GetLayerInstanceCount(EMTADetailLayer::Water);
	Check(TEXT("Wasserflaeche"), Water >= 100, Num(Water) + TEXT(" Kacheln"));
	Check(TEXT("Meeresgrund"), GetLayerInstanceCount(EMTADetailLayer::Seabed) > 0, Num(GetLayerInstanceCount(EMTADetailLayer::Seabed)));
	Check(TEXT("Kaimauern/Molen/Promenade"), Stats.QuayPieces >= 20, Num(Stats.QuayPieces));
	Check(TEXT("Kuestenstrasse + Route 9"), GetLayerInstanceCount(EMTADetailLayer::Roads) >= 20, Num(GetLayerInstanceCount(EMTADetailLayer::Roads)) + TEXT(" Segmente"));
	Check(TEXT("Wasserspiegel unter Stadtboden"), WaterLevel < 0.f && SeabedLevel < WaterLevel, FString::Printf(TEXT("%.0f / %.0f cm"), WaterLevel, SeabedLevel));

	// Berge.
	Check(TEXT("Berge (>= 30 Gipfel)"), Stats.MountainPeaks >= 30, Num(Stats.MountainPeaks));
	Check(TEXT("Huegel/Ruecken/Klippen"), GetLayerInstanceCount(EMTADetailLayer::Hills) >= 10, Num(GetLayerInstanceCount(EMTADetailLayer::Hills)));

	// Landmarken je Bezirk.
	const int32 MinPerDistrict[] = { 2, 2, 2, 2, 2, 2 };
	for (int32 D = 0; D < MTADistrict::Num(); ++D)
	{
		const int32 N = GetLandmarkCount(static_cast<EMTADistrict>(D));
		Check(FString::Printf(TEXT("Landmarken %s"), MTADistrict::ShortName(static_cast<EMTADistrict>(D))), N >= MinPerDistrict[D], Num(N));
	}
	auto HasLandmark = [this](const TCHAR* Id) { return Landmarks.ContainsByPredicate([Id](const FMTALandmarkInfo& L) { return L.Id == FName(Id); }); };
	Check(TEXT("Kranreihe"), HasLandmark(TEXT("crane_row")), TEXT("Saltmarket"));
	Check(TEXT("Kestrel Wheel"), HasLandmark(TEXT("kestrel_wheel")) && GetLayerInstanceCount(EMTADetailLayer::WheelFrame) > 50, Num(GetLayerInstanceCount(EMTADetailLayer::WheelFrame)) + TEXT(" Teile"));
	Check(TEXT("Glockenturm St. Merrin"), HasLandmark(TEXT("st_merrin")), TEXT("Lantern Hill"));
	Check(TEXT("Wasserturm Greenmoor"), HasLandmark(TEXT("greenmoor_water_tower")), TEXT("Greenmoor"));
	Check(TEXT("Raffinerie-Fackel"), HasLandmark(TEXT("refinery_flare")), TEXT("Rustline"));

	// Moeblierung.
	Check(TEXT("Ampeln/Stoppschilder"), Stats.TrafficLights + Stats.StopSigns >= 60, FString::Printf(TEXT("%d Ampeln, %d Stopp"), Stats.TrafficLights, Stats.StopSigns));
	Check(TEXT("Strassenschilder"), Stats.StreetSigns >= 30, Num(Stats.StreetSigns));
	Check(TEXT("Zebrastreifen-Schilder"), Stats.CrosswalkSigns >= 20, Num(Stats.CrosswalkSigns));
	Check(TEXT("Hydranten"), Stats.Hydrants >= 100, Num(Stats.Hydrants));
	Check(TEXT("Muelleimer"), Stats.TrashBins >= 100, Num(Stats.TrashBins));
	Check(TEXT("Baenke"), Stats.Benches >= 50, Num(Stats.Benches));
	Check(TEXT("Poller"), Stats.Bollards >= 50, Num(Stats.Bollards));
	Check(TEXT("Leitplanken"), Stats.GuardrailSegments >= 40, Num(Stats.GuardrailSegments));
	Check(TEXT("Werbetafeln"), Stats.Billboards >= 6, Num(Stats.Billboards));
	Check(TEXT("Aegis-Kameramasten"), Stats.AegisCameras >= 30 && AegisCameras.Num() == Stats.AegisCameras, Num(AegisCameras.Num()));
	Check(TEXT("Plakate/Graffiti"), Stats.PostersAndGraffiti >= 30, Num(Stats.PostersAndGraffiti));
	Check(TEXT("Neon-Schilder"), Stats.NeonSigns >= 30, Num(Stats.NeonSigns));
	Check(TEXT("Container"), Stats.Containers >= 150, Num(Stats.Containers));
	// Aegis-Dichte muss Core > Rustline sein (spielbare Eigenschaft).
	int32 CamCore = 0, CamRust = 0;
	for (const FMTAAegisCameraPoint& P : AegisCameras)
	{
		CamCore += P.District == EMTADistrict::Core ? 1 : 0;
		CamRust += P.District == EMTADistrict::Rustline ? 1 : 0;
	}
	Check(TEXT("Aegis-Dichte Core > Rustline"), CamCore > CamRust, FString::Printf(TEXT("%d / %d"), CamCore, CamRust));

	// Begehbare Gebaeude.
	const UWorld* World = GetWorld();
	const int32 Alive = GetSpawnedEntryCount();
	int32 PerDistrict[MTADistrict::Num()] = {};
	for (const AMTABuildingEntry* Entry : SpawnedEntries)
	{
		if (IsValid(Entry))
		{
			const int32 D = static_cast<int32>(Entry->District);
			if (D >= 0 && D < MTADistrict::Num()) { ++PerDistrict[D]; }
		}
	}
	Check(TEXT("Standorte geplant (>= 60)"), EnterableSites.Num() >= 60, Num(EnterableSites.Num()));
	if (World && World->IsGameWorld())
	{
		Check(TEXT("Begehbare Gebaeude (>= 60)"), Alive >= 60, Num(Alive));
		FString Dist;
		bool bAll = true;
		for (int32 D = 0; D < MTADistrict::Num(); ++D)
		{
			Dist += FString::Printf(TEXT("%s=%d "), MTADistrict::ShortName(static_cast<EMTADistrict>(D)), PerDistrict[D]);
			bAll &= PerDistrict[D] >= 3;
		}
		Check(TEXT("Gebaeude in allen 6 Bezirken"), bAll, Dist.TrimEnd());
		Check(TEXT("Tuermarkierungen"), GetLayerInstanceCount(EMTADetailLayer::DoorSigns) >= Alive * 3, Num(GetLayerInstanceCount(EMTADetailLayer::DoorSigns)));
	}

	// Budget.
	const int32 Total = GetTotalInstanceCount();
	Check(TEXT("Instanz-Budget (ohne Wasser)"), Total - Water <= InstanceBudget, FString::Printf(TEXT("%d / %d"), Total - Water, InstanceBudget));
	Check(TEXT("Wasser-Budget"), Water <= WaterTileBudget, FString::Printf(TEXT("%d / %d"), Water, WaterTileBudget));
	TInlineComponentArray<UActorComponent*> Comps(this);
	Check(TEXT("Komponenten-Budget"), Comps.Num() <= ComponentBudget, FString::Printf(TEXT("%d / %d"), Comps.Num(), ComponentBudget));
	bool bCull = true;
	for (int32 L = 0; L < static_cast<int32>(EMTADetailLayer::Count); ++L)
	{
		const FLayerSpec& Spec = GetLayerSpec(L);
		const UHierarchicalInstancedStaticMeshComponent* Comp = DetailLayers.IsValidIndex(L) ? DetailLayers[L].Get() : nullptr;
		if (Spec.CullEnd > 0 && Comp)
		{
			int32 CullStart = 0;
			int32 CullEndValue = 0;
			Comp->GetCullDistances(CullStart, CullEndValue);
			bCull &= CullEndValue > 0;
		}
	}
	Check(TEXT("Sichtweiten der Kleinteile gesetzt"), bCull, TEXT("HISM InstanceEndCullDistance"));
	Check(TEXT("Aktuell zur Stadt"), BuiltForCityHash != 0 && BuiltForCityHash == ComputeCityHash(ResolveCity()), TEXT("Fingerabdruck"));

	FString Report = FString::Printf(TEXT("WORLD SELFTEST %s: %d Fehler, %d Instanzen (%d Wasser), %d Landmarken, %d Kameras, %d Gebaeude\n"),
		Failures == 0 ? TEXT("OK") : TEXT("FEHLER"), Failures, Total, Water, Landmarks.Num(), AegisCameras.Num(), Alive);
	for (const FString& Line : Lines)
	{
		UE_LOG(LogMTAWorldDetail, Display, TEXT("%s"), *Line);
		Report += Line + TEXT("\n");
	}
	UE_LOG(LogMTAWorldDetail, Display, TEXT("WORLD SELFTEST: %d Fehler"), Failures);
	return Report;
}
