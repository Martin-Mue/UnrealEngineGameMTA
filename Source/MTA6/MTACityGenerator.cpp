#include "MTACityGenerator.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SceneComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogMTACity, Log, All);

/** Indizes der Custom-Data von Boden/Gehweg (Rasterdaten fuer das world-aligned Bodenmaterial). */
namespace MTAGroundData
{
	inline constexpr int32 OriginX = 0;
	inline constexpr int32 OriginY = 1;
	inline constexpr int32 Period = 2;
	inline constexpr int32 RoadWidth = 3;
	inline constexpr int32 HalfX = 4;
	inline constexpr int32 HalfY = 5;
	inline constexpr int32 Num = 6;
}

namespace
{
	// Standardpfade der von tools/ue/build_materials.py erzeugten Materialien.
	const TCHAR* const DefaultBuildingMaterial = TEXT("/Game/MTA6/Materials/M_MTA_Building.M_MTA_Building");
	const TCHAR* const DefaultGroundMaterial   = TEXT("/Game/MTA6/Materials/M_MTA_Ground.M_MTA_Ground");
	const TCHAR* const DefaultParkMaterial     = TEXT("/Game/MTA6/Materials/M_MTA_Park.M_MTA_Park");
	const TCHAR* const DefaultLampMaterial     = TEXT("/Game/MTA6/Materials/M_MTA_LampEmissive.M_MTA_LampEmissive");
	const TCHAR* const DefaultMetalMaterial    = TEXT("/Game/MTA6/Materials/M_MTA_Metal.M_MTA_Metal");
	const TCHAR* const DefaultTrunkMaterial    = TEXT("/Game/MTA6/Materials/M_MTA_TreeTrunk.M_MTA_TreeTrunk");
	const TCHAR* const DefaultFoliageMaterial  = TEXT("/Game/MTA6/Materials/M_MTA_Foliage.M_MTA_Foliage");
	const TCHAR* const DefaultRoofMaterial     = TEXT("/Game/MTA6/Materials/M_MTA_Roof.M_MTA_Roof");

	/** Wurzel aus 2 (Dachprismen: Wuerfel um 45 Grad gerollt). */
	constexpr float Sqrt2 = 1.41421356f;

	/** Weiche Landzungen-Funktion: 0 bis Start, danach etwa linear mit Slope (ohne Knick). */
	float HeadlandOffset(float AbsX, float Start, float Slope)
	{
		const float D = FMath::Max(0.f, AbsX - Start);
		return Slope * D * D / (D + 10000.f);
	}

	/** Gemeinsame Custom-Data eines Gebaeudeteils (MTABuildingData). */
	void FillBuildingData(float* Data, const FLinearColor& Tint, float HeightCm, float WindowSeed, float Wear, float Lit,
		EMTABuildingStyle Style, EMTADistrict District, float Neon, float Detail, const FLinearColor& Accent)
	{
		Data[MTABuildingData::TintR] = Tint.R;
		Data[MTABuildingData::TintG] = Tint.G;
		Data[MTABuildingData::TintB] = Tint.B;
		Data[MTABuildingData::HeightMeters] = HeightCm / 100.f;
		Data[MTABuildingData::WindowSeed] = WindowSeed;
		Data[MTABuildingData::Wear] = Wear;
		Data[MTABuildingData::LitRatio] = Lit;
		Data[MTABuildingData::Style] = static_cast<float>(static_cast<uint8>(Style));
		Data[MTABuildingData::District] = static_cast<float>(static_cast<uint8>(District));
		Data[MTABuildingData::Neon] = Neon;
		Data[MTABuildingData::Detail] = Detail;
		Data[MTABuildingData::AccentR] = Accent.R;
		Data[MTABuildingData::AccentG] = Accent.G;
		Data[MTABuildingData::AccentB] = Accent.B;
	}

	/** Engine-Grundkoerper sind 100 cm gross und mittig gepivotet. */
	constexpr float ShapeSize = 100.f;

	/** Deterministischer Teil-Seed je Block (unabhaengig von der Reihenfolge der Generierung). */
	int32 BlockSeed(int32 Seed, int32 BX, int32 BY, int32 Salt)
	{
		uint32 H = HashCombine(GetTypeHash(Seed), GetTypeHash(BX * 73856093));
		H = HashCombine(H, GetTypeHash(BY * 19349663));
		H = HashCombine(H, GetTypeHash(Salt));
		return static_cast<int32>(H & 0x7FFFFFFF);
	}

	FTransform BoxTransform(const FVector& Center, const FVector& SizeCm, float YawDeg = 0.f)
	{
		return FTransform(FRotator(0.f, YawDeg, 0.f), Center, SizeCm / ShapeSize);
	}

	FLinearColor Darken(const FLinearColor& C, float Factor)
	{
		return FLinearColor(C.R * Factor, C.G * Factor, C.B * Factor, C.A);
	}

	/** Gewichtete Auswahl 0..3 aus einem FVector4. */
	int32 PickWeighted(FRandomStream& Rng, const FVector4& W)
	{
		const double Weights[4] = { FMath::Max(0.0, W.X), FMath::Max(0.0, W.Y), FMath::Max(0.0, W.Z), FMath::Max(0.0, W.W) };
		const double Sum = Weights[0] + Weights[1] + Weights[2] + Weights[3];
		if (Sum <= UE_SMALL_NUMBER)
		{
			return 1;
		}
		double R = Rng.FRand() * Sum;
		for (int32 I = 0; I < 4; ++I)
		{
			if (R < Weights[I])
			{
				return I;
			}
			R -= Weights[I];
		}
		return 3;
	}
}

// ============================================================================ FInstanceBatch

void AMTACityGenerator::FInstanceBatch::Add(const FTransform& T, TConstArrayView<float> Data)
{
	Transforms.Add(T);
	if (NumCustom > 0)
	{
		const int32 Start = CustomData.AddZeroed(NumCustom);
		const int32 Count = FMath::Min(NumCustom, Data.Num());
		for (int32 I = 0; I < Count; ++I)
		{
			CustomData[Start + I] = Data[I];
		}
	}
}

// ============================================================================ Konstruktion

AMTACityGenerator::AMTACityGenerator()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	RootComponent = SceneRoot;

	auto MakeHISM = [this](const TCHAR* Name) -> UHierarchicalInstancedStaticMeshComponent*
	{
		UHierarchicalInstancedStaticMeshComponent* Comp = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(Name);
		Comp->SetupAttachment(SceneRoot);
		// Statisch: Lumen-Karten und Virtual-Shadow-Map-Seiten bleiben gecacht (wichtig fuer die 5700 XT).
		Comp->SetMobility(EComponentMobility::Static);
		return Comp;
	};

	GroundTiles = MakeHISM(TEXT("GroundTiles"));
	Sidewalks   = MakeHISM(TEXT("Sidewalks"));
	ParkLawns   = MakeHISM(TEXT("ParkLawns"));
	Buildings   = MakeHISM(TEXT("Buildings"));
	RoofDetails = MakeHISM(TEXT("RoofDetails"));
	Antennas    = MakeHISM(TEXT("Antennas"));
	Chimneys    = MakeHISM(TEXT("Chimneys"));
	LampPoles   = MakeHISM(TEXT("LampPoles"));
	LampHeads   = MakeHISM(TEXT("LampHeads"));
	TreeTrunks  = MakeHISM(TEXT("TreeTrunks"));
	TreeCrowns  = MakeHISM(TEXT("TreeCrowns"));
	PitchedRoofs = MakeHISM(TEXT("PitchedRoofs"));

	PlazaDistricts = { EMTADistrict::LanternHill, EMTADistrict::Greenmoor };

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	CubeMesh = CubeFinder.Object;
	CylinderMesh = CylinderFinder.Object;
	SphereMesh = SphereFinder.Object;

	for (int32 D = 0; D < MTADistrict::Num(); ++D)
	{
		DistrictProfiles.Add(MTADistrict::MakeDefaultProfile(static_cast<EMTADistrict>(D)));
	}
}

void AMTACityGenerator::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (bGenerateOnConstruction)
	{
		Generate();
	}
}

void AMTACityGenerator::BeginPlay()
{
	Super::BeginPlay();
	const bool bMissing = Buildings == nullptr || Buildings->GetInstanceCount() == 0 || LampPositions.Num() == 0;
	if (bRegenerateOnBeginPlay || bMissing)
	{
		Generate();
	}
	EnsureLampLightPool();
}

void AMTACityGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyLampLightPool();
	Super::EndPlay(EndPlayReason);
}

// ============================================================================ Generierung

void AMTACityGenerator::Clear()
{
	for (UHierarchicalInstancedStaticMeshComponent* Comp : { GroundTiles.Get(), Sidewalks.Get(), ParkLawns.Get(), Buildings.Get(), RoofDetails.Get(),
		Antennas.Get(), Chimneys.Get(), LampPoles.Get(), LampHeads.Get(), TreeTrunks.Get(), TreeCrowns.Get(), PitchedRoofs.Get() })
	{
		if (Comp)
		{
			Comp->ClearInstances();
		}
	}
	DestroyLampLightPool();
	BlockDistricts.Reset();
	LampPositions.Reset();
	LampColors.Reset();
	CityLots.Reset();
	DistrictPlazaBlocks.Reset();
	LandmarkBlockIndex = INDEX_NONE;
	GeneratedBuildingCount = 0;
	LastLampViewLocation = FVector(TNumericLimits<float>::Max());
	LastNightAlpha = -1.f;
}

