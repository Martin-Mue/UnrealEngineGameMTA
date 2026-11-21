#include "MTAFurnishing.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

namespace MTAInteriorJson
{
	double Num(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, double Default)
	{
		double V = Default;
		return (Obj.IsValid() && Obj->TryGetNumberField(Key, V)) ? V : Default;
	}

	FString Str(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, const FString& Default)
	{
		FString V;
		return (Obj.IsValid() && Obj->TryGetStringField(Key, V)) ? V : Default;
	}

	bool Bool(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, bool Default)
	{
		bool V = Default;
		return (Obj.IsValid() && Obj->TryGetBoolField(Key, V)) ? V : Default;
	}

	TArray<double> Nums(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, int32 MinCount, double Default)
	{
		TArray<double> Out;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj.IsValid() && Obj->TryGetArrayField(Key, Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				double D = Default;
				if (V.IsValid()) { V->TryGetNumber(D); }
				Out.Add(D);
			}
		}
		while (Out.Num() < MinCount) { Out.Add(Default); }
		return Out;
	}

	TArray<FName> Names(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		TArray<FName> Out;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj.IsValid() && Obj->TryGetArrayField(Key, Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				if (V.IsValid()) { Out.Add(FName(*V->AsString())); }
			}
		}
		return Out;
	}

	TArray<TSharedPtr<FJsonObject>> Objects(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		TArray<TSharedPtr<FJsonObject>> Out;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj.IsValid() && Obj->TryGetArrayField(Key, Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				if (V.IsValid() && V->Type == EJson::Object) { Out.Add(V->AsObject()); }
			}
		}
		return Out;
	}

	TSharedPtr<FJsonObject> Object(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Child = nullptr;
		if (Obj.IsValid() && Obj->TryGetObjectField(Key, Child) && Child) { return *Child; }
		return nullptr;
	}

	TSharedPtr<FJsonObject> LoadFile(const FString& Path, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("Datei nicht lesbar: %s"), *Path);
			return nullptr;
		}
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("JSON ungueltig: %s"), *Path);
			return nullptr;
		}
		return Root;
	}
}

using namespace MTAInteriorJson;

bool FMTAFurnItem::AllowedFor(const TArray<FName>& StyleTags) const
{
	if (Styles.Num() == 0) { return true; }
	for (const FName& S : Styles)
	{
		if (StyleTags.Contains(S)) { return true; }
	}
	return false;
}

FLinearColor FMTAFurnishingLibrary::ParseColor(const FString& Hex, const FLinearColor& Fallback)
{
	if (Hex.Len() < 4) { return Fallback; }
	// FColor::FromHex akzeptiert "#rgb" und "#rrggbb"; FLinearColor(FColor) rechnet sRGB -> linear
	return FLinearColor(FColor::FromHex(Hex));
}

EMTAPrimShape FMTAFurnishingLibrary::ParseShape(const FString& S)
{
	if (S == TEXT("cylinder")) return EMTAPrimShape::Cylinder;
	if (S == TEXT("sphere"))   return EMTAPrimShape::Sphere;
	if (S == TEXT("cone"))     return EMTAPrimShape::Cone;
	if (S == TEXT("plane"))    return EMTAPrimShape::Plane;
	return EMTAPrimShape::Cube;
}

static FVector MetersVec(const TArray<double>& A)
{
	return FVector(A[0], A[1], A[2]) * 100.0;
}

