#include "MTAWildernessBuilder.h"

#include "MTACityGenerator.h"
#include "MTADiscoverySubsystem.h"
#include "MTAWorldDetail.h"
#include "ProceduralMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "MTAWilderness"

DEFINE_LOG_CATEGORY_STATIC(LogMTAWilderness, Log, All);

namespace
{
	// ------------------------------------------------------------------ Ebenen, Meshes, Materialien

	namespace WL
	{
		enum : int32
		{
			TreeTrunk, CrownSphere, CrownCone, Undergrowth, RockBlock, Boulder, Log, Earth, WoodPole,
			Metal, MetalCyl, Cloth, Glow, Water, Prints, Road, Count
		};
	}

	namespace WMesh
	{
		enum : int32 { Cube, Cylinder, Sphere, Cone };
	}

	namespace WMat
	{
		enum : int32 { Trunk, Foliage, Mountain, Concrete, Metal, Sign, Neon, Water, Road, Count };
	}

	/** Von tools/ue/build_materials.py erzeugte Materialien (gleiche Custom-Data-Belegung wie AMTAWorldDetail). */
	const TCHAR* const WildMaterialPaths[WMat::Count] = {
		TEXT("/Game/MTA6/Materials/M_MTA_TreeTrunk.M_MTA_TreeTrunk"),
		TEXT("/Game/MTA6/Materials/M_MTA_Foliage.M_MTA_Foliage"),
		TEXT("/Game/MTA6/Materials/M_MTA_Mountain.M_MTA_Mountain"),
		TEXT("/Game/MTA6/Materials/M_MTA_Concrete.M_MTA_Concrete"),
		TEXT("/Game/MTA6/Materials/M_MTA_PaintedMetal.M_MTA_PaintedMetal"),
		TEXT("/Game/MTA6/Materials/M_MTA_Signage.M_MTA_Signage"),
		TEXT("/Game/MTA6/Materials/M_MTA_Neon.M_MTA_Neon"),
		TEXT("/Game/MTA6/Materials/M_MTA_Water.M_MTA_Water"),
		TEXT("/Game/MTA6/Materials/M_MTA_Road.M_MTA_Road"),
	};

	struct FWildLayerSpec
	{
		const TCHAR* LayerName;
		int32 Mesh;
		int32 Mat;
		int32 Collision; // 0 keine, 1 nur Abfrage, 2 voll
		bool bShadow;
		int32 CullEnd;   // cm, 0 = nie ausblenden
	};

	const FWildLayerSpec& LayerSpec(int32 L)
	{
		static const FWildLayerSpec Specs[] = {
			{ TEXT("WildTreeTrunks"),   WMesh::Cylinder, WMat::Trunk,    2, true,  30000 },
			{ TEXT("WildCrownSpheres"), WMesh::Sphere,   WMat::Foliage,  0, true,  60000 },
			{ TEXT("WildCrownCones"),   WMesh::Cone,     WMat::Foliage,  0, true,  60000 },
			{ TEXT("WildUndergrowth"),  WMesh::Sphere,   WMat::Foliage,  0, false, 9000  },
			{ TEXT("WildRockBlocks"),   WMesh::Cube,     WMat::Mountain, 2, true,  0     },
			{ TEXT("WildBoulders"),     WMesh::Sphere,   WMat::Mountain, 2, true,  25000 },
			{ TEXT("WildLogs"),         WMesh::Cylinder, WMat::Trunk,    2, true,  15000 },
			{ TEXT("WildEarth"),        WMesh::Cube,     WMat::Concrete, 2, true,  0     },
			{ TEXT("WildWoodPoles"),    WMesh::Cylinder, WMat::Concrete, 2, true,  20000 },
			{ TEXT("WildMetal"),        WMesh::Cube,     WMat::Metal,    2, true,  20000 },
			{ TEXT("WildMetalCyl"),     WMesh::Cylinder, WMat::Metal,    2, true,  15000 },
			{ TEXT("WildCloth"),        WMesh::Cube,     WMat::Sign,     1, false, 15000 },
			{ TEXT("WildGlow"),         WMesh::Sphere,   WMat::Neon,     0, false, 30000 },
			{ TEXT("WildWater"),        WMesh::Cube,     WMat::Water,    0, false, 0     },
			{ TEXT("WildPrints"),       WMesh::Sphere,   WMat::Concrete, 0, false, 4000  },
			{ TEXT("WildRoad"),         WMesh::Cube,     WMat::Road,     2, false, 0     },
		};
		static_assert(static_cast<int32>(UE_ARRAY_COUNT(Specs)) == static_cast<int32>(WL::Count), "Ebenentabelle passt nicht zu WL");
		return Specs[FMath::Clamp(L, 0, static_cast<int32>(WL::Count) - 1)];
	}

	constexpr int32 NumWildData = 7;

	/** Custom-Data einer Instanz (7 Werte, Belegung je Material wie in build_materials.py). */
	struct FWD
	{
		float V[NumWildData] = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
	};

	FWD DColor(const FLinearColor& C) { FWD D; D.V[0] = C.R; D.V[1] = C.G; D.V[2] = C.B; return D; }
	/** M_MTA_Mountain: Toenung, keine Schneegrenze, kein Dunst. */
	FWD DRock(const FLinearColor& C) { FWD D = DColor(C); D.V[3] = 9999.f; D.V[4] = 0.f; return D; }
	/** M_MTA_Concrete: Muster 0 glatt, 1 Sand/Schotter, 3 Planken, 6 Erde. */
	FWD DConcrete(const FLinearColor& C, float Wear, float Pattern) { FWD D = DColor(C); D.V[3] = Wear; D.V[4] = Pattern; return D; }
	FWD DPaint(const FLinearColor& C, float Wear, float Metallic, float Stripes) { FWD D = DColor(C); D.V[3] = Wear; D.V[4] = Metallic; D.V[5] = Stripes; return D; }
	FWD DNeon(const FLinearColor& C, float Intensity, float Flicker, float Day) { FWD D = DColor(C); D.V[3] = Intensity; D.V[4] = Flicker; D.V[5] = Day; return D; }
	FWD DSign(const FLinearColor& C, float Kind, float SignSeed, float Glow) { FWD D = DColor(C); D.V[3] = Kind; D.V[4] = SignSeed; D.V[5] = Glow; return D; }
	/** M_MTA_Water: Kuestenlinie weit weg (kein Schaum), Grundfarbe = C. */
	FWD DWater(const FLinearColor& C) { FWD D = DColor(C); D.V[3] = 1.0e7f; D.V[4] = 0.f; D.V[5] = 1.0e7f; D.V[6] = 0.f; return D; }
	FWD DRoad(float LenCm, float WidthCm) { FWD D; D.V[3] = LenCm; D.V[4] = WidthCm; D.V[5] = 0.f; return D; }
	/** M_MTA_Waterfall: Farbe, Deckkraft, Geschwindigkeit, Art (0 Band, 1 Gischt, 2 Flaeche), Phase. */
	FWD DFall(const FLinearColor& C, float Opacity, float Speed, float Kind, float Phase)
	{
		FWD D = DColor(C); D.V[3] = Opacity; D.V[4] = Speed; D.V[5] = Kind; D.V[6] = Phase; return D;
	}

	// ------------------------------------------------------------------ Layout (lokale Meter)

	constexpr float CliffU = 470.f;      // Felswand / Wasserfall (U)
	constexpr float FallV = 420.f;       // Mitte des Falls (V)
	constexpr float CliffHeight = 22.f;  // Fallhoehe ueber dem Talboden
	constexpr float BasinRadius = 10.5f;
	constexpr float RiverHalfWidth = 3.5f;
	constexpr float StreamHalfWidth = 1.5f;
	constexpr float PondRadius = 18.f;
	const FVector2D PondCenter(115.0, 520.0);

	float ValleyZ(float U) { return 0.04f * FMath::Max(0.f, U - 25.f); }
	float BaseZ() { return ValleyZ(CliffU - 6.f); }
	float FloorZ() { return BaseZ() + 0.3f; }
	float PlateauZ() { return BaseZ() + CliffHeight; }
	FVector2D BasinCenter() { return FVector2D(CliffU - 16.f, FallV); }

	/** Smoothstep, auch mit A > B (fallend). */
	float SStep(float A, float B, float X)
	{
		const float T = FMath::Clamp((X - A) / (B - A), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	/** Wie stark die Felswand an V ausgepraegt ist (1 = senkrecht am Wasserfall). */
	float Cliffness(float V) { return SStep(320.f, 370.f, V) * (1.f - SStep(500.f, 550.f, V)); }

	/** Seitliche Huegel an West- und Ostrand (m). */
	float SideHills(float V) { return 16.f * (SStep(130.f, 40.f, V) + SStep(540.f, 620.f, V)); }

	float Hash2(int32 X, int32 Y, int32 S)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u + static_cast<uint32>(Y) * 668265263u + static_cast<uint32>(S) * 2246822519u;
		H = (H ^ (H >> 13)) * 1274126177u;
		H ^= H >> 16;
		return static_cast<float>(H & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
	}

	float ValueNoise(float X, float Y, int32 S)
	{
		const int32 IX = FMath::FloorToInt(X);
		const int32 IY = FMath::FloorToInt(Y);
		const float FX = X - static_cast<float>(IX);
		const float FY = Y - static_cast<float>(IY);
		const float SX = FX * FX * (3.f - 2.f * FX);
		const float SY = FY * FY * (3.f - 2.f * FY);
		const float A = FMath::Lerp(Hash2(IX, IY, S), Hash2(IX + 1, IY, S), SX);
		const float B = FMath::Lerp(Hash2(IX, IY + 1, S), Hash2(IX + 1, IY + 1, S), SX);
		return FMath::Lerp(A, B, SY);
	}

	/** Mehrschichtiges Rauschen 0..1 (4 Oktaven, Grundwellenlaenge 90 m). */
	float Fbm01(float U, float V, int32 S)
	{
		float Sum = 0.f, Norm = 0.f, Amp = 1.f, Freq = 1.f / 90.f;
		for (int32 O = 0; O < 4; ++O)
		{
			Sum += Amp * ValueNoise(U * Freq, V * Freq, S + O * 17);
			Norm += Amp;
			Amp *= 0.5f;
			Freq *= 2.03f;
		}
		return Sum / Norm;
	}

	float SegDist(const FVector2D& P, const FVector2D& A, const FVector2D& B, float& OutT)
	{
		const FVector2D AB = B - A;
		const double L2 = AB.SizeSquared();
		double T = L2 > 1e-6 ? FVector2D::DotProduct(P - A, AB) / L2 : 0.0;
		T = FMath::Clamp(T, 0.0, 1.0);
		OutT = static_cast<float>(T);
		return static_cast<float>(FVector2D::Distance(P, A + AB * T));
	}

	TArray<FVector2D> Resample(const TArray<FVector2D>& Pts, float Step)
	{
		TArray<FVector2D> Out;
		if (Pts.Num() == 0)
		{
			return Out;
		}
		Out.Add(Pts[0]);
		for (int32 I = 1; I < Pts.Num(); ++I)
		{
			const FVector2D A = Pts[I - 1];
			const FVector2D B = Pts[I];
			const double Len = FVector2D::Distance(A, B);
			const int32 N = FMath::Max(1, FMath::CeilToInt(static_cast<float>(Len) / Step));
			for (int32 K = 1; K <= N; ++K)
			{
				Out.Add(FMath::Lerp(A, B, static_cast<double>(K) / N));
			}
		}
		return Out;
	}

	/** Alle Wildnis-Entdeckungen, deren Ort dieser Builder baut (Anker oder Interaktion). */
	const TCHAR* const WildDiscoveryIds[] = {
		TEXT("verd_veil_passage"), TEXT("verd_grotto_chest"), TEXT("verd_grotto_note"), TEXT("verd_wreck_clearing"),
		TEXT("verd_lookout"), TEXT("verd_campfire"), TEXT("verd_animal_tracks"), TEXT("verd_watchtower"),
		TEXT("verd_gorge_bridge"), TEXT("verd_waterfall_view"), TEXT("verd_waterfall_cave"), TEXT("verd_seed_vault"),
	};

	/** Laufzeit-Komponente mit eindeutigem Namen (mehrfaches Build ohne Namenskonflikt). */
	template <typename TComp>
	TComp* NewWildComponent(AActor* Outer, const TCHAR* BaseName)
	{
		return NewObject<TComp>(Outer, MakeUniqueObjectName(Outer, TComp::StaticClass(), FName(BaseName)));
	}

	UMTADiscoverySubsystem* DiscoveryOf(const UObject* Ctx)
	{
		const UWorld* World = Ctx ? Ctx->GetWorld() : nullptr;
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UMTADiscoverySubsystem>() : nullptr;
	}

	// Farben
	const FLinearColor WoodCol(0.36f, 0.25f, 0.15f);
	const FLinearColor DarkWood(0.22f, 0.15f, 0.09f);
	const FLinearColor RockCol(0.85f, 0.82f, 0.78f);
	const FLinearColor RiverCol(0.04f, 0.11f, 0.09f);
}

// =====================================================================================================
// Marker
// =====================================================================================================

AMTAWildernessMarker::AMTAWildernessMarker()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
	SetActorHiddenInGame(true);
}

// =====================================================================================================
// Interaktionspunkt
// =====================================================================================================

AMTAWildernessInteractable::AMTAWildernessInteractable()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	InteractVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractVolume"));
	InteractVolume->SetupAttachment(SceneRoot);
	InteractVolume->InitBoxExtent(FVector(80.f, 80.f, 80.f));
	InteractVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractVolume->SetCollisionObjectType(ECC_WorldDynamic);
	InteractVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	// Blick-Trace der Interaktion (ECC_Visibility) soll treffen; Naehe-Overlap filtert ueber den Objekttyp.
	InteractVolume->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractVolume->SetGenerateOverlapEvents(false);
	InteractVolume->SetCanEverAffectNavigation(false);

	PromptText = LOCTEXT("Inspect", "Untersuchen");
	UsedText = LOCTEXT("Done", "Bereits untersucht");
}

void AMTAWildernessInteractable::Configure(FName InDiscoveryId, const FText& InPrompt, const FText& InUsedText, const FVector& HalfExtentCm)
{
	DiscoveryId = InDiscoveryId;
	PromptText = InPrompt;
	UsedText = InUsedText;
	if (InteractVolume)
	{
		InteractVolume->SetBoxExtent(HalfExtentCm, true);
	}
	if (!InDiscoveryId.IsNone())
	{
		Tags.AddUnique(InDiscoveryId);
	}
	if (const UMTADiscoverySubsystem* Disc = DiscoveryOf(this))
	{
		bUsed = !DiscoveryId.IsNone() && Disc->IsDiscovered(DiscoveryId);
	}
}

void AMTAWildernessInteractable::BeginPlay()
{
	Super::BeginPlay();
	if (const UMTADiscoverySubsystem* Disc = DiscoveryOf(this))
	{
		bUsed = bUsed || (!DiscoveryId.IsNone() && Disc->IsDiscovered(DiscoveryId));
	}
}

FMTAInteractionInfo AMTAWildernessInteractable::GetInteractionInfo_Implementation(AActor* Interactor) const
{
	FMTAInteractionInfo Info;
	Info.Prompt = PromptText;
	Info.Kind = EMTAInteractionKind::Generic;
	Info.bEnabled = !bLocked && !bUsed;
	Info.DisabledReason = bLocked ? LockedReason : UsedText;
	Info.Priority = 1;
	return Info;
}

bool AMTAWildernessInteractable::CanInteract_Implementation(AActor* Interactor) const
{
	return !bLocked && !bUsed;
}

void AMTAWildernessInteractable::Interact_Implementation(AActor* Interactor)
{
	if (bLocked || bUsed)
	{
		return;
	}
	FString Message = PromptText.ToString();
	if (UMTADiscoverySubsystem* Disc = DiscoveryOf(this))
	{
		if (!DiscoveryId.IsNone())
		{
			const bool bOk = Disc->ForceDiscover(DiscoveryId, true) || Disc->IsDiscovered(DiscoveryId);
			if (!bOk)
			{
				return; // Bedingungen (Kette/Sichtbarkeit) noch nicht erfuellt
			}
			FMTADiscoveryDef Def;
			if (Disc->GetDiscovery(DiscoveryId, Def))
			{
				Message = Def.Lore.IsEmpty() ? Def.Title : Def.Title + TEXT(": ") + Def.Lore;
			}
		}
	}
	bUsed = true;
	OnUsed.Broadcast(DiscoveryId, Message);
	if (AMTAWildernessBuilder* Builder = Cast<AMTAWildernessBuilder>(GetOwner()))
	{
		Builder->OnWildernessDiscovery.Broadcast(DiscoveryId, Message);
	}
	UE_LOG(LogMTAWilderness, Log, TEXT("Wildnis-Interaktion %s: %s"), *DiscoveryId.ToString(), *Message);
}

// =====================================================================================================
// Builder: Konstruktion, Platzierung
// =====================================================================================================

AMTAWildernessBuilder::AMTAWildernessBuilder()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	RootComponent = SceneRoot;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	CubeMesh = CubeFinder.Object;
	CylinderMesh = CylinderFinder.Object;
	SphereMesh = SphereFinder.Object;
	ConeMesh = ConeFinder.Object;

	TerrainMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/MTA6/Materials/M_MTA_Terrain.M_MTA_Terrain")));
	TerrainFallbackMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/MTA6/Materials/M_MTA_Mountain.M_MTA_Mountain")));
	WaterfallMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/MTA6/Materials/M_MTA_Waterfall.M_MTA_Waterfall")));
	WaterfallFallbackMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/MTA6/Materials/M_MTA_Water.M_MTA_Water")));
	WaterfallSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/MTA6/Audio/Wilderness/amb_waterfall.amb_waterfall")));
	WaterfallFallbackSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Generated/amb_rain.amb_rain")));
}

AMTAWildernessBuilder* AMTAWildernessBuilder::FindWildernessBuilder(const UObject* WorldContext)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<AMTAWildernessBuilder> It(World);
	return It ? *It : nullptr;
}

AMTAWildernessBuilder* AMTAWildernessBuilder::FindOrSpawn(UObject* WorldContext)
{
	if (AMTAWildernessBuilder* Found = FindWildernessBuilder(WorldContext))
	{
		if (!Found->IsBuilt() && Found->HasActorBegunPlay())
		{
			Found->Build();
		}
		return Found;
	}
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || !World->IsGameWorld())
	{
		return nullptr;
	}
	FTransform Placement;
	FString Label, Report;
	const bool bFree = ComputePlacement(World, Placement, Label, Report);
	if (!bFree)
	{
		UE_LOG(LogMTAWilderness, Warning, TEXT("Wildnis: kein Platz ohne Strassen-/Wasser-/Stadtkonflikt gefunden, nehme '%s'.\n%s"), *Label, *Report);
	}
	AMTAWildernessBuilder* Spawned = World->SpawnActorDeferred<AMTAWildernessBuilder>(AMTAWildernessBuilder::StaticClass(), Placement,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Spawned)
	{
		return nullptr;
	}
	Spawned->PlacementName = Label;
	Spawned->PlacementReport = Report;
	Spawned->FinishSpawning(Placement); // BeginPlay -> Build
	return Spawned;
}

