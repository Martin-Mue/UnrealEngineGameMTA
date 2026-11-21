#include "MTAAppearance.h"
#include "MTAWardrobe.h"
#include "Engine/GameInstance.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogMTAAppearance, Log, All);

// ---------------------------------------------------------------------------------------------
// JSON-/Farb-Helfer
// ---------------------------------------------------------------------------------------------

namespace MTAAppearanceJson
{
	bool LoadJsonFile(const FString& Path, TSharedPtr<FJsonObject>& OutObject)
	{
		const FString File = FPaths::IsRelative(Path) ? FPaths::ProjectContentDir() / Path : Path;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			UE_LOG(LogMTAAppearance, Error, TEXT("MTA6: Datei nicht lesbar: %s"), *File);
			return false;
		}
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), OutObject) || !OutObject.IsValid())
		{
			UE_LOG(LogMTAAppearance, Error, TEXT("MTA6: Kein gueltiges JSON: %s"), *File);
			return false;
		}
		return true;
	}

	FLinearColor ParseColor(const FString& Hex, const FLinearColor& Fallback)
	{
		if (Hex.IsEmpty())
		{
			return Fallback;
		}
		return FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
	}

	FString ColorToHex(const FLinearColor& Color)
	{
		const FColor C = Color.ToFColorSRGB();
		return FString::Printf(TEXT("#%02X%02X%02X"), C.R, C.G, C.B);
	}
}

namespace
{
	const TCHAR* GRegionIds[] = { TEXT("face"), TEXT("neck"), TEXT("scalp"), TEXT("armleft"), TEXT("armright"), TEXT("hands"), TEXT("torso"), TEXT("back"), TEXT("legs") };
	const TCHAR* GKindIds[] = { TEXT("scar"), TEXT("tattoo"), TEXT("implant") };
	const TCHAR* GSlotIds[] = { TEXT("hat"), TEXT("glasses"), TEXT("mask"), TEXT("neck"), TEXT("top"), TEXT("jacket"), TEXT("fullbody"), TEXT("bottom"), TEXT("gloves"), TEXT("shoes"), TEXT("bag") };
	const TCHAR* GDistrictIds[] = { TEXT("core"), TEXT("saltmarket"), TEXT("lanternhill"), TEXT("greenmoor"), TEXT("rustline"), TEXT("sunstrip") };
	const TCHAR* GStyleIds[] = { TEXT("workwear"), TEXT("business"), TEXT("casual"), TEXT("street"), TEXT("sport"), TEXT("formal"), TEXT("uniform"), TEXT("tactical"), TEXT("underground"), TEXT("luxury"), TEXT("beach"), TEXT("weather") };

	template <typename TEnum, int32 N>
	bool EnumFromId(const TCHAR* (&Ids)[N], const FString& Id, TEnum& Out)
	{
		for (int32 i = 0; i < N; ++i)
		{
			if (Id.Equals(Ids[i], ESearchCase::IgnoreCase))
			{
				Out = static_cast<TEnum>(i);
				return true;
			}
		}
		return false;
	}

	FString GetStr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FString& Def = FString())
	{
		FString V;
		return (O.IsValid() && O->TryGetStringField(Key, V)) ? V : Def;
	}

	double GetNum(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Def = 0.0)
	{
		double V = 0.0;
		return (O.IsValid() && O->TryGetNumberField(Key, V)) ? V : Def;
	}

	TSharedPtr<FJsonObject> GetObj(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* P = nullptr;
		if (O.IsValid() && O->TryGetObjectField(Key, P) && P)
		{
			return *P;
		}
		return nullptr;
	}

	/** Eingebaute Hautton-Palette (Fallback, identisch zu appearance.json). */
	const TArray<FLinearColor>& DefaultSkinTones()
	{
		static TArray<FLinearColor> Tones;
		if (Tones.Num() == 0)
		{
			const TCHAR* Hex[] = { TEXT("#f3d9c4"), TEXT("#ecc9a8"), TEXT("#e2b58f"), TEXT("#d4a074"), TEXT("#c48b5e"), TEXT("#b37748"),
				TEXT("#9d6438"), TEXT("#85502d"), TEXT("#6e3f24"), TEXT("#58301b"), TEXT("#442414"), TEXT("#321a0f") };
			for (const TCHAR* H : Hex)
			{
				Tones.Add(MTAAppearanceJson::ParseColor(H, FLinearColor::White));
			}
		}
		return Tones;
	}

	FLinearColor SampleTones(const TArray<FLinearColor>& Tones, float T)
	{
		if (Tones.Num() == 0)
		{
			return FLinearColor::White;
		}
		const float F = FMath::Clamp(T, 0.f, 1.f) * (Tones.Num() - 1);
		const int32 A = FMath::FloorToInt(F);
		const int32 B = FMath::Min(A + 1, Tones.Num() - 1);
		return FMath::Lerp(Tones[A], Tones[B], F - A);
	}

	/** Gleichverteilung mit leichter Glockenform (Mittel aus zwei Zufallszahlen), Ergebnis -1..1. */
	float Bell(FRandomStream& S)
	{
		return (S.FRand() + S.FRand()) - 1.f;
	}

	int32 PickIndex(const TArray<float>& Weights, FRandomStream& S)
	{
		float Sum = 0.f;
		for (float W : Weights) { Sum += FMath::Max(0.f, W); }
		if (Sum <= KINDA_SMALL_NUMBER) { return 0; }
		float R = S.FRand() * Sum;
		for (int32 i = 0; i < Weights.Num(); ++i)
		{
			R -= FMath::Max(0.f, Weights[i]);
			if (R <= 0.f) { return i; }
		}
		return Weights.Num() - 1;
	}
}

// ---------------------------------------------------------------------------------------------
// FMTAAppearance
// ---------------------------------------------------------------------------------------------

EMTALifeStage FMTAAppearance::GetLifeStage() const
{
	if (Age < 13) { return EMTALifeStage::Child; }
	if (Age < 18) { return EMTALifeStage::Teen; }
	if (Age < 60) { return EMTALifeStage::Adult; }
	return EMTALifeStage::Elder;
}