void AMTACityGenerator::Generate()
{
	if (!CubeMesh || !CylinderMesh || !SphereMesh)
	{
		UE_LOG(LogMTACity, Error, TEXT("%s: Grundkoerper (/Engine/BasicShapes) fehlen, Generierung abgebrochen."), *GetName());
		return;
	}
	if (RoadWidth >= BlockPeriod - 2.f * SidewalkWidth - 200.f)
	{
		UE_LOG(LogMTACity, Warning, TEXT("%s: Strasse + Gehweg sind breiter als die Periode, Bloecke bleiben leer."), *GetName());
	}

	const double StartTime = FPlatformTime::Seconds();
	Clear();

	FBuildContext Ctx;
	ResolveProfiles(Ctx.Profiles);
	AssignDistricts();

	Ctx.Ground.NumCustom = MTAGroundData::Num;
	Ctx.Sidewalk.NumCustom = MTAGroundData::Num;
	Ctx.Park.NumCustom = MTAPropData::Num;
	Ctx.Building.NumCustom = MTABuildingData::Num;
	Ctx.Head.NumCustom = MTAPropData::Num;
	Ctx.Crown.NumCustom = MTAPropData::Num;
	Ctx.Pitched.NumCustom = MTAPropData::Num;

	if (bGenerateGround)
	{
		BuildGround(Ctx);
	}

	const int32 LandmarkBlock = bSpawnLandmarks ? FindLandmarkBlock() : INDEX_NONE;
	LandmarkBlockIndex = LandmarkBlock;
	ComputePlazaBlocks();
	for (int32 BY = 0; BY < BlocksY; ++BY)
	{
		for (int32 BX = 0; BX < BlocksX; ++BX)
		{
			BuildBlock(Ctx, BX, BY, (BX + BY * BlocksX) == LandmarkBlock);
		}
	}
	BuildLamps(Ctx);

	// Komponenten konfigurieren: Mesh, Material, Kollision, Schatten, Sichtweite, Custom-Data.
	UMaterialInterface* GroundMat = ResolveMaterial(GroundMaterial, DefaultGroundMaterial);
	UMaterialInterface* SidewalkMat = SidewalkMaterial ? SidewalkMaterial.Get() : GroundMat;
	const bool PropShadow = bSmallPropsCastShadow;

	ConfigureComponent(GroundTiles, CubeMesh, GroundMat, true, false, 0, MTAGroundData::Num, true);
	ConfigureComponent(Sidewalks, CubeMesh, SidewalkMat, true, false, 0, MTAGroundData::Num, true);
	ConfigureComponent(ParkLawns, CubeMesh, ResolveMaterial(ParkMaterial, DefaultParkMaterial), false, false, 0, MTAPropData::Num, false);
	ConfigureComponent(Buildings, CubeMesh, ResolveMaterial(BuildingMaterial, DefaultBuildingMaterial), true, true, BuildingCullDistance, MTABuildingData::Num, true);
	ConfigureComponent(RoofDetails, CubeMesh, ResolveMaterial(MetalMaterial, DefaultMetalMaterial), false, PropShadow, RoofDetailCullDistance, 0, false);
	ConfigureComponent(Antennas, CylinderMesh, ResolveMaterial(MetalMaterial, DefaultMetalMaterial), false, PropShadow, RoofDetailCullDistance * 2, 0, false);
	ConfigureComponent(Chimneys, CylinderMesh, ResolveMaterial(MetalMaterial, DefaultMetalMaterial), true, true, BuildingCullDistance, 0, true);
	ConfigureComponent(LampPoles, CylinderMesh, ResolveMaterial(MetalMaterial, DefaultMetalMaterial), true, PropShadow, LampCullDistance, 0, false);
	ConfigureComponent(LampHeads, CubeMesh, ResolveMaterial(LampMaterial, DefaultLampMaterial), false, false, LampCullDistance * 2, MTAPropData::Num, false);
	ConfigureComponent(TreeTrunks, CylinderMesh, ResolveMaterial(TreeTrunkMaterial, DefaultTrunkMaterial), true, PropShadow, TreeCullDistance, 0, false);
	ConfigureComponent(TreeCrowns, SphereMesh, ResolveMaterial(FoliageMaterial, DefaultFoliageMaterial), false, true, TreeCullDistance, MTAPropData::Num, false);
	// Daecher gehoeren zur Silhouette: keine Sichtweitengrenze, Schatten an, keine Kollision (liegen im Gebaeude).
	ConfigureComponent(PitchedRoofs, CubeMesh, ResolveMaterial(RoofMaterial, DefaultRoofMaterial), false, true, BuildingCullDistance, MTAPropData::Num, false);

	// Die Leuchtkrone der Landmarke liegt in LampHeads und soll weit sichtbar sein.
	if (bSpawnLandmarks && LandmarkBlock != INDEX_NONE)
	{
		LampHeads->SetCullDistances(0, 0);
	}

	ApplyBatch(GroundTiles, Ctx.Ground);
	ApplyBatch(Sidewalks, Ctx.Sidewalk);
	ApplyBatch(ParkLawns, Ctx.Park);
	ApplyBatch(Buildings, Ctx.Building);
	ApplyBatch(RoofDetails, Ctx.Roof);
	ApplyBatch(Antennas, Ctx.Antenna);
	ApplyBatch(Chimneys, Ctx.Chimney);
	ApplyBatch(LampPoles, Ctx.Pole);
	ApplyBatch(LampHeads, Ctx.Head);
	ApplyBatch(TreeTrunks, Ctx.Trunk);
	ApplyBatch(TreeCrowns, Ctx.Crown);
	ApplyBatch(PitchedRoofs, Ctx.Pitched);

	GeneratedBuildingCount = Ctx.Building.Transforms.Num();

	// Parzellen in Weltkoordinaten ablegen (der Generator wird nur verschoben, nicht gedreht).
	const FVector Origin = GetActorLocation();
	CityLots = MoveTemp(Ctx.Lots);
	for (FMTACityLot& Lot : CityLots)
	{
		Lot.Center += FVector2D(Origin.X, Origin.Y);
		Lot.BaseZ += static_cast<float>(Origin.Z);
		Lot.RoofTopZ += static_cast<float>(Origin.Z);
	}

	UE_LOG(LogMTACity, Log, TEXT("%s: Stadt erzeugt (Seed %d, %dx%d Bloecke): %d Gebaeudeteile, %d Laternen, %d Baeume, %.1f ms."),
		*GetName(), Seed, BlocksX, BlocksY, GeneratedBuildingCount, LampPositions.Num(), Ctx.Trunk.Transforms.Num(),
		(FPlatformTime::Seconds() - StartTime) * 1000.0);

	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		EnsureLampLightPool();
	}
}