bool AMTAWildernessBuilder::ComputePlacement(UWorld* World, FTransform& OutTransform, FString& OutName, FString& OutReport)
{
	const AMTACityGenerator* Gen = AMTACityGenerator::FindCityGenerator(World);
	const FVector CityO = Gen ? Gen->GetActorLocation() : FVector::ZeroVector;
	const FVector2D HalfExt = Gen ? Gen->GetCityHalfExtent() : FVector2D(MTACity::HalfExtent, MTACity::HalfExtent);
	const double RoadW = Gen ? Gen->RoadWidth : MTACity::RoadWidth;
	const double GH = Gen ? Gen->GetGroundHalfSize() : 200000.0;
	const double Period = Gen ? Gen->BlockPeriod : MTACity::Period;
	const int32 BlocksAlongX = Gen ? Gen->BlocksX : MTACity::Blocks;
	const double Route9X = (BlocksAlongX / 2) * Period - HalfExt.X;
	const double EdgeN = HalfExt.X + RoadW * 0.5 + 100.0;
	const double EdgeE = HalfExt.Y + RoadW * 0.5 + 100.0;
	const double SU = 60000.0;
	const double SV = 64000.0;

	struct FCand
	{
		const TCHAR* Label;
		FVector Origin; // stadtlokal (cm)
		double Yaw;
	};
	TArray<FCand> Cands;
	Cands.Add({ TEXT("Nord (hinter Greenmoor)"), FVector(EdgeN, -16000.0, 0.0), 0.0 });
	Cands.Add({ TEXT("Nord-Ost"), FVector(EdgeN, 0.0, 0.0), 0.0 });
	Cands.Add({ TEXT("Ost (Ashridge-Vorland)"), FVector(Route9X + 3000.0 + SV, EdgeE, 0.0), 90.0 });

	// Hindernisse aus AMTAWorldDetail (stadtlokal): harte = Strassen/Wasser/Kai, weiche = Berge/Huegel.
	struct FFoot
	{
		FBox2D Box;
		FVector2D Center;
		double Radius;
		bool bHard;
		bool bCircle;
		FString What;
	};
	TArray<FFoot> Feet;
	if (AMTAWorldDetail* Detail = AMTAWorldDetail::FindWorldDetail(World))
	{
		TInlineComponentArray<UHierarchicalInstancedStaticMeshComponent*> Comps(Detail);
		for (UHierarchicalInstancedStaticMeshComponent* Comp : Comps)
		{
			if (!Comp)
			{
				continue;
			}
			const FString CompName = Comp->GetName();
			const bool bHard = CompName == TEXT("Roads") || CompName == TEXT("Water") || CompName == TEXT("Quay") || CompName == TEXT("Seabed") || CompName == TEXT("Piles");
			const bool bMountain = CompName == TEXT("Mountains");
			const bool bHill = CompName == TEXT("Hills");
			if (!bHard && !bMountain && !bHill)
			{
				continue;
			}
			for (int32 I = 0; I < Comp->GetInstanceCount(); ++I)
			{
				FTransform T;
				if (!Comp->GetInstanceTransform(I, T, true))
				{
					continue;
				}
				FFoot F;
				F.bHard = bHard;
				F.bCircle = bMountain;
				F.What = CompName;
				F.Box = FBox2D(ForceInit);
				for (int32 K = 0; K < 8; ++K)
				{
					const FVector Corner((K & 1) ? 50.0 : -50.0, (K & 2) ? 50.0 : -50.0, (K & 4) ? 50.0 : -50.0);
					const FVector P = T.TransformPosition(Corner) - CityO;
					F.Box += FVector2D(P.X, P.Y);
				}
				const FVector L = T.GetLocation() - CityO;
				F.Center = FVector2D(L.X, L.Y);
				F.Radius = 50.0 * FMath::Max(FMath::Abs(T.GetScale3D().X), FMath::Abs(T.GetScale3D().Y)) * 0.97;
				Feet.Add(F);
			}
		}
	}

	int32 Best = INDEX_NONE;
	int32 BestSoft = TNumericLimits<int32>::Max();
	OutReport.Reset();
	for (int32 C = 0; C < Cands.Num(); ++C)
	{
		const FCand& Cand = Cands[C];
		const FTransform Local(FRotator(0.0, Cand.Yaw, 0.0), Cand.Origin);
		FBox2D Region(ForceInit);
		for (int32 K = 0; K < 4; ++K)
		{
			const FVector P = Local.TransformPosition(FVector((K & 1) ? SU : 0.0, (K & 2) ? SV : 0.0, 0.0));
			Region += FVector2D(P.X, P.Y);
		}
		int32 Hard = 0;
		int32 Soft = 0;
		TArray<FString> Notes;

		const FBox2D CityBox(FVector2D(-(HalfExt.X + RoadW * 0.5), -(HalfExt.Y + RoadW * 0.5)), FVector2D(HalfExt.X + RoadW * 0.5, HalfExt.Y + RoadW * 0.5));
		if (Region.Intersect(CityBox)) { ++Hard; Notes.Add(TEXT("Stadtflaeche")); }
		const FBox2D GroundBox(FVector2D(-GH + 1000.0, -GH + 1000.0), FVector2D(GH - 1000.0, GH - 1000.0));
		if (!GroundBox.IsInside(Region.Min) || !GroundBox.IsInside(Region.Max)) { ++Hard; Notes.Add(TEXT("ausserhalb des Stadtbodens")); }
		const FBox2D Route9(FVector2D(Route9X - 2000.0, HalfExt.Y), FVector2D(Route9X + 2000.0, GH));
		if (Region.Intersect(Route9)) { ++Hard; Notes.Add(TEXT("Route 9")); }
		if (Gen && Gen->bGenerateCoast)
		{
			for (int32 S = 0; S <= 16; ++S)
			{
				const double X = FMath::Lerp(Region.Min.X, Region.Max.X, S / 16.0);
				const double CoastY = Gen->GetCoastLocalY(static_cast<float>(X), false);
				if (Region.Min.Y < CoastY + 6000.0) { ++Hard; Notes.Add(TEXT("Kueste/Kuestenstrasse")); break; }
			}
		}
		int32 HardFeet = 0;
		for (const FFoot& F : Feet)
		{
			bool bHit = false;
			if (F.bCircle)
			{
				const FVector2D Closest(FMath::Clamp(F.Center.X, Region.Min.X, Region.Max.X), FMath::Clamp(F.Center.Y, Region.Min.Y, Region.Max.Y));
				bHit = FVector2D::Distance(Closest, F.Center) < F.Radius;
			}
			else
			{
				bHit = Region.Intersect(F.Box);
			}
			if (!bHit)
			{
				continue;
			}
			if (F.bHard)
			{
				++HardFeet;
			}
			else
			{
				++Soft;
			}
		}
		if (HardFeet > 0) { Hard += HardFeet; Notes.Add(FString::Printf(TEXT("%d Strassen-/Wasserteile"), HardFeet)); }
		if (Soft > 0) { Notes.Add(FString::Printf(TEXT("%d Berg-/Huegelfuesse (weich)"), Soft)); }

		OutReport += FString::Printf(TEXT("Kandidat %s: %s\n"), Cand.Label, Notes.Num() == 0 ? TEXT("frei") : *FString::Join(Notes, TEXT(", ")));
		if (Hard == 0 && Soft < BestSoft)
		{
			Best = C;
			BestSoft = Soft;
			if (Soft == 0)
			{
				break;
			}
		}
	}
	// Frei = keine harten Konflikte (Berg-/Huegelfuesse sind nur weich und werden gemeldet).
	const bool bFree = (Best != INDEX_NONE);
	if (Best == INDEX_NONE)
	{
		Best = 0;
	}
	OutName = Cands[Best].Label;
	FVector WorldOrigin = CityO + Cands[Best].Origin;
	WorldOrigin.Z = CityO.Z;
	OutTransform = FTransform(FRotator(0.0, Cands[Best].Yaw, 0.0), WorldOrigin);
	return bFree;
}

// =====================================================================================================
// Builder: Hilfen
// =====================================================================================================

FVector AMTAWildernessBuilder::LocalM(float U, float V, float ZOffset) const
{
	return FVector(U, V, HeightAt(U, V) + ZOffset);
}

FVector AMTAWildernessBuilder::ToWorld(const FVector& LocalMeters) const
{
	return GetActorTransform().TransformPosition(LocalMeters * 100.0);
}

float AMTAWildernessBuilder::SampleGrid(const TArray<float>& Field, float U, float V) const
{
	if (NU < 2 || NV < 2 || Field.Num() != NU * NV)
	{
		return 0.f;
	}
	const float FU = FMath::Clamp(U / Grid, 0.f, static_cast<float>(NU - 1) - 0.001f);
	const float FV = FMath::Clamp(V / Grid, 0.f, static_cast<float>(NV - 1) - 0.001f);
	const int32 I = FMath::FloorToInt(FU);
	const int32 J = FMath::FloorToInt(FV);
	const float TU = FU - static_cast<float>(I);
	const float TV = FV - static_cast<float>(J);
	const float A = FMath::Lerp(Field[I + J * NU], Field[I + 1 + J * NU], TU);
	const float B = FMath::Lerp(Field[I + (J + 1) * NU], Field[I + 1 + (J + 1) * NU], TU);
	return FMath::Lerp(A, B, TV);
}

float AMTAWildernessBuilder::HeightAt(float U, float V) const
{
	return Heights.Num() > 0 ? SampleGrid(Heights, U, V) : NaturalHeight(U, V);
}

FVector2D AMTAWildernessBuilder::GroundSlopeAt(float U, float V) const
{
	const float DU = (HeightAt(U + 1.5f, V) - HeightAt(U - 1.5f, V)) / 3.f;
	const float DV = (HeightAt(U, V + 1.5f) - HeightAt(U, V - 1.5f)) / 3.f;
	return FVector2D(DU, DV);
}

bool AMTAWildernessBuilder::InCaveHole(float U, float V) const
{
	return FMath::Abs(V - FallV) < 4.5f && U > CliffU - 3.f && U < CliffU + 3.f;
}

float AMTAWildernessBuilder::NaturalHeight(float U, float V) const
{
	const float EdgeIn = SStep(0.f, 35.f, U);
	const float Outer = SStep(0.f, 45.f, V) * (1.f - SStep(595.f, 640.f, V)) * (1.f - SStep(560.f, 600.f, U));
	const float Valley = ValleyZ(U);
	const float C = Cliffness(V);
	const float Plateau = PlateauZ() + 0.03f * FMath::Max(0.f, U - CliffU);
	const float Ramp = FMath::Lerp(SStep(CliffU - 80.f, CliffU + 30.f, U), SStep(CliffU - 1.5f, CliffU + 1.5f, U), C);
	const float Base = Valley + FMath::Max(0.f, Plateau - Valley) * Ramp;
	const float Side = SideHills(V);
	// Am Wasserfall kein Rauschen (Ledge, Becken und Hoehle brauchen exakte Hoehen).
	const float NearFall = C * (1.f - SStep(25.f, 45.f, FMath::Abs(U - CliffU)));
	const float Amp = (3.5f + 0.2f * Side) * (1.f - NearFall);
	const float F = Fbm01(U, V, Seed);
	const float Detail = ValueNoise(U / 7.f, V / 7.f, Seed + 91) * 0.6f;
	const float Bumps = Amp * FMath::Clamp((F - 0.28f) * 1.7f, 0.f, 1.2f) + Detail * (1.f - NearFall);
	return FMath::Max(0.05f, EdgeIn * Outer * (Base + Side + Bumps));
}

int32 AMTAWildernessBuilder::PlannedInstances() const
{
	int32 N = 0;
	for (const FWildBatch& B : Batches)
	{
		N += B.Transforms.Num();
	}
	return N;
}

void AMTAWildernessBuilder::AddInst(int32 Layer, const FTransform& LocalCm, const float* Data)
{
	if (!Batches.IsValidIndex(Layer))
	{
		return;
	}
	FWildBatch& B = Batches[Layer];
	B.Transforms.Add(LocalCm);
	const int32 Start = B.Data.AddZeroed(NumWildData);
	if (Data)
	{
		for (int32 I = 0; I < NumWildData; ++I)
		{
			B.Data[Start + I] = Data[I];
		}
	}
}

void AMTAWildernessBuilder::AddBoxM(int32 Layer, const FVector& CenterM, const FVector& SizeM3, const FRotator& Rot, const float* Data)
{
	// Grundkoerper: 100 cm, mittig gepivotet -> Skalierung = Groesse in m.
	AddInst(Layer, FTransform(Rot, CenterM * 100.0, SizeM3), Data);
}

void AMTAWildernessBuilder::AddBoxMinMax(int32 Layer, const FVector& MinM, const FVector& MaxM, const float* Data)
{
	AddBoxM(Layer, (MinM + MaxM) * 0.5, (MaxM - MinM).GetAbs(), FRotator::ZeroRotator, Data);
}

void AMTAWildernessBuilder::AddColumnM(int32 Layer, const FVector& BaseM, float DiameterM, float HeightM, const float* Data)
{
	AddBoxM(Layer, BaseM + FVector(0.0, 0.0, HeightM * 0.5f), FVector(DiameterM, DiameterM, HeightM), FRotator::ZeroRotator, Data);
}

void AMTAWildernessBuilder::AddRodM(int32 Layer, const FVector& AM, const FVector& BM, float DiameterM, const float* Data)
{
	const FVector D = BM - AM;
	const double Len = D.Size();
	if (Len < 0.01)
	{
		return;
	}
	const FRotator Rot = FRotationMatrix::MakeFromZ(D / Len).Rotator();
	AddBoxM(Layer, (AM + BM) * 0.5, FVector(DiameterM, DiameterM, Len), Rot, Data);
}

void AMTAWildernessBuilder::AddBeamM(int32 Layer, const FVector& AM, const FVector& BM, float ThickM, float WidthM, const float* Data)
{
	const FVector D = BM - AM;
	const double Len = D.Size();
	if (Len < 0.01)
	{
		return;
	}
	AddBoxM(Layer, (AM + BM) * 0.5, FVector(Len, WidthM, ThickM), D.Rotation(), Data);
}

// =====================================================================================================
// Builder: Erzeugung
// =====================================================================================================

void AMTAWildernessBuilder::BeginPlay()
{
	Super::BeginPlay();
	if (PlacementName.IsEmpty())
	{
		PlacementName = TEXT("manuell platziert");
	}
	if (!bBuilt)
	{
		Build();
	}
}

void AMTAWildernessBuilder::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Nur beim gezielten Zerstoeren aufraeumen; beim Weltende raeumt die Engine selbst ab.
	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		for (AActor* A : SpawnedActors)
		{
			if (IsValid(A))
			{
				A->Destroy();
			}
		}
	}
	SpawnedActors.Reset();
	Super::EndPlay(EndPlayReason);
}

void AMTAWildernessBuilder::ClearWilderness()
{
	auto Kill = [](UActorComponent* Comp)
	{
		if (IsValid(Comp))
		{
			Comp->DestroyComponent();
		}
	};
	for (UProceduralMeshComponent* C : TerrainChunks) { Kill(C); }
	for (UHierarchicalInstancedStaticMeshComponent* C : InstanceLayers) { Kill(C); }
	for (UTextRenderComponent* C : SignTexts) { Kill(C); }
	Kill(Ribbons);
	Kill(Mist);
	Kill(VeilTrigger);
	Kill(WaterfallAudio);
	Kill(CaveLight);
	Kill(CampLight);
	TerrainChunks.Reset();
	InstanceLayers.Reset();
	SignTexts.Reset();
	Ribbons = nullptr;
	Mist = nullptr;
	VeilTrigger = nullptr;
	WaterfallAudio = nullptr;
	CaveLight = nullptr;
	CampLight = nullptr;
	for (AActor* A : SpawnedActors)
	{
		if (IsValid(A))
		{
			A->Destroy();
		}
	}
	SpawnedActors.Reset();
	Batches.Reset();
	Heights.Reset();
	PathDist.Reset();
	PathKind.Reset();
	RiverDist.Reset();
	Trails.Reset();
	RiverPts.Reset();
	StreamPts.Reset();
	Clearings.Reset();
	RibbonV.Reset();
	RibbonWidth.Reset();
	RibbonSpeed.Reset();
	RibbonPhase.Reset();
	MistBase.Reset();
	TitleCache.Reset();
	bBuilt = false;
}

void AMTAWildernessBuilder::ResolveAssets()
{
	LoadedMaterials.Reset();
	for (int32 M = 0; M < WMat::Count; ++M)
	{
		LoadedMaterials.Add(LoadObject<UMaterialInterface>(nullptr, WildMaterialPaths[M], nullptr, LOAD_NoWarn | LOAD_Quiet));
	}

	// Regionsname aus wilderness.json (Anzeige in Statuszeile/Karte).
	FString Text;
	TSharedPtr<FJsonObject> Json;
	const FString File = FPaths::ProjectContentDir() / TEXT("Data/wilderness.json");
	if (FFileHelper::LoadFileToString(Text, *File) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid())
	{
		const TSharedPtr<FJsonObject>* Region = nullptr;
		FString RegionName;
		if (Json->TryGetObjectField(TEXT("region"), Region) && (*Region)->TryGetStringField(TEXT("name"), RegionName) && !RegionName.IsEmpty())
		{
			RegionTitle = RegionName;
		}
	}
}

void AMTAWildernessBuilder::ComputeEntry()
{
	EntryV = 220.f;
	bEntryAlignedWithCityRoad = false;
	const AMTACityGenerator* Gen = AMTACityGenerator::FindCityGenerator(this);
	if (!Gen)
	{
		return;
	}
	const FVector CityO = Gen->GetActorLocation();
	const FVector2D HalfExt = Gen->GetCityHalfExtent();
	const FTransform& T = GetActorTransform();
	float BestErr = 1.0e9f;
	// Rasterstrassen beider Richtungen: konstantes Y (verlaeuft entlang X) und konstantes X (entlang Y).
	for (int32 Family = 0; Family < 2; ++Family)
	{
		const int32 Count = (Family == 0 ? Gen->BlocksY : Gen->BlocksX) + 1;
		for (int32 K = 0; K < Count; ++K)
		{
			FVector A, B;
			if (Family == 0)
			{
				const double Y = CityO.Y + K * Gen->BlockPeriod - HalfExt.Y;
				A = FVector(CityO.X - 1.0e6, Y, CityO.Z);
				B = FVector(CityO.X + 1.0e6, Y, CityO.Z);
			}
			else
			{
				const double X = CityO.X + K * Gen->BlockPeriod - HalfExt.X;
				A = FVector(X, CityO.Y - 1.0e6, CityO.Z);
				B = FVector(X, CityO.Y + 1.0e6, CityO.Z);
			}
			const FVector LA = T.InverseTransformPosition(A) / 100.0;
			const FVector LB = T.InverseTransformPosition(B) / 100.0;
			const FVector D = LB - LA;
			if (FMath::Abs(D.X) < FMath::Abs(D.Y) * 5.0)
			{
				continue; // nicht parallel zur Zufahrtsrichtung U
			}
			const double TParam = -LA.X / D.X;
			const float V = static_cast<float>(LA.Y + D.Y * TParam);
			if (V < 110.f || V > 330.f)
			{
				continue;
			}
			if (FMath::Abs(V - 220.f) < BestErr)
			{
				BestErr = FMath::Abs(V - 220.f);
				EntryV = V;
				bEntryAlignedWithCityRoad = true;
			}
		}
	}
}

