#include "MTAInteriorInstance.h"
#include "MTAInteriorInteractive.h"
#include "MTAInteriorData.h"
#include "MTAInteriorSubsystem.h"
#include "MTAShopComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr int32 NumShapes = (int32)EMTAPrimShape::Count;
	constexpr int32 NumLayers = (int32)EMTAPrimLayer::Count;
}

AMTAInteriorInstance::AMTAInteriorInstance()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickInterval = 0.05f;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// Laufzeit-Spawn: Movable (keine gebackene Beleuchtung, Lumen/dynamische Lichter)
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;

	// Platzhalter-Grundformen der Engine (immer vorhanden, keine Projekt-Assets noetig)
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	ShapeMeshes = { CubeMesh.Object, CylinderMesh.Object, SphereMesh.Object, ConeMesh.Object, PlaneMesh.Object };

	InteractiveClass = AMTAInteriorInteractive::StaticClass();
}

UInstancedStaticMeshComponent* AMTAInteriorInstance::GetOrCreatePrimComponent(EMTAPrimShape Shape, EMTAPrimLayer Layer)
{
	if (PrimComponents.Num() != NumShapes * NumLayers) { PrimComponents.SetNum(NumShapes * NumLayers); }
	const int32 Index = (int32)Layer * NumShapes + (int32)Shape;
	if (PrimComponents[Index]) { return PrimComponents[Index]; }

	const FName Name(*FString::Printf(TEXT("Prims_L%d_S%d"), (int32)Layer, (int32)Shape));
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this, Name);
	ISM->SetMobility(EComponentMobility::Movable);
	ISM->SetupAttachment(Root);
	ISM->SetStaticMesh(ShapeMeshes.IsValidIndex((int32)Shape) ? ShapeMeshes[(int32)Shape].Get() : nullptr);
	ISM->SetNumCustomDataFloats(4);
	UMaterialInterface* Mat = Layer == EMTAPrimLayer::Glass ? GlassMaterial.Get()
		: Layer == EMTAPrimLayer::Backdrop ? BackdropMaterial.Get() : PrimMaterial.Get();
	if (Mat) { ISM->SetMaterial(0, Mat); }
	switch (Layer)
	{
	case EMTAPrimLayer::Solid:
	case EMTAPrimLayer::Glass:
		ISM->SetCollisionProfileName(TEXT("BlockAll"));
		break;
	default:
		ISM->SetCollisionProfileName(TEXT("NoCollision"));
		ISM->SetCanEverAffectNavigation(false);
		break;
	}
	ISM->SetCastShadow(Layer == EMTAPrimLayer::Solid);
	ISM->RegisterComponent();
	AddInstanceComponent(ISM);
	PrimComponents[Index] = ISM;
	return ISM;
}