void AMTACityGenerator::ConfigureComponent(UHierarchicalInstancedStaticMeshComponent* Comp, UStaticMesh* Mesh, UMaterialInterface* Material,
	bool bCollision, bool bCastShadow, int32 CullEnd, int32 NumCustomData, bool bAffectDistanceField) const
{
	if (!Comp)
	{
		return;
	}
	Comp->SetStaticMesh(Mesh);
	if (Material)
	{
		Comp->SetMaterial(0, Material);
	}
	Comp->SetNumCustomDataFloats(NumCustomData);

	if (bCollision)
	{
		// Gebaeude, Boden, Gehwege, Masten, Staemme: Fahrzeug muss dagegen fahren.
		Comp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	else
	{
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	}
	Comp->SetCanEverAffectNavigation(bCollision);
	Comp->SetCastShadow(bCastShadow);

	// Kleine Teile nicht ins Lumen-/Distance-Field-Szenenbild aufnehmen: spart Speicher und
	// Software-RT-Kosten; ihr Beitrag zur indirekten Beleuchtung ist vernachlaessigbar.
	Comp->bAffectDistanceFieldLighting = bAffectDistanceField;

	// Sichtweite: Instanzen verschwinden zwischen 80 % und 100 % der Distanz (weiches Ausblenden
	// gibt es nur mit Dither im Material, daher bewusst grosszuegige Werte).
	if (CullEnd > 0)
	{
		Comp->SetCullDistances(FMath::RoundToInt(CullEnd * 0.8f), CullEnd);
	}
	else
	{
		Comp->SetCullDistances(0, 0);
	}
}

void AMTACityGenerator::ApplyBatch(UHierarchicalInstancedStaticMeshComponent* Comp, const FInstanceBatch& Batch) const
{
	if (!Comp || Batch.Transforms.Num() == 0)
	{
		return;
	}
	Comp->AddInstances(Batch.Transforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false);

	if (Batch.NumCustom > 0 && Batch.CustomData.Num() == Batch.Transforms.Num() * Batch.NumCustom)
	{
		// Bereichs-Variante (UE 5.8, InstanceIndexEnd inklusiv): ein Aufruf fuer alle Instanzen.
		// Die HISM-Klasse verdeckt die Bereichs-Variante; deshalb explizit ueber die Basisklasse aufrufen.
		Comp->UInstancedStaticMeshComponent::SetCustomData(0, Batch.Transforms.Num() - 1, TConstArrayView<float>(Batch.CustomData), /*bMarkRenderStateDirty*/ false);
	}
	Comp->MarkRenderStateDirty();
}

UMaterialInterface* AMTACityGenerator::ResolveMaterial(UMaterialInterface* Assigned, const TCHAR* DefaultPath) const
{
	if (Assigned)
	{
		return Assigned;
	}
	// Fallback auf die vom Python-Skript erzeugten Assets; fehlen sie, bleibt das Mesh-Standardmaterial.
	return LoadObject<UMaterialInterface>(nullptr, DefaultPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

void AMTACityGenerator::ResolveProfiles(TArray<FMTADistrictProfile>& OutProfiles) const
{
	OutProfiles.Reset(MTADistrict::Num());
	for (int32 D = 0; D < MTADistrict::Num(); ++D)
	{
		if (DistrictProfiles.IsValidIndex(D))
		{
			FMTADistrictProfile P = DistrictProfiles[D];
			if (P.Palette.Num() == 0)
			{
				P.Palette = MTADistrict::MakeDefaultProfile(static_cast<EMTADistrict>(D)).Palette;
			}
			if (P.LookVersion < MTADistrict::CurrentLookVersion)
			{
				// Profil aus aelterem Level/Editor: Look-Felder (Dach, Neon, Moeblierung) vom Standard uebernehmen.
				MTADistrict::CopyLookFields(MTADistrict::MakeDefaultProfile(static_cast<EMTADistrict>(D)), P);
			}
			P.MaxHeight = FMath::Max(P.MaxHeight, P.MinHeight);
			OutProfiles.Add(MoveTemp(P));
		}
		else
		{
			OutProfiles.Add(MTADistrict::MakeDefaultProfile(static_cast<EMTADistrict>(D)));
		}
	}
}

FMTADistrictProfile AMTACityGenerator::GetDistrictProfile(EMTADistrict District) const
{
	const int32 D = static_cast<int32>(District);
	if (DistrictProfiles.IsValidIndex(D))
	{
		FMTADistrictProfile P = DistrictProfiles[D];
		if (P.LookVersion < MTADistrict::CurrentLookVersion)
		{
			MTADistrict::CopyLookFields(MTADistrict::MakeDefaultProfile(District), P);
		}
		return P;
	}
	return MTADistrict::MakeDefaultProfile(District);
}

EMTADistrict AMTACityGenerator::ComputeDistrict(int32 BX, int32 BY) const
{
	if (const EMTADistrict* Override = DistrictOverrides.Find(FIntPoint(BX, BY)))
	{
		return *Override;
	}
	if (DistrictMode == EMTADistrictMode::Uniform)
	{
		return UniformDistrict;
	}

	// U = West->Ost (Y-Achse), V = Sued->Nord (X-Achse), jeweils Blockmitte normiert.
	float U = (BY + 0.5f) / FMath::Max(1, BlocksY);
	float V = (BX + 0.5f) / FMath::Max(1, BlocksX);

	if (DistrictMode == EMTADistrictMode::NoisyLayout && DistrictNoiseStrength > 0.f)
	{
		// Seedabhaengiger Versatz im Noise-Feld, damit verschiedene Seeds verschiedene Grenzen ergeben.
		FRandomStream Rng(Seed ^ 0x5D1C7);
		const FVector2D Offset(Rng.FRandRange(0.f, 1000.f), Rng.FRandRange(0.f, 1000.f));
		const FVector2D P(U * DistrictNoiseFrequency, V * DistrictNoiseFrequency);
		const float NU = FMath::PerlinNoise2D(P + Offset);
		const float NV = FMath::PerlinNoise2D(P + Offset + FVector2D(37.7, 91.3));
		U = FMath::Clamp(U + NU * DistrictNoiseStrength, 0.f, 1.f);
		V = FMath::Clamp(V + NV * DistrictNoiseStrength, 0.f, 1.f);
	}
	return MTADistrict::ClassifyNormalized(U, V);
}

void AMTACityGenerator::AssignDistricts()
{
	BlockDistricts.SetNum(BlocksX * BlocksY);
	for (int32 BY = 0; BY < BlocksY; ++BY)
	{
		for (int32 BX = 0; BX < BlocksX; ++BX)
		{
			BlockDistricts[BX + BY * BlocksX] = ComputeDistrict(BX, BY);
		}
	}
}

int32 AMTACityGenerator::FindLandmarkBlock() const
{
	int32 Best = INDEX_NONE;
	double BestDist = TNumericLimits<double>::Max();
	for (int32 BY = 0; BY < BlocksY; ++BY)
	{
		for (int32 BX = 0; BX < BlocksX; ++BX)
		{
			const int32 Index = BX + BY * BlocksX;
			const bool bCore = BlockDistricts.IsValidIndex(Index) && BlockDistricts[Index] == EMTADistrict::Core;
			// Core-Bloecke bevorzugen; gibt es keinen, nimmt der naechste Block die Landmarke.
			const double Dist = FVector2D::DistSquared(LocalBlockCenter(BX, BY), DowntownCenter) + (bCore ? 0.0 : 1.0e12);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = Index;
			}
		}
	}
	return Best;
}

void AMTACityGenerator::ComputePlazaBlocks()
{
	DistrictPlazaBlocks.Init(FIntPoint(-1, -1), MTADistrict::Num());
	if (!bReserveDistrictPlazas)
	{
		return;
	}
	for (const EMTADistrict District : PlazaDistricts)
	{
		const int32 D = static_cast<int32>(District);
		if (!DistrictPlazaBlocks.IsValidIndex(D))
		{
			continue;
		}
		// Schwerpunkt der Bezirksbloecke; der naechstgelegene Block wird Platz (deterministisch).
		FVector2D Sum = FVector2D::ZeroVector;
		int32 Count = 0;
		for (int32 BY = 0; BY < BlocksY; ++BY)
		{
			for (int32 BX = 0; BX < BlocksX; ++BX)
			{
				const int32 Index = BX + BY * BlocksX;
				if (BlockDistricts.IsValidIndex(Index) && BlockDistricts[Index] == District && Index != LandmarkBlockIndex)
				{
					Sum += FVector2D(BX, BY);
					++Count;
				}
			}
		}
		if (Count == 0)
		{
			continue;
		}
		const FVector2D Mean = Sum / Count;
		double Best = TNumericLimits<double>::Max();
		for (int32 BY = 0; BY < BlocksY; ++BY)
		{
			for (int32 BX = 0; BX < BlocksX; ++BX)
			{
				const int32 Index = BX + BY * BlocksX;
				if (!BlockDistricts.IsValidIndex(Index) || BlockDistricts[Index] != District || Index == LandmarkBlockIndex)
				{
					continue;
				}
				const double Dist = FVector2D::DistSquared(FVector2D(BX, BY), Mean);
				if (Dist < Best - 1.0e-6)
				{
					Best = Dist;
					DistrictPlazaBlocks[D] = FIntPoint(BX, BY);
				}
			}
		}
	}
}

bool AMTACityGenerator::IsPlazaBlock(int32 BX, int32 BY) const
{
	return DistrictPlazaBlocks.Contains(FIntPoint(BX, BY));
}

void AMTACityGenerator::BuildPlaza(FBuildContext& Ctx, FRandomStream& Rng, const FVector2D& Center, const FMTADistrictProfile& Profile, EMTADistrict District) const
{
	const float Inner = BlockPeriod - RoadWidth;
	if (District == EMTADistrict::Greenmoor)
	{
		// Country-Club-Rasen (Wasserturm steht in der Mitte).
		const float LawnSize = FMath::Max(200.f, Inner - 2.f * FMath::Min(SidewalkWidth, 400.f));
		const float Data[MTAPropData::Num] = { Profile.FoliageColor.R, Profile.FoliageColor.G, Profile.FoliageColor.B,
			static_cast<float>(static_cast<uint8>(District)), Profile.Wear };
		Ctx.Park.Add(BoxTransform(FVector(Center.X, Center.Y, CurbHeight + 5.f), FVector(LawnSize, LawnSize, 10.f)), Data);
	}
	// Baumring am Rand, Mitte bleibt fuer die Landmarke frei.
	const float R = Inner * 0.5f - 700.f;
	for (int32 I = 0; I < 8; ++I)
	{
		const float A = I * UE_TWO_PI / 8.f + UE_PI / 8.f;
		AddTree(Ctx, Rng, Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R, Profile, District, District == EMTADistrict::Sunstrip);
	}
}

float AMTACityGenerator::CoastStripStep() const
{
	const int32 StripsPerTile = FMath::Max(1, FMath::RoundToInt(GroundTileSize / FMath::Max(500.f, CoastStripWidth)));
	return GroundTileSize / StripsPerTile;
}

float AMTACityGenerator::GetGroundHalfSize() const
{
	const int32 Tiles = FMath::Max(1, FMath::CeilToInt(2.f * GroundExtent / GroundTileSize));
	return Tiles * GroundTileSize * 0.5f;
}

FVector2D AMTACityGenerator::GetCityHalfExtent() const
{
	return FVector2D(BlocksX * BlockPeriod * 0.5f, BlocksY * BlockPeriod * 0.5f);
}

float AMTACityGenerator::GetCoastLocalY(float LocalX, bool bStepped) const
{
	float X = LocalX;
	if (bStepped)
	{
		// Mitte des Bodenstreifens, in dem LocalX liegt (gleiches Raster wie BuildGround).
		const float GroundHalf = GetGroundHalfSize();
		const float Step = CoastStripStep();
		X = -GroundHalf + (FMath::FloorToFloat((LocalX + GroundHalf) / Step) + 0.5f) * Step;
	}
	const float BaseY = -(BlocksY * BlockPeriod * 0.5f + CoastOffset);
	return BaseY - HeadlandOffset(FMath::Abs(X), BayHeadlandStart, BayHeadlandSlope);
}

float AMTACityGenerator::GetCoastWorldY(float WorldX, bool bStepped) const
{
	const FVector Origin = GetActorLocation();
	return GetCoastLocalY(WorldX - static_cast<float>(Origin.X), bStepped) + static_cast<float>(Origin.Y);
}

bool AMTACityGenerator::GetDistrictPlaza(EMTADistrict District, FVector& OutCenter, float& OutInnerSize) const
{
	const int32 D = static_cast<int32>(District);
	if (!DistrictPlazaBlocks.IsValidIndex(D) || DistrictPlazaBlocks[D].X < 0)
	{
		return false;
	}
	OutCenter = GetBlockCenterLocation(DistrictPlazaBlocks[D].X, DistrictPlazaBlocks[D].Y);
	OutInnerSize = BlockPeriod - RoadWidth;
	return true;
}

bool AMTACityGenerator::GetLandmarkTowerLocation(FVector& OutBase) const
{
	if (LandmarkBlockIndex == INDEX_NONE || BlocksX <= 0)
	{
		return false;
	}
	OutBase = GetBlockCenterLocation(LandmarkBlockIndex % BlocksX, LandmarkBlockIndex / BlocksX);
	return true;
}

FVector2D AMTACityGenerator::LocalBlockCenter(int32 BX, int32 BY) const
{
	return FVector2D(LocalRoadCenter(0, BX) + BlockPeriod * 0.5f, LocalRoadCenter(1, BY) + BlockPeriod * 0.5f);
}

float AMTACityGenerator::LocalRoadCenter(int32 Axis, int32 Index) const
{
	const int32 Count = Axis == 0 ? BlocksX : BlocksY;
	return Index * BlockPeriod - Count * BlockPeriod * 0.5f;
}

float AMTACityGenerator::ComputeBuildingHeight(FRandomStream& Rng, const FVector2D& Center, const FMTADistrictProfile& Profile) const
{
	const float Dist01 = FMath::Clamp(static_cast<float>(FVector2D::Distance(Center, DowntownCenter) / DowntownRadius), 0.f, 1.f);
	float Profile01;
	if (HeightProfileCurve)
	{
		Profile01 = FMath::Clamp(HeightProfileCurve->GetFloatValue(Dist01), 0.f, 1.f);
	}
	else
	{
		Profile01 = FMath::Pow(1.f - Dist01, HeightFalloffExponent);
	}

	const float R = FMath::Pow(Rng.FRand(), Profile.HeightBias);
	const float Base = FMath::Lerp(Profile.MinHeight, Profile.MaxHeight, R);
	const float Scale = FMath::Lerp(OutskirtsScale, DowntownPeakScale, Profile01);
	float H = FMath::Lerp(Base, Base * Scale, Profile.DowntownInfluence);
	H = FMath::Clamp(H, 300.f, MaxBuildingHeight);

	// Auf ganze Geschosse runden, mindestens ein Geschoss.
	return FMath::Max(1.f, FMath::RoundToFloat(H / FloorHeight)) * FloorHeight;
}

void AMTACityGenerator::BuildGround(FBuildContext& Ctx) const
{
	const float HalfX = BlocksX * BlockPeriod * 0.5f;
	const float HalfY = BlocksY * BlockPeriod * 0.5f;
	const FVector Origin = GetActorLocation();
	const float GroundData[MTAGroundData::Num] = {
		static_cast<float>(Origin.X), static_cast<float>(Origin.Y), BlockPeriod, RoadWidth, HalfX, HalfY };

	const int32 Tiles = FMath::Max(1, FMath::CeilToInt(2.f * GroundExtent / GroundTileSize));
	const float Start = -Tiles * GroundTileSize * 0.5f + GroundTileSize * 0.5f;
	const float HalfTile = GroundTileSize * 0.5f;
	const int32 StripsPerTile = FMath::Max(1, FMath::RoundToInt(GroundTileSize / FMath::Max(500.f, CoastStripWidth)));
	const float StripW = GroundTileSize / StripsPerTile;
	for (int32 Y = 0; Y < Tiles; ++Y)
	{
		for (int32 X = 0; X < Tiles; ++X)
		{
			// Oberkante bei Z = 0, 100 cm dick (verhindert Durchrutschen bei hohen Geschwindigkeiten).
			const FVector Center(Start + X * GroundTileSize, Start + Y * GroundTileSize, -50.f);
			if (!bGenerateCoast)
			{
				Ctx.Ground.Add(BoxTransform(Center, FVector(GroundTileSize, GroundTileSize, 100.f)), GroundData);
				continue;
			}

			// Kueste: Meer liegt westlich der Linie. Die Linie liegt am weitesten oestlich bei |X| minimal.
			const float X0 = static_cast<float>(Center.X) - HalfTile;
			const float X1 = static_cast<float>(Center.X) + HalfTile;
			const float Y0 = static_cast<float>(Center.Y) - HalfTile;
			const float Y1 = static_cast<float>(Center.Y) + HalfTile;
			const float CoastEast = GetCoastLocalY(FMath::Clamp(0.f, X0, X1), false);
			const float CoastWest = FMath::Min(GetCoastLocalY(X0, false), GetCoastLocalY(X1, false));
			if (Y0 >= CoastEast + 1.f)
			{
				Ctx.Ground.Add(BoxTransform(Center, FVector(GroundTileSize, GroundTileSize, 100.f)), GroundData);
				continue;
			}
			if (Y1 <= CoastWest - StripW)
			{
				continue; // komplett Meer
			}
			// Angeschnittene Kachel: in Streifen entlang X zerlegen, jeder Streifen endet an der (gestuften) Kueste.
			for (int32 S = 0; S < StripsPerTile; ++S)
			{
				const float SX = X0 + (S + 0.5f) * StripW;
				const float Cut = FMath::Max(Y0, GetCoastLocalY(SX, true));
				if (Cut >= Y1 - 1.f)
				{
					continue;
				}
				const float Len = Y1 - Cut;
				Ctx.Ground.Add(BoxTransform(FVector(SX, Cut + Len * 0.5f, -50.f), FVector(StripW, Len, 100.f)), GroundData);
			}
		}
	}

	// Gehwegplatten: eine je Block, Oberkante = Bordsteinhoehe.
	if (CurbHeight > 0.5f)
	{
		const float Inner = BlockPeriod - RoadWidth;
		for (int32 BY = 0; BY < BlocksY; ++BY)
		{
			for (int32 BX = 0; BX < BlocksX; ++BX)
			{
				const FVector2D C = LocalBlockCenter(BX, BY);
				Ctx.Sidewalk.Add(BoxTransform(FVector(C.X, C.Y, CurbHeight * 0.5f), FVector(Inner, Inner, CurbHeight)), GroundData);
			}
		}
	}
}

void AMTACityGenerator::BuildBlock(FBuildContext& Ctx, int32 BX, int32 BY, bool bLandmarkBlock) const
{
	const int32 Index = BX + BY * BlocksX;
	const EMTADistrict District = BlockDistricts.IsValidIndex(Index) ? BlockDistricts[Index] : EMTADistrict::Core;
	const FMTADistrictProfile& Profile = Ctx.Profiles[static_cast<int32>(District)];
	const FVector2D Center = LocalBlockCenter(BX, BY);
	FRandomStream Rng(BlockSeed(Seed, BX, BY, 1));

	if (bLandmarkBlock)
	{
		BuildLandmarkTower(Ctx, Center);
		BuildStreetTrees(Ctx, Rng, Center, Profile, District);
		return;
	}
	if (IsPlazaBlock(BX, BY))
	{
		// Reservierter Platz: Landmarke setzt AMTAWorldDetail.
		BuildPlaza(Ctx, Rng, Center, Profile, District);
		return;
	}

	// Landnutzung: Park, Industrie oder Bebauung (Reihenfolge fest, damit der Seed stabil bleibt).
	const float ParkRoll = Rng.FRand();
	const float IndustryRoll = Rng.FRand();
	if (ParkRoll < Profile.ParkChance * ParkShare)
	{
		BuildPark(Ctx, Rng, Center, Profile, District);
		return;
	}
	if (IndustryRoll < Profile.IndustrialChance)
	{
		const int32 FirstLot = Ctx.Lots.Num();
		BuildIndustrialBlock(Ctx, Rng, Center, Profile, District);
		// Blockindex nachtragen (BuildIndustrialBlock kennt nur die Blockmitte).
		for (int32 I = FirstLot; I < Ctx.Lots.Num(); ++I)
		{
			Ctx.Lots[I].Block = FIntPoint(BX, BY);
			Ctx.Lots[I].Seed = BlockSeed(Seed, BX, BY, 200 + I - FirstLot);
		}
		BuildStreetTrees(Ctx, Rng, Center, Profile, District);
		return;
	}

	// Bebaubare Flaeche (Godot: 54 m bei 80 m Periode, 16 m Strasse, 5 m Gehweg).
	const float B = FMath::Max(400.f, BlockPeriod - RoadWidth - 2.f * SidewalkWidth);
	// double, weil FRandomStream::FRandRange in UE 5.8 FVector::FReal liefert (keine Narrowing-Fehler in {}-Init).
	struct FLot { FVector2D C; double W; double D; };
	TArray<FLot, TInlineAllocator<9>> Lots;

	const int32 Split = PickWeighted(Rng, Profile.LotSplitWeights);
	switch (Split)
	{
	case 0: // ein grosses Gebaeude
		Lots.Add({ Center, B * Rng.FRandRange(0.74f, 1.f), B * Rng.FRandRange(0.74f, 1.f) });
		break;
	case 1: // zwei Haelften
		if (Rng.FRand() < 0.5f)
		{
			const float D = B * Rng.FRandRange(0.81f, 1.f);
			Lots.Add({ Center + FVector2D(-0.26f * B, 0.f), 0.48f * B, D });
			Lots.Add({ Center + FVector2D(0.26f * B, 0.f), 0.48f * B, B * Rng.FRandRange(0.81f, 1.f) });
		}
		else
		{
			Lots.Add({ Center + FVector2D(0.f, -0.26f * B), B * Rng.FRandRange(0.81f, 1.f), 0.48f * B });
			Lots.Add({ Center + FVector2D(0.f, 0.26f * B), B * Rng.FRandRange(0.81f, 1.f), 0.48f * B });
		}
		break;
	case 2: // vier Viertel
		for (int32 SX = -1; SX <= 1; SX += 2)
		{
			for (int32 SY = -1; SY <= 1; SY += 2)
			{
				Lots.Add({ Center + FVector2D(SX * 0.26f * B, SY * 0.26f * B), B * Rng.FRandRange(0.41f, 0.48f), B * Rng.FRandRange(0.41f, 0.48f) });
			}
		}
		break;
	default: // 3x3 kleinteilig (Altstadt, Villen)
		for (int32 SX = -1; SX <= 1; ++SX)
		{
			for (int32 SY = -1; SY <= 1; ++SY)
			{
				Lots.Add({ Center + FVector2D(SX * B / 3.f, SY * B / 3.f), B * Rng.FRandRange(0.24f, 0.31f), B * Rng.FRandRange(0.24f, 0.31f) });
			}
		}
		break;
	}

	for (int32 LotIndex = 0; LotIndex < Lots.Num(); ++LotIndex)
	{
		const FLot& Lot = Lots[LotIndex];
		// Zufallswerte immer ziehen (auch bei leeren Parzellen), damit die Folge stabil bleibt.
		const bool bEmpty = Rng.FRand() < Profile.EmptyLotChance;
		const float H = ComputeBuildingHeight(Rng, Lot.C, Profile);
		const FLinearColor Tint = Profile.Palette[Rng.RandHelper(Profile.Palette.Num())];
		FMTACityLot Record = MakeLot(Lot.C, Center, Lot.W, Lot.D, BX, BY, District, LotIndex, Split == 0);
		if (bEmpty)
		{
			// Leere Parzelle: in gruenen Bezirken ein Gartenbaum, sonst Brache.
			if (Profile.StreetTreesPerBlock > 0)
			{
				AddTree(Ctx, Rng, Lot.C, Profile, District, District == EMTADistrict::Sunstrip);
			}
			Record.bEmpty = true;
			Record.Height = 0.f;
			Record.RoofTopZ = CurbHeight;
			Ctx.Lots.Add(Record);
			continue;
		}
		EMTABuildingStyle Style = Profile.PrimaryStyle;
		if (Style == EMTABuildingStyle::Plaster && H > 4500.f)
		{
			Style = EMTABuildingStyle::GlassSteel;
		}
		Record.Height = H;
		Record.Style = Style;
		AddBuilding(Ctx, Rng, Lot.C, Lot.W, Lot.D, H, Tint, Profile, District, Style, CurbHeight, &Record);
		Ctx.Lots.Add(Record);
	}
	BuildStreetTrees(Ctx, Rng, Center, Profile, District);
}

void AMTACityGenerator::AddBuilding(FBuildContext& Ctx, FRandomStream& Rng, const FVector2D& Center, float Width, float Depth, float Height,
	const FLinearColor& Tint, const FMTADistrictProfile& Profile, EMTADistrict District, EMTABuildingStyle Style, float ZBase,
	FMTACityLot* OutLot) const
{
	// Bezirks-Look: Neon-Ladenband nur bei einem Teil der Gebaeude, Staerke variiert.
	const float NeonRoll = Rng.FRand();
	const float Neon = (NeonRoll < Profile.NeonShare && Style != EMTABuildingStyle::Villa) ? Rng.FRandRange(0.5f, 1.f) : 0.f;
	const float Detail = FMath::Clamp(Profile.FacadeDetail + Rng.FRandRange(-0.15f, 0.15f), 0.f, 1.f);

	auto PushBox = [&](const FVector2D& C, float W, float D, float H, float Z0, const FLinearColor& Color, float TotalHeight, float BoxNeon)
	{
		float Data[MTABuildingData::Num];
		FillBuildingData(Data, Color, TotalHeight, static_cast<float>(Rng.RandRange(0, 9999)),
			FMath::Clamp(Profile.Wear + Rng.FRandRange(-0.1f, 0.1f), 0.f, 1.f), Profile.LitWindowRatio, Style, District,
			BoxNeon, Detail, Profile.AccentColor);
		Ctx.Building.Add(BoxTransform(FVector(C.X, C.Y, Z0 + H * 0.5f), FVector(W, D, H)), Data);
	};

	PushBox(Center, Width, Depth, Height, ZBase, Tint, Height, Neon);
	const float Top = ZBase + Height;
	float RoofTop = Top;
	bool bFlat = true;

	const bool bTower = Height > 3800.f && Style != EMTABuildingStyle::Industrial && Style != EMTABuildingStyle::Villa;
	if (bTower)
	{
		// Zurueckgesetztes Obergeschoss + Antenne (wie im Godot-Prototyp).
		const bool bSetback = Profile.RoofStyle == EMTARoofStyle::Setback;
		const float TierH = FMath::Max(FloorHeight, FMath::RoundToFloat(Height * 0.22f / FloorHeight) * FloorHeight);
		PushBox(Center, Width * 0.7f, Depth * 0.7f, TierH, Top, Darken(Tint, 0.92f), Height + TierH, 0.f);
		float CrownTop = Top + TierH;
		if (bSetback && Height > 6000.f)
		{
			// Core: dritte, schmale Stufe -> unverwechselbare Treppen-Silhouette.
			const float Tier2 = FMath::Max(FloorHeight, FMath::RoundToFloat(Height * 0.1f / FloorHeight) * FloorHeight);
			PushBox(Center, Width * 0.45f, Depth * 0.45f, Tier2, CrownTop, Darken(Tint, 0.85f), Height + TierH + Tier2, 0.f);
			CrownTop += Tier2;
		}
		if (bSetback)
		{
			// Lichtkrone: schmales emissives Band unter der Dachkante der obersten Stufe (LampHeads-Material, nachts an).
			const float Inset = Height > 6000.f ? 0.45f : 0.7f;
			const float CrownData[MTAPropData::Num] = { Profile.AccentColor.R, Profile.AccentColor.G, Profile.AccentColor.B,
				static_cast<float>(static_cast<uint8>(District)), 0.f };
			Ctx.Head.Add(BoxTransform(FVector(Center.X, Center.Y, CrownTop - 90.f), FVector(Width * Inset + 16.f, Depth * Inset + 16.f, 60.f)), CrownData);
		}
		const float AntennaH = Rng.FRandRange(800.f, 1600.f);
		Ctx.Antenna.Add(FTransform(FRotator::ZeroRotator, FVector(Center.X, Center.Y, CrownTop + AntennaH * 0.5f),
			FVector(0.45f, 0.45f, AntennaH / ShapeSize)));
		RoofTop = CrownTop + AntennaH;
	}
	else
	{
		const float Wear = Profile.Wear;
		const bool bGable = Style == EMTABuildingStyle::Villa
			|| (Profile.RoofStyle == EMTARoofStyle::Gable && Style == EMTABuildingStyle::Plaster && Height <= 2500.f
				&& Rng.FRand() < Profile.PitchedRoofChance);
		const bool bSaw = Style == EMTABuildingStyle::Industrial && Profile.RoofStyle == EMTARoofStyle::Sawtooth;
		const float Shade = Rng.FRandRange(0.85f, 1.1f);
		const FLinearColor RoofCol = Darken(Profile.RoofColor, Shade);
		if (bGable || bSaw)
		{
			AddGableRoof(Ctx, Center, Width, Depth, Top, RoofCol, District, Wear, bSaw);
			RoofTop = bSaw ? Top + FMath::Max(Width, Depth) / FMath::Clamp(FMath::RoundToFloat(FMath::Max(Width, Depth) / 700.f), 2.f, 8.f) * 0.52f
			               : Top + FMath::Min(Width, Depth) * 0.52f;
			bFlat = false;
		}
		else
		{
			// Klimageraet/Aufbau auf dem Dach.
			const FVector2D Offset(Width * Rng.FRandRange(-0.25f, 0.25f), Depth * Rng.FRandRange(-0.25f, 0.25f));
			Ctx.Roof.Add(BoxTransform(FVector(Center.X + Offset.X, Center.Y + Offset.Y, Top + 80.f), FVector(Width * 0.25f, Depth * 0.2f, 160.f)));
			if (Style == EMTABuildingStyle::Plaster && Rng.FRand() < 0.35f)
			{
				// Wassertank auf aelteren Wohnhaeusern (Zylinder).
				Ctx.Chimney.Add(FTransform(FRotator::ZeroRotator, FVector(Center.X - Offset.X, Center.Y - Offset.Y, Top + 150.f), FVector(2.4f, 2.4f, 3.f)));
			}
			// Dach-Kleinkram je Bezirk: 0-3 Antennenstaebe.
			const int32 Sticks = FMath::FloorToInt(Profile.RooftopClutter * 3.f * Rng.FRand() + 0.3f);
			for (int32 I = 0; I < Sticks; ++I)
			{
				const float AH = Rng.FRandRange(250.f, 700.f);
				const FVector2D P(Center.X + Width * Rng.FRandRange(-0.4f, 0.4f), Center.Y + Depth * Rng.FRandRange(-0.4f, 0.4f));
				Ctx.Antenna.Add(FTransform(FRotator::ZeroRotator, FVector(P.X, P.Y, Top + AH * 0.5f), FVector(0.12f, 0.12f, AH / ShapeSize)));
			}
			RoofTop = Top + 160.f;
		}
	}

	if (OutLot)
	{
		OutLot->RoofTopZ = RoofTop;
		OutLot->bFlatRoof = bFlat;
		OutLot->bTower = bTower;
		OutLot->bIndustrial = Style == EMTABuildingStyle::Industrial;
		OutLot->Style = Style;
		OutLot->Height = Height;
	}
}

void AMTACityGenerator::AddGableRoof(FBuildContext& Ctx, const FVector2D& Center, float Width, float Depth, float Top, const FLinearColor& Color,
	EMTADistrict District, float Wear, bool bSawtooth) const
{
	// Ein um 45 Grad um die Firstachse gerollter Wuerfel ergibt im Querschnitt eine Raute; die untere Haelfte liegt
	// im Gebaeude, sichtbar bleibt ein 45-Grad-Satteldach (TRS-Transformationen koennen nicht scheren).
	const float Data[MTAPropData::Num] = { Color.R, Color.G, Color.B, static_cast<float>(static_cast<uint8>(District)), Wear };
	const bool bAlongX = Width >= Depth;
	const float Long = bAlongX ? Width : Depth;
	const float Span = bAlongX ? Depth : Width;
	if (!bSawtooth)
	{
		// First entlang der langen Seite, 4 % Ueberstand.
		const float S = Span / Sqrt2 * 1.04f;
		const FRotator Rot(0.f, bAlongX ? 0.f : 90.f, 45.f);
		Ctx.Pitched.Add(FTransform(Rot, FVector(Center.X, Center.Y, Top), FVector(Long * 1.02f, S, S) / ShapeSize), Data);
		return;
	}
	// Sheddach: mehrere kleine Firste quer zur langen Seite.
	const int32 N = FMath::Clamp(FMath::RoundToInt(Long / 700.f), 2, 8);
	const float Step = Long / N;
	const float S = Step / Sqrt2;
	for (int32 I = 0; I < N; ++I)
	{
		const float Along = -Long * 0.5f + Step * (I + 0.5f);
		const FVector2D P = bAlongX ? Center + FVector2D(Along, 0.f) : Center + FVector2D(0.f, Along);
		// First quer zur langen Seite: bei bAlongX verlaeuft er entlang Y.
		const FRotator Rot(0.f, bAlongX ? 90.f : 0.f, 45.f);
		Ctx.Pitched.Add(FTransform(Rot, FVector(P.X, P.Y, Top), FVector(Span, S, S) / ShapeSize), Data);
	}
}

FMTACityLot AMTACityGenerator::MakeLot(const FVector2D& LotCenter, const FVector2D& BlockCenter, double W, double D, int32 BX, int32 BY,
	EMTADistrict District, int32 LotIndex, bool bSingleLot) const
{
	FMTACityLot L;
	L.Center = LotCenter;
	L.Size = FVector2D(W, D);
	L.BaseZ = CurbHeight;
	L.RoofTopZ = CurbHeight;
	L.District = District;
	L.Block = FIntPoint(BX, BY);
	L.Seed = BlockSeed(Seed, BX, BY, 100 + LotIndex);

	// Strassenfront: Achsen, auf denen die Parzelle aus der Blockmitte verschoben ist, zeigen zur Strasse.
	const FVector2D Off = LotCenter - BlockCenter;
	const bool bX = FMath::Abs(Off.X) > 1.0;
	const bool bY = FMath::Abs(Off.Y) > 1.0;
	const FVector2D DirX(Off.X >= 0.0 ? 1.0 : -1.0, 0.0);
	const FVector2D DirY(0.0, Off.Y >= 0.0 ? 1.0 : -1.0);
	if (bX && bY)
	{
		L.StreetDir = FMath::Abs(Off.X) >= FMath::Abs(Off.Y) ? DirX : DirY;
		L.SecondStreetDir = FMath::Abs(Off.X) >= FMath::Abs(Off.Y) ? DirY : DirX;
	}
	else if (bX)
	{
		L.StreetDir = DirX;
	}
	else if (bY)
	{
		L.StreetDir = DirY;
	}
	else if (bSingleLot)
	{
		// Ein Gebaeude fuellt den Block: Front deterministisch je Block, alle Seiten liegen an Strassen.
		const bool bUseX = ((BX + BY) & 1) == 0;
		const double Sign = (BX & 1) == 0 ? 1.0 : -1.0;
		L.StreetDir = bUseX ? FVector2D(Sign, 0.0) : FVector2D(0.0, Sign);
		L.SecondStreetDir = bUseX ? FVector2D(0.0, Sign) : FVector2D(Sign, 0.0);
	}
	else
	{
		L.bHasFrontage = false; // Mitte eines 3x3-Blocks
	}
	return L;
}

void AMTACityGenerator::BuildPark(FBuildContext& Ctx, FRandomStream& Rng, const FVector2D& Center, const FMTADistrictProfile& Profile, EMTADistrict District) const
{
	const float Inner = BlockPeriod - RoadWidth;
	const float LawnSize = FMath::Max(200.f, Inner - 2.f * FMath::Min(SidewalkWidth, 400.f));
	const float Data[MTAPropData::Num] = { Profile.FoliageColor.R, Profile.FoliageColor.G, Profile.FoliageColor.B,
		static_cast<float>(static_cast<uint8>(District)), Profile.Wear };
	// Rasen liegt 10 cm dick auf der Gehwegplatte.
	Ctx.Park.Add(BoxTransform(FVector(Center.X, Center.Y, CurbHeight + 5.f), FVector(LawnSize, LawnSize, 10.f)), Data);

	const float Range = LawnSize * 0.5f - 300.f;
	for (int32 I = 0; I < TreesPerPark; ++I)
	{
		const FVector2D P = Center + FVector2D(Rng.FRandRange(-Range, Range), Rng.FRandRange(-Range, Range));
		AddTree(Ctx, Rng, P, Profile, District, District == EMTADistrict::Sunstrip);
	}
}

void AMTACityGenerator::BuildIndustrialBlock(FBuildContext& Ctx, FRandomStream& Rng, const FVector2D& Center, const FMTADistrictProfile& Profile, EMTADistrict District) const
{
	const float B = FMath::Max(400.f, BlockPeriod - RoadWidth - 2.f * SidewalkWidth);
	const bool bAlongX = Rng.FRand() < 0.5f;
	const int32 Halls = Rng.FRand() < 0.5f ? 1 : 2;

	// Hallen auf einer Blockseite, Hof mit Schornsteinen/Tanks auf der anderen.
	const float HallDepth = B * Rng.FRandRange(0.5f, 0.65f);
	const float HallOffset = (B - HallDepth) * 0.5f;
	for (int32 I = 0; I < Halls; ++I)
	{
		const float Span = B / Halls;
		const float Along = -B * 0.5f + Span * (I + 0.5f);
		const FVector2D C = bAlongX ? Center + FVector2D(Along, -HallOffset) : Center + FVector2D(-HallOffset, Along);
		const float W = bAlongX ? Span * 0.94f : HallDepth;
		const float D = bAlongX ? HallDepth : Span * 0.94f;
		const float H = FMath::RoundToFloat(Rng.FRandRange(800.f, 1500.f) / 100.f) * 100.f;
		const FLinearColor Tint = Profile.Palette[Rng.RandHelper(Profile.Palette.Num())];
		FMTACityLot Record = MakeLot(C, Center, W, D, 0, 0, District, I, false);
		AddBuilding(Ctx, Rng, C, W, D, H, Tint, Profile, District, EMTABuildingStyle::Industrial, CurbHeight, &Record);
		Ctx.Lots.Add(Record);
	}

	// Hof: 1-3 Schornsteine und 0-2 Tanks.
	const float YardDepth = B - HallDepth;
	const FVector2D YardCenter = bAlongX ? Center + FVector2D(0.f, HallDepth * 0.5f) : Center + FVector2D(HallDepth * 0.5f, 0.f);
	const int32 Stacks = Rng.RandRange(1, 3);
	for (int32 I = 0; I < Stacks; ++I)
	{
		const float Diameter = Rng.FRandRange(250.f, 450.f);
		const float H = Rng.FRandRange(2500.f, 5000.f);
		const FVector2D Jitter(Rng.FRandRange(-0.4f, 0.4f) * B, Rng.FRandRange(-0.3f, 0.3f) * YardDepth);
		const FVector2D P = YardCenter + (bAlongX ? Jitter : FVector2D(Jitter.Y, Jitter.X));
		Ctx.Chimney.Add(FTransform(FRotator::ZeroRotator, FVector(P.X, P.Y, CurbHeight + H * 0.5f), FVector(Diameter, Diameter, H) / ShapeSize));
	}
	const int32 Tanks = Rng.RandRange(0, 2);
	for (int32 I = 0; I < Tanks; ++I)
	{
		const float Diameter = FMath::Min(Rng.FRandRange(800.f, 1300.f), YardDepth * 0.8f);
		const float H = Rng.FRandRange(600.f, 1200.f);
		const float Along = (I == 0 ? -0.25f : 0.25f) * B;
		const FVector2D P = YardCenter + (bAlongX ? FVector2D(Along, 0.f) : FVector2D(0.f, Along));
		Ctx.Chimney.Add(FTransform(FRotator::ZeroRotator, FVector(P.X, P.Y, CurbHeight + H * 0.5f), FVector(Diameter, Diameter, H) / ShapeSize));
	}
}

void AMTACityGenerator::BuildLandmarkTower(FBuildContext& Ctx, const FVector2D& Center) const
{
	// Aurelia Tower: 190 m, drei Stufen, LED-Krone (emissiv in LampHeads), Spitze als Antenne.
	const FMTADistrictProfile& Core = Ctx.Profiles[static_cast<int32>(EMTADistrict::Core)];
	const FLinearColor Tint = Core.LeadColor;
	const float Foot = FMath::Min(3800.f, BlockPeriod - RoadWidth - 2.f * SidewalkWidth);

	auto Push = [&](float W, float H, float Z0, float Factor)
	{
		float Data[MTABuildingData::Num];
		const FLinearColor C = Darken(Tint, Factor);
		FillBuildingData(Data, C, 19000.f, 4242.f, 0.f, 0.75f, EMTABuildingStyle::GlassSteel, EMTADistrict::Core,
			0.f, 1.f, Core.AccentColor);
		Ctx.Building.Add(BoxTransform(FVector(Center.X, Center.Y, Z0 + H * 0.5f), FVector(W, W, H)), Data);
	};

	const float Z0 = CurbHeight;
	Push(Foot, 12000.f, Z0, 1.f);
	Push(Foot * 0.74f, 5000.f, Z0 + 12000.f, 0.95f);
	Push(Foot * 0.48f, 1700.f, Z0 + 17000.f, 0.9f);

	// LED-Krone knapp unter der Spitze, leicht groesser als die oberste Stufe.
	const float CrownData[MTAPropData::Num] = { 0.55f, 0.8f, 1.f, static_cast<float>(static_cast<uint8>(EMTADistrict::Core)), 0.f };
	Ctx.Head.Add(BoxTransform(FVector(Center.X, Center.Y, Z0 + 18500.f), FVector(Foot * 0.52f, Foot * 0.52f, 250.f)), CrownData);

	// Spitze bis 190 m + Mast.
	Ctx.Antenna.Add(FTransform(FRotator::ZeroRotator, FVector(Center.X, Center.Y, Z0 + 18700.f + 1150.f), FVector(0.8f, 0.8f, 23.f)));
}

void AMTACityGenerator::AddTree(FBuildContext& Ctx, FRandomStream& Rng, const FVector2D& Pos, const FMTADistrictProfile& Profile, EMTADistrict District, bool bPalm) const
{
	const float S = Rng.FRandRange(0.8f, 1.4f);
	const float Z0 = CurbHeight;
	const float Shade = Rng.FRandRange(0.85f, 1.1f);
	const float Data[MTAPropData::Num] = { Profile.FoliageColor.R * Shade, Profile.FoliageColor.G * Shade, Profile.FoliageColor.B * Shade,
		static_cast<float>(static_cast<uint8>(District)), Profile.Wear };

	if (bPalm)
	{
		// Palme: hoher, schlanker Stamm, flache Krone.
		const float TrunkH = 700.f * S;
		Ctx.Trunk.Add(FTransform(FRotator::ZeroRotator, FVector(Pos.X, Pos.Y, Z0 + TrunkH * 0.5f), FVector(0.35f * S, 0.35f * S, TrunkH / ShapeSize)));
		Ctx.Crown.Add(FTransform(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), FVector(Pos.X, Pos.Y, Z0 + TrunkH + 60.f * S),
			FVector(4.5f * S, 4.5f * S, 1.6f * S)), Data);
	}
	else
	{
		// Laubbaum: Godot-Masse (Stamm 3 m, Krone Radius 2,8 m) mal S.
		Ctx.Trunk.Add(FTransform(FRotator::ZeroRotator, FVector(Pos.X, Pos.Y, Z0 + 150.f * S), FVector(0.75f * S, 0.75f * S, 3.f * S)));
		Ctx.Crown.Add(FTransform(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), FVector(Pos.X, Pos.Y, Z0 + 520.f * S),
			FVector(5.6f * S, 5.6f * S, 5.0f * S)), Data);
	}
}