void AMTAWildernessBuilder::Build()
{
	ClearWilderness();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	ResolveAssets();
	if (!CubeMesh || !CylinderMesh || !SphereMesh || !ConeMesh)
	{
		UE_LOG(LogMTAWilderness, Error, TEXT("Wildnis: Grundkoerper (/Engine/BasicShapes) fehlen, nichts erzeugt."));
		return;
	}
	const double StartTime = FPlatformTime::Seconds();
	Grid = GridSpacingM;
	ComputeEntry();
	Parking = FVector2D(80.0, EntryV);

	BuildHeightfield();
	BuildTerrainMesh();
	CreateLayers();
	BuildStructures();
	BuildCave();
	BuildWaterfall();
	BuildSurprises();
	BuildVegetation();
	ApplyBatches();
	SpawnDiscoveryActors();
	bBuilt = true;

	UE_LOG(LogMTAWilderness, Log, TEXT("Wildnis %s erzeugt (%s): %d Gelaende-Kacheln, %d Instanzen, Zufahrt V=%.0f m%s, %.0f ms."),
		*RegionTitle, *PlacementName, TerrainChunks.Num(), GetTotalInstanceCount(), EntryV,
		bEntryAlignedWithCityRoad ? TEXT(" (fluchtet mit Stadtstrasse)") : TEXT(""), (FPlatformTime::Seconds() - StartTime) * 1000.0);
}

// ---------------------------------------------------------------------------------------------- Hoehenfeld

void AMTAWildernessBuilder::ApplyTrail(FWildTrail& Trail, bool bUseCurrentHeights)
{
	const TArray<FVector2D> Pts = Resample(Trail.Points, 2.f);
	const int32 N = Pts.Num();
	if (N < 2)
	{
		return;
	}
	TArray<float> Z;
	TArray<float> Dist;
	Z.SetNumUninitialized(N);
	Dist.SetNumUninitialized(N);
	float Acc = 0.f;
	for (int32 I = 0; I < N; ++I)
	{
		const float U = static_cast<float>(Pts[I].X);
		const float V = static_cast<float>(Pts[I].Y);
		Z[I] = bUseCurrentHeights ? HeightAt(U, V) : NaturalHeight(U, V);
		if (I > 0)
		{
			Acc += static_cast<float>(FVector2D::Distance(Pts[I - 1], Pts[I]));
		}
		Dist[I] = Acc;
	}
	if (Trail.bVehicle)
	{
		Z[0] = 0.05f; // gleiche Hoehe wie die Stadtkante
	}
	// Glaetten (+-10 m), Endpunkte bleiben.
	{
		const TArray<float> Raw = Z;
		for (int32 I = 1; I < N - 1; ++I)
		{
			float Sum = 0.f;
			int32 Cnt = 0;
			for (int32 K = FMath::Max(0, I - 5); K <= FMath::Min(N - 1, I + 5); ++K)
			{
				Sum += Raw[K];
				++Cnt;
			}
			Z[I] = Sum / static_cast<float>(Cnt);
		}
	}
	// Feste Stuetzpunkte: Stadtkante (Fahrweg) und ebener Abschnitt (Parkplatz). Der Parkplatz wird spaeter
	// auf genau diese Hoehe eingeebnet; ohne festen Abschnitt entstand dort eine Stufe zwischen Fahrbahn und Platz.
	TArray<bool> Fixed;
	Fixed.Init(false, N);
	if (Trail.bVehicle)
	{
		Fixed[0] = true;
	}
	if (Trail.FlatToU > Trail.FlatFromU)
	{
		float Sum = 0.f;
		int32 Cnt = 0;
		float FirstDist = -1.f;
		for (int32 I = 0; I < N; ++I)
		{
			const float U = static_cast<float>(Pts[I].X);
			if (U >= Trail.FlatFromU && U <= Trail.FlatToU)
			{
				Sum += Z[I];
				++Cnt;
				FirstDist = FirstDist < 0.f ? Dist[I] : FirstDist;
			}
		}
		if (Cnt > 0)
		{
			float Level = Sum / static_cast<float>(Cnt);
			if (Trail.bVehicle)
			{
				// Mit Reserve von der Stadtkante aus erreichbar halten (85 % der zulaessigen Steigung).
				const float Reach = 0.85f * Trail.MaxSlope * FirstDist;
				Level = FMath::Clamp(Level, Z[0] - Reach, Z[0] + Reach);
			}
			Level = FMath::Max(0.05f, Level);
			for (int32 I = 0; I < N; ++I)
			{
				const float U = static_cast<float>(Pts[I].X);
				if (U >= Trail.FlatFromU && U <= Trail.FlatToU)
				{
					Z[I] = Level;
					Fixed[I] = true;
				}
			}
		}
	}
	// Steigung begrenzen: abwechselnd vorwaerts/rueckwaerts ueber die freien Punkte, bis nichts mehr aendert.
	for (int32 Pass = 0; Pass < 40; ++Pass)
	{
		float Change = 0.f;
		for (int32 I = 1; I < N; ++I)
		{
			if (Fixed[I])
			{
				continue;
			}
			const float Ds = FMath::Max(0.01f, Dist[I] - Dist[I - 1]);
			const float NewZ = FMath::Clamp(Z[I], Z[I - 1] - Trail.MaxSlope * Ds, Z[I - 1] + Trail.MaxSlope * Ds);
			Change = FMath::Max(Change, FMath::Abs(NewZ - Z[I]));
			Z[I] = NewZ;
		}
		for (int32 I = N - 2; I >= 0; --I)
		{
			if (Fixed[I])
			{
				continue;
			}
			const float Ds = FMath::Max(0.01f, Dist[I + 1] - Dist[I]);
			const float NewZ = FMath::Clamp(Z[I], Z[I + 1] - Trail.MaxSlope * Ds, Z[I + 1] + Trail.MaxSlope * Ds);
			Change = FMath::Max(Change, FMath::Abs(NewZ - Z[I]));
			Z[I] = NewZ;
		}
		if (Change < 1.0e-4f)
		{
			break;
		}
	}
	for (int32 I = 0; I < N; ++I)
	{
		Z[I] = FMath::Max(0.05f, Z[I]);
	}

	Trail.Samples.Reset();
	for (int32 I = 0; I < N; ++I)
	{
		Trail.Samples.Add(FVector(Pts[I].X, Pts[I].Y, Z[I]));
	}

	// Auf das Raster: naechster Segmentabstand je Vertex, Hoehe linear entlang des Segments.
	const float Reach = Trail.HalfWidth + Trail.Shoulder;
	TArray<float> BestD;
	TArray<float> BestZ;
	BestD.Init(1.0e4f, NU * NV);
	BestZ.Init(0.f, NU * NV);
	for (int32 S = 0; S < N - 1; ++S)
	{
		const FVector2D A = Pts[S];
		const FVector2D B = Pts[S + 1];
		const int32 I0 = FMath::Max(0, FMath::FloorToInt((static_cast<float>(FMath::Min(A.X, B.X)) - Reach) / Grid));
		const int32 I1 = FMath::Min(NU - 1, FMath::CeilToInt((static_cast<float>(FMath::Max(A.X, B.X)) + Reach) / Grid));
		const int32 J0 = FMath::Max(0, FMath::FloorToInt((static_cast<float>(FMath::Min(A.Y, B.Y)) - Reach) / Grid));
		const int32 J1 = FMath::Min(NV - 1, FMath::CeilToInt((static_cast<float>(FMath::Max(A.Y, B.Y)) + Reach) / Grid));
		for (int32 J = J0; J <= J1; ++J)
		{
			for (int32 I = I0; I <= I1; ++I)
			{
				float T = 0.f;
				const float D = SegDist(FVector2D(I * Grid, J * Grid), A, B, T);
				const int32 Idx = I + J * NU;
				if (D < BestD[Idx])
				{
					BestD[Idx] = D;
					BestZ[Idx] = FMath::Lerp(Z[S], Z[S + 1], T);
				}
			}
		}
	}
	const float Kind = Trail.bVehicle ? 1.f : 0.5f;
	for (int32 Idx = 0; Idx < NU * NV; ++Idx)
	{
		const float D = BestD[Idx];
		if (D >= Reach)
		{
			continue;
		}
		const float W = D <= Trail.HalfWidth ? 1.f : 1.f - SStep(Trail.HalfWidth, Reach, D);
		// Fusspfade veraendern die Fahrbahn nicht (sonst Buckel an den Abzweigen).
		if (Trail.bVehicle || PathKind[Idx] < 1.f)
		{
			Heights[Idx] = FMath::Lerp(Heights[Idx], BestZ[Idx], W);
		}
		const float Edge = D - Trail.HalfWidth;
		if (Edge < PathDist[Idx])
		{
			PathDist[Idx] = Edge;
			if (Edge < 0.8f)
			{
				PathKind[Idx] = FMath::Max(PathKind[Idx], Kind);
			}
		}
	}
}

void AMTAWildernessBuilder::CarveWater()
{
	auto CarvePolyline = [this](const TArray<FVector>& Pts, float HalfW, float BedDepth)
	{
		const float Reach = HalfW + 14.f;
		for (int32 S = 0; S + 1 < Pts.Num(); ++S)
		{
			const FVector2D A(Pts[S].X, Pts[S].Y);
			const FVector2D B(Pts[S + 1].X, Pts[S + 1].Y);
			const int32 I0 = FMath::Max(0, FMath::FloorToInt((static_cast<float>(FMath::Min(A.X, B.X)) - Reach) / Grid));
			const int32 I1 = FMath::Min(NU - 1, FMath::CeilToInt((static_cast<float>(FMath::Max(A.X, B.X)) + Reach) / Grid));
			const int32 J0 = FMath::Max(0, FMath::FloorToInt((static_cast<float>(FMath::Min(A.Y, B.Y)) - Reach) / Grid));
			const int32 J1 = FMath::Min(NV - 1, FMath::CeilToInt((static_cast<float>(FMath::Max(A.Y, B.Y)) + Reach) / Grid));
			for (int32 J = J0; J <= J1; ++J)
			{
				for (int32 I = I0; I <= I1; ++I)
				{
					float T = 0.f;
					const float D = SegDist(FVector2D(I * Grid, J * Grid), A, B, T);
					if (D >= Reach)
					{
						continue;
					}
					const float Wz = FMath::Lerp(static_cast<float>(Pts[S].Z), static_cast<float>(Pts[S + 1].Z), T);
					const float Target = D < HalfW ? Wz - BedDepth + 0.6f * FMath::Square(D / HalfW) : Wz + 0.25f + (D - HalfW) * 0.5f;
					const int32 Idx = I + J * NU;
					Heights[Idx] = FMath::Max(0.05f, FMath::Min(Heights[Idx], Target));
					RiverDist[Idx] = FMath::Min(RiverDist[Idx], D - HalfW);
				}
			}
		}
	};
	auto CarveDisc = [this](const FVector2D& C, float Radius, float Water, float Depth)
	{
		const float Reach = Radius + 10.f;
		const int32 I0 = FMath::Max(0, FMath::FloorToInt((static_cast<float>(C.X) - Reach) / Grid));
		const int32 I1 = FMath::Min(NU - 1, FMath::CeilToInt((static_cast<float>(C.X) + Reach) / Grid));
		const int32 J0 = FMath::Max(0, FMath::FloorToInt((static_cast<float>(C.Y) - Reach) / Grid));
		const int32 J1 = FMath::Min(NV - 1, FMath::CeilToInt((static_cast<float>(C.Y) + Reach) / Grid));
		for (int32 J = J0; J <= J1; ++J)
		{
			for (int32 I = I0; I <= I1; ++I)
			{
				const float D = static_cast<float>(FVector2D::Distance(FVector2D(I * Grid, J * Grid), C));
				if (D >= Reach)
				{
					continue;
				}
				const float Target = D < Radius ? Water - Depth + 0.9f * FMath::Square(D / Radius) : Water + 0.25f + (D - Radius) * 0.5f;
				const int32 Idx = I + J * NU;
				Heights[Idx] = FMath::Max(0.05f, FMath::Min(Heights[Idx], Target));
				RiverDist[Idx] = FMath::Min(RiverDist[Idx], D - Radius);
			}
		}
	};
	auto RimMin = [this](const FVector2D& C, float Radius)
	{
		float M = 1.0e4f;
		for (int32 K = 0; K < 32; ++K)
		{
			const float A = K * UE_TWO_PI / 32.f;
			M = FMath::Min(M, NaturalHeight(static_cast<float>(C.X) + FMath::Cos(A) * Radius, static_cast<float>(C.Y) + FMath::Sin(A) * Radius));
		}
		return M;
	};

	// Becken unter dem Fall.
	const FVector2D BC = BasinCenter();
	const float BasinWater = FMath::Min(BaseZ() - 0.35f, RimMin(BC, BasinRadius + 1.5f) - 0.35f);
	FallBottomZ = BasinWater;

	// Bach vom Becken zum Teich ("Die Gruene Ader"), Wasserspiegel faellt monoton.
	const TArray<FVector2D> RiverCtrl = {
		FVector2D(BC.X - 6.0, BC.Y + 10.0), FVector2D(420.0, 440.0), FVector2D(380.0, 452.0), FVector2D(340.0, 470.0),
		FVector2D(290.0, 482.0), FVector2D(240.0, 500.0), FVector2D(190.0, 512.0), FVector2D(PondCenter.X + 15.0, PondCenter.Y) };
	const TArray<FVector2D> RiverDense = Resample(RiverCtrl, 2.f);
	RiverPts.Reset();
	float Prev = BasinWater;
	for (int32 I = 0; I < RiverDense.Num(); ++I)
	{
		const FVector2D P = RiverDense[I];
		const FVector2D Dir = (I + 1 < RiverDense.Num() ? RiverDense[I + 1] - P : P - RiverDense[I - 1]).GetSafeNormal();
		const FVector2D Perp(-Dir.Y, Dir.X);
		float Low = NaturalHeight(static_cast<float>(P.X), static_cast<float>(P.Y)) - 0.7f;
		Low = FMath::Min(Low, NaturalHeight(static_cast<float>(P.X + Perp.X * 5.0), static_cast<float>(P.Y + Perp.Y * 5.0)) - 0.5f);
		Low = FMath::Min(Low, NaturalHeight(static_cast<float>(P.X - Perp.X * 5.0), static_cast<float>(P.Y - Perp.Y * 5.0)) - 0.5f);
		const float Wz = FMath::Max(1.2f, FMath::Min(Prev, Low));
		RiverPts.Add(FVector(P.X, P.Y, Wz));
		Prev = Wz;
	}
	PondWater = FMath::Max(1.f, FMath::Min(Prev, RimMin(PondCenter, PondRadius + 1.5f) - 0.35f));

	// Bach auf dem Plateau bis zur Fallkante.
	const float LipWater = PlateauZ() - 0.25f;
	FallTopZ = LipWater;
	const TArray<FVector2D> StreamCtrl = { FVector2D(552.0, 468.0), FVector2D(525.0, 452.0), FVector2D(498.0, 436.0), FVector2D(CliffU + 1.0, FallV) };
	const TArray<FVector2D> StreamDense = Resample(StreamCtrl, 2.f);
	StreamPts.Reset();
	Prev = 1.0e4f;
	for (const FVector2D& P : StreamDense)
	{
		const float Wz = FMath::Max(LipWater, FMath::Min(Prev, NaturalHeight(static_cast<float>(P.X), static_cast<float>(P.Y)) - 0.5f));
		StreamPts.Add(FVector(P.X, P.Y, Wz));
		Prev = Wz;
	}

	CarvePolyline(RiverPts, RiverHalfWidth, 0.8f);
	CarvePolyline(StreamPts, StreamHalfWidth, 0.5f);
	CarveDisc(BC, BasinRadius, BasinWater, 1.3f);
	CarveDisc(PondCenter, PondRadius, PondWater, 1.2f);
}