FLinearColor FMTAAppearance::GetSkinColor() const
{
	return SampleTones(DefaultSkinTones(), SkinTone);
}

FString FMTAAppearance::GetDefaultSaveDir()
{
	return FPaths::ProjectSavedDir() / TEXT("Characters");
}

TSharedRef<FJsonObject> FMTAAppearance::ToJson() const
{
	using namespace MTAAppearanceJson;
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("id"), Id.ToString());
	J->SetStringField(TEXT("name"), DisplayName);
	J->SetStringField(TEXT("archetype"), ArchetypeId.ToString());
	J->SetStringField(TEXT("origin"), OriginId.ToString());
	J->SetStringField(TEXT("frame"), UMTAAppearanceLibrary::FrameToId(Frame));
	J->SetStringField(TEXT("bodyType"), UMTAAppearanceLibrary::BodyTypeToId(BodyType));
	J->SetNumberField(TEXT("age"), Age);
	J->SetNumberField(TEXT("heightCm"), HeightCm);
	J->SetNumberField(TEXT("build"), Build);
	J->SetNumberField(TEXT("muscle"), Muscle);
	J->SetNumberField(TEXT("skinTone"), SkinTone);
	J->SetNumberField(TEXT("freckles"), Freckles);
	J->SetStringField(TEXT("eyeColor"), ColorToHex(EyeColor));

	TSharedRef<FJsonObject> Morphs = MakeShared<FJsonObject>();
	for (const TPair<FName, float>& P : FaceMorphs)
	{
		Morphs->SetNumberField(P.Key.ToString(), P.Value);
	}
	J->SetObjectField(TEXT("faceMorphs"), Morphs);

	TSharedRef<FJsonObject> Hair = MakeShared<FJsonObject>();
	Hair->SetStringField(TEXT("style"), HairStyle.ToString());
	Hair->SetStringField(TEXT("color"), ColorToHex(HairColor));
	Hair->SetNumberField(TEXT("gray"), HairGray);
	J->SetObjectField(TEXT("hair"), Hair);

	TSharedRef<FJsonObject> Beard = MakeShared<FJsonObject>();
	Beard->SetStringField(TEXT("style"), BeardStyle.ToString());
	Beard->SetStringField(TEXT("color"), ColorToHex(BeardColor));
	J->SetObjectField(TEXT("beard"), Beard);

	TSharedRef<FJsonObject> Mk = MakeShared<FJsonObject>();
	Mk->SetNumberField(TEXT("intensity"), Makeup.Intensity);
	Mk->SetStringField(TEXT("lip"), ColorToHex(Makeup.LipColor));
	Mk->SetStringField(TEXT("eye"), ColorToHex(Makeup.EyeColor));
	J->SetObjectField(TEXT("makeup"), Mk);

	TArray<TSharedPtr<FJsonValue>> MarkArr;
	for (const FMTABodyMark& M : Marks)
	{
		TSharedRef<FJsonObject> MO = MakeShared<FJsonObject>();
		MO->SetStringField(TEXT("id"), M.Id.ToString());
		MO->SetStringField(TEXT("kind"), GKindIds[static_cast<int32>(M.Kind)]);
		MO->SetStringField(TEXT("region"), GRegionIds[static_cast<int32>(M.Region)]);
		MO->SetNumberField(TEXT("intensity"), M.Intensity);
		MO->SetStringField(TEXT("color"), ColorToHex(M.Color));
		MarkArr.Add(MakeShared<FJsonValueObject>(MO));
	}
	J->SetArrayField(TEXT("marks"), MarkArr);

	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	for (const TPair<EMTAWardrobeSlot, FName>& P : Outfit)
	{
		Out->SetStringField(UMTAAppearanceLibrary::SlotToId(P.Key), P.Value.ToString());
	}
	J->SetObjectField(TEXT("outfit"), Out);

	TSharedRef<FJsonObject> Tints = MakeShared<FJsonObject>();
	for (const TPair<EMTAWardrobeSlot, FLinearColor>& P : OutfitTints)
	{
		Tints->SetStringField(UMTAAppearanceLibrary::SlotToId(P.Key), ColorToHex(P.Value));
	}
	J->SetObjectField(TEXT("tints"), Tints);
	return J;
}