void AMTAInteriorInstance::BuildFrom(const FMTAInteriorBuildResult& Result, const FMTAInteriorTemplate& Template, FName InEntryKey,
	EMTADistrict InDistrict, FName DefaultRole)
{
	EntryKey = InEntryKey;
	TemplateId = Template.Id;
	RoomType = Template.RoomType;
	DisplayLabel = Template.Label;
	District = InDistrict;
	Rooms = Result.Rooms;
	LocalEntranceSpawns = Result.EntranceSpawns;
	LocalBounds = Result.Bounds;
	CostKB = Result.EstimateCostKB();
	RoomLightsOn.Init(true, Rooms.Num());

	LocalNpcSlots = Result.NpcSlots;
	for (FMTANPCSlot& Slot : LocalNpcSlots)
	{
		if (Slot.Role.IsNone()) { Slot.Role = DefaultRole; }
	}

	// 1) Primitive nach (Form, Ebene) sammeln und gebuendelt als Instanzen anlegen
	TArray<TArray<int32>> Buckets;
	Buckets.SetNum(NumShapes * NumLayers);
	for (int32 i = 0; i < Result.Prims.Num(); ++i)
	{
		const FMTAPrimInstance& P = Result.Prims[i];
		Buckets[(int32)P.Layer * NumShapes + (int32)P.Shape].Add(i);
	}
	for (int32 B = 0; B < Buckets.Num(); ++B)
	{
		if (Buckets[B].Num() == 0) { continue; }
		const EMTAPrimLayer Layer = (EMTAPrimLayer)(B / NumShapes);
		const EMTAPrimShape Shape = (EMTAPrimShape)(B % NumShapes);
		UInstancedStaticMeshComponent* ISM = GetOrCreatePrimComponent(Shape, Layer);
		if (Layer == EMTAPrimLayer::Backdrop) { BackdropComponentIndex = B; }
		TArray<FTransform> Xfs;
		Xfs.Reserve(Buckets[B].Num());
		for (int32 Idx : Buckets[B]) { Xfs.Add(Result.Prims[Idx].Xf); }
		const int32 First = ISM->GetInstanceCount();
		ISM->AddInstances(Xfs, false, false, Layer == EMTAPrimLayer::Solid);
		for (int32 k = 0; k < Buckets[B].Num(); ++k)
		{
			const FMTAPrimInstance& P = Result.Prims[Buckets[B][k]];
			const float Data[4] = { P.Color.R, P.Color.G, P.Color.B, P.Emissive };
			ISM->SetCustomData(First + k, TArrayView<const float>(Data, 4), k == Buckets[B].Num() - 1);
		}
	}

	// 2) Lichter
	for (const FMTALightSpec& L : Result.Lights)
	{
		ULocalLightComponent* Light = nullptr;
		if (L.bWindow)
		{
			URectLightComponent* Rect = NewObject<URectLightComponent>(this);
			Rect->SetSourceWidth(FMath::Max(50.f, L.Width));
			Rect->SetSourceHeight(FMath::Max(50.f, L.Height));
			Rect->SetBarnDoorAngle(60.f);
			Light = Rect;
		}
		else
		{
			UPointLightComponent* Point = NewObject<UPointLightComponent>(this);
			Point->SetSourceRadius(8.f);
			Light = Point;
		}
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetupAttachment(Root);
		Light->SetRelativeLocationAndRotation(L.Pos, L.Rot);
		Light->SetIntensityUnits(ELightUnits::Unitless);
		Light->SetIntensity(L.Intensity);
		Light->SetLightColor(L.Color);
		Light->SetAttenuationRadius(L.Radius);
		Light->SetCastShadows(L.bShadows);
		Light->RegisterComponent();
		AddInstanceComponent(Light);
		LightComponents.Add(Light);
		FLightInfo Info;
		Info.BaseIntensity = L.Intensity;
		Info.RoomIndex = L.RoomIndex;
		Info.bFlicker = L.bFlicker;
		Info.bWindow = L.bWindow;
		LightInfos.Add(Info);
	}

	// 3) Interaktive Objekte
	UWorld* World = GetWorld();
	if (World && InteractiveClass)
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		UMTAInteriorSubsystem* Sub = World->GetSubsystem<UMTAInteriorSubsystem>();
		for (const FMTAInteractiveSpec& Spec : Result.Interactives)
		{
			// Bereits gefundene Geheimnisse nicht erneut auslegen
			if (Spec.Kind == EMTAInteractiveKind::Secret && Sub && Sub->IsSecretFound(MakeGlobalSecretId(Spec.SecretId))) { continue; }
			const FTransform WorldXf = Spec.Xf * GetActorTransform();
			AMTAInteriorInteractive* Act = World->SpawnActor<AMTAInteriorInteractive>(InteractiveClass, WorldXf, Params);
			if (!Act) { continue; }
			Act->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
			Act->Setup(Spec, this);
			Interactives.Add(Act);
		}
	}

	// 4) Laden
	if (!Template.Shop.IsNone())
	{
		Shop = NewObject<UMTAShopComponent>(this, TEXT("Shop"));
		Shop->RegisterComponent();
		AddInstanceComponent(Shop);
		Shop->InitFromData(Template.Shop, District);
	}
}

FName AMTAInteriorInstance::MakeGlobalSecretId(FName LocalId) const
{
	return FName(*FString::Printf(TEXT("%s@%s"), *LocalId.ToString(), *EntryKey.ToString()));
}

void AMTAInteriorInstance::ApplyDaylight(float Hour, float WeatherDim)
{
	// Sonnenhoehe grob: 6 Uhr Aufgang, 18 Uhr Untergang
	const float H = FMath::Fmod(FMath::Fmod(Hour, 24.f) + 24.f, 24.f);
	const float Sun = FMath::Clamp(FMath::Sin((H - 6.f) / 12.f * PI), 0.f, 1.f);
	const float Twilight = FMath::Clamp(1.f - FMath::Abs(H - 6.5f) / 1.5f, 0.f, 1.f) + FMath::Clamp(1.f - FMath::Abs(H - 18.f) / 1.5f, 0.f, 1.f);
	const float Dim = 1.f - 0.7f * FMath::Clamp(WeatherDim, 0.f, 1.f);

	const FLinearColor Noon(0.95f, 0.97f, 1.f), Dusk(1.f, 0.62f, 0.38f), Night(0.32f, 0.4f, 0.7f);
	FLinearColor LightC = FLinearColor::LerpUsingHSV(Noon, Dusk, FMath::Clamp(Twilight, 0.f, 1.f));
	if (Sun <= 0.01f) { LightC = Night; }
	const float LightI = (Sun > 0.01f ? Sun : 0.04f) * Dim * WindowDaylightIntensity;

	for (int32 i = 0; i < LightComponents.Num(); ++i)
	{
		if (!LightInfos[i].bWindow || !LightComponents[i]) { continue; }
		LightInfos[i].BaseIntensity = LightI;
		LightComponents[i]->SetLightColor(LightC);
		LightComponents[i]->SetIntensity(LightI);
	}

	// Kulisse: Tageshimmel oder Stadtschein bei Nacht
	if (PrimComponents.IsValidIndex(BackdropComponentIndex) && PrimComponents[BackdropComponentIndex])
	{
		UInstancedStaticMeshComponent* ISM = PrimComponents[BackdropComponentIndex];
		const FLinearColor Sky = FLinearColor::LerpUsingHSV(FLinearColor(0.45f, 0.62f, 0.9f), FLinearColor(0.95f, 0.5f, 0.3f), FMath::Clamp(Twilight, 0.f, 1.f));
		const FLinearColor CityGlow(0.06f, 0.05f, 0.1f);
		const FLinearColor C = FLinearColor::LerpUsingHSV(CityGlow, Sky, Sun) * Dim;
		const float E = FMath::Lerp(0.6f, BackdropDayEmissive, Sun) * Dim;
		const int32 N = ISM->GetInstanceCount();
		for (int32 k = 0; k < N; ++k)
		{
			const float Data[4] = { C.R, C.G, C.B, E };
			ISM->SetCustomData(k, TArrayView<const float>(Data, 4), k == N - 1);
		}
	}
}