void AMTAWildernessBuilder::BuildHeightfield()
{
	NU = FMath::FloorToInt(static_cast<float>(SizeM.X) / Grid) + 1;
	NV = FMath::FloorToInt(static_cast<float>(SizeM.Y) / Grid) + 1;
	Heights.SetNumUninitialized(NU * NV);
	for (int32 J = 0; J < NV; ++J)
	{
		for (int32 I = 0; I < NU; ++I)
		{
			Heights[I + J * NU] = NaturalHeight(I * Grid, J * Grid);
		}
	}
	PathDist.Init(1.0e4f, NU * NV);
	PathKind.Init(0.f, NU * NV);
	RiverDist.Init(1.0e4f, NU * NV);

	CarveWater();

	// Haengebruecke quer ueber den Bach (zwischen Stuetzpunkt 4 und 5 des Bachs).
	{
		const FVector2D R3(340.0, 470.0);
		const FVector2D R4(290.0, 482.0);
		const FVector2D M = (R3 + R4) * 0.5;
		const FVector2D D = (R4 - R3).GetSafeNormal();
		const FVector2D P(-D.Y, D.X);
		const FVector2D A = M + P * 9.0;
		const FVector2D B = M - P * 9.0;
		BridgeA = A.Y < B.Y ? A : B; // West-/Talseite (kleineres V)
		BridgeB = A.Y < B.Y ? B : A;
	}

	// Lichtungen (U, V, Radius, Einebnung 0..1).
	Clearings.Reset();
	Clearings.Add(FVector4(250.0, 150.0, 16.0, 0.7));  // Wrack
	Clearings.Add(FVector4(185.0, 420.0, 10.0, 0.8));  // Lagerfeuer
	Clearings.Add(FVector4(330.0, 582.0, 6.0, 0.9));   // Wachturm
	Clearings.Add(FVector4(317.0, 582.0, 4.0, 0.0));   // Fuss der Turmrampe
	Clearings.Add(FVector4(530.0, 300.0, 7.0, 0.7));   // Aussichtsfels
	Clearings.Add(FVector4(365.0, 452.0, 4.0, 0.0));   // Tierspuren
	Clearings.Add(FVector4(440.0, 386.0, 8.0, 0.0));   // Aussicht auf den Fall
	Clearings.Add(FVector4(BridgeA.X, BridgeA.Y, 3.0, 0.0));
	Clearings.Add(FVector4(BridgeB.X, BridgeB.Y, 3.0, 0.0));
	for (const FVector4& Cl : Clearings)
	{
		const float Flat = static_cast<float>(Cl.W);
		if (Flat <= 0.f)
		{
			continue;
		}
		const FVector2D C(Cl.X, Cl.Y);
		const float Radius = static_cast<float>(Cl.Z);
		const float CenterZ = HeightAt(static_cast<float>(C.X), static_cast<float>(C.Y));
		for (int32 J = 0; J < NV; ++J)
		{
			for (int32 I = 0; I < NU; ++I)
			{
				const float D = static_cast<float>(FVector2D::Distance(FVector2D(I * Grid, J * Grid), C));
				if (D < Radius)
				{
					const int32 Idx = I + J * NU;
					Heights[Idx] = FMath::Lerp(Heights[Idx], CenterZ, Flat * (1.f - SStep(Radius * 0.6f, Radius, D)));
				}
			}
		}
	}

	// Fahrweg (Forststrasse) von der Stadtstrasse bis zur Aussicht auf den Wasserfall, max. 9 % Steigung.
	Trails.Reset();
	{
		FWildTrail Road;
		Road.Points = { FVector2D(0.0, EntryV), FVector2D(45.0, EntryV), Parking, FVector2D(115.0, EntryV),
			FVector2D(160.0, 236.0), FVector2D(230.0, 255.0), FVector2D(300.0, 300.0), FVector2D(360.0, 345.0),
			FVector2D(410.0, 372.0), FVector2D(440.0, 386.0) };
		Road.HalfWidth = 3.5f;
		Road.Shoulder = 5.f;
		Road.MaxSlope = 0.085f; // Reserve zur Testgrenze 12 % (Fugen, Rampen, Messraster)
		Road.bVehicle = true;
		// Parkplatz-Abschnitt eben (Parkplatz 30 m lang + 4 m Uebergang je Seite).
		Road.FlatFromU = static_cast<float>(Parking.X) - 19.f;
		Road.FlatToU = static_cast<float>(Parking.X) + 19.f;
		ApplyTrail(Road, true);
		Trails.Add(Road);
	}
	// Parkplatz 30 x 24 m eben auf Strassenhoehe.
	{
		const float PZ = HeightAt(static_cast<float>(Parking.X), static_cast<float>(Parking.Y));
		for (int32 J = 0; J < NV; ++J)
		{
			for (int32 I = 0; I < NU; ++I)
			{
				const float DU = FMath::Abs(I * Grid - static_cast<float>(Parking.X)) - 15.f;
				const float DV = FMath::Abs(J * Grid - static_cast<float>(Parking.Y)) - 12.f;
				const float D = FMath::Max(DU, DV);
				if (D < 4.f)
				{
					const int32 Idx = I + J * NU;
					Heights[Idx] = FMath::Lerp(Heights[Idx], PZ, D <= 0.f ? 1.f : 1.f - SStep(0.f, 4.f, D));
					if (D < 0.f)
					{
						PathDist[Idx] = FMath::Min(PathDist[Idx], D);
						PathKind[Idx] = 1.f;
					}
				}
			}
		}
	}
	// Fusspfade.
	auto AddFootTrail = [this](const TArray<FVector2D>& Pts, float MaxSlope)
	{
		FWildTrail T;
		T.Points = Pts;
		T.HalfWidth = 1.1f;
		T.Shoulder = 2.f;
		T.MaxSlope = MaxSlope;
		ApplyTrail(T, true);
		Trails.Add(T);
	};
	AddFootTrail({ FVector2D(440.0, 386.0), FVector2D(456.0, 393.0), FVector2D(CliffU - 7.0, FallV - 21.0), FVector2D(CliffU - 3.2, FallV - 20.0) }, 0.3f);
	AddFootTrail({ FVector2D(410.0, 372.0), FVector2D(445.0, 338.0), FVector2D(495.0, 300.0), FVector2D(512.0, 289.0), FVector2D(530.0, 292.0), FVector2D(530.0, 300.0) }, 0.35f);
	AddFootTrail({ FVector2D(300.0, 300.0), FVector2D(306.0, 380.0), FVector2D(310.0, 420.0), BridgeA }, 0.3f);
	AddFootTrail({ BridgeB, FVector2D(322.0, 520.0), FVector2D(322.0, 560.0), FVector2D(316.0, 581.0) }, 0.35f);
	AddFootTrail({ FVector2D(230.0, 255.0), FVector2D(210.0, 330.0), FVector2D(190.0, 405.0) }, 0.3f);
	AddFootTrail({ FVector2D(160.0, 236.0), FVector2D(195.0, 195.0), FVector2D(238.0, 160.0) }, 0.3f);
	AddFootTrail({ FVector2D(306.0, 380.0), FVector2D(340.0, 430.0), FVector2D(362.0, 449.0) }, 0.3f);

	for (float& H : Heights)
	{
		H = FMath::Max(0.05f, H);
	}
}

void AMTAWildernessBuilder::BuildTerrainMesh()
{
	const int32 N = NU * NV;
	TArray<FVector> Pos;
	TArray<FVector> Nrm;
	TArray<FVector2D> UV;
	TArray<FLinearColor> Col;
	TArray<FProcMeshTangent> Tan;
	Pos.SetNumUninitialized(N);
	Nrm.SetNumUninitialized(N);
	UV.SetNumUninitialized(N);
	Col.SetNumUninitialized(N);
	Tan.SetNumUninitialized(N);

	const FLinearColor Humus(0.045f, 0.075f, 0.03f);
	const FLinearColor Moss(0.085f, 0.13f, 0.04f);
	const FLinearColor Ridge(0.10f, 0.12f, 0.05f);
	const FLinearColor CityGrass(0.16f, 0.27f, 0.12f); // wie M_MTA_Ground ausserhalb der Stadt
	const FLinearColor Mud(0.17f, 0.15f, 0.11f);
	const FLinearColor Gravel(0.30f, 0.28f, 0.23f);
	const FLinearColor Dirt(0.19f, 0.14f, 0.09f);
	const FLinearColor WetRock(0.22f, 0.21f, 0.20f);

	for (int32 J = 0; J < NV; ++J)
	{
		for (int32 I = 0; I < NU; ++I)
		{
			const int32 Idx = I + J * NU;
			const float U = I * Grid;
			const float V = J * Grid;
			const float H = Heights[Idx];
			Pos[Idx] = FVector(U * 100.0, V * 100.0, H * 100.0);
			const float HL = Heights[FMath::Max(0, I - 1) + J * NU];
			const float HR = Heights[FMath::Min(NU - 1, I + 1) + J * NU];
			const float HD = Heights[I + FMath::Max(0, J - 1) * NU];
			const float HU = Heights[I + FMath::Min(NV - 1, J + 1) * NU];
			const float DU = (HR - HL) / (Grid * static_cast<float>(FMath::Min(NU - 1, I + 1) - FMath::Max(0, I - 1)));
			const float DV = (HU - HD) / (Grid * static_cast<float>(FMath::Min(NV - 1, J + 1) - FMath::Max(0, J - 1)));
			Nrm[Idx] = FVector(-DU, -DV, 1.0).GetSafeNormal();
			Tan[Idx] = FProcMeshTangent(FVector(1.0, 0.0, DU).GetSafeNormal(), false);
			UV[Idx] = FVector2D(U / 4.0, V / 4.0);

			const float F = ValueNoise(U / 6.f, V / 6.f, Seed + 99);
			FLinearColor C = FMath::Lerp(Humus, Moss, F);
			if (U > CliffU + 6.f || SideHills(V) > 8.f)
			{
				C = FMath::Lerp(C, Ridge, 0.6f);
			}
			C = FMath::Lerp(C, CityGrass, 1.f - SStep(8.f, 45.f, U));
			if (Cliffness(V) > 0.5f && FMath::Abs(U - CliffU) < 6.f)
			{
				C = FMath::Lerp(C, WetRock, 0.7f);
			}
			const float RD = RiverDist[Idx];
			if (RD < 3.f)
			{
				C = FMath::Lerp(Mud, C, SStep(-1.f, 3.f, RD));
			}
			float A = 0.f;
			const float PD = PathDist[Idx];
			if (PD < 1.2f)
			{
				const float W = 1.f - SStep(-0.3f, 1.2f, PD);
				C = FMath::Lerp(C, PathKind[Idx] > 0.75f ? Gravel : Dirt, W);
				A = W;
			}
			C.A = A;
			Col[Idx] = C;
		}
	}

	const int32 Chunks = FMath::Clamp(ChunksPerAxis, 1, 8);
	UMaterialInterface* TerrainMat = TerrainMaterial.IsNull() ? nullptr : TerrainMaterial.LoadSynchronous();
	ResolvedTerrainMaterial = TerrainMat ? TerrainMat->GetPathName() : FString();
	if (!TerrainMat)
	{
		TerrainMat = TerrainFallbackMaterial.IsNull() ? nullptr : TerrainFallbackMaterial.LoadSynchronous();
		ResolvedTerrainMaterial = TerrainMat ? TerrainMat->GetPathName() + TEXT(" (Rueckfall)") : TEXT("FEHLT");
	}
	for (int32 CY = 0; CY < Chunks; ++CY)
	{
		for (int32 CX = 0; CX < Chunks; ++CX)
		{
			const int32 I0 = (NU - 1) * CX / Chunks;
			const int32 I1 = (NU - 1) * (CX + 1) / Chunks;
			const int32 J0 = (NV - 1) * CY / Chunks;
			const int32 J1 = (NV - 1) * (CY + 1) / Chunks;
			const int32 W = I1 - I0 + 1;
			TArray<FVector> CPos;
			TArray<FVector> CNrm;
			TArray<FVector2D> CUV;
			TArray<FLinearColor> CCol;
			TArray<FProcMeshTangent> CTan;
			TArray<int32> Tris;
			for (int32 J = J0; J <= J1; ++J)
			{
				for (int32 I = I0; I <= I1; ++I)
				{
					const int32 Idx = I + J * NU;
					CPos.Add(Pos[Idx]);
					CNrm.Add(Nrm[Idx]);
					CUV.Add(UV[Idx]);
					CCol.Add(Col[Idx]);
					CTan.Add(Tan[Idx]);
				}
			}
			for (int32 J = J0; J < J1; ++J)
			{
				for (int32 I = I0; I < I1; ++I)
				{
					// Hoehlenmund: Felswand-Quads hinter dem Wasserfall auslassen (Bauten decken die Raender ab).
					if (InCaveHole((I + 0.5f) * Grid, (J + 0.5f) * Grid))
					{
						continue;
					}
					const int32 A = (I - I0) + (J - J0) * W;
					const int32 B = A + 1;
					const int32 C = A + W;
					const int32 D = C + 1;
					// Vorderseite nach +Z (Wicklung wie UKismetProceduralMeshLibrary::CreateGridMeshWelded).
					Tris.Add(A); Tris.Add(C); Tris.Add(B);
					Tris.Add(B); Tris.Add(C); Tris.Add(D);
				}
			}
			const FName ChunkName(*FString::Printf(TEXT("WildTerrain_%d_%d"), CX, CY));
			UProceduralMeshComponent* Chunk = NewObject<UProceduralMeshComponent>(this, MakeUniqueObjectName(this, UProceduralMeshComponent::StaticClass(), ChunkName));
			Chunk->SetupAttachment(SceneRoot);
			Chunk->SetMobility(EComponentMobility::Static);
			Chunk->bUseAsyncCooking = false;           // Kollision sofort (Selbsttest, Fahrzeuge beim Start)
			Chunk->bUseComplexAsSimpleCollision = true; // Dreiecks-Kollision, keine Konvexe noetig
			Chunk->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Chunk->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Chunk->SetCastShadow(true);
			Chunk->RegisterComponent();
			Chunk->CreateMeshSection_LinearColor(0, CPos, Tris, CNrm, CUV, CCol, CTan, true, false);
			if (TerrainMat)
			{
				Chunk->SetMaterial(0, TerrainMat);
			}
			TerrainChunks.Add(Chunk);
		}
	}
}

// ---------------------------------------------------------------------------------------------- Ebenen

void AMTAWildernessBuilder::CreateLayers()
{
	Batches.SetNum(WL::Count);
	for (int32 L = 0; L < WL::Count; ++L)
	{
		const FWildLayerSpec& Spec = LayerSpec(L);
		UHierarchicalInstancedStaticMeshComponent* Comp = NewWildComponent<UHierarchicalInstancedStaticMeshComponent>(this, Spec.LayerName);
		UStaticMesh* Mesh = CubeMesh;
		switch (Spec.Mesh)
		{
		case WMesh::Cylinder: Mesh = CylinderMesh; break;
		case WMesh::Sphere:   Mesh = SphereMesh; break;
		case WMesh::Cone:     Mesh = ConeMesh; break;
		default: break;
		}
		Comp->SetStaticMesh(Mesh);
		if (LoadedMaterials.IsValidIndex(Spec.Mat) && LoadedMaterials[Spec.Mat])
		{
			Comp->SetMaterial(0, LoadedMaterials[Spec.Mat]);
		}
		Comp->SetNumCustomDataFloats(NumWildData);
		if (Spec.Collision > 0)
		{
			Comp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Comp->SetCollisionEnabled(Spec.Collision == 1 ? ECollisionEnabled::QueryOnly : ECollisionEnabled::QueryAndPhysics);
		}
		else
		{
			Comp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		Comp->SetCanEverAffectNavigation(Spec.Collision == 2);
		Comp->SetCastShadow(Spec.bShadow);
		const int32 CullEnd = Spec.CullEnd > 0 ? FMath::RoundToInt(static_cast<float>(Spec.CullEnd) * CullDistanceScale) : 0;
		Comp->SetCullDistances(CullEnd > 0 ? FMath::RoundToInt(static_cast<float>(CullEnd) * 0.8f) : 0, CullEnd);
		Comp->SetupAttachment(SceneRoot);
		Comp->SetMobility(EComponentMobility::Static);
		Comp->RegisterComponent();
		InstanceLayers.Add(Comp);
	}
}

void AMTAWildernessBuilder::ApplyBatches()
{
	for (int32 L = 0; L < InstanceLayers.Num() && L < Batches.Num(); ++L)
	{
		UHierarchicalInstancedStaticMeshComponent* Comp = InstanceLayers[L];
		const FWildBatch& B = Batches[L];
		if (!Comp || B.Transforms.Num() == 0)
		{
			continue;
		}
		Comp->AddInstances(B.Transforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false);
		if (B.Data.Num() == B.Transforms.Num() * NumWildData)
		{
			// HISM verdeckt die Bereichs-Variante: explizit ueber die Basisklasse.
			Comp->UInstancedStaticMeshComponent::SetCustomData(0, B.Transforms.Num() - 1, TConstArrayView<float>(B.Data), false);
		}
		Comp->MarkRenderStateDirty();
	}
}

// ---------------------------------------------------------------------------------------------- Bauten

void AMTAWildernessBuilder::BuildStructures()
{
	// Forststrasse (Asphaltband) von der Stadtstrasse bis hinter den Parkplatz, Oberkante 9 cm ueber dem Weg.
	if (Trails.Num() > 0 && Trails[0].Samples.Num() > 3)
	{
		const TArray<FVector>& S = Trails[0].Samples;
		const float Top = 0.04f; // flach ueber dem eingeebneten Weg: kleine Kanten am Asphaltende
		const float Thick = 0.4f;
		auto RoadBeam = [this, Top, Thick](const FVector& A, const FVector& B)
		{
			const FVector Off(0.0, 0.0, Top - Thick * 0.5f);
			// Je 0,25 m Ueberlappung, damit an den Knickfugen keine Luecke entsteht.
			const FVector Dir = (B - A).GetSafeNormal();
			const float Len = static_cast<float>(FVector::Dist(A, B)) + 0.5f;
			AddBeamM(WL::Road, A + Off - Dir * 0.25, B + Off + Dir * 0.25, Thick, 7.f, DRoad(Len * 100.f, 700.f).V);
		};
		// Rampe vom Stadtboden (Z = 0) auf die Fahrbahn, 5 m lang.
		AddBeamM(WL::Road, FVector(-1.0, EntryV, -Thick * 0.5f), S[2] + FVector(0.0, 0.0, Top - Thick * 0.5f), Thick, 7.f, DRoad(600.f, 700.f).V);
		for (int32 I = 2; I + 3 < S.Num() && S[I].X < Parking.X + 30.0; I += 3)
		{
			RoadBeam(S[I], S[I + 3]);
		}
	}
	// Parkplatz (Schotter), Pfosten, Schranke, Schild.
	const float PZ = HeightAt(static_cast<float>(Parking.X), static_cast<float>(Parking.Y));
	AddBoxM(WL::Earth, FVector(Parking.X, Parking.Y, PZ + 0.02f - 0.15f), FVector(30.0, 24.0, 0.3), FRotator::ZeroRotator,
		DConcrete(FLinearColor(0.42f, 0.40f, 0.36f), 0.3f, 1.f).V);
	const FWD Post = DConcrete(DarkWood, 0.5f, 3.f);
	for (int32 K = 0; K < 6; ++K)
	{
		const float U = static_cast<float>(Parking.X) - 14.f + K * 5.6f;
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			const float V = static_cast<float>(Parking.Y) + Side * 12.5f;
			AddColumnM(WL::WoodPole, FVector(U, V, HeightAt(U, V) - 0.2f), 0.18f, 1.1f, Post.V);
		}
	}
	{
		// Schlagbaum "Projekt Wurzelwerk" (offen) am Waldrand.
		const float U = 112.f;
		const float VL = EntryV - 4.6f;
		const float Z = HeightAt(U, VL);
		const FWD Boom = DPaint(FLinearColor(0.8f, 0.05f, 0.05f), 0.7f, 0.2f, 4.f);
		AddBoxM(WL::Metal, FVector(U, VL, Z + 0.6f), FVector(0.3, 0.3, 1.2), FRotator::ZeroRotator, Boom.V);
		AddBeamM(WL::Metal, FVector(U, VL, Z + 1.1f), FVector(U, VL + 2.0f, Z + 6.2f), 0.12f, 0.12f, Boom.V);
		// Schild vor dem Parkplatz, Text zur Stadt hin.
		const float SU = 58.f;
		const float SV = EntryV - 7.f;
		const float SZ = HeightAt(SU, SV);
		AddColumnM(WL::WoodPole, FVector(SU, SV - 1.2f, SZ - 0.2f), 0.15f, 2.6f, Post.V);
		AddColumnM(WL::WoodPole, FVector(SU, SV + 1.2f, SZ - 0.2f), 0.15f, 2.6f, Post.V);
		AddBoxM(WL::Cloth, FVector(SU, SV, SZ + 1.9f), FVector(0.08, 2.8, 1.1), FRotator::ZeroRotator,
			DSign(FLinearColor(0.12f, 0.28f, 0.16f), 4.f, 3.f, 0.f).V);
		auto AddSignText = [this](const FString& Str, const FVector& LocalMeters, float Size, const FColor& Color)
		{
			UTextRenderComponent* T = NewObject<UTextRenderComponent>(this, NAME_None, RF_Transient);
			T->SetMobility(EComponentMobility::Movable);
			T->SetupAttachment(SceneRoot);
			T->RegisterComponent();
			// Text zeigt nach +X der Komponente: Richtung Stadt = lokal -U.
			T->SetRelativeLocationAndRotation(LocalMeters * 100.0, FRotator(0.0, 180.0, 0.0));
			T->SetText(FText::FromString(Str));
			T->SetWorldSize(Size);
			T->SetHorizontalAlignment(EHTA_Center);
			T->SetVerticalAlignment(EVRTA_TextCenter);
			T->SetTextRenderColor(Color);
			T->SetCastShadow(false);
			T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			T->SetCullDistance(6000.f);
			SignTexts.Add(T);
		};
		AddSignText(RegionTitle.ToUpper(), FVector(SU - 0.06f, SV, SZ + 2.15f), 32.f, FColor(235, 230, 200));
		AddSignText(TEXT("PROJEKT WURZELWERK - ZUTRITT NUR MIT KENNUNG"), FVector(SU - 0.06f, SV, SZ + 1.7f), 11.f, FColor(230, 220, 160));
	}

	// Wasserflaechen: Bach (Segmente je ca. 8 m), Becken, Teich, Plateau-Bach.
	auto AddWaterStrip = [this](const TArray<FVector>& Pts, float Width)
	{
		for (int32 I = 0; I + 1 < Pts.Num(); I += 4)
		{
			const FVector A = Pts[I];
			const FVector B = Pts[FMath::Min(I + 4, Pts.Num() - 1)];
			const FVector2D D(B.X - A.X, B.Y - A.Y);
			const double Len = D.Size();
			if (Len < 0.5)
			{
				continue;
			}
			const float Z = static_cast<float>(FMath::Max(A.Z, B.Z));
			AddBoxM(WL::Water, FVector((A.X + B.X) * 0.5, (A.Y + B.Y) * 0.5, Z - 0.05f), FVector(Len + 1.5, Width, 0.1),
				FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)), 0.0), DWater(RiverCol).V);
		}
	};
	AddWaterStrip(RiverPts, RiverHalfWidth * 2.f + 1.5f);
	AddWaterStrip(StreamPts, StreamHalfWidth * 2.f + 1.2f);
	for (int32 K = 0; K < 2; ++K)
	{
		const FRotator Rot(0.0, K * 45.0, 0.0);
		AddBoxM(WL::Water, FVector(BasinCenter().X, BasinCenter().Y, FallBottomZ - 0.05f), FVector(BasinRadius * 2.0, BasinRadius * 2.0, 0.1), Rot,
			DWater(FLinearColor(0.05f, 0.13f, 0.12f)).V);
		AddBoxM(WL::Water, FVector(PondCenter.X, PondCenter.Y, PondWater - 0.05f), FVector(PondRadius * 2.0, PondRadius * 2.0, 0.1), Rot,
			DWater(FLinearColor(0.03f, 0.08f, 0.06f)).V);
	}

	// Haengebruecke der Vermesser (verd_gorge_bridge).
	{
		const float ZA = HeightAt(static_cast<float>(BridgeA.X), static_cast<float>(BridgeA.Y)) + 0.25f;
		const float ZB = HeightAt(static_cast<float>(BridgeB.X), static_cast<float>(BridgeB.Y)) + 0.25f;
		const FVector2D Dir = (BridgeB - BridgeA).GetSafeNormal();
		const FVector2D Lat(-Dir.Y, Dir.X);
		const int32 Segs = 8;
		const FWD Plank = DConcrete(WoodCol, 0.6f, 3.f);
		const FWD Rope = DPaint(FLinearColor(0.35f, 0.3f, 0.22f), 0.4f, 0.1f, 0.f);
		auto DeckAt = [this, ZA, ZB](float T)
		{
			const FVector2D P = FMath::Lerp(BridgeA, BridgeB, static_cast<double>(T));
			return FVector(P.X, P.Y, FMath::Lerp(ZA, ZB, T) - 0.6f * FMath::Sin(T * UE_PI));
		};
		for (int32 K = 0; K < Segs; ++K)
		{
			const FVector P0 = DeckAt(static_cast<float>(K) / Segs);
			const FVector P1 = DeckAt(static_cast<float>(K + 1) / Segs);
			AddBeamM(WL::Earth, P0 - FVector(0.0, 0.0, 0.06), P1 - FVector(0.0, 0.0, 0.06), 0.12f, 1.7f, Plank.V);
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				const FVector Off(Lat.X * Side * 0.85, Lat.Y * Side * 0.85, 1.0);
				AddRodM(WL::MetalCyl, P0 + Off, P1 + Off, 0.05f, Rope.V);
			}
		}
		BridgeDeckZ = static_cast<float>(DeckAt(0.5f).Z);
		for (int32 End = 0; End < 2; ++End)
		{
			const FVector2D E = End == 0 ? BridgeA - Dir * 0.6 : BridgeB + Dir * 0.6;
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				const FVector2D P = E + Lat * (Side * 0.95);
				const float Z = HeightAt(static_cast<float>(P.X), static_cast<float>(P.Y));
				AddColumnM(WL::WoodPole, FVector(P.X, P.Y, Z - 0.3f), 0.2f, 1.8f, Post.V);
				AddBoxM(WL::Cloth, FVector(P.X, P.Y, Z + 1.75f), FVector(0.04, 0.5, 0.3), FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)), 0.0),
					DSign(FLinearColor(0.9f, 0.75f, 0.1f), 10.f, 1.f, 0.f).V);
			}
		}
	}
}