bool FMTAAppearance::FromJson(const TSharedPtr<FJsonObject>& J)
{
	using namespace MTAAppearanceJson;
	if (!J.IsValid())
	{
		return false;
	}
	Id = FName(*GetStr(J, TEXT("id")));
	DisplayName = GetStr(J, TEXT("name"));
	ArchetypeId = FName(*GetStr(J, TEXT("archetype")));
	OriginId = FName(*GetStr(J, TEXT("origin")));
	UMTAAppearanceLibrary::FrameFromId(GetStr(J, TEXT("frame")), Frame);
	UMTAAppearanceLibrary::BodyTypeFromId(GetStr(J, TEXT("bodyType")), BodyType);
	Age = FMath::Clamp(static_cast<int32>(GetNum(J, TEXT("age"), 30)), 3, 95);
	HeightCm = FMath::Clamp(static_cast<float>(GetNum(J, TEXT("heightCm"), 172)), 90.f, 215.f);
	Build = FMath::Clamp(static_cast<float>(GetNum(J, TEXT("build"), 0.5)), 0.f, 1.f);
	Muscle = FMath::Clamp(static_cast<float>(GetNum(J, TEXT("muscle"), 0.4)), 0.f, 1.f);
	SkinTone = FMath::Clamp(static_cast<float>(GetNum(J, TEXT("skinTone"), 0.4)), 0.f, 1.f);
	Freckles = FMath::Clamp(static_cast<float>(GetNum(J, TEXT("freckles"), 0)), 0.f, 1.f);
	EyeColor = ParseColor(GetStr(J, TEXT("eyeColor")), EyeColor);

	FaceMorphs.Reset();
	if (TSharedPtr<FJsonObject> Morphs = GetObj(J, TEXT("faceMorphs")))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Morphs->Values)
		{
			double V = 0.0;
			if (P.Value.IsValid() && P.Value->TryGetNumber(V))
			{
				FaceMorphs.Add(FName(*P.Key), FMath::Clamp(static_cast<float>(V), -1.f, 1.f));
			}
		}
	}

	if (TSharedPtr<FJsonObject> Hair = GetObj(J, TEXT("hair")))
	{
		HairStyle = FName(*GetStr(Hair, TEXT("style")));
		HairColor = ParseColor(GetStr(Hair, TEXT("color")), HairColor);
		HairGray = FMath::Clamp(static_cast<float>(GetNum(Hair, TEXT("gray"), 0)), 0.f, 1.f);
	}
	if (TSharedPtr<FJsonObject> Beard = GetObj(J, TEXT("beard")))
	{
		BeardStyle = FName(*GetStr(Beard, TEXT("style")));
		BeardColor = ParseColor(GetStr(Beard, TEXT("color")), BeardColor);
	}
	if (TSharedPtr<FJsonObject> Mk = GetObj(J, TEXT("makeup")))
	{
		Makeup.Intensity = FMath::Clamp(static_cast<float>(GetNum(Mk, TEXT("intensity"), 0)), 0.f, 1.f);
		Makeup.LipColor = ParseColor(GetStr(Mk, TEXT("lip")), Makeup.LipColor);
		Makeup.EyeColor = ParseColor(GetStr(Mk, TEXT("eye")), Makeup.EyeColor);
	}

	Marks.Reset();
	const TArray<TSharedPtr<FJsonValue>>* MarkArr = nullptr;
	if (J->TryGetArrayField(TEXT("marks"), MarkArr) && MarkArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *MarkArr)
		{
			const TSharedPtr<FJsonObject> MO = V.IsValid() ? V->AsObject() : nullptr;
			if (!MO.IsValid())
			{
				continue;
			}
			FMTABodyMark M;
			M.Id = FName(*GetStr(MO, TEXT("id")));
			EnumFromId(GKindIds, GetStr(MO, TEXT("kind")), M.Kind);
			EnumFromId(GRegionIds, GetStr(MO, TEXT("region")), M.Region);
			M.Intensity = FMath::Clamp(static_cast<float>(GetNum(MO, TEXT("intensity"), 1)), 0.f, 1.f);
			M.Color = ParseColor(GetStr(MO, TEXT("color")), FLinearColor::White);
			Marks.Add(M);
		}
	}

	Outfit.Reset();
	if (TSharedPtr<FJsonObject> Out = GetObj(J, TEXT("outfit")))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Out->Values)
		{
			EMTAWardrobeSlot Slot;
			FString ItemId;
			if (UMTAAppearanceLibrary::SlotFromId(P.Key, Slot) && P.Value.IsValid() && P.Value->TryGetString(ItemId) && !ItemId.IsEmpty())
			{
				Outfit.Add(Slot, FName(*ItemId));
			}
		}
	}
	OutfitTints.Reset();
	if (TSharedPtr<FJsonObject> Tints = GetObj(J, TEXT("tints")))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Tints->Values)
		{
			EMTAWardrobeSlot Slot;
			FString Hex;
			if (UMTAAppearanceLibrary::SlotFromId(P.Key, Slot) && P.Value.IsValid() && P.Value->TryGetString(Hex))
			{
				OutfitTints.Add(Slot, ParseColor(Hex, FLinearColor::White));
			}
		}
	}
	return true;
}

FString FMTAAppearance::ToJsonString(bool bPretty) const
{
	FString Out;
	if (bPretty)
	{
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(ToJson(), W);
	}
	else
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(ToJson(), W);
	}
	return Out;
}

bool FMTAAppearance::FromJsonString(const FString& Text)
{
	TSharedPtr<FJsonObject> Parsed;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Parsed) || !Parsed.IsValid())
	{
		return false;
	}
	return FromJson(Parsed);
}