void AMTAInteriorInstance::SetDormant(bool bInDormant)
{
	bDormant = bInDormant;
	SetActorHiddenInGame(bInDormant);
	SetActorEnableCollision(!bInDormant);
	SetActorTickEnabled(!bInDormant);
	for (AMTAInteriorInteractive* Act : Interactives)
	{
		if (Act) { Act->SetDormant(bInDormant); }
	}
	UpdateLightVisibility();
	if (const UWorld* World = GetWorld()) { LastUsedTime = World->GetTimeSeconds(); }
}

void AMTAInteriorInstance::UpdateLightVisibility()
{
	for (int32 i = 0; i < LightComponents.Num(); ++i)
	{
		if (!LightComponents[i]) { continue; }
		const int32 Room = LightInfos[i].RoomIndex;
		const bool bRoomOn = LightInfos[i].bWindow || !RoomLightsOn.IsValidIndex(Room) || RoomLightsOn[Room];
		LightComponents[i]->SetVisibility(!bDormant && bRoomOn);
	}
}

bool AMTAInteriorInstance::GetEntranceSpawn(EMTAEntranceKind Kind, FTransform& OutWorld) const
{
	const FTransform* Local = LocalEntranceSpawns.Find(Kind);
	if (!Local) { Local = LocalEntranceSpawns.Find(EMTAEntranceKind::Front); }
	if (!Local)
	{
		for (const TPair<EMTAEntranceKind, FTransform>& P : LocalEntranceSpawns) { Local = &P.Value; break; }
	}
	if (!Local) { return false; }
	OutWorld = *Local * GetActorTransform();
	return true;
}

TArray<FMTANPCSlot> AMTAInteriorInstance::GetNPCSlots() const
{
	TArray<FMTANPCSlot> Out = LocalNpcSlots;
	for (FMTANPCSlot& S : Out) { S.Transform = S.Transform * GetActorTransform(); }
	return Out;
}

void AMTAInteriorInstance::SetRoomLights(int32 RoomIndex, bool bOn)
{
	if (!RoomLightsOn.IsValidIndex(RoomIndex)) { return; }
	RoomLightsOn[RoomIndex] = bOn;
	UpdateLightVisibility();
}

void AMTAInteriorInstance::ToggleRoomLights(int32 RoomIndex)
{
	if (RoomLightsOn.IsValidIndex(RoomIndex)) { SetRoomLights(RoomIndex, !RoomLightsOn[RoomIndex]); }
}

void AMTAInteriorInstance::ActivateLink(FName LinkId)
{
	if (LinkId.IsNone()) { return; }
	if (LinkId == FName(TEXT("lights_off")))
	{
		for (int32 r = 0; r < RoomLightsOn.Num(); ++r) { RoomLightsOn[r] = false; }
		UpdateLightVisibility();
		return;
	}
	for (AMTAInteriorInteractive* Act : Interactives)
	{
		if (Act && (Act->InteractiveId == LinkId || Act->LocalSecretId == LinkId)) { Act->ActivateFromLink(); }
	}
}

void AMTAInteriorInstance::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDormant) { return; }
	// Flackernde Roehren/Neon: zufaellige kurze Aussetzer
	for (int32 i = 0; i < LightComponents.Num(); ++i)
	{
		if (!LightInfos[i].bFlicker || !LightComponents[i]) { continue; }
		const float R = FMath::FRand();
		LightComponents[i]->SetIntensity(R < 0.06f ? LightInfos[i].BaseIntensity * 0.08f : (R < 0.12f ? LightInfos[i].BaseIntensity * 0.6f : LightInfos[i].BaseIntensity));
	}
}