void AMTAWildernessBuilder::BuildCave()
{
	const float Zf = FloorZ();
	const float Zp = PlateauZ();
	const float U0 = CliffU;
	const float V0 = FallV;
	const FWD Rock = DRock(FLinearColor(0.75f, 0.73f, 0.7f));
	const FWD FloorData = DConcrete(FLinearColor(0.24f, 0.22f, 0.19f), 0.6f, 6.f);
	const FWD Ledge = DConcrete(FLinearColor(0.28f, 0.27f, 0.25f), 0.7f, 0.f);

	// Felsband hinter dem Wasservorhang.
	AddBoxMinMax(WL::Earth, FVector(U0 - 4.5f, V0 - 21.f, Zf - 1.5f), FVector(U0 + 0.5f, V0 + 8.f, Zf), Ledge.V);
	// Fallkante (deckt auch das Loch im Plateau ab).
	AddBoxMinMax(WL::RockBlock, FVector(U0 - 5.f, V0 - 7.f, Zp - 1.6f), FVector(U0 + 5.5f, V0 + 7.f, Zp - 0.4f), Rock.V);
	// Felsplombe ueber dem Eingang und Laibungen.
	AddBoxMinMax(WL::RockBlock, FVector(U0 - 1.f, V0 - 5.5f, Zf + 3.2f), FVector(U0 + 3.f, V0 + 5.5f, Zp - 0.4f), Rock.V);
	AddBoxMinMax(WL::RockBlock, FVector(U0 - 1.f, V0 - 5.5f, Zf - 1.f), FVector(U0 + 6.f, V0 - 1.6f, Zf + 3.2f), Rock.V);
	AddBoxMinMax(WL::RockBlock, FVector(U0 - 1.f, V0 + 1.6f, Zf - 1.f), FVector(U0 + 6.f, V0 + 5.5f, Zf + 3.2f), Rock.V);
	// Stollen-Decke, Boden.
	AddBoxMinMax(WL::RockBlock, FVector(U0 + 3.f, V0 - 5.5f, Zf + 3.2f), FVector(U0 + 6.f, V0 + 5.5f, Zf + 4.2f), Rock.V);
	AddBoxMinMax(WL::Earth, FVector(U0 - 1.f, V0 - 6.f, Zf - 1.f), FVector(U0 + 16.5f, V0 + 6.f, Zf), FloorData.V);
	// Grotte 9 x 9 m.
	AddBoxMinMax(WL::RockBlock, FVector(U0 + 15.f, V0 - 6.f, Zf - 1.f), FVector(U0 + 16.5f, V0 + 6.f, Zf + 5.5f), Rock.V);
	AddBoxMinMax(WL::RockBlock, FVector(U0 + 6.f, V0 - 6.f, Zf - 1.f), FVector(U0 + 16.5f, V0 - 4.5f, Zf + 5.5f), Rock.V);
	AddBoxMinMax(WL::RockBlock, FVector(U0 + 6.f, V0 + 4.5f, Zf - 1.f), FVector(U0 + 16.5f, V0 + 6.f, Zf + 5.5f), Rock.V);
	AddBoxMinMax(WL::RockBlock, FVector(U0 + 5.5f, V0 - 6.f, Zf - 1.f), FVector(U0 + 6.5f, V0 - 1.6f, Zf + 5.5f), Rock.V);
	AddBoxMinMax(WL::RockBlock, FVector(U0 + 5.5f, V0 + 1.6f, Zf - 1.f), FVector(U0 + 6.5f, V0 + 6.f, Zf + 5.5f), Rock.V);
	AddBoxMinMax(WL::RockBlock, FVector(U0 + 5.5f, V0 - 6.f, Zf + 4.5f), FVector(U0 + 16.5f, V0 + 6.f, Zf + 5.5f), Rock.V);
	AddBoxMinMax(WL::RockBlock, FVector(U0 + 5.5f, V0 - 1.6f, Zf + 4.2f), FVector(U0 + 6.5f, V0 + 1.6f, Zf + 4.5f), Rock.V);

	// Wurzeln an den Waenden.
	FRandomStream R(Seed ^ 0xCA7E);
	const FWD RootData = DColor(FLinearColor(0.3f, 0.22f, 0.14f));
	for (int32 K = 0; K < 8; ++K)
	{
		const float Side = (K % 2 == 0) ? -4.3f : 4.3f;
		const float U = U0 + 7.f + R.FRandRange(0.f, 7.f);
		AddRodM(WL::Log, FVector(U, V0 + Side, Zf + 4.4f), FVector(U + R.FRandRange(-1.5f, 1.5f), V0 + Side * 0.97f, Zf + R.FRandRange(0.f, 1.5f)), R.FRandRange(0.06f, 0.14f), RootData.V);
	}

	// Stahltuer an der Rueckwand (der Keim dahinter, verd_seed_vault), Kartenleser.
	AddBoxM(WL::Metal, FVector(U0 + 14.9f, V0, Zf + 1.2f), FVector(0.2, 1.6, 2.4), FRotator::ZeroRotator, DPaint(FLinearColor(0.32f, 0.34f, 0.35f), 0.6f, 0.8f, 0.f).V);
	AddBoxM(WL::Glow, FVector(U0 + 14.75f, V0 + 1.05f, Zf + 1.3f), FVector(0.06, 0.06, 0.06), FRotator::ZeroRotator, DNeon(FLinearColor(1.f, 0.08f, 0.04f), 8.f, 2.f, 1.f).V);
	AddBoxM(WL::Glow, FVector(U0 + 14.8f, V0 - 0.5f, Zf + 2.55f), FVector(0.05, 0.9, 0.04), FRotator::ZeroRotator, DNeon(FLinearColor(1.f, 0.55f, 0.2f), 4.f, 0.3f, 1.f).V);

	// Kiste der Vermesser.
	const FVector Chest(U0 + 12.f, V0 - 2.5f, Zf);
	AddBoxM(WL::Earth, Chest + FVector(0.0, 0.0, 0.3), FVector(1.2, 0.7, 0.6), FRotator(0.0, 8.0, 0.0), DConcrete(WoodCol, 0.5f, 3.f).V);
	AddBoxM(WL::Earth, Chest + FVector(0.0, 0.0, 0.66), FVector(1.25, 0.75, 0.12), FRotator(0.0, 8.0, 0.0), DConcrete(DarkWood, 0.5f, 3.f).V);
	for (int32 K = -1; K <= 1; K += 2)
	{
		AddBoxM(WL::Metal, Chest + FVector(K * 0.4, 0.0, 0.36), FVector(0.06, 0.78, 0.66), FRotator(0.0, 8.0, 0.0), DPaint(FLinearColor(0.55f, 0.42f, 0.2f), 0.5f, 0.9f, 0.f).V);
	}
	// Notiz auf einer Kiste.
	const FVector Crate(U0 + 11.f, V0 + 2.8f, Zf);
	AddBoxM(WL::Earth, Crate + FVector(0.0, 0.0, 0.3), FVector(0.6, 0.6, 0.6), FRotator(0.0, -12.0, 0.0), DConcrete(WoodCol * 0.8f, 0.6f, 3.f).V);
	AddBoxM(WL::Cloth, Crate + FVector(0.0, 0.0, 0.615), FVector(0.3, 0.21, 0.01), FRotator(0.0, 20.0, 0.0), DSign(FLinearColor(0.92f, 0.9f, 0.82f), 10.f, 2.f, 0.f).V);
	// Laterne am Eingang und Licht in der Grotte.
	AddBoxM(WL::Glow, FVector(U0 + 2.f, V0 + 1.3f, Zf + 2.4f), FVector(0.22, 0.22, 0.3), FRotator::ZeroRotator, DNeon(FLinearColor(1.f, 0.6f, 0.25f), 12.f, 0.15f, 1.f).V);
	AddRodM(WL::MetalCyl, FVector(U0 + 2.f, V0 + 1.3f, Zf + 2.55f), FVector(U0 + 2.f, V0 + 1.55f, Zf + 3.1f), 0.02f, DPaint(FLinearColor(0.1f, 0.1f, 0.1f), 0.5f, 0.8f, 0.f).V);

	CaveLight = NewWildComponent<UPointLightComponent>(this, TEXT("WildCaveLight"));
	CaveLight->SetupAttachment(SceneRoot);
	CaveLight->SetMobility(EComponentMobility::Movable);
	CaveLight->RegisterComponent();
	CaveLight->SetIntensityUnits(ELightUnits::Candelas);
	CaveLight->SetRelativeLocation(FVector(U0 + 8.f, V0, Zf + 3.5f) * 100.0);
	CaveLight->SetIntensity(45.f);
	CaveLight->SetLightColor(FLinearColor(1.f, 0.62f, 0.3f));
	CaveLight->SetAttenuationRadius(1800.f);
	CaveLight->SetCastShadows(false);

	// Durchgang hinter dem Vorhang (Entdeckung verd_veil_passage).
	VeilTrigger = NewWildComponent<UBoxComponent>(this, TEXT("WildVeilTrigger"));
	VeilTrigger->SetupAttachment(SceneRoot);
	VeilTrigger->SetMobility(EComponentMobility::Movable);
	VeilTrigger->SetCollisionProfileName(TEXT("Trigger"));
	VeilTrigger->SetGenerateOverlapEvents(true);
	VeilTrigger->SetCanEverAffectNavigation(false);
	VeilTrigger->RegisterComponent();
	VeilTrigger->SetRelativeLocation(FVector(U0 - 0.25f, V0, Zf + 1.5f) * 100.0);
	VeilTrigger->SetBoxExtent(FVector(225.f, 160.f, 150.f), true);
}

void AMTAWildernessBuilder::BuildWaterfall()
{
	UMaterialInterface* FallMat = WaterfallMaterial.IsNull() ? nullptr : WaterfallMaterial.LoadSynchronous();
	bWaterfallMaterialIsFallback = FallMat == nullptr;
	if (!FallMat)
	{
		FallMat = WaterfallFallbackMaterial.IsNull() ? nullptr : WaterfallFallbackMaterial.LoadSynchronous();
	}
	ResolvedWaterfallMaterial = FallMat ? FallMat->GetPathName() + (bWaterfallMaterialIsFallback ? TEXT(" (Rueckfall)") : TEXT("")) : TEXT("FEHLT");
	if (FallMat)
	{
		LoadedMaterials.Add(FallMat);
	}

	FallU = CliffU - 5.6f; // 0,6 m vor der Fallkante, ueber dem Beckenrand
	FRandomStream R(Seed ^ 0xFA11);
	RibbonV.Reset();
	RibbonWidth.Reset();
	RibbonSpeed.Reset();
	RibbonPhase.Reset();
	for (int32 K = 0; K < 7; ++K)
	{
		RibbonV.Add(FallV - 4.5f + K * 1.5f + R.FRandRange(-0.2f, 0.2f));
		RibbonWidth.Add(R.FRandRange(0.9f, 1.4f));
		RibbonSpeed.Add(R.FRandRange(5.f, 7.f));
		RibbonPhase.Add(R.FRandRange(0.f, 20.f));
	}

	auto MakeIsm = [this, FallMat](const TCHAR* CompName, UStaticMesh* Mesh)
	{
		UInstancedStaticMeshComponent* Comp = NewWildComponent<UInstancedStaticMeshComponent>(this, CompName);
		Comp->SetStaticMesh(Mesh);
		if (FallMat)
		{
			Comp->SetMaterial(0, FallMat);
		}
		Comp->SetNumCustomDataFloats(NumWildData);
		Comp->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetCastShadow(false);
		Comp->SetupAttachment(SceneRoot);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->RegisterComponent();
		return Comp;
	};

	Ribbons = MakeIsm(TEXT("WildWaterfallRibbons"), CubeMesh);
	TArray<FTransform> Init;
	TArray<float> Data;
	for (int32 K = 0; K < RibbonV.Num(); ++K)
	{
		for (int32 S = 0; S < SegmentsPerRibbon; ++S)
		{
			Init.Add(FTransform(FRotator::ZeroRotator, FVector(FallU, RibbonV[K], (FallTopZ + FallBottomZ) * 0.5f) * 100.0, FVector(0.08, RibbonWidth[K], 1.0)));
			const FWD D = bWaterfallMaterialIsFallback
				? DWater(FLinearColor(0.35f, 0.45f, 0.48f))
				: DFall(FLinearColor(0.72f, 0.86f, 0.93f), R.FRandRange(0.55f, 0.8f), RibbonSpeed[K] * 0.25f, 0.f, R.FRandRange(0.f, 1.f));
			Data.Append(D.V, NumWildData);
		}
	}
	Ribbons->AddInstances(Init, false, false);
	Ribbons->SetCustomData(0, Init.Num() - 1, TConstArrayView<float>(Data), true);

	// Gischt nur mit dem transluzenten Material (mit dem opaken Rueckfall waeren es feste Kugeln).
	MistBase.Reset();
	if (!bWaterfallMaterialIsFallback)
	{
		Mist = MakeIsm(TEXT("WildWaterfallMist"), SphereMesh);
		TArray<FTransform> MistInit;
		TArray<float> MistData;
		for (int32 K = 0; K < 12; ++K)
		{
			const FVector Base(FallU - R.FRandRange(0.5f, 3.f), FallV + R.FRandRange(-5.5f, 5.5f), FallBottomZ + R.FRandRange(0.f, 0.6f));
			MistBase.Add(Base);
			MistInit.Add(FTransform(FRotator::ZeroRotator, Base * 100.0, FVector(1.0)));
			const FWD D = DFall(FLinearColor(0.85f, 0.9f, 0.95f), 0.f, 0.f, 1.f, static_cast<float>(K) / 12.f);
			MistData.Append(D.V, NumWildData);
		}
		Mist->AddInstances(MistInit, false, false);
		Mist->SetCustomData(0, MistInit.Num() - 1, TConstArrayView<float>(MistData), true);
	}

	// Wasser auf der Fallkante (Rinne im Fels).
	AddBoxM(WL::Water, FVector(CliffU - 1.5f, FallV, FallTopZ - 0.05f), FVector(7.0, 10.0, 0.1), FRotator::ZeroRotator, DWater(RiverCol).V);

	// Felsbrocken am Fuss des Falls (Spritzsteine).
	const FWD Wet = DRock(FLinearColor(0.55f, 0.55f, 0.55f));
	for (int32 K = 0; K < 6; ++K)
	{
		const float S = R.FRandRange(0.8f, 1.6f);
		AddBoxM(WL::Boulder, FVector(FallU - R.FRandRange(0.5f, 2.5f), FallV + R.FRandRange(-6.f, 6.f), FallBottomZ - 0.2f), FVector(S, S * 1.2f, S * 0.6f),
			FRotator(0.0, R.FRandRange(0.f, 360.f), 0.0), Wet.V);
	}

	// Wasserfallton (3D-Loop).
	USoundBase* Sound = WaterfallSound.IsNull() ? nullptr : WaterfallSound.LoadSynchronous();
	ResolvedSound = Sound ? Sound->GetPathName() : FString();
	if (!Sound)
	{
		Sound = WaterfallFallbackSound.IsNull() ? nullptr : WaterfallFallbackSound.LoadSynchronous();
		ResolvedSound = Sound ? Sound->GetPathName() + TEXT(" (Rueckfall)") : TEXT("FEHLT");
	}
	if (Sound)
	{
		WaterfallAudio = NewWildComponent<UAudioComponent>(this, TEXT("WildWaterfallAudio"));
		WaterfallAudio->SetAutoActivate(false);
		WaterfallAudio->SetupAttachment(SceneRoot);
		WaterfallAudio->SetMobility(EComponentMobility::Movable);
		WaterfallAudio->bOverrideAttenuation = true;
		WaterfallAudio->AttenuationOverrides.bAttenuate = true;
		WaterfallAudio->AttenuationOverrides.bSpatialize = true;
		WaterfallAudio->AttenuationOverrides.AttenuationShape = EAttenuationShape::Sphere;
		WaterfallAudio->AttenuationOverrides.AttenuationShapeExtents = FVector(1500.0, 0.0, 0.0);
		WaterfallAudio->AttenuationOverrides.FalloffDistance = 38500.f;
		WaterfallAudio->AttenuationOverrides.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		WaterfallAudio->RegisterComponent();
		WaterfallAudio->SetRelativeLocation(FVector(FallU - 3.f, FallV, FallBottomZ + 4.f) * 100.0);
		WaterfallAudio->SetSound(Sound);
		WaterfallAudio->Play();
	}
}