void AMTACityGenerator::BuildStreetTrees(FBuildContext& Ctx, FRandomStream& Rng, const FVector2D& Center, const FMTADistrictProfile& Profile, EMTADistrict District) const
{
	const int32 Count = Profile.StreetTreesPerBlock;
	if (Count <= 0)
	{
		return;
	}
	// Baumreihe auf dem Gehweg, 200 cm vom Bordstein; Blockmitte bleibt fuer die Laterne frei.
	const float Edge = (BlockPeriod - RoadWidth) * 0.5f - 200.f;
	const int32 PerSide = FMath::Max(1, FMath::DivideAndRoundUp(Count, 4));
	int32 Placed = 0;
	for (int32 Side = 0; Side < 4 && Placed < Count; ++Side)
	{
		for (int32 I = 0; I < PerSide && Placed < Count; ++I)
		{
			float T = (I + 0.5f) / PerSide * 2.f - 1.f; // -1..1
			if (FMath::Abs(T) < 0.15f)
			{
				T = T < 0.f ? -0.3f : 0.3f;
			}
			const float Along = T * (Edge - 400.f);
			FVector2D P;
			switch (Side)
			{
			case 0: P = Center + FVector2D(Along, -Edge); break;
			case 1: P = Center + FVector2D(Along, Edge); break;
			case 2: P = Center + FVector2D(-Edge, Along); break;
			default: P = Center + FVector2D(Edge, Along); break;
			}
			AddTree(Ctx, Rng, P, Profile, District, District == EMTADistrict::Sunstrip);
			++Placed;
		}
	}
}