bool FMTAAppearance::SaveToFile(const FString& FilePath) const
{
	const FString File = FPaths::IsRelative(FilePath) ? GetDefaultSaveDir() / FilePath : FilePath;
	return FFileHelper::SaveStringToFile(ToJsonString(true), *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool FMTAAppearance::LoadFromFile(const FString& FilePath)
{
	const FString File = FPaths::IsRelative(FilePath) ? GetDefaultSaveDir() / FilePath : FilePath;
	FString Text;
	return FFileHelper::LoadFileToString(Text, *File) && FromJsonString(Text);
}

// ---------------------------------------------------------------------------------------------
// UMTAAppearanceAsset
// ---------------------------------------------------------------------------------------------

bool UMTAAppearanceAsset::ImportFromJsonString(const FString& JsonText)
{
	FMTAAppearance Parsed;
	if (!Parsed.FromJsonString(JsonText))
	{
		return false;
	}
	Modify();
	Appearance = Parsed;
	return true;
}

FString UMTAAppearanceAsset::ExportToJsonString() const
{
	return Appearance.ToJsonString(true);
}

// ---------------------------------------------------------------------------------------------
// UMTAAppearanceLibrary
// ---------------------------------------------------------------------------------------------

const TArray<FName>& UMTAAppearanceLibrary::GetFaceMorphNames()
{
	static const TArray<FName> Names = {
		TEXT("jaw_width"), TEXT("chin_length"), TEXT("chin_prominence"), TEXT("nose_length"), TEXT("nose_width"), TEXT("nose_bridge"),
		TEXT("eye_size"), TEXT("eye_spacing"), TEXT("eye_tilt"), TEXT("brow_height"), TEXT("brow_thickness"), TEXT("cheek_fullness"),
		TEXT("cheekbone_height"), TEXT("lip_fullness"), TEXT("mouth_width"), TEXT("ear_size"), TEXT("forehead_height"), TEXT("face_length"),
		TEXT("face_width"), TEXT("neck_thickness") };
	return Names;
}

FString UMTAAppearanceLibrary::SlotToId(EMTAWardrobeSlot Slot) { return GSlotIds[static_cast<int32>(Slot)]; }
bool UMTAAppearanceLibrary::SlotFromId(const FString& Id, EMTAWardrobeSlot& OutSlot) { return EnumFromId(GSlotIds, Id, OutSlot); }
FString UMTAAppearanceLibrary::DistrictToId(EMTADistrict District) { return GDistrictIds[FMath::Clamp(static_cast<int32>(District), 0, 5)]; }
bool UMTAAppearanceLibrary::DistrictFromId(const FString& Id, EMTADistrict& OutDistrict) { return EnumFromId(GDistrictIds, Id, OutDistrict); }
bool UMTAAppearanceLibrary::StyleFromId(const FString& Id, EMTAClothingStyle& Out) { return EnumFromId(GStyleIds, Id, Out); }

bool UMTAAppearanceLibrary::FrameFromId(const FString& Id, EMTABodyFrame& Out)
{
	const TCHAR* Ids[] = { TEXT("feminine"), TEXT("masculine"), TEXT("androgynous") };
	return EnumFromId(Ids, Id, Out);
}

FString UMTAAppearanceLibrary::FrameToId(EMTABodyFrame Frame)
{
	const TCHAR* Ids[] = { TEXT("feminine"), TEXT("masculine"), TEXT("androgynous") };
	return Ids[static_cast<int32>(Frame)];
}

bool UMTAAppearanceLibrary::BodyTypeFromId(const FString& Id, EMTABodyType& Out)
{
	const TCHAR* Ids[] = { TEXT("slim"), TEXT("average"), TEXT("athletic"), TEXT("heavy") };
	return EnumFromId(Ids, Id, Out);
}

FString UMTAAppearanceLibrary::BodyTypeToId(EMTABodyType Type)
{
	const TCHAR* Ids[] = { TEXT("slim"), TEXT("average"), TEXT("athletic"), TEXT("heavy") };
	return Ids[static_cast<int32>(Type)];
}

void UMTAAppearanceLibrary::ApplyMorphTargets(USkeletalMeshComponent* Body, const FMTAAppearance& A)
{
	if (!Body)
	{
		return;
	}
	for (const FName& Name : GetFaceMorphNames())
	{
		const float* V = A.FaceMorphs.Find(Name);
		Body->SetMorphTarget(FName(*(TEXT("mta_") + Name.ToString())), V ? *V : 0.f, true);
	}
	Body->SetMorphTarget(TEXT("mta_build"), A.Build, true);
	Body->SetMorphTarget(TEXT("mta_muscle"), A.Muscle, true);
	Body->SetMorphTarget(TEXT("mta_age"), FMath::Clamp((A.Age - 18) / 70.f, 0.f, 1.f), true);
}

void UMTAAppearanceLibrary::ApplyMaterialParameters(UPrimitiveComponent* Component, const FMTAAppearance& A)
{
	if (!Component)
	{
		return;
	}
	const int32 Num = Component->GetNumMaterials();
	for (int32 i = 0; i < Num; ++i)
	{
		UMaterialInstanceDynamic* MID = Component->CreateDynamicMaterialInstance(i);
		if (!MID)
		{
			continue;
		}
		MID->SetScalarParameterValue(TEXT("MTA_SkinTone"), A.SkinTone);
		MID->SetScalarParameterValue(TEXT("MTA_Freckles"), A.Freckles);
		MID->SetScalarParameterValue(TEXT("MTA_HairGray"), A.HairGray);
		MID->SetScalarParameterValue(TEXT("MTA_MakeupIntensity"), A.Makeup.Intensity);
		MID->SetScalarParameterValue(TEXT("MTA_Age"), FMath::Clamp((A.Age - 18) / 70.f, 0.f, 1.f));
		MID->SetVectorParameterValue(TEXT("MTA_SkinColor"), A.GetSkinColor());
		MID->SetVectorParameterValue(TEXT("MTA_EyeColor"), A.EyeColor);
		MID->SetVectorParameterValue(TEXT("MTA_HairColor"), A.HairColor);
		MID->SetVectorParameterValue(TEXT("MTA_BeardColor"), A.BeardColor);
		MID->SetVectorParameterValue(TEXT("MTA_LipColor"), A.Makeup.LipColor);
		MID->SetVectorParameterValue(TEXT("MTA_EyeShadow"), A.Makeup.EyeColor);
	}
}

void UMTAAppearanceLibrary::ApplyBodyScale(USceneComponent* Body, const FMTAAppearance& A, float ReferenceHeightCm)
{
	if (Body && ReferenceHeightCm > 1.f)
	{
		Body->SetRelativeScale3D(FVector(A.HeightCm / ReferenceHeightCm));
	}
}

// ---------------------------------------------------------------------------------------------
// UMTAAppearanceSubsystem
// ---------------------------------------------------------------------------------------------

void UMTAAppearanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Garderobe wird fuer Outfit-Erzeugung gebraucht und muss vorher geladen sein.
	Collection.InitializeDependency<UMTAWardrobeSubsystem>();
	LoadAppearanceData();
}

namespace
{
	TArray<TPair<FLinearColor, float>> ReadColorWeights(const TSharedPtr<FJsonObject>& Root, const TCHAR* Key, const TCHAR* KindFilter)
	{
		TArray<TPair<FLinearColor, float>> Out;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Root->TryGetArrayField(Key, Arr) || !Arr)
		{
			return Out;
		}
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid())
			{
				continue;
			}
			if (KindFilter && GetStr(O, TEXT("kind")) != KindFilter)
			{
				continue;
			}
			Out.Emplace(MTAAppearanceJson::ParseColor(GetStr(O, TEXT("color")), FLinearColor::Black), static_cast<float>(GetNum(O, TEXT("weight"), 1)));
		}
		return Out;
	}

	TArray<FName> ReadNames(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		TArray<FName> Out;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (O->TryGetArrayField(Key, Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				FString S;
				if (V->TryGetString(S))
				{
					Out.Add(FName(*S));
				}
			}
		}
		return Out;
	}
}