void AMTAWildernessBuilder::BuildSurprises()
{
	FRandomStream R(Seed ^ 0x5A5A);

	// 1) Wrack auf der Lichtung (verd_wreck_clearing).
	{
		const FVector2D C(250.0, 150.0);
		const float Z = HeightAt(static_cast<float>(C.X), static_cast<float>(C.Y));
		const FRotator Yaw(0.0, 35.0, 6.0);
		const FTransform Car(Yaw, FVector(C.X, C.Y, Z));
		const FWD Body = DPaint(FLinearColor(0.32f, 0.36f, 0.22f), 0.95f, 0.4f, 0.f);
		AddBoxM(WL::Metal, Car.TransformPosition(FVector(0.0, 0.0, 0.75)), FVector(4.2, 1.9, 0.8), Yaw, Body.V);
		AddBoxM(WL::Metal, Car.TransformPosition(FVector(-0.4, 0.0, 1.5)), FVector(2.2, 1.7, 0.7), Yaw, Body.V);
		const FWD Tire = DPaint(FLinearColor(0.05f, 0.05f, 0.05f), 0.3f, 0.f, 0.f);
		for (int32 K = 0; K < 3; ++K)
		{
			const FVector P = Car.TransformPosition(FVector(K == 0 ? 1.4 : -1.4, K == 2 ? 0.95 : -0.95, 0.35));
			AddRodM(WL::MetalCyl, P - Car.GetRotation().GetRightVector() * 0.12, P + Car.GetRotation().GetRightVector() * 0.12, 0.7f, Tire.V);
		}
		AddBoxM(WL::Undergrowth, Car.TransformPosition(FVector(-0.4, 0.0, 2.0)), FVector(1.6, 1.4, 0.6), Yaw, DColor(FLinearColor(0.12f, 0.32f, 0.06f)).V);
		AddBoxM(WL::Cloth, Car.TransformPosition(FVector(0.5, 0.0, 1.55)), FVector(0.05, 1.5, 0.5), Yaw + FRotator(-30.0, 0.0, 0.0), DSign(FLinearColor(0.1f, 0.12f, 0.12f), 10.f, 0.f, 0.f).V);
	}

	// 2) Lagerfeuer mit Zelt (verd_campfire).
	{
		const FVector2D C(185.0, 420.0);
		const float Z = HeightAt(static_cast<float>(C.X), static_cast<float>(C.Y));
		const FWD Stone = DRock(FLinearColor(0.6f, 0.58f, 0.55f));
		for (int32 K = 0; K < 9; ++K)
		{
			const float A = K * UE_TWO_PI / 9.f;
			AddBoxM(WL::Boulder, FVector(C.X + FMath::Cos(A) * 0.8, C.Y + FMath::Sin(A) * 0.8, Z + 0.05f), FVector(0.35, 0.3, 0.25), FRotator(0.0, K * 40.0, 0.0), Stone.V);
		}
		const FWD Wood = DColor(FLinearColor(0.2f, 0.12f, 0.06f));
		AddRodM(WL::Log, FVector(C.X - 0.5, C.Y - 0.3, Z + 0.1f), FVector(C.X + 0.5, C.Y + 0.3, Z + 0.15f), 0.12f, Wood.V);
		AddRodM(WL::Log, FVector(C.X - 0.4, C.Y + 0.45, Z + 0.1f), FVector(C.X + 0.45, C.Y - 0.4, Z + 0.2f), 0.1f, Wood.V);
		AddBoxM(WL::Glow, FVector(C.X, C.Y, Z + 0.08f), FVector(0.6, 0.6, 0.12), FRotator::ZeroRotator, DNeon(FLinearColor(1.f, 0.3f, 0.05f), 6.f, 0.8f, 0.4f).V);
		// Zelt (A-Form) und Dosen.
		const FVector Tent(C.X + 3.5, C.Y + 2.0, HeightAt(static_cast<float>(C.X) + 3.5f, static_cast<float>(C.Y) + 2.f));
		const FWD Canvas = DSign(FLinearColor(0.2f, 0.32f, 0.18f), 10.f, 4.f, 0.f);
		AddBoxM(WL::Cloth, Tent + FVector(0.0, -0.55, 0.65), FVector(2.4, 0.06, 1.55), FRotator(0.0, 0.0, -45.0), Canvas.V);
		AddBoxM(WL::Cloth, Tent + FVector(0.0, 0.55, 0.65), FVector(2.4, 0.06, 1.55), FRotator(0.0, 0.0, 45.0), Canvas.V);
		const FWD Can = DPaint(FLinearColor(0.6f, 0.6f, 0.58f), 0.6f, 0.9f, 0.f);
		for (int32 K = 0; K < 3; ++K)
		{
			AddColumnM(WL::MetalCyl, FVector(C.X - 1.6, C.Y - 0.6 + K * 0.18, Z + 0.25f), 0.08f, 0.11f, Can.V);
		}
		AddBoxM(WL::Boulder, FVector(C.X - 1.6, C.Y - 0.4, Z), FVector(0.7, 0.9, 0.5), FRotator::ZeroRotator, Stone.V);

		CampLight = NewWildComponent<UPointLightComponent>(this, TEXT("WildCampLight"));
		CampLight->SetupAttachment(SceneRoot);
		CampLight->SetMobility(EComponentMobility::Movable);
		CampLight->RegisterComponent();
		CampLight->SetIntensityUnits(ELightUnits::Candelas);
		CampLight->SetRelativeLocation(FVector(C.X, C.Y, Z + 0.6f) * 100.0);
		CampLight->SetIntensity(15.f);
		CampLight->SetLightColor(FLinearColor(1.f, 0.45f, 0.15f));
		CampLight->SetAttenuationRadius(900.f);
		CampLight->SetCastShadows(false);
	}

	// 3) Tierspuren am Bach (verd_animal_tracks): Hufpaare zum Wasser, ein Stiefelabdruck.
	{
		const FWD Print = DConcrete(FLinearColor(0.08f, 0.06f, 0.04f), 0.f, 6.f);
		const FVector2D Start(357.0, 446.0);
		const FVector2D Dir = (FVector2D(368.0, 457.0) - Start).GetSafeNormal();
		const FVector2D Lat(-Dir.Y, Dir.X);
		for (int32 K = 0; K < 9; ++K)
		{
			const FVector2D P = Start + Dir * (K * 1.1) + Lat * ((K % 2) ? 0.18 : -0.18);
			const float Z = HeightAt(static_cast<float>(P.X), static_cast<float>(P.Y));
			const FRotator Rot(0.0, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)), 0.0);
			AddBoxM(WL::Prints, FVector(P.X, P.Y, Z), FVector(0.1, 0.05, 0.03), Rot, Print.V);
			AddBoxM(WL::Prints, FVector(P.X + Lat.X * 0.07, P.Y + Lat.Y * 0.07, Z), FVector(0.1, 0.05, 0.03), Rot, Print.V);
		}
		const FVector2D Boot = Start + Dir * 5.0 + Lat * 0.8;
		AddBoxM(WL::Prints, FVector(Boot.X, Boot.Y, HeightAt(static_cast<float>(Boot.X), static_cast<float>(Boot.Y))), FVector(0.3, 0.11, 0.03),
			FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)) + 10.0, 0.0), Print.V);
	}

	// 4) Verlassener Wachturm (verd_watchtower) mit Rampe.
	{
		const FVector2D C(330.0, 582.0);
		const float Z = HeightAt(static_cast<float>(C.X), static_cast<float>(C.Y));
		const float PlatformZ = Z + 7.f;
		const FWD Pole = DConcrete(DarkWood, 0.7f, 3.f);
		const FWD Plank = DConcrete(WoodCol, 0.7f, 3.f);
		for (int32 SX = -1; SX <= 1; SX += 2)
		{
			for (int32 SY = -1; SY <= 1; SY += 2)
			{
				const float LZ = HeightAt(static_cast<float>(C.X) + SX * 1.6f, static_cast<float>(C.Y) + SY * 1.6f);
				AddColumnM(WL::WoodPole, FVector(C.X + SX * 1.6, C.Y + SY * 1.6, LZ - 0.5f), 0.3f, PlatformZ + 3.3f - LZ, Pole.V);
			}
		}
		AddBoxM(WL::Earth, FVector(C.X, C.Y, PlatformZ - 0.1f), FVector(4.0, 4.0, 0.2), FRotator::ZeroRotator, Plank.V);
		// Bruestung (Rampenseite -U offen).
		AddBoxM(WL::Earth, FVector(C.X + 1.95, C.Y, PlatformZ + 0.55f), FVector(0.1, 4.0, 0.9), FRotator::ZeroRotator, Plank.V);
		AddBoxM(WL::Earth, FVector(C.X, C.Y - 1.95, PlatformZ + 0.55f), FVector(4.0, 0.1, 0.9), FRotator::ZeroRotator, Plank.V);
		AddBoxM(WL::Earth, FVector(C.X, C.Y + 1.95, PlatformZ + 0.55f), FVector(4.0, 0.1, 0.9), FRotator::ZeroRotator, Plank.V);
		// Schiefes Dach.
		AddBoxM(WL::Cloth, FVector(C.X, C.Y, PlatformZ + 3.1f), FVector(4.6, 4.6, 0.08), FRotator(8.0, 0.0, 3.0), DSign(FLinearColor(0.25f, 0.2f, 0.15f), 10.f, 5.f, 0.f).V);
		// Rampe (ca. 32 Grad) von -U auf die Plattform.
		const FVector Top(C.X - 2.0, C.Y, PlatformZ);
		const float RU = static_cast<float>(C.X) - 13.f;
		const FVector Bottom(RU, C.Y, HeightAt(RU, static_cast<float>(C.Y)));
		AddBeamM(WL::Earth, Bottom - FVector(0.0, 0.0, 0.1), Top - FVector(0.0, 0.0, 0.1), 0.2f, 1.3f, Plank.V);
		for (int32 K = 1; K < 8; ++K)
		{
			const FVector P = FMath::Lerp(Bottom, Top, K / 8.0);
			AddBoxM(WL::Earth, P + FVector(0.0, 0.0, 0.03), FVector(0.08, 1.3, 0.06), FRotator::ZeroRotator, Plank.V);
		}
		TowerPlatformM = FVector(C.X, C.Y, PlatformZ + 1.f);
	}

	// 5) Aussichtsfels mit Fernglas (verd_lookout), Blick zur Stadt (-U).
	{
		const FVector2D C(530.0, 300.0);
		const float Z = HeightAt(static_cast<float>(C.X), static_cast<float>(C.Y));
		AddBoxM(WL::RockBlock, FVector(C.X - 2.0, C.Y, Z - 0.4f), FVector(5.0, 6.0, 1.0), FRotator(0.0, 12.0, 2.0), DRock(FLinearColor(0.7f, 0.68f, 0.64f)).V);
		const FWD Steel = DPaint(FLinearColor(0.2f, 0.42f, 0.35f), 0.5f, 0.6f, 0.f);
		const FVector Foot(C.X - 3.0, C.Y, Z + 0.1f);
		AddColumnM(WL::MetalCyl, Foot, 0.12f, 1.15f, Steel.V);
		AddBoxM(WL::Metal, Foot + FVector(0.0, 0.0, 1.3), FVector(0.35, 0.5, 0.3), FRotator::ZeroRotator, Steel.V);
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			AddRodM(WL::MetalCyl, Foot + FVector(0.1, Side * 0.13, 1.33), Foot + FVector(-0.35, Side * 0.13, 1.36), 0.12f, Steel.V);
		}
		const FWD Rail = DConcrete(DarkWood, 0.6f, 3.f);
		for (int32 K = -2; K <= 2; ++K)
		{
			const FVector P(C.X - 4.4, C.Y + K * 1.2, HeightAt(static_cast<float>(C.X) - 4.4f, static_cast<float>(C.Y) + K * 1.2f));
			AddColumnM(WL::WoodPole, P - FVector(0.0, 0.0, 0.3), 0.12f, 1.3f, Rail.V);
		}
		AddBeamM(WL::Earth, FVector(C.X - 4.4, C.Y - 2.4, Z + 0.95f), FVector(C.X - 4.4, C.Y + 2.4, Z + 0.95f), 0.1f, 0.1f, Rail.V);
	}

	// Felsvorspruenge in der Wand (nicht am Fall selbst) und Felsbrocken am Wandfuss.
	const FWD Cliff = DRock(RockCol);
	for (float V = 335.f; V < 545.f; V += R.FRandRange(3.f, 6.f))
	{
		if (FMath::Abs(V - FallV) < 30.f)
		{
			continue;
		}
		const float C = Cliffness(V);
		if (C < 0.4f)
		{
			continue;
		}
		const float S = R.FRandRange(2.5f, 6.f);
		AddBoxM(WL::RockBlock, FVector(CliffU - 0.2f + R.FRandRange(-0.6f, 0.6f), V, R.FRandRange(BaseZ() + 2.f, PlateauZ() - 1.f)),
			FVector(S, S * R.FRandRange(0.8f, 1.4f), S * R.FRandRange(0.6f, 1.2f)), FRotator(R.FRandRange(-15.f, 15.f), R.FRandRange(0.f, 360.f), R.FRandRange(-15.f, 15.f)), Cliff.V);
		if (R.FRand() < 0.6f)
		{
			const float B = R.FRandRange(1.2f, 3.f);
			AddBoxM(WL::Boulder, FVector(CliffU - R.FRandRange(2.f, 6.f), V + R.FRandRange(-2.f, 2.f), HeightAt(CliffU - 4.f, V) + B * 0.2f),
				FVector(B, B * 1.1f, B * 0.7f), FRotator(0.0, R.FRandRange(0.f, 360.f), 0.0), Cliff.V);
		}
	}
}