void AMTACityGenerator::BuildLamps(FBuildContext& Ctx)
{
	LampPositions.Reset();
	LampColors.Reset();

	// Der Generator wird nur verschoben, nicht gedreht/skaliert (Raster ist achsparallel).
	const FVector Origin = GetActorLocation();
	const float HalfRoad = RoadWidth * 0.5f;
	const float SideOffset = HalfRoad + 100.f;   // Godot: 9 m bei 8 m halber Strasse
	const float CornerOffset = HalfRoad + 150.f; // Godot: 9,5 m
	FRandomStream Rng(BlockSeed(Seed, -1, -1, 7));

	auto AddLamp = [&](const FVector2D& Foot, const FVector2D& TowardRoad, bool bForce)
	{
		const EMTADistrict District = GetDistrictAtLocation(Origin + FVector(Foot.X, Foot.Y, 0.f));
		const FMTADistrictProfile& Profile = Ctx.Profiles[static_cast<int32>(District)];
		const float Roll = Rng.FRand();
		if (!bForce && Roll > Profile.LampDensity)
		{
			return;
		}
		// Fusspunkt liegt auf dem Gehweg, ausserhalb der Stadt auf dem Boden.
		const float LocalHalfX = BlocksX * BlockPeriod * 0.5f;
		const float LocalHalfY = BlocksY * BlockPeriod * 0.5f;
		const bool bInside = FMath::Abs(Foot.X) < LocalHalfX && FMath::Abs(Foot.Y) < LocalHalfY;
		const float Z0 = bInside ? CurbHeight : 0.f;

		Ctx.Pole.Add(FTransform(FRotator::ZeroRotator, FVector(Foot.X, Foot.Y, Z0 + 350.f), FVector(0.25f, 0.25f, 7.f)));

		// Leuchtenkopf 60 cm Richtung Fahrbahn, Laengsseite quer zur Strasse.
		const FVector2D Dir = TowardRoad.GetSafeNormal();
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
		const FVector2D HeadPos = Foot + Dir * 60.f;
		const float Data[MTAPropData::Num] = { Profile.LampColor.R, Profile.LampColor.G, Profile.LampColor.B,
			static_cast<float>(static_cast<uint8>(District)), Profile.Wear };
		Ctx.Head.Add(BoxTransform(FVector(HeadPos.X, HeadPos.Y, Z0 + 710.f), FVector(90.f, 50.f, 18.f), Yaw), Data);

		LampPositions.Add(Origin + FVector(HeadPos.X, HeadPos.Y, Z0));
		LampColors.Add(Profile.LampColor);
	};

	// Laternen in Segmentmitte beidseits jeder Strasse (Nord-Sued-Strassen: konstantes X).
	for (int32 K = 0; K <= BlocksX; ++K)
	{
		const float RX = LocalRoadCenter(0, K);
		for (int32 J = 0; J < BlocksY; ++J)
		{
			const float Mid = LocalRoadCenter(1, J) + BlockPeriod * 0.5f;
			for (int32 S = -1; S <= 1; S += 2)
			{
				AddLamp(FVector2D(RX + S * SideOffset, Mid), FVector2D(-S, 0.f), false);
			}
		}
	}
	// Ost-West-Strassen: konstantes Y.
	for (int32 K = 0; K <= BlocksY; ++K)
	{
		const float RY = LocalRoadCenter(1, K);
		for (int32 I = 0; I < BlocksX; ++I)
		{
			const float Mid = LocalRoadCenter(0, I) + BlockPeriod * 0.5f;
			for (int32 S = -1; S <= 1; S += 2)
			{
				AddLamp(FVector2D(Mid, RY + S * SideOffset), FVector2D(0.f, -S), false);
			}
		}
	}
	// Kreuzungen: vier Ecklaternen, Kopf zeigt diagonal auf die Kreuzung (immer gesetzt).
	for (int32 IX = 0; IX <= BlocksX; ++IX)
	{
		for (int32 IY = 0; IY <= BlocksY; ++IY)
		{
			const FVector2D C(LocalRoadCenter(0, IX), LocalRoadCenter(1, IY));
			for (int32 SX = -1; SX <= 1; SX += 2)
			{
				for (int32 SY = -1; SY <= 1; SY += 2)
				{
					AddLamp(C + FVector2D(SX * CornerOffset, SY * CornerOffset), FVector2D(-SX, -SY), true);
				}
			}
		}
	}
}