bool UMTAAppearanceSubsystem::LoadAppearanceData(const FString& Path)
{
	TSharedPtr<FJsonObject> Root;
	if (!MTAAppearanceJson::LoadJsonFile(Path.IsEmpty() ? TEXT("Data/appearance.json") : Path, Root))
	{
		return false;
	}

	Archetypes.Reset(); ArchetypeIndex.Reset(); MarkDefs.Reset(); HairStyles.Reset(); BeardStyles.Reset();
	SkinTones.Reset(); KeyCharacters.Reset(); CreatorPresets.Reset(); OriginOutfits.Reset(); CrowdMeshGroups.Reset();

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Root->TryGetArrayField(TEXT("skinTones"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString S;
			if (V->TryGetString(S)) { SkinTones.Add(MTAAppearanceJson::ParseColor(S, FLinearColor::White)); }
		}
	}
	EyeColors = ReadColorWeights(Root, TEXT("eyeColors"), nullptr);
	NaturalHair = ReadColorWeights(Root, TEXT("hairColors"), TEXT("natural"));
	DyedHair = ReadColorWeights(Root, TEXT("hairColors"), TEXT("dyed"));

	if (Root->TryGetArrayField(TEXT("hairStyles"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FMTAHairStyleDef D;
			D.Id = FName(*GetStr(O, TEXT("id")));
			D.DisplayName = GetStr(O, TEXT("name"));
			D.AgeMin = static_cast<int32>(GetNum(O, TEXT("ageMin"), 0));
			D.AgeMax = static_cast<int32>(GetNum(O, TEXT("ageMax"), 99));
			D.Groom = FSoftObjectPath(GetStr(O, TEXT("groom")));
			D.Cards = FSoftObjectPath(GetStr(O, TEXT("cards")));
			for (const FName& F : ReadNames(O, TEXT("frames")))
			{
				EMTABodyFrame Fr;
				if (UMTAAppearanceLibrary::FrameFromId(F.ToString(), Fr)) { D.Frames.Add(Fr); }
			}
			HairStyles.Add(D);
		}
	}
	if (Root->TryGetArrayField(TEXT("beardStyles"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			BeardStyles.Add(FName(*GetStr(V->AsObject(), TEXT("id"))));
		}
	}
	if (Root->TryGetArrayField(TEXT("marks"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FMTAMarkDef D;
			D.Id = FName(*GetStr(O, TEXT("id")));
			D.DisplayName = GetStr(O, TEXT("name"));
			EnumFromId(GKindIds, GetStr(O, TEXT("kind")), D.Kind);
			bool bGlow = false;
			O->TryGetBoolField(TEXT("glows"), bGlow);
			D.bGlows = bGlow;
			D.Conspicuous = static_cast<float>(GetNum(O, TEXT("conspicuous"), 0));
			D.Color = MTAAppearanceJson::ParseColor(GetStr(O, TEXT("color")), FLinearColor::White);
			D.Decal = FSoftObjectPath(GetStr(O, TEXT("decal")));
			for (const FName& R : ReadNames(O, TEXT("regions")))
			{
				EMTABodyRegion Reg;
				if (EnumFromId(GRegionIds, R.ToString(), Reg)) { D.Regions.Add(Reg); }
			}
			MarkDefs.Add(D);
		}
	}
	if (const TSharedPtr<FJsonObject> Origins = GetObj(Root, TEXT("originOutfits")))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Origins->Values)
		{
			FString S;
			if (P.Value->TryGetString(S)) { OriginOutfits.Add(FName(*P.Key), FName(*S)); }
		}
	}
	if (const TSharedPtr<FJsonObject> Groups = GetObj(Root, TEXT("crowdMeshGroups")))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : Groups->Values)
		{
			TArray<FSoftObjectPath>& List = CrowdMeshGroups.FindOrAdd(FName(*P.Key));
			const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
			if (P.Value->TryGetArray(Paths) && Paths)
			{
				for (const TSharedPtr<FJsonValue>& PV : *Paths)
				{
					FString S;
					if (PV->TryGetString(S) && !S.IsEmpty()) { List.Add(FSoftObjectPath(S)); }
				}
			}
		}
	}
	if (Root->TryGetArrayField(TEXT("archetypes"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FMTAArchetype A;
			A.Id = FName(*GetStr(O, TEXT("id")));
			A.DisplayName = GetStr(O, TEXT("name"));
			A.Tags = ReadNames(O, TEXT("tags"));
			A.Faction = FName(*GetStr(O, TEXT("faction")));
			A.AgeMin = static_cast<int32>(GetNum(O, TEXT("ageMin"), 20));
			A.AgeMax = static_cast<int32>(GetNum(O, TEXT("ageMax"), 55));
			TArray<float> W;
			auto ReadNums = [&O](const TCHAR* Key, TArray<float>& Out)
			{
				Out.Reset();
				const TArray<TSharedPtr<FJsonValue>>* N = nullptr;
				if (O->TryGetArrayField(Key, N) && N)
				{
					for (const TSharedPtr<FJsonValue>& NV : *N) { double D = 0; NV->TryGetNumber(D); Out.Add(static_cast<float>(D)); }
				}
			};
			ReadNums(TEXT("frameWeights"), W);
			if (W.Num() >= 3) { A.FrameWeights = FVector(W[0], W[1], W[2]); }
			ReadNums(TEXT("bodyWeights"), W);
			if (W.Num() >= 4) { A.BodyWeights = FVector4(W[0], W[1], W[2], W[3]); }
			ReadNums(TEXT("districtWeights"), A.DistrictWeights);
			A.DistrictWeights.SetNumZeroed(MTADistrict::Num());
			A.DayWeight = static_cast<float>(GetNum(O, TEXT("dayWeight"), 1));
			A.NightWeight = static_cast<float>(GetNum(O, TEXT("nightWeight"), 1));
			A.OutfitPresets = ReadNames(O, TEXT("outfitPresets"));
			A.BeardChance = static_cast<float>(GetNum(O, TEXT("beardChance"), 0));
			A.MakeupChance = static_cast<float>(GetNum(O, TEXT("makeupChance"), 0));
			A.ScarChance = static_cast<float>(GetNum(O, TEXT("scarChance"), 0));
			A.TattooChance = static_cast<float>(GetNum(O, TEXT("tattooChance"), 0));
			A.ImplantChance = static_cast<float>(GetNum(O, TEXT("implantChance"), 0));
			A.DyedHairChance = static_cast<float>(GetNum(O, TEXT("dyedHairChance"), 0));
			A.HeightOffsetCm = static_cast<float>(GetNum(O, TEXT("heightOffsetCm"), 0));
			A.MarkPool = ReadNames(O, TEXT("markPool"));
			A.MeshGroup = FName(*GetStr(O, TEXT("meshGroup"), TEXT("adult")));
			ArchetypeIndex.Add(A.Id, Archetypes.Add(A));
		}
	}
	if (Root->TryGetArrayField(TEXT("keyCharacters"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr) { KeyCharacters.Add(AppearanceFromJson(V->AsObject())); }
	}
	if (Root->TryGetArrayField(TEXT("creatorPresets"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr) { CreatorPresets.Add(AppearanceFromJson(V->AsObject())); }
	}
	UE_LOG(LogMTAAppearance, Log, TEXT("MTA6: %d Archetypen, %d Marken, %d Haarstile, %d Schluesselfiguren geladen"),
		Archetypes.Num(), MarkDefs.Num(), HairStyles.Num(), KeyCharacters.Num());
	return Archetypes.Num() > 0;
}

FMTAAppearance UMTAAppearanceSubsystem::AppearanceFromJson(const TSharedPtr<FJsonObject>& O) const
{
	FMTAAppearance A;
	if (!O.IsValid() || !A.FromJson(O))
	{
		return A;
	}
	// Marken ohne Art/Region aus dem Katalog ergaenzen.
	for (FMTABodyMark& M : A.Marks)
	{
		if (const FMTAMarkDef* D = FindMarkDef(M.Id))
		{
			M.Kind = D->Kind;
			if (D->Regions.Num() > 0 && !D->Regions.Contains(M.Region)) { M.Region = D->Regions[0]; }
			if (M.Color == FLinearColor::White) { M.Color = D->Color; }
		}
	}
	// Schluesselfiguren/Presets: Outfit aus "outfitPreset" ableiten (feste Items, chance 1).
	const FName PresetId(*GetStr(O, TEXT("outfitPreset")));
	if (!PresetId.IsNone() && A.Outfit.Num() == 0)
	{
		if (const UGameInstance* GI = GetGameInstance())
		{
			if (const UMTAWardrobeSubsystem* Wardrobe = GI->GetSubsystem<UMTAWardrobeSubsystem>())
			{
				FRandomStream Stream(static_cast<int32>(GetTypeHash(A.Id)));
				Wardrobe->BuildOutfitFromPreset(PresetId, Stream, A.Frame, A.GetLifeStage() == EMTALifeStage::Child, A.Outfit, A.OutfitTints);
			}
		}
	}
	return A;
}

const FMTAArchetype* UMTAAppearanceSubsystem::FindArchetypePtr(FName Id) const
{
	const int32* I = ArchetypeIndex.Find(Id);
	return I ? &Archetypes[*I] : nullptr;
}

bool UMTAAppearanceSubsystem::FindArchetype(FName Id, FMTAArchetype& Out) const
{
	if (const FMTAArchetype* A = FindArchetypePtr(Id)) { Out = *A; return true; }
	return false;
}

const FMTAMarkDef* UMTAAppearanceSubsystem::FindMarkDef(FName Id) const
{
	return MarkDefs.FindByPredicate([Id](const FMTAMarkDef& D) { return D.Id == Id; });
}

bool UMTAAppearanceSubsystem::GetKeyCharacter(FName Id, FMTAAppearance& Out) const
{
	if (const FMTAAppearance* K = KeyCharacters.FindByPredicate([Id](const FMTAAppearance& A) { return A.Id == Id; }))
	{
		Out = *K;
		return true;
	}
	return false;
}

FName UMTAAppearanceSubsystem::GetOriginOutfitPreset(FName OriginId) const
{
	const FName* P = OriginOutfits.Find(OriginId);
	return P ? *P : NAME_None;
}

FLinearColor UMTAAppearanceSubsystem::SampleSkinColor(float Tone) const
{
	return SkinTones.Num() > 0 ? SampleTones(SkinTones, Tone) : SampleTones(DefaultSkinTones(), Tone);
}

TArray<FLinearColor> UMTAAppearanceSubsystem::GetEyeColorOptions() const
{
	TArray<FLinearColor> Out;
	for (const TPair<FLinearColor, float>& P : EyeColors) { Out.Add(P.Key); }
	return Out;
}

TArray<FLinearColor> UMTAAppearanceSubsystem::GetHairColorOptions() const
{
	TArray<FLinearColor> Out;
	for (const TPair<FLinearColor, float>& P : NaturalHair) { Out.Add(P.Key); }
	for (const TPair<FLinearColor, float>& P : DyedHair) { Out.Add(P.Key); }
	return Out;
}

TArray<FName> UMTAAppearanceSubsystem::GetHairStylesFor(EMTABodyFrame Frame, int32 Age) const
{
	TArray<FName> Out;
	for (const FMTAHairStyleDef& H : HairStyles)
	{
		if (Age >= H.AgeMin && Age <= H.AgeMax && (H.Frames.Num() == 0 || H.Frames.Contains(Frame)))
		{
			Out.Add(H.Id);
		}
	}
	return Out;
}

FLinearColor UMTAAppearanceSubsystem::PickWeightedColor(const TArray<TPair<FLinearColor, float>>& Colors, FRandomStream& Stream) const
{
	if (Colors.Num() == 0)
	{
		return FLinearColor(0.02f, 0.01f, 0.01f);
	}
	TArray<float> W;
	for (const TPair<FLinearColor, float>& P : Colors) { W.Add(P.Value); }
	return Colors[PickIndex(W, Stream)].Key;
}

FName UMTAAppearanceSubsystem::PickArchetype(const FMTALookContext& Context, FRandomStream& Stream) const
{
	if (Archetypes.Num() == 0)
	{
		return NAME_None;
	}
	const int32 D = FMath::Clamp(static_cast<int32>(Context.District), 0, MTADistrict::Num() - 1);
	TArray<float> W;
	for (const FMTAArchetype& A : Archetypes)
	{
		const float Time = Context.IsNight() ? A.NightWeight : A.DayWeight;
		W.Add((A.DistrictWeights.IsValidIndex(D) ? A.DistrictWeights[D] : 0.f) * Time);
	}
	return Archetypes[PickIndex(W, Stream)].Id;
}

FMTAAppearance UMTAAppearanceSubsystem::GenerateFromArchetype(FName ArchetypeId, int32 Seed, const FMTALookContext& Context) const
{
	FRandomStream Stream(Seed);
	const FMTAArchetype* A = FindArchetypePtr(ArchetypeId);
	if (!A)
	{
		if (Archetypes.Num() == 0) { return FMTAAppearance(); }
		A = &Archetypes[0];
	}
	FMTAAppearance Out = GenerateFromArchetypeStream(*A, Stream, Context);
	Out.Id = FName(*FString::Printf(TEXT("gen_%s_%d"), *A->Id.ToString(), Seed));
	return Out;
}

FMTAAppearance UMTAAppearanceSubsystem::GenerateFromArchetypeStream(const FMTAArchetype& Arch, FRandomStream& S, const FMTALookContext& Context) const
{
	FMTAAppearance A;
	A.ArchetypeId = Arch.Id;
	A.DisplayName = Arch.DisplayName;

	// Koerperrahmen und Alter
	{
		TArray<float> W = { static_cast<float>(Arch.FrameWeights.X), static_cast<float>(Arch.FrameWeights.Y), static_cast<float>(Arch.FrameWeights.Z) };
		A.Frame = static_cast<EMTABodyFrame>(PickIndex(W, S));
	}
	A.Age = S.RandRange(Arch.AgeMin, FMath::Max(Arch.AgeMin, Arch.AgeMax));
	const EMTALifeStage Stage = A.GetLifeStage();
	{
		TArray<float> W = { static_cast<float>(Arch.BodyWeights.X), static_cast<float>(Arch.BodyWeights.Y), static_cast<float>(Arch.BodyWeights.Z), static_cast<float>(Arch.BodyWeights.W) };
		A.BodyType = static_cast<EMTABodyType>(PickIndex(W, S));
		if (Stage == EMTALifeStage::Child && A.BodyType > EMTABodyType::Average) { A.BodyType = EMTABodyType::Average; }
	}

	// Groesse, Fuelle
	float Base = A.Frame == EMTABodyFrame::Feminine ? 164.f : (A.Frame == EMTABodyFrame::Masculine ? 177.f : 170.f);
	const float Jitter = Bell(S) * 8.f;
	if (Stage == EMTALifeStage::Child) { A.HeightCm = 100.f + (A.Age - 4) * 5.5f + Bell(S) * 4.f; }
	else if (Stage == EMTALifeStage::Teen) { A.HeightCm = FMath::Lerp(150.f, Base, (A.Age - 13) / 5.f) + Bell(S) * 4.f; }
	else { A.HeightCm = Base + Jitter + Arch.HeightOffsetCm - (Stage == EMTALifeStage::Elder ? 3.f : 0.f); }
	A.HeightCm = FMath::Clamp(A.HeightCm, 95.f, 205.f);
	static const float BuildByType[] = { 0.2f, 0.5f, 0.4f, 0.82f };
	static const float MuscleByType[] = { 0.15f, 0.35f, 0.65f, 0.45f };
	A.Build = FMath::Clamp(BuildByType[static_cast<int32>(A.BodyType)] + Bell(S) * 0.1f, 0.f, 1.f);
	A.Muscle = FMath::Clamp(MuscleByType[static_cast<int32>(A.BodyType)] + Bell(S) * 0.1f, 0.f, 1.f);

	// Haut, Augen
	A.SkinTone = S.FRand();
	A.Freckles = (A.SkinTone < 0.45f && S.FRand() < 0.2f) ? 0.1f + S.FRand() * 0.4f : 0.f;
	A.EyeColor = PickWeightedColor(EyeColors, S);

	// Gesichts-Morphs
	for (const FName& Name : UMTAAppearanceLibrary::GetFaceMorphNames())
	{
		A.FaceMorphs.Add(Name, FMath::Clamp(Bell(S) * 0.7f, -1.f, 1.f));
	}

	// Haar
	const TArray<FName> HairOptions = GetHairStylesFor(A.Frame, A.Age);
	A.HairStyle = HairOptions.Num() > 0 ? HairOptions[S.RandRange(0, HairOptions.Num() - 1)] : FName();
	const bool bDyed = S.FRand() < Arch.DyedHairChance;
	A.HairColor = PickWeightedColor(bDyed ? DyedHair : NaturalHair, S);
	A.HairGray = (!bDyed && A.Age > 42) ? FMath::Clamp((A.Age - 42) / 40.f + Bell(S) * 0.15f, 0.f, 1.f) : 0.f;

	// Bart
	const float BeardRoll = S.FRand();
	const float BeardChance = A.Frame == EMTABodyFrame::Masculine ? Arch.BeardChance : (A.Frame == EMTABodyFrame::Androgynous ? Arch.BeardChance * 0.15f : 0.f);
	A.BeardStyle = BeardStyles.Num() > 0 ? BeardStyles[0] : FName();
	if (A.Age >= 16 && BeardRoll < BeardChance && BeardStyles.Num() > 1)
	{
		A.BeardStyle = BeardStyles[S.RandRange(1, BeardStyles.Num() - 1)];
	}
	A.BeardColor = A.HairColor;

	// Make-up
	const float MakeupRoll = S.FRand();
	const float MakeupChance = A.Frame == EMTABodyFrame::Masculine ? Arch.MakeupChance * 0.2f : Arch.MakeupChance;
	if (MakeupRoll < MakeupChance)
	{
		A.Makeup.Intensity = 0.2f + S.FRand() * 0.6f;
		static const FLinearColor Lips[] = { FLinearColor(0.35f, 0.05f, 0.07f), FLinearColor(0.2f, 0.02f, 0.05f), FLinearColor(0.45f, 0.12f, 0.1f), FLinearColor(0.08f, 0.02f, 0.12f) };
		A.Makeup.LipColor = Lips[S.RandRange(0, 3)];
	}
	else { S.FRand(); }

	// Marken (Narben/Tattoos/Implantate)
	auto AddMark = [&](EMTAMarkKind Kind)
	{
		TArray<const FMTAMarkDef*> Candidates;
		for (const FName& Id : Arch.MarkPool)
		{
			if (const FMTAMarkDef* D = FindMarkDef(Id)) { if (D->Kind == Kind) { Candidates.Add(D); } }
		}
		if (Candidates.Num() == 0)
		{
			for (const FMTAMarkDef& D : MarkDefs) { if (D.Kind == Kind && D.Conspicuous < 0.6f) { Candidates.Add(&D); } }
		}
		if (Candidates.Num() == 0) { return; }
		const FMTAMarkDef* D = Candidates[S.RandRange(0, Candidates.Num() - 1)];
		FMTABodyMark M;
		M.Id = D->Id; M.Kind = D->Kind; M.Color = D->Color;
		M.Region = D->Regions.Num() > 0 ? D->Regions[S.RandRange(0, D->Regions.Num() - 1)] : EMTABodyRegion::Torso;
		M.Intensity = 0.5f + S.FRand() * 0.5f;
		A.Marks.Add(M);
	};
	const bool bAdult = Stage != EMTALifeStage::Child;
	if (S.FRand() < Arch.ScarChance) { AddMark(EMTAMarkKind::Scar); }
	if (bAdult && S.FRand() < Arch.TattooChance) { AddMark(EMTAMarkKind::Tattoo); }
	if (bAdult && S.FRand() < Arch.ImplantChance) { AddMark(EMTAMarkKind::Implant); }

	// Kleidung
	if (Arch.OutfitPresets.Num() > 0)
	{
		const FName PresetId = Arch.OutfitPresets[S.RandRange(0, Arch.OutfitPresets.Num() - 1)];
		if (const UGameInstance* GI = GetGameInstance())
		{
			if (const UMTAWardrobeSubsystem* Wardrobe = GI->GetSubsystem<UMTAWardrobeSubsystem>())
			{
				const bool bChild = Stage == EMTALifeStage::Child;
				Wardrobe->BuildOutfitFromPreset(PresetId, S, A.Frame, bChild, A.Outfit, A.OutfitTints);
				Wardrobe->AdaptOutfitToWeather(A.Outfit, A.OutfitTints, Context, S, A.Frame, bChild, Arch.Faction);
			}
		}
	}
	return A;
}

FMTAAppearance UMTAAppearanceSubsystem::GenerateRandomPlayerLook(int32 Seed) const
{
	FMTAArchetype Arch;
	if (const FMTAArchetype* Commuter = FindArchetypePtr(TEXT("local_commuter"))) { Arch = *Commuter; }
	else if (Archetypes.Num() > 0) { Arch = Archetypes[0]; }
	Arch.AgeMin = 20; Arch.AgeMax = 45;
	Arch.FrameWeights = FVector(0.45, 0.45, 0.1);
	Arch.ScarChance = 0.1f; Arch.TattooChance = 0.15f; Arch.ImplantChance = 0.05f;
	FRandomStream Stream(Seed);
	FMTAAppearance A = GenerateFromArchetypeStream(Arch, Stream, FMTALookContext());
	A.ArchetypeId = NAME_None;
	A.Id = TEXT("player");
	A.DisplayName = TEXT("");
	return A;
}

float UMTAAppearanceSubsystem::ComputeAegisConspicuousness(const FMTAAppearance& A) const
{
	float Value = 0.f;
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UMTAWardrobeSubsystem* Wardrobe = GI->GetSubsystem<UMTAWardrobeSubsystem>())
		{
			Value = Wardrobe->ComputeConspicuousness(A.Outfit);
		}
	}
	float MarkMax = 0.f;
	for (const FMTABodyMark& M : A.Marks)
	{
		if (const FMTAMarkDef* D = FindMarkDef(M.Id)) { MarkMax = FMath::Max(MarkMax, D->Conspicuous * M.Intensity); }
	}
	const float MakeupBoost = A.Makeup.Intensity > 0.6f ? 0.05f : 0.f;
	return FMath::Clamp(0.7f * Value + 0.3f * MarkMax + MakeupBoost, 0.f, 1.f);
}