void AMTAWildernessBuilder::BuildVegetation()
{
	FRandomStream R(Seed ^ 0x7EE5);
	const float SizeU = static_cast<float>(SizeM.X);
	const float SizeV = static_cast<float>(SizeM.Y);

	auto Density = [this, SizeU, SizeV](float U, float V, bool bUnder) -> float
	{
		if (U < 18.f || U > SizeU - 3.f || V < 3.f || V > SizeV - 3.f)
		{
			return 0.f;
		}
		const float PD = SampleGrid(PathDist, U, V);
		if (PD < (bUnder ? 0.6f : 2.f))
		{
			return 0.f;
		}
		const float RD = SampleGrid(RiverDist, U, V);
		if (RD < (bUnder ? 0.3f : 1.5f))
		{
			return 0.f;
		}
		if (Cliffness(V) > 0.3f && U > CliffU - 24.f && U < CliffU + 8.f)
		{
			return 0.f;
		}
		if (FMath::Abs(U - static_cast<float>(Parking.X)) < 20.f && FMath::Abs(V - static_cast<float>(Parking.Y)) < 16.f)
		{
			return 0.f;
		}
		const float Side = SideHills(V);
		float D = (U > CliffU + 4.f || Side > 8.f) ? 0.45f : (U < 200.f ? 0.55f : 0.85f);
		if (RD < 12.f)
		{
			D *= 1.5f; // Ufer dichter
		}
		D *= 0.3f + 0.7f * SStep(18.f, 60.f, U); // zur Stadt hin lichter
		if (PD < 5.f)
		{
			D *= 0.35f;
		}
		for (const FVector4& Cl : Clearings)
		{
			const float Dist = static_cast<float>(FVector2D::Distance(FVector2D(U, V), FVector2D(Cl.X, Cl.Y)));
			if (Dist < static_cast<float>(Cl.Z))
			{
				return 0.f;
			}
			if (Dist < static_cast<float>(Cl.Z) + 6.f)
			{
				D *= 0.4f;
			}
		}
		if (GroundSlopeAt(U, V).Size() > 0.85)
		{
			return 0.f;
		}
		return D;
	};

	// Felsbrocken.
	for (float U0 = 0.f; U0 < SizeU; U0 += 12.f)
	{
		for (float V0 = 0.f; V0 < SizeV; V0 += 12.f)
		{
			const float U = U0 + R.FRandRange(1.f, 11.f);
			const float V = V0 + R.FRandRange(1.f, 11.f);
			const float Roll = R.FRand();
			if (U < 25.f || SampleGrid(PathDist, U, V) < 1.5f || (Cliffness(V) > 0.3f && U > CliffU - 24.f && U < CliffU + 8.f))
			{
				continue;
			}
			bool bClear = false;
			for (const FVector4& Cl : Clearings)
			{
				bClear |= FVector2D::Distance(FVector2D(U, V), FVector2D(Cl.X, Cl.Y)) < Cl.Z + 1.0;
			}
			if (bClear)
			{
				continue;
			}
			const float Slope = static_cast<float>(GroundSlopeAt(U, V).Size());
			const float RD = SampleGrid(RiverDist, U, V);
			const float P = 0.05f + 0.5f * FMath::Clamp(Slope - 0.4f, 0.f, 1.f) + (RD > 0.5f && RD < 4.f ? 0.25f : 0.f) + (U > CliffU + 4.f ? 0.15f : 0.f);
			if (Roll > P || PlannedInstances() >= MaxInstances - 2500)
			{
				continue;
			}
			const float S = R.FRandRange(0.8f, 3.f);
			const FVector Size(S, S * R.FRandRange(0.6f, 1.2f), S * R.FRandRange(0.4f, 0.8f));
			const FLinearColor Tint = RockCol * R.FRandRange(0.8f, 1.1f);
			const FRotator Rot(R.FRandRange(-20.f, 20.f), R.FRandRange(0.f, 360.f), R.FRandRange(-20.f, 20.f));
			const bool bRound = R.FRand() < 0.4f;
			if (SampleGrid(PathDist, U, V) < S * 0.75f + 0.5f)
			{
				continue; // Brocken darf nicht in den Weg ragen
			}
			AddBoxM(bRound ? WL::Boulder : WL::RockBlock, FVector(U, V, HeightAt(U, V) + Size.Z * 0.2), Size, Rot, DRock(Tint).V);
		}
	}

	// Liegende Baumstaemme.
	const FWD LogData = DColor(FLinearColor::White);
	for (float U0 = 20.f; U0 < SizeU; U0 += 25.f)
	{
		for (float V0 = 0.f; V0 < SizeV; V0 += 25.f)
		{
			const float U = U0 + R.FRandRange(2.f, 23.f);
			const float V = V0 + R.FRandRange(2.f, 23.f);
			const float Roll = R.FRand();
			const float Yaw = R.FRandRange(0.f, UE_TWO_PI);
			const float Len = R.FRandRange(4.f, 9.f);
			const float Dia = R.FRandRange(0.4f, 0.8f);
			const FVector2D Half(FMath::Cos(Yaw) * Len * 0.5f, FMath::Sin(Yaw) * Len * 0.5f);
			const FVector2D A = FVector2D(U, V) - Half;
			const FVector2D B = FVector2D(U, V) + Half;
			if (Roll > 0.3f || Density(U, V, false) < 0.2f)
			{
				continue;
			}
			bool bBlocked = false;
			for (int32 K = 0; K <= 4; ++K)
			{
				const FVector2D P = FMath::Lerp(A, B, K / 4.0);
				bBlocked |= SampleGrid(PathDist, static_cast<float>(P.X), static_cast<float>(P.Y)) < 2.f
					|| SampleGrid(RiverDist, static_cast<float>(P.X), static_cast<float>(P.Y)) < 0.5f;
			}
			if (bBlocked)
			{
				continue;
			}
			AddRodM(WL::Log, FVector(A.X, A.Y, HeightAt(static_cast<float>(A.X), static_cast<float>(A.Y)) + Dia * 0.4f),
				FVector(B.X, B.Y, HeightAt(static_cast<float>(B.X), static_cast<float>(B.Y)) + Dia * 0.4f), Dia, LogData.V);
		}
	}

	// Baeume: drei Sorten (Baumfarn/Palme, Kiefer, Wuergefeige/Laubbaum).
	const int32 TreeCap = MaxInstances - 900;
	bool bFull = false;
	const float Cell = 7.5f;
	for (float U0 = 0.f; U0 < SizeU && !bFull; U0 += Cell)
	{
		for (float V0 = 0.f; V0 < SizeV; V0 += Cell)
		{
			const float U = U0 + R.FRandRange(0.5f, Cell - 0.5f);
			const float V = V0 + R.FRandRange(0.5f, Cell - 0.5f);
			const float Roll = R.FRand();
			const float Pick = R.FRand();
			const float D = Density(U, V, false);
			if (Roll > D * 0.3f * FoliageDensity)
			{
				continue;
			}
			if (PlannedInstances() + 3 > TreeCap)
			{
				bFull = true;
				break;
			}
			const FVector Base(U, V, HeightAt(U, V) - 0.3f);
			const bool bRidge = U > CliffU + 4.f || SideHills(V) > 8.f;
			int32 Species = 0;
			if (bRidge) { Species = Pick < 0.75f ? 1 : 0; }
			else if (U < 200.f) { Species = Pick < 0.6f ? 2 : (Pick < 0.85f ? 0 : 1); }
			else { Species = Pick < 0.55f ? 0 : 2; }

			const float Shade = R.FRandRange(0.8f, 1.15f);
			if (Species == 0)
			{
				const float H = R.FRandRange(7.f, 11.f);
				const FVector Top = Base + FVector(R.FRandRange(-0.8f, 0.8f), R.FRandRange(-0.8f, 0.8f), H);
				AddRodM(WL::TreeTrunk, Base, Top, R.FRandRange(0.3f, 0.45f), nullptr);
				const float W = R.FRandRange(4.5f, 6.5f);
				const FWD Leaf = DColor(FLinearColor(0.10f, 0.30f, 0.08f) * Shade);
				AddBoxM(WL::CrownSphere, Top + FVector(0.0, 0.0, 0.3), FVector(W, W, W * 0.28f), FRotator(0.0, R.FRandRange(0.f, 360.f), 0.0), Leaf.V);
				AddBoxM(WL::CrownSphere, Top + FVector(0.0, 0.0, 1.1), FVector(W * 0.6f, W * 0.6f, W * 0.3f), FRotator(R.FRandRange(-10.f, 10.f), 0.0, 0.0), Leaf.V);
			}
			else if (Species == 1)
			{
				const float H = R.FRandRange(8.f, 13.f);
				const float W = R.FRandRange(3.8f, 5.f);
				AddColumnM(WL::TreeTrunk, Base, R.FRandRange(0.35f, 0.55f), H * 0.8f, nullptr);
				const FWD Needle = DColor(FLinearColor(0.05f, 0.15f, 0.07f) * Shade);
				AddColumnM(WL::CrownCone, Base + FVector(0.0, 0.0, H * 0.25f), W, H * 0.6f, Needle.V);
				AddColumnM(WL::CrownCone, Base + FVector(0.0, 0.0, H * 0.55f), W * 0.7f, H * 0.45f, Needle.V);
			}
			else
			{
				const float H = R.FRandRange(5.f, 7.5f);
				const float W = R.FRandRange(7.f, 10.f);
				AddColumnM(WL::TreeTrunk, Base, R.FRandRange(0.7f, 1.1f), H + 1.f, nullptr);
				const FWD Leaf = DColor(FLinearColor(0.13f, 0.28f, 0.07f) * Shade);
				AddBoxM(WL::CrownSphere, Base + FVector(0.0, 0.0, H + W * 0.15f), FVector(W, W * R.FRandRange(0.85f, 1.1f), W * 0.6f), FRotator(0.0, R.FRandRange(0.f, 360.f), 0.0), Leaf.V);
				const float A = R.FRandRange(0.f, UE_TWO_PI);
				AddBoxM(WL::CrownSphere, Base + FVector(FMath::Cos(A) * W * 0.3f, FMath::Sin(A) * W * 0.3f, H + W * 0.35f), FVector(W * 0.6f, W * 0.6f, W * 0.4f), FRotator::ZeroRotator, Leaf.V);
			}
		}
	}

	// Unterholz: Farne, Bueske (flache Kugeln), am Ufer dichter.
	bFull = false;
	const float UCell = 4.f;
	for (float U0 = 0.f; U0 < SizeU && !bFull; U0 += UCell)
	{
		for (float V0 = 0.f; V0 < SizeV; V0 += UCell)
		{
			const float U = U0 + R.FRandRange(0.3f, UCell - 0.3f);
			const float V = V0 + R.FRandRange(0.3f, UCell - 0.3f);
			const float Roll = R.FRand();
			const float ColorPick = R.FRand();
			float D = Density(U, V, true);
			if (SampleGrid(RiverDist, U, V) < 10.f)
			{
				D *= 1.6f;
			}
			if (Roll > D * 0.11f * FoliageDensity)
			{
				continue;
			}
			if (PlannedInstances() + 1 > MaxInstances)
			{
				bFull = true;
				break;
			}
			const float W = R.FRandRange(1.2f, 2.6f);
			const float H = R.FRandRange(0.45f, 1.1f);
			FLinearColor C = ColorPick < 0.5f ? FLinearColor(0.12f, 0.32f, 0.06f) : FLinearColor(0.06f, 0.18f, 0.05f);
			if (ColorPick > 0.96f)
			{
				C = FLinearColor(0.4f, 0.08f, 0.1f); // Bromelie aus den Gewaechshaeusern
			}
			AddBoxM(WL::Undergrowth, FVector(U, V, HeightAt(U, V) + H * 0.3f), FVector(W, W * R.FRandRange(0.7f, 1.f), H), FRotator(0.0, R.FRandRange(0.f, 360.f), 0.0), DColor(C).V);
		}
	}
}

// ---------------------------------------------------------------------------------------------- Entdeckungen

void AMTAWildernessBuilder::SpawnMarker(FName Id, const FVector& LocalMeters)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMTAWildernessMarker* M = World->SpawnActor<AMTAWildernessMarker>(AMTAWildernessMarker::StaticClass(), ToWorld(LocalMeters), GetActorRotation(), Params);
	if (M)
	{
		M->Tags.AddUnique(Id);
		SpawnedActors.Add(M);
	}
}

AMTAWildernessInteractable* AMTAWildernessBuilder::SpawnInteractable(FName Id, const FVector& LocalMeters, const FText& Prompt, const FText& Used, const FVector& HalfExtentM)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMTAWildernessInteractable* A = World->SpawnActor<AMTAWildernessInteractable>(AMTAWildernessInteractable::StaticClass(), ToWorld(LocalMeters), GetActorRotation(), Params);
	if (A)
	{
		A->Configure(Id, Prompt, Used, HalfExtentM * 100.0);
		SpawnedActors.Add(A);
	}
	return A;
}

void AMTAWildernessBuilder::SpawnDiscoveryActors()
{
	const float Zf = FloorZ();
	// Ankerpunkte (Hoehe ca. Koerpermitte, die Entdeckungsradien sind 3D).
	SpawnMarker(TEXT("verd_wreck_clearing"), LocalM(250.f, 150.f, 1.f));
	SpawnMarker(TEXT("verd_campfire"), LocalM(185.f, 420.f, 1.f));
	SpawnMarker(TEXT("verd_animal_tracks"), LocalM(362.f, 451.f, 1.f));
	SpawnMarker(TEXT("verd_watchtower"), TowerPlatformM);
	{
		const FVector2D Mid = (BridgeA + BridgeB) * 0.5;
		SpawnMarker(TEXT("verd_gorge_bridge"), FVector(Mid.X, Mid.Y, BridgeDeckZ + 1.f));
	}
	SpawnMarker(TEXT("verd_waterfall_view"), LocalM(440.f, 386.f, 1.f));
	SpawnMarker(TEXT("verd_veil_passage"), FVector(CliffU, FallV, Zf + 1.f));
	SpawnMarker(TEXT("verd_waterfall_cave"), FVector(CliffU + 1.f, FallV, Zf + 1.f));

	SpawnInteractable(TEXT("verd_grotto_chest"), FVector(CliffU + 12.f, FallV - 2.5f, Zf + 0.5f),
		LOCTEXT("ChestOpen", "Kiste oeffnen"), LOCTEXT("ChestDone", "Leer"), FVector(0.8, 0.6, 0.6));
	SpawnInteractable(TEXT("verd_grotto_note"), FVector(CliffU + 11.f, FallV + 2.8f, Zf + 0.7f),
		LOCTEXT("NoteRead", "Notiz lesen"), LOCTEXT("NoteDone", "Gelesen"), FVector(0.5, 0.5, 0.4));
	SpawnInteractable(TEXT("verd_lookout"), LocalM(527.f, 300.f, 1.3f),
		LOCTEXT("Binoculars", "Fernglas benutzen"), LOCTEXT("BinocularsDone", "Die Stadt liegt still da"), FVector(0.8, 0.8, 0.8));
	if (AMTAWildernessInteractable* Door = SpawnInteractable(TEXT("verd_seed_vault"), FVector(CliffU + 14.6f, FallV, Zf + 1.2f),
		LOCTEXT("Door", "Stahltuer untersuchen"), LOCTEXT("DoorDone", "Verschlossen"), FVector(0.5, 1.0, 1.2)))
	{
		// Der Keim gehoert zur Kette Projekt Wurzelwerk (AMTAWildernessRegion, Zugangskarte), hier nur Kulisse.
		Door->bLocked = true;
		Door->LockedReason = LOCTEXT("DoorLocked", "Verschlossen. Ein Kartenleser blinkt rot.");
	}

	if (UMTADiscoverySubsystem* Disc = DiscoveryOf(this))
	{
		Disc->RefreshAnchors();
		bVeilFound = Disc->IsDiscovered(TEXT("verd_veil_passage"));
	}
}

FString AMTAWildernessBuilder::DiscoveryTitle(FName Id) const
{
	if (const FString* Cached = TitleCache.Find(Id))
	{
		return *Cached;
	}
	FString Title = Id.ToString();
	if (const UMTADiscoverySubsystem* Disc = DiscoveryOf(this))
	{
		FMTADiscoveryDef Def;
		if (Disc->GetDiscovery(Id, Def) && !Def.Title.IsEmpty())
		{
			Title = Def.Title;
		}
	}
	return Title;
}

// =====================================================================================================
// Laufzeit
// =====================================================================================================

void AMTAWildernessBuilder::AnimateWaterfall(float Time)
{
	if (Ribbons && RibbonV.Num() > 0)
	{
		const float H = FMath::Max(1.f, FallTopZ - FallBottomZ);
		const float Seg = (H * 1.25f) / static_cast<float>(SegmentsPerRibbon - 1);
		const float Range = H + Seg;
		const float Step = Range / static_cast<float>(SegmentsPerRibbon);
		TArray<FTransform> Xf;
		Xf.Reserve(RibbonV.Num() * SegmentsPerRibbon);
		for (int32 K = 0; K < RibbonV.Num(); ++K)
		{
			for (int32 S = 0; S < SegmentsPerRibbon; ++S)
			{
				const float Travel = FMath::Fmod(Time * RibbonSpeed[K] + RibbonPhase[K] + S * Step, Range);
				const float Top = FMath::Min(FallTopZ, FallTopZ + Seg - Travel);
				const float Bottom = FMath::Max(FallBottomZ, FallTopZ + Seg - Travel - Seg);
				const float Len = FMath::Max(0.01f, Top - Bottom);
				// Wasser schiesst leicht nach vorn: je tiefer, desto weiter von der Wand.
				const float Fall01 = (FallTopZ - (Top + Bottom) * 0.5f) / H;
				const float U = FallU - 0.8f * Fall01;
				const float Wobble = 1.f + 0.06f * FMath::Sin(Time * 3.1f + K * 1.7f + S);
				Xf.Add(FTransform(FRotator::ZeroRotator, FVector(U, RibbonV[K], (Top + Bottom) * 0.5f) * 100.0,
					FVector(0.08, RibbonWidth[K] * Wobble * (1.f + 0.25f * Fall01), Len)));
			}
		}
		Ribbons->BatchUpdateInstancesTransforms(0, Xf, false, true, false);
	}
	if (Mist && MistBase.Num() > 0)
	{
		TArray<FTransform> Xf;
		TArray<float> Data;
		for (int32 K = 0; K < MistBase.Num(); ++K)
		{
			const float Age = FMath::Fmod(Time / 3.2f + static_cast<float>(K) / MistBase.Num(), 1.f);
			const float S = 1.2f + 4.f * Age;
			Xf.Add(FTransform(FRotator::ZeroRotator, (MistBase[K] + FVector(-Age * 1.5f, 0.0, Age * 2.5f)) * 100.0, FVector(S, S, S * 0.8f)));
			const float Opacity = 0.35f * FMath::Square(FMath::Sin(Age * UE_PI));
			const FWD D = DFall(FLinearColor(0.85f, 0.9f, 0.95f), Opacity, 0.f, 1.f, static_cast<float>(K) / 12.f);
			Data.Append(D.V, NumWildData);
		}
		Mist->BatchUpdateInstancesTransforms(0, Xf, false, true, false);
		Mist->SetCustomData(0, Xf.Num() - 1, TConstArrayView<float>(Data), true);
	}
}

void AMTAWildernessBuilder::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBuilt)
	{
		return;
	}
	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Pawn)
	{
		return;
	}
	const FVector PawnLoc = Pawn->GetActorLocation();
	const float Time = GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds()) : 0.f;
	const FVector FallWorld = ToWorld(FVector(FallU, FallV, (FallTopZ + FallBottomZ) * 0.5f));
	if (FVector::Dist(PawnLoc, FallWorld) < AnimationRadiusM * 100.f)
	{
		AnimateWaterfall(Time);
	}
	if (CampLight && Time >= NextCampFlicker)
	{
		NextCampFlicker = Time + 0.08f;
		if (FVector::Dist(PawnLoc, CampLight->GetComponentLocation()) < 8000.f)
		{
			CampLight->SetIntensity(12.f + 6.f * FMath::Abs(FMath::Sin(Time * 7.3f) * FMath::Sin(Time * 2.9f + 1.f)));
		}
	}
	// Durch den Vorhang: Spieler im Hoehlenmund -> Entdeckung.
	if (!bVeilFound && VeilTrigger)
	{
		const FVector P = VeilTrigger->GetComponentTransform().InverseTransformPosition(PawnLoc);
		const FVector Ext = VeilTrigger->GetUnscaledBoxExtent();
		if (FMath::Abs(P.X) <= Ext.X && FMath::Abs(P.Y) <= Ext.Y && FMath::Abs(P.Z) <= Ext.Z + 60.0)
		{
			bVeilFound = true;
			const FName Id(TEXT("verd_veil_passage"));
			if (UMTADiscoverySubsystem* Disc = DiscoveryOf(this))
			{
				Disc->ForceDiscover(Id, false);
			}
			OnWildernessDiscovery.Broadcast(Id, DiscoveryTitle(Id));
			UE_LOG(LogMTAWilderness, Log, TEXT("Wildnis: Spieler hat den Wasservorhang durchschritten."));
		}
	}
}

// =====================================================================================================
// Abfragen
// =====================================================================================================

FVector AMTAWildernessBuilder::GetWildernessCenter() const
{
	const float U = static_cast<float>(SizeM.X) * 0.5f;
	const float V = static_cast<float>(SizeM.Y) * 0.5f;
	return ToWorld(FVector(U, V, HeightAt(U, V)));
}