// ============================================================================ Laternenlichter

void AMTACityGenerator::EnsureLampLightPool()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || LampLightPoolSize <= 0 || LampLightPool.Num() == LampLightPoolSize)
	{
		return;
	}
	DestroyLampLightPool();
	for (int32 I = 0; I < LampLightPoolSize; ++I)
	{
		USpotLightComponent* Light = NewObject<USpotLightComponent>(this, NAME_None, RF_Transient);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetupAttachment(SceneRoot);
		Light->SetUsingAbsoluteLocation(true);
		Light->SetUsingAbsoluteRotation(true);
		Light->RegisterComponent();
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(0.f);
		Light->SetAttenuationRadius(LampLightRadius);
		Light->SetInnerConeAngle(35.f);
		Light->SetOuterConeAngle(70.f);
		Light->SetCastShadows(false);              // ohne Schatten: 12 Lichter kosten auf der 5700 XT kaum etwas
		Light->SetVolumetricScatteringIntensity(1.5f); // sichtbarer Lichtkegel im volumetrischen Nebel
		Light->SetWorldRotation(FRotator(-90.f, 0.f, 0.f));
		Light->SetVisibility(false);
		LampLightPool.Add(Light);
	}
}

void AMTACityGenerator::DestroyLampLightPool()
{
	for (USpotLightComponent* Light : LampLightPool)
	{
		if (IsValid(Light))
		{
			Light->DestroyComponent();
		}
	}
	LampLightPool.Reset();
}