int32 UMTAAppearanceSubsystem::ValidateData(TArray<FString>& Errors) const
{
	const UMTAWardrobeSubsystem* Wardrobe = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMTAWardrobeSubsystem>() : nullptr;
	const int32 Start = Errors.Num();
	for (const FMTAArchetype& A : Archetypes)
	{
		if (A.OutfitPresets.Num() == 0) { Errors.Add(FString::Printf(TEXT("Archetyp %s ohne Outfit-Preset"), *A.Id.ToString())); }
		for (const FName& P : A.OutfitPresets)
		{
			if (!Wardrobe || !Wardrobe->FindPreset(P)) { Errors.Add(FString::Printf(TEXT("Archetyp %s: Preset %s fehlt"), *A.Id.ToString(), *P.ToString())); }
		}
		for (const FName& M : A.MarkPool)
		{
			if (!FindMarkDef(M)) { Errors.Add(FString::Printf(TEXT("Archetyp %s: Marke %s fehlt"), *A.Id.ToString(), *M.ToString())); }
		}
	}
	auto CheckAppearance = [&](const FMTAAppearance& K)
	{
		if (!K.HairStyle.IsNone() && !HairStyles.ContainsByPredicate([&](const FMTAHairStyleDef& H) { return H.Id == K.HairStyle; }))
		{
			Errors.Add(FString::Printf(TEXT("%s: Haarstil %s fehlt"), *K.Id.ToString(), *K.HairStyle.ToString()));
		}
		for (const FMTABodyMark& M : K.Marks) { if (!FindMarkDef(M.Id)) { Errors.Add(FString::Printf(TEXT("%s: Marke %s fehlt"), *K.Id.ToString(), *M.Id.ToString())); } }
		for (const TPair<EMTAWardrobeSlot, FName>& P : K.Outfit)
		{
			if (!Wardrobe || !Wardrobe->FindItem(P.Value)) { Errors.Add(FString::Printf(TEXT("%s: Kleidung %s fehlt"), *K.Id.ToString(), *P.Value.ToString())); }
		}
	};
	for (const FMTAAppearance& K : KeyCharacters) { CheckAppearance(K); }
	for (const FMTAAppearance& K : CreatorPresets) { CheckAppearance(K); }
	if (Wardrobe) { Wardrobe->ValidateData(Errors); }
	return Errors.Num() - Start;
}