FVector AMTAWildernessBuilder::GetEntranceLocation() const
{
	return ToWorld(FVector(0.0, EntryV, 0.1));
}

FVector AMTAWildernessBuilder::GetParkingLocation() const
{
	return ToWorld(FVector(Parking.X, Parking.Y, HeightAt(static_cast<float>(Parking.X), static_cast<float>(Parking.Y)) + 0.1f));
}

FVector AMTAWildernessBuilder::GetSecretEntranceLocation() const
{
	return ToWorld(FVector(CliffU, FallV, FloorZ()));
}

FVector AMTAWildernessBuilder::GetWaterfallLocation() const
{
	return ToWorld(FVector(BasinCenter().X, BasinCenter().Y, BaseZ()));
}

FText AMTAWildernessBuilder::GetRegionName() const
{
	return FText::FromString(RegionTitle);
}

bool AMTAWildernessBuilder::IsInsideWilderness(const FVector& WorldLocation) const
{
	const FVector L = GetActorTransform().InverseTransformPosition(WorldLocation) / 100.0;
	return L.X >= 0.0 && L.Y >= 0.0 && L.X <= SizeM.X && L.Y <= SizeM.Y;
}

FText AMTAWildernessBuilder::GetRegionNameAt(const UObject* WorldContext, const FVector& WorldLocation)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return FText::GetEmpty();
	}
	for (TActorIterator<AMTAWildernessBuilder> It(World); It; ++It)
	{
		if (It->IsInsideWilderness(WorldLocation))
		{
			return It->GetRegionName();
		}
	}
	return FText::GetEmpty();
}

bool AMTAWildernessBuilder::GetTerrainHeightAt(const FVector& WorldLocation, float& OutWorldZ) const
{
	if (!IsInsideWilderness(WorldLocation) || Heights.Num() == 0)
	{
		return false;
	}
	const FVector L = GetActorTransform().InverseTransformPosition(WorldLocation) / 100.0;
	OutWorldZ = static_cast<float>(ToWorld(FVector(L.X, L.Y, HeightAt(static_cast<float>(L.X), static_cast<float>(L.Y)))).Z);
	return true;
}

int32 AMTAWildernessBuilder::GetTotalInstanceCount() const
{
	int32 N = 0;
	for (const UHierarchicalInstancedStaticMeshComponent* C : InstanceLayers)
	{
		N += C ? C->GetInstanceCount() : 0;
	}
	N += Ribbons ? Ribbons->GetInstanceCount() : 0;
	N += Mist ? Mist->GetInstanceCount() : 0;
	return N;
}

// =====================================================================================================
// Selbsttest
// =====================================================================================================

bool AMTAWildernessBuilder::TraceGround(const FVector& LocalTopM, float DepthM, FVector& OutHitLocalM, UPrimitiveComponent** OutComp) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const FVector Start = ToWorld(LocalTopM);
	const FVector End = ToWorld(LocalTopM - FVector(0.0, 0.0, DepthM));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MTAWildernessTest), true);
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
	{
		return false;
	}
	OutHitLocalM = GetActorTransform().InverseTransformPosition(Hit.ImpactPoint) / 100.0;
	if (OutComp)
	{
		*OutComp = Hit.GetComponent();
	}
	return true;
}

FString AMTAWildernessBuilder::RunWildernessSelfTest()
{
	TArray<FString> Lines;
	int32 Failures = 0;
	auto Check = [&Lines, &Failures](const FString& What, bool bOk, const FString& Detail)
	{
		Lines.Add(FString::Printf(TEXT("WILDNISTEST %s: %s (%s)"), *What, bOk ? TEXT("OK") : TEXT("FEHLER"), *Detail));
		if (!bOk)
		{
			++Failures;
		}
	};
	UWorld* World = GetWorld();
	Check(TEXT("Gebaut"), bBuilt && World != nullptr, PlacementName);

	// Gelaende und Kollision.
	int32 Tris = 0;
	bool bCollision = TerrainChunks.Num() > 0;
	for (UProceduralMeshComponent* C : TerrainChunks)
	{
		if (!C)
		{
			bCollision = false;
			continue;
		}
		const FProcMeshSection* Section = C->GetProcMeshSection(0);
		Tris += Section ? Section->ProcIndexBuffer.Num() / 3 : 0;
		bCollision &= C->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics && C->GetBodySetup() != nullptr;
	}
	Check(TEXT("Gelaende vorhanden"), TerrainChunks.Num() > 0 && Tris > 1000, FString::Printf(TEXT("%d Kacheln, %d Dreiecke, %d x %d Raster"), TerrainChunks.Num(), Tris, NU, NV));
	// Linientest auf Fusspfad-Punkte (dort stehen weder Baeume noch Bauten), bis die Gelaende-Kollision trifft.
	FVector Hit = FVector::ZeroVector;
	UPrimitiveComponent* HitComp = nullptr;
	bool bHitTerrain = false;
	for (int32 T = Trails.Num() - 1; T >= 1 && !bHitTerrain; --T)
	{
		const TArray<FVector>& S = Trails[T].Samples;
		for (int32 I = S.Num() / 3; I < S.Num() && !bHitTerrain; I += FMath::Max(1, S.Num() / 6))
		{
			bHitTerrain = TraceGround(FVector(S[I].X, S[I].Y, S[I].Z + 30.0), 60.f, Hit, &HitComp) && Cast<UProceduralMeshComponent>(HitComp) != nullptr;
		}
	}
	Check(TEXT("Gelaende-Kollision (Linientest)"), bCollision && bHitTerrain,
		FString::Printf(TEXT("Treffer %s bei Z %.1f m"), HitComp ? *HitComp->GetName() : TEXT("keiner"), Hit.Z));

	// Instanzbudget.
	const int32 Inst = GetTotalInstanceCount();
	Check(TEXT("Instanzen 1500..6000"), Inst >= 1500 && Inst <= 6000, FString::Printf(TEXT("%d (Baeume %d Staemme, Unterholz %d, Felsen %d)"), Inst,
		InstanceLayers.IsValidIndex(WL::TreeTrunk) && InstanceLayers[WL::TreeTrunk] ? InstanceLayers[WL::TreeTrunk]->GetInstanceCount() : 0,
		InstanceLayers.IsValidIndex(WL::Undergrowth) && InstanceLayers[WL::Undergrowth] ? InstanceLayers[WL::Undergrowth]->GetInstanceCount() : 0,
		(InstanceLayers.IsValidIndex(WL::RockBlock) && InstanceLayers[WL::RockBlock] ? InstanceLayers[WL::RockBlock]->GetInstanceCount() : 0)
		+ (InstanceLayers.IsValidIndex(WL::Boulder) && InstanceLayers[WL::Boulder] ? InstanceLayers[WL::Boulder]->GetInstanceCount() : 0)));
	bool bCull = InstanceLayers.Num() == WL::Count;
	for (int32 L = 0; L < InstanceLayers.Num(); ++L)
	{
		int32 CS = 0, CE = 0;
		if (InstanceLayers[L])
		{
			InstanceLayers[L]->GetCullDistances(CS, CE);
		}
		bCull &= LayerSpec(L).CullEnd == 0 || CE > 0;
	}
	Check(TEXT("Sichtweiten gesetzt"), bCull, TEXT("Vegetation/Kleinteile mit InstanceEndCullDistance"));

	// Stadtflaeche.
	FBox2D Region(ForceInit);
	for (int32 K = 0; K < 4; ++K)
	{
		const FVector P = ToWorld(FVector((K & 1) ? SizeM.X : 0.0, (K & 2) ? SizeM.Y : 0.0, 0.0));
		Region += FVector2D(P.X, P.Y);
	}
	if (const AMTACityGenerator* Gen = AMTACityGenerator::FindCityGenerator(this))
	{
		const FVector O = Gen->GetActorLocation();
		const FVector2D HalfExt = Gen->GetCityHalfExtent();
		const double E = Gen->RoadWidth * 0.5;
		const FBox2D City(FVector2D(O.X - HalfExt.X - E, O.Y - HalfExt.Y - E), FVector2D(O.X + HalfExt.X + E, O.Y + HalfExt.Y + E));
		Check(TEXT("Keine Ueberlappung mit der Stadtflaeche"), !Region.Intersect(City),
			FString::Printf(TEXT("Region X %.0f..%.0f m, Y %.0f..%.0f m"), Region.Min.X / 100.0, Region.Max.X / 100.0, Region.Min.Y / 100.0, Region.Max.Y / 100.0));
	}
	else
	{
		Check(TEXT("Keine Ueberlappung mit der Stadtflaeche"), true, TEXT("kein Stadtgenerator in der Welt"));
	}
	const FString PlacementDetail = PlacementReport.IsEmpty() ? FString(TEXT("manuell platziert, nicht geprueft")) : PlacementReport.Replace(TEXT("\n"), TEXT(" | "));
	Check(TEXT("Platzierung ohne Strassen/Wasser/Kueste"), bPlacementFree, PlacementDetail);
	Check(TEXT("Zufahrt fluchtet mit Stadtstrasse"), bEntryAlignedWithCityRoad || !AMTACityGenerator::FindCityGenerator(this), FString::Printf(TEXT("V = %.1f m"), EntryV));

	// Steigung des Fahrwegs (Linientests auf Strasse/Gelaende).
	float MaxSlope = 0.f;
	int32 Missing = 0;
	if (Trails.Num() > 0)
	{
		TArray<FVector2D> Pts;
		for (const FVector& S : Trails[0].Samples)
		{
			Pts.Add(FVector2D(S.X, S.Y));
		}
		const TArray<FVector2D> Fine = Resample(Pts, 1.f);
		TArray<float> Z;
		for (const FVector2D& P : Fine)
		{
			FVector G;
			const float Expect = HeightAt(static_cast<float>(P.X), static_cast<float>(P.Y));
			if (TraceGround(FVector(P.X, P.Y, Expect + 3.0), 8.f, G))
			{
				Z.Add(static_cast<float>(G.Z));
			}
			else
			{
				++Missing;
				Z.Add(Expect);
			}
		}
		for (int32 I = 0; I + 4 < Z.Num(); ++I)
		{
			const float Ds = static_cast<float>(FVector2D::Distance(Fine[I], Fine[I + 4]));
			if (Ds > 0.5f)
			{
				MaxSlope = FMath::Max(MaxSlope, FMath::Abs(Z[I + 4] - Z[I]) / Ds);
			}
		}
	}
	Check(TEXT("Fahrweg-Steigung <= 12 %"), Trails.Num() > 0 && MaxSlope <= 0.12f && Missing == 0, FString::Printf(TEXT("max. %.1f %%, %d Fehltreffer"), MaxSlope * 100.f, Missing));

	// Wasserfall, Hoehle, Eingang.
	const int32 RibbonCount = Ribbons ? Ribbons->GetInstanceCount() : 0;
	Check(TEXT("Wasserfall"), RibbonCount >= 20 && FallTopZ - FallBottomZ > 15.f,
		FString::Printf(TEXT("%d Bandsegmente, %.1f m hoch, Gischt %d, Ton %s"), RibbonCount, FallTopZ - FallBottomZ, Mist ? Mist->GetInstanceCount() : 0, WaterfallAudio ? TEXT("ja") : TEXT("nein")));
	const int32 Rocks = InstanceLayers.IsValidIndex(WL::RockBlock) && InstanceLayers[WL::RockBlock] ? InstanceLayers[WL::RockBlock]->GetInstanceCount() : 0;
	Check(TEXT("Hoehle und Eingang vorhanden"), VeilTrigger != nullptr && CaveLight != nullptr && Rocks >= 12, FString::Printf(TEXT("Felsbauteile %d"), Rocks));

	// Erreichbarkeit: Strasse -> Aussicht -> Felsband -> Hoehle -> Grotte.
	{
		const float Zf = FloorZ();
		TArray<FVector> Route; // U, V, erwartete Hoehe
		if (Trails.Num() > 0)
		{
			const TArray<FVector>& S = Trails[0].Samples;
			for (int32 I = 0; I < S.Num(); I += 5)
			{
				Route.Add(S[I]);
			}
			Route.Add(S.Last());
		}
		if (Trails.Num() > 1)
		{
			for (const FVector& S : Trails[1].Samples)
			{
				Route.Add(S);
			}
		}
		for (float V = FallV - 18.f; V <= FallV; V += 3.f)
		{
			Route.Add(FVector(CliffU - 3.2f, V, Zf));
		}
		Route.Add(FVector(CliffU - 1.f, FallV, Zf));
		Route.Add(FVector(CliffU + 1.5f, FallV, Zf));
		Route.Add(FVector(CliffU + 4.f, FallV, Zf));
		Route.Add(FVector(CliffU + 7.f, FallV, Zf));
		Route.Add(FVector(CliffU + 9.f, FallV, Zf));

		int32 Blocked = 0;
		int32 NoGround = 0;
		int32 Steep = 0;
		FString FirstProblem;
		FVector PrevGround = FVector::ZeroVector;
		bool bHavePrev = false;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MTAWildernessReach), true);
		for (const FVector& P : Route)
		{
			FVector G;
			// In der Hoehle unter der Decke ansetzen, sonst 2,5 m ueber der erwarteten Hoehe.
			const bool bInCave = P.X > CliffU - 1.5f && FMath::Abs(P.Y - FallV) < 5.f;
			const FVector Top(P.X, P.Y, (bInCave ? Zf : static_cast<float>(P.Z)) + 2.5f);
			if (!TraceGround(Top, 8.f, G))
			{
				++NoGround;
				if (FirstProblem.IsEmpty()) { FirstProblem = FString::Printf(TEXT("kein Boden bei U %.0f V %.0f"), P.X, P.Y); }
				continue;
			}
			if (bHavePrev && World)
			{
				const double Dist = FVector2D::Distance(FVector2D(G.X, G.Y), FVector2D(PrevGround.X, PrevGround.Y));
				if (Dist > 0.2 && FMath::Abs(G.Z - PrevGround.Z) > FMath::Max(0.5, Dist * 1.0))
				{
					++Steep;
					if (FirstProblem.IsEmpty()) { FirstProblem = FString::Printf(TEXT("Stufe %.1f m bei U %.0f V %.0f"), G.Z - PrevGround.Z, P.X, P.Y); }
				}
				FHitResult Block;
				if (World->LineTraceSingleByChannel(Block, ToWorld(PrevGround + FVector(0.0, 0.0, 1.0)), ToWorld(G + FVector(0.0, 0.0, 1.0)), ECC_WorldStatic, Params))
				{
					++Blocked;
					if (FirstProblem.IsEmpty()) { FirstProblem = FString::Printf(TEXT("blockiert von %s bei U %.0f V %.0f"), Block.GetComponent() ? *Block.GetComponent()->GetName() : TEXT("?"), P.X, P.Y); }
				}
			}
			PrevGround = G;
			bHavePrev = true;
		}
		Check(TEXT("Eingang erreichbar (Strasse -> Hoehle)"), Blocked == 0 && NoGround == 0 && Steep == 0,
			FString::Printf(TEXT("%d Punkte, %d blockiert, %d ohne Boden, %d zu steil%s%s"), Route.Num(), Blocked, NoGround, Steep,
				FirstProblem.IsEmpty() ? TEXT("") : TEXT(": "), *FirstProblem));
		FVector CaveFloor;
		const bool bFloor = TraceGround(FVector(CliffU + 9.f, FallV, Zf + 2.5f), 6.f, CaveFloor);
		Check(TEXT("Hoehlenboden plausibel"), bFloor && FMath::Abs(CaveFloor.Z - Zf) < 0.5 && Zf > BaseZ() && Zf < PlateauZ() - 5.f,
			FString::Printf(TEXT("Boden %.2f m, Becken %.2f m, Plateau %.2f m"), bFloor ? CaveFloor.Z : -1.0, FallBottomZ, PlateauZ()));
	}

	// Entdeckungen registriert und verankert.
	if (UMTADiscoverySubsystem* Disc = DiscoveryOf(this))
	{
		int32 Ok = 0;
		TArray<FString> Bad;
		const FBox2D Wide(Region.Min - FVector2D(5000.0, 5000.0), Region.Max + FVector2D(5000.0, 5000.0));
		for (const TCHAR* Id : WildDiscoveryIds)
		{
			FMTADiscoveryDef Def;
			FVector Loc;
			if (Disc->GetDiscovery(FName(Id), Def) && Disc->GetDiscoveryLocation(FName(Id), Loc) && Wide.IsInside(FVector2D(Loc.X, Loc.Y)))
			{
				++Ok;
			}
			else
			{
				Bad.Add(Id);
			}
		}
		Check(TEXT("Entdeckungen registriert"), Bad.Num() == 0, FString::Printf(TEXT("%d von %d%s%s"), Ok, static_cast<int32>(UE_ARRAY_COUNT(WildDiscoveryIds)),
			Bad.Num() ? TEXT(", fehlt: ") : TEXT(""), *FString::Join(Bad, TEXT(", "))));
	}
	else
	{
		Check(TEXT("Entdeckungen registriert"), false, TEXT("UMTADiscoverySubsystem fehlt"));
	}

	// Assets.
	bool bMats = LoadedMaterials.Num() >= WMat::Count;
	FString MissingMats;
	for (int32 M = 0; M < WMat::Count; ++M)
	{
		if (!LoadedMaterials.IsValidIndex(M) || !LoadedMaterials[M])
		{
			bMats = false;
			MissingMats += FString(WildMaterialPaths[M]) + TEXT(" ");
		}
	}
	Check(TEXT("Grundkoerper geladen"), CubeMesh && CylinderMesh && SphereMesh && ConeMesh, TEXT("/Engine/BasicShapes"));
	Check(TEXT("Materialien geladen"), bMats, MissingMats.IsEmpty() ? TEXT("9 Projektmaterialien") : MissingMats);
	Check(TEXT("Gelaende-Material"), !ResolvedTerrainMaterial.IsEmpty() && ResolvedTerrainMaterial != TEXT("FEHLT"), ResolvedTerrainMaterial);
	Check(TEXT("Wasserfall-Material"), !ResolvedWaterfallMaterial.IsEmpty() && ResolvedWaterfallMaterial != TEXT("FEHLT"), ResolvedWaterfallMaterial);
	Check(TEXT("Wasserfall-Ton"), !ResolvedSound.IsEmpty() && ResolvedSound != TEXT("FEHLT"), ResolvedSound);

	FString Report = FString::Printf(TEXT("WILDNIS SELFTEST %s: %s, %d Instanzen, Zentrum %s\n"), *RegionTitle, *PlacementName, Inst, *GetWildernessCenter().ToString());
	for (const FString& Line : Lines)
	{
		UE_LOG(LogMTAWilderness, Display, TEXT("%s"), *Line);
		Report += Line + TEXT("\n");
	}
	const FString Result = FString::Printf(TEXT("WILDNISTEST ERGEBNIS: %d Fehler"), Failures);
	UE_LOG(LogMTAWilderness, Display, TEXT("%s"), *Result);
	Report += Result;
	return Report;
}

#undef LOCTEXT_NAMESPACE