void AMTACityGenerator::UpdateLampLights(const FVector& ViewLocation, float NightAlpha)
{
	NightAlpha = FMath::Clamp(NightAlpha, 0.f, 1.f);
	EnsureLampLightPool();
	if (LampLightPool.Num() == 0 || LampPositions.Num() == 0)
	{
		return;
	}

	const bool bMoved = FVector::DistSquared(ViewLocation, LastLampViewLocation) > FMath::Square(300.f);
	const bool bNightChanged = FMath::Abs(NightAlpha - LastNightAlpha) > 0.01f;
	if (!bMoved && !bNightChanged)
	{
		return;
	}
	LastNightAlpha = NightAlpha;

	if (NightAlpha <= 0.01f)
	{
		for (USpotLightComponent* Light : LampLightPool)
		{
			Light->SetVisibility(false);
		}
		return;
	}

	if (bMoved)
	{
		LastLampViewLocation = ViewLocation;
		// Naechste Laternen bestimmen (wenige hundert Eintraege, Sortierung alle 0,25 s ist billig).
		TArray<int32> Order;
		Order.Reserve(LampPositions.Num());
		for (int32 I = 0; I < LampPositions.Num(); ++I)
		{
			Order.Add(I);
		}
		Order.Sort([this, &ViewLocation](int32 A, int32 B)
		{
			return FVector::DistSquared2D(LampPositions[A], ViewLocation) < FVector::DistSquared2D(LampPositions[B], ViewLocation);
		});
		for (int32 I = 0; I < LampLightPool.Num(); ++I)
		{
			USpotLightComponent* Light = LampLightPool[I];
			if (!Order.IsValidIndex(I))
			{
				Light->SetVisibility(false);
				continue;
			}
			const int32 Lamp = Order[I];
			Light->SetWorldLocation(LampPositions[Lamp] + FVector(0.f, 0.f, 690.f));
			Light->SetLightColor(LampColors.IsValidIndex(Lamp) ? LampColors[Lamp] : FLinearColor(1.f, 0.8f, 0.55f));
		}
	}

	for (USpotLightComponent* Light : LampLightPool)
	{
		Light->SetIntensity(LampLightIntensity * NightAlpha);
		Light->SetVisibility(true);
	}
}

