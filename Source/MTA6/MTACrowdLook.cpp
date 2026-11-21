#include "MTACrowdLook.h"
#include "MTAWardrobe.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

void UMTACrowdLookSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMTAAppearanceSubsystem>();
}

int32 UMTACrowdLookSubsystem::HashSeed(int64 SeedId, int32 Salt)
{
	// Stabiler 64->32-Bit-Mix (kein Platform-Hash), damit Ergebnisse plattformuebergreifend gleich bleiben.
	uint64 X = static_cast<uint64>(SeedId) + 0x9E3779B97F4A7C15ULL * static_cast<uint64>(Salt + 1);
	X = (X ^ (X >> 30)) * 0xBF58476D1CE4E5B9ULL;
	X = (X ^ (X >> 27)) * 0x94D049BB133111EBULL;
	X = X ^ (X >> 31);
	return static_cast<int32>(X & 0x7FFFFFFF);
}

FMTACrowdLook UMTACrowdLookSubsystem::BuildLook(int64 SeedId, const FMTALookContext& Context) const
{
	FMTACrowdLook Look;
	Look.SeedId = SeedId;
	const UGameInstance* GI = GetGameInstance();
	const UMTAAppearanceSubsystem* Appearances = GI ? GI->GetSubsystem<UMTAAppearanceSubsystem>() : nullptr;
	if (!Appearances || !Appearances->IsLoaded())
	{
		// Ohne Daten: neutrale Figur, damit der Aufrufer trotzdem etwas darstellen kann.
		Look.Appearance.Id = FName(*FString::Printf(TEXT("crowd_%lld"), SeedId));
		FillCustomData(Look.Appearance, 1.f, 1.f, -1, Look.CustomData);
		return Look;
	}

	FRandomStream Stream(HashSeed(SeedId, 1));
	const FName ArchetypeId = Appearances->PickArchetype(Context, Stream);
	const FMTAArchetype* Arch = Appearances->FindArchetypePtr(ArchetypeId);
	Look.ArchetypeId = ArchetypeId;
	Look.Appearance = Appearances->GenerateFromArchetype(ArchetypeId, HashSeed(SeedId, 2), Context);
	Look.Appearance.Id = FName(*FString::Printf(TEXT("crowd_%lld"), SeedId));

	// Mesh-Gruppe: Alter/Rahmen -> adult_fem, adult_masc, child, elder
	FString Group = Arch ? Arch->MeshGroup.ToString() : TEXT("adult");
	if (Group == TEXT("adult"))
	{
		FRandomStream Pick(HashSeed(SeedId, 3));
		const bool bFem = Look.Appearance.Frame == EMTABodyFrame::Feminine || (Look.Appearance.Frame == EMTABodyFrame::Androgynous && Pick.FRand() < 0.5f);
		Group = bFem ? TEXT("adult_fem") : TEXT("adult_masc");
	}
	Look.MeshGroup = FName(*Group);
	const TArray<FSoftObjectPath>* Variants = Appearances->GetCrowdMeshGroups().Find(Look.MeshGroup);
	if (Variants && Variants->Num() > 0)
	{
		FRandomStream Pick(HashSeed(SeedId, 4));
		Look.MeshVariant = Pick.RandRange(0, Variants->Num() - 1);
	}

	Look.HeightScale = Look.Appearance.HeightCm / ReferenceHeightCm;
	Look.WidthScale = FMath::Lerp(0.85f, 1.2f, Look.Appearance.Build);
	FillCustomData(Look.Appearance, Look.HeightScale, Look.WidthScale, Look.MeshVariant, Look.CustomData);
	Look.Conspicuousness = Appearances->ComputeAegisConspicuousness(Look.Appearance);
	return Look;
}

FMTACrowdLook UMTACrowdLookSubsystem::GenerateLook(int64 SeedId, const FMTALookContext& Context) const
{
	return BuildLook(SeedId, Context);
}

FMTACrowdLook UMTACrowdLookSubsystem::GenerateStableLook(int64 SeedId, EMTADistrict District) const
{
	FMTALookContext Ctx;
	Ctx.District = District;
	Ctx.Hour = 12.f;        // feste Tageszeit: Archetyp aendert sich nicht
	Ctx.TemperatureC = 18.f; // gemaessigt: keine Wetteranpassung
	Ctx.bRaining = false;
	return BuildLook(SeedId, Ctx);
}