bool FMTAFurnishingLibrary::LoadFromJson(const TSharedPtr<FJsonObject>& Root, FString& OutError)
{
	Items.Reset();
	Styles.Reset();
	RoomKinds.Reset();
	TrashIds.Reset();
	if (!Root.IsValid()) { OutError = TEXT("furnishing.json fehlt"); return false; }

	for (const TSharedPtr<FJsonObject>& J : Objects(Root, TEXT("items")))
	{
		FMTAFurnItem It;
		It.Id = FName(*Str(J, TEXT("id")));
		It.Label = Str(J, TEXT("label"));
		It.Category = FName(*Str(J, TEXT("category")));
		const FString Mount = Str(J, TEXT("mount"), TEXT("floor"));
		It.Mount = Mount == TEXT("wall") ? EMTAFurnMount::Wall : Mount == TEXT("ceiling") ? EMTAFurnMount::Ceiling
			: Mount == TEXT("surface") ? EMTAFurnMount::Surface : EMTAFurnMount::Floor;
		const TArray<double> Fp = Nums(J, TEXT("footprint"), 2, 0.5);
		It.Footprint = FVector2D(Fp[0], Fp[1]) * 100.0;
		It.bCollision = Bool(J, TEXT("collision"), true);
		It.Styles = Names(J, TEXT("styles"));
		It.Activity = FName(*Str(J, TEXT("activity")));
		It.Interactive = FName(*Str(J, TEXT("interactive")));
		for (const TSharedPtr<FJsonObject>& P : Objects(J, TEXT("parts")))
		{
			FMTAFurnPart Part;
			Part.Shape = ParseShape(Str(P, TEXT("shape"), TEXT("cube")));
			Part.Pos = MetersVec(Nums(P, TEXT("pos"), 3));
			Part.Size = MetersVec(Nums(P, TEXT("size"), 3, 0.1));
			const TArray<double> R = Nums(P, TEXT("rot"), 3);
			Part.Rot = FRotator(R[0], R[1], R[2]);
			Part.Color = ParseColor(Str(P, TEXT("color")));
			Part.Emissive = (float)Num(P, TEXT("emissive"), 0.0);
			It.Height = FMath::Max(It.Height, (float)(Part.Pos.Z + Part.Size.Z * 0.5));
			It.Parts.Add(Part);
		}
		if (const TSharedPtr<FJsonObject> L = Object(J, TEXT("light")))
		{
			It.Light.bValid = true;
			It.Light.Pos = MetersVec(Nums(L, TEXT("pos"), 3));
			It.Light.Color = ParseColor(Str(L, TEXT("color")), FLinearColor(1.f, 0.85f, 0.6f));
			It.Light.Intensity = (float)Num(L, TEXT("intensity"), 800.0);
			It.Light.Radius = (float)Num(L, TEXT("radius"), 4.5) * 100.f;
		}
		if (Names(J, TEXT("tags")).Contains(FName(TEXT("trash")))) { TrashIds.Add(It.Id); }
		if (It.Id.IsNone() || It.Parts.Num() == 0) { continue; }
		Items.Add(It.Id, MoveTemp(It));
	}

	for (const TSharedPtr<FJsonObject>& J : Objects(Root, TEXT("styles")))
	{
		FMTAStyleDef S;
		S.Id = FName(*Str(J, TEXT("id")));
		S.Label = Str(J, TEXT("label"));
		S.Tags = Names(J, TEXT("tags"));
		S.Wear = (float)Num(J, TEXT("wear"), 0.3);
		S.Clutter = (float)Num(J, TEXT("clutter"), 1.0);
		const TArray<double> T = Nums(J, TEXT("trash"), 2);
		S.Trash = FIntPoint((int32)T[0], (int32)FMath::Max(T[0], T[1]));
		S.Plants = (float)Num(J, TEXT("plants"), 0.3);
		S.Neon = (float)Num(J, TEXT("neon"), 0.1);
		S.Flicker = (float)Num(J, TEXT("flicker"), 0.0);
		S.Wall = ParseColor(Str(J, TEXT("wall")), S.Wall);
		S.Floor = ParseColor(Str(J, TEXT("floor")), S.Floor);
		S.Ceiling = ParseColor(Str(J, TEXT("ceiling")), S.Ceiling);
		S.LightColor = ParseColor(Str(J, TEXT("lightColor")), S.LightColor);
		S.LightIntensity = (float)Num(J, TEXT("lightIntensity"), 1000.0);
		S.LightsPer10m2 = (float)Num(J, TEXT("lightsPer10m2"), 0.8);
		Styles.Add(S.Id, MoveTemp(S));
	}

	for (const TSharedPtr<FJsonObject>& J : Objects(Root, TEXT("roomKinds")))
	{
		TArray<FMTAFurnRule>& Rules = RoomKinds.FindOrAdd(FName(*Str(J, TEXT("kind"))));
		for (const TSharedPtr<FJsonObject>& RJ : Objects(J, TEXT("rules")))
		{
			FMTAFurnRule R;
			R.Pick = Names(RJ, TEXT("pick"));
			const FString P = Str(RJ, TEXT("place"), TEXT("wall"));
			R.Place = P == TEXT("corner") ? EMTAFurnPlace::Corner : P == TEXT("center") ? EMTAFurnPlace::Center
				: P == TEXT("scatter") ? EMTAFurnPlace::Scatter : P == TEXT("wallDecor") ? EMTAFurnPlace::WallDecor
				: P == TEXT("ceiling") ? EMTAFurnPlace::Ceiling : P == TEXT("surface") ? EMTAFurnPlace::Surface : EMTAFurnPlace::Wall;
			const TArray<double> C = Nums(RJ, TEXT("count"), 2, 1.0);
			R.Count = FIntPoint((int32)C[0], (int32)FMath::Max(C[0], C[1]));
			R.Chance = (float)Num(RJ, TEXT("chance"), 1.0);
			R.bClutter = Bool(RJ, TEXT("clutter"), false);
			Rules.Add(MoveTemp(R));
		}
	}

	if (Items.Num() == 0 || Styles.Num() == 0)
	{
		OutError = TEXT("furnishing.json enthaelt keine Objekte oder Stile");
		return false;
	}
	return true;
}

const FMTAFurnItem* FMTAFurnishingLibrary::PickItem(const TArray<FName>& Pick, const FMTAStyleDef& Style, FRandomStream& Rng) const
{
	TArray<const FMTAFurnItem*, TInlineAllocator<8>> Allowed, Any;
	for (const FName& Id : Pick)
	{
		if (const FMTAFurnItem* It = Items.Find(Id))
		{
			Any.Add(It);
			if (It->AllowedFor(Style.Tags)) { Allowed.Add(It); }
		}
	}
	const auto& Pool = Allowed.Num() > 0 ? Allowed : Any;
	return Pool.Num() > 0 ? Pool[Rng.RandRange(0, Pool.Num() - 1)] : nullptr;
}

void FMTAFurnishingLibrary::GetTrashItems(const FMTAStyleDef& Style, TArray<const FMTAFurnItem*>& Out) const
{
	for (const FName& Id : TrashIds)
	{
		const FMTAFurnItem* It = Items.Find(Id);
		if (It && It->AllowedFor(Style.Tags)) { Out.Add(It); }
	}
}