// ============================================================================ Abfragen (Strassennetz)

float AMTACityGenerator::GetRoadCenter(int32 Index)
{
	return MTACity::RoadCenter(Index);
}

void AMTACityGenerator::GetCityConstants(int32& OutBlocks, float& OutPeriod, float& OutRoadWidth, float& OutLaneOffset)
{
	OutBlocks = MTACity::Blocks;
	OutPeriod = MTACity::Period;
	OutRoadWidth = MTACity::RoadWidth;
	OutLaneOffset = MTACity::LaneOffset;
}

AMTACityGenerator* AMTACityGenerator::FindCityGenerator(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<AMTACityGenerator> It(World);
	return It ? *It : nullptr;
}

int32 AMTACityGenerator::GetRoadCount(int32 Axis) const
{
	return (Axis == 0 ? BlocksX : BlocksY) + 1;
}

float AMTACityGenerator::GetRoadCoordinate(int32 Axis, int32 Index) const
{
	const FVector Origin = GetActorLocation();
	return LocalRoadCenter(Axis, Index) + static_cast<float>(Axis == 0 ? Origin.X : Origin.Y);
}

FVector AMTACityGenerator::GetIntersectionLocation(int32 IX, int32 IY) const
{
	return GetActorLocation() + FVector(LocalRoadCenter(0, IX), LocalRoadCenter(1, IY), 0.f);
}

FVector AMTACityGenerator::GetBlockCenterLocation(int32 BX, int32 BY) const
{
	const FVector2D C = LocalBlockCenter(BX, BY);
	return GetActorLocation() + FVector(C.X, C.Y, CurbHeight);
}

float AMTACityGenerator::GetLaneCoordinate(int32 RunAxis, int32 RoadIndex, int32 Direction, int32 LaneIndex) const
{
	// Rechtsverkehr im linkshaendigen Unreal-System (Z oben): Fahrt in +X -> rechte Seite ist +Y,
	// Fahrt in +Y -> rechte Seite ist -X.
	const float Dir = Direction >= 0 ? 1.f : -1.f;
	const float Offset = MTACity::LaneOffset * (RoadWidth / MTACity::RoadWidth) + LaneIndex * 400.f;
	if (RunAxis == 0)
	{
		// Strasse entlang X liegt bei konstantem Y (Strassenindex auf der Y-Achse).
		return GetRoadCoordinate(1, RoadIndex) + Dir * Offset;
	}
	return GetRoadCoordinate(0, RoadIndex) - Dir * Offset;
}

int32 AMTACityGenerator::GetNearestRoadIndex(int32 Axis, float WorldCoordinate) const
{
	const FVector Origin = GetActorLocation();
	const float Local = WorldCoordinate - static_cast<float>(Axis == 0 ? Origin.X : Origin.Y);
	const int32 Count = Axis == 0 ? BlocksX : BlocksY;
	const float FromStart = Local + Count * BlockPeriod * 0.5f;
	return FMath::Clamp(FMath::RoundToInt(FromStart / BlockPeriod), 0, Count);
}

bool AMTACityGenerator::IsOnRoad(const FVector& WorldLocation) const
{
	const FVector Local = WorldLocation - GetActorLocation();
	const float HalfRoad = RoadWidth * 0.5f;
	const float HalfX = BlocksX * BlockPeriod * 0.5f + HalfRoad;
	const float HalfY = BlocksY * BlockPeriod * 0.5f + HalfRoad;
	if (FMath::Abs(Local.X) > HalfX || FMath::Abs(Local.Y) > HalfY)
	{
		return false;
	}
	const int32 IX = GetNearestRoadIndex(0, WorldLocation.X);
	const int32 IY = GetNearestRoadIndex(1, WorldLocation.Y);
	return FMath::Abs(Local.X - LocalRoadCenter(0, IX)) <= HalfRoad || FMath::Abs(Local.Y - LocalRoadCenter(1, IY)) <= HalfRoad;
}

EMTADistrict AMTACityGenerator::GetBlockDistrict(int32 BX, int32 BY) const
{
	BX = FMath::Clamp(BX, 0, BlocksX - 1);
	BY = FMath::Clamp(BY, 0, BlocksY - 1);
	const int32 Index = BX + BY * BlocksX;
	return BlockDistricts.IsValidIndex(Index) ? BlockDistricts[Index] : ComputeDistrict(BX, BY);
}

EMTADistrict AMTACityGenerator::GetDistrictAtLocation(const FVector& WorldLocation) const
{
	const FVector Local = WorldLocation - GetActorLocation();
	const int32 BX = FMath::FloorToInt((Local.X + BlocksX * BlockPeriod * 0.5f) / BlockPeriod);
	const int32 BY = FMath::FloorToInt((Local.Y + BlocksY * BlockPeriod * 0.5f) / BlockPeriod);
	return GetBlockDistrict(BX, BY);
}