void UMTACrowdLookSubsystem::FillCustomData(const FMTAAppearance& A, float HeightScale, float WidthScale, int32 MeshVariant, TArray<float>& Out)
{
	Out.Reset();
	Out.SetNumZeroed(MTACrowdData::Num);
	auto SetRGB = [&Out](int32 Base, const FLinearColor& C)
	{
		Out[Base] = C.R; Out[Base + 1] = C.G; Out[Base + 2] = C.B;
	};
	auto Tint = [&A](EMTAWardrobeSlot Slot, const FLinearColor& Fallback)
	{
		const FLinearColor* T = A.OutfitTints.Find(Slot);
		return T ? *T : Fallback;
	};

	Out[MTACrowdData::SkinTone] = A.SkinTone;
	Out[MTACrowdData::HeightScale] = HeightScale;
	Out[MTACrowdData::BuildScale] = WidthScale;

	// Fullbody ersetzt Top/Bottom: dann beide Slots aus dessen Tint.
	const FLinearColor Neutral(0.2f, 0.2f, 0.2f);
	const FLinearColor Full = Tint(EMTAWardrobeSlot::Fullbody, Neutral);
	const bool bFull = A.Outfit.Contains(EMTAWardrobeSlot::Fullbody);
	SetRGB(MTACrowdData::TopTint, bFull ? Full : Tint(EMTAWardrobeSlot::Top, Neutral));
	SetRGB(MTACrowdData::BottomTint, bFull ? Full : Tint(EMTAWardrobeSlot::Bottom, Neutral));

	// Haarfarbe mit Ergrauung mischen.
	SetRGB(MTACrowdData::HairTint, FMath::Lerp(A.HairColor, FLinearColor(0.6f, 0.6f, 0.62f), A.HairGray));
	Out[MTACrowdData::MeshVariant] = static_cast<float>(MeshVariant);
	const FLinearColor Accent = A.OutfitTints.Contains(EMTAWardrobeSlot::Jacket) ? Tint(EMTAWardrobeSlot::Jacket, Neutral) : Tint(EMTAWardrobeSlot::Bag, Neutral);
	SetRGB(MTACrowdData::AccentTint, Accent);
}

UStaticMesh* UMTACrowdLookSubsystem::GetPlaceholderMesh() const
{
	static const FSoftObjectPath Path(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	return Cast<UStaticMesh>(Path.TryLoad());
}

void UMTACrowdLookSubsystem::ConfigureInstancedComponent(UInstancedStaticMeshComponent* Component, bool bUsePlaceholderIfEmpty) const
{
	if (!Component)
	{
		return;
	}
	Component->SetNumCustomDataFloats(MTACrowdData::Num);
	if (bUsePlaceholderIfEmpty && !Component->GetStaticMesh())
	{
		Component->SetStaticMesh(GetPlaceholderMesh());
	}
}

FTransform UMTACrowdLookSubsystem::MakeInstanceTransform(const FMTACrowdLook& Look, const FVector& Foot, float Yaw, bool bPlaceholder) const
{
	const FRotator Rot(0.f, Yaw, 0.f);
	if (bPlaceholder)
	{
		// Zylinder: 100 cm Durchmesser/Hoehe, Pivot Mitte -> Skalierung in "Meter", Anhebung um halbe Hoehe.
		const float Height = Look.Appearance.HeightCm;
		const float Width = PlaceholderBaseWidthCm * Look.WidthScale;
		return FTransform(Rot, Foot + FVector(0.f, 0.f, Height * 0.5f), FVector(Width / 100.f, Width / 100.f, Height / 100.f));
	}
	return FTransform(Rot, Foot, FVector(Look.WidthScale, Look.WidthScale, Look.HeightScale));
}

int32 UMTACrowdLookSubsystem::AddCrowdInstance(UInstancedStaticMeshComponent* Component, const FMTACrowdLook& Look, const FVector& Foot, float Yaw, bool bPlaceholder) const
{
	if (!Component)
	{
		return INDEX_NONE;
	}
	if (Component->NumCustomDataFloats != MTACrowdData::Num)
	{
		Component->SetNumCustomDataFloats(MTACrowdData::Num);
	}
	const int32 Index = Component->AddInstance(MakeInstanceTransform(Look, Foot, Yaw, bPlaceholder), /*bWorldSpace=*/true);
	if (Index != INDEX_NONE)
	{
		Component->SetCustomData(Index, MakeArrayView(Look.CustomData), /*bMarkRenderStateDirty=*/false);
	}
	return Index;
}

bool UMTACrowdLookSubsystem::UpdateCrowdInstanceLook(UInstancedStaticMeshComponent* Component, int32 InstanceIndex, const FMTACrowdLook& Look) const
{
	return Component && Component->SetCustomData(InstanceIndex, MakeArrayView(Look.CustomData), /*bMarkRenderStateDirty=*/true);
}

FSoftObjectPath UMTACrowdLookSubsystem::GetMeshPathForLook(const FMTACrowdLook& Look) const
{
	const UGameInstance* GI = GetGameInstance();
	const UMTAAppearanceSubsystem* Appearances = GI ? GI->GetSubsystem<UMTAAppearanceSubsystem>() : nullptr;
	if (Appearances && Look.MeshVariant >= 0)
	{
		if (const TArray<FSoftObjectPath>* V = Appearances->GetCrowdMeshGroups().Find(Look.MeshGroup))
		{
			if (V->IsValidIndex(Look.MeshVariant)) { return (*V)[Look.MeshVariant]; }
		}
	}
	return FSoftObjectPath();
}
