#include "MTAInteriorData.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

using namespace MTAInteriorJson;

namespace
{
	constexpr double M = 100.0; // Meter -> cm

	FBox2D RectFromJson(const TSharedPtr<FJsonObject>& J, const TCHAR* Key)
	{
		const TArray<double> R = Nums(J, Key, 4);
		return FBox2D(FVector2D(R[0], R[1]) * M, FVector2D(R[0] + R[2], R[1] + R[3]) * M);
	}

	void ReadPoint(const TSharedPtr<FJsonObject>& J, FMTAPointDef& P)
	{
		P.Floor = (int32)Num(J, TEXT("floor"), 0.0);
		P.Room = FName(*Str(J, TEXT("room")));
		const TArray<double> A = Nums(J, TEXT("at"), 3, 0.0);
		P.At = FVector(A[0], A[1], A[2]) * M;
		P.Yaw = (float)Num(J, TEXT("rot"), 0.0);
	}

	FMTAAccessRule AccessFromJson(const TSharedPtr<FJsonObject>& J)
	{
		FMTAAccessRule A;
		const TArray<double> Open = Nums(J, TEXT("open"));
		A.bHasOpeningHours = Open.Num() >= 2;
		if (A.bHasOpeningHours) { A.OpenHour = (float)Open[0]; A.CloseHour = (float)Open[1]; }
		A.Faction = Str(J, TEXT("faction"));
		A.MinStanding = (float)Num(J, TEXT("minStanding"), -1000.0);
		A.KeyId = FName(*Str(J, TEXT("key")));
		A.HackDifficulty = (int32)Num(J, TEXT("hack"), 0.0);
		return A;
	}

	EMTAInteractiveKind InteractiveKindFromString(const FString& S)
	{
		if (S == TEXT("door"))   return EMTAInteractiveKind::Door;
		if (S == TEXT("switch")) return EMTAInteractiveKind::Switch;
		if (S == TEXT("hiding")) return EMTAInteractiveKind::HidingSpot;
		if (S == TEXT("secret")) return EMTAInteractiveKind::Secret;
		return EMTAInteractiveKind::Terminal;
	}
}

float FMTAInteriorTemplate::GetFloorBaseZ(int32 Level, float SlabThickness) const
{
	float Z = 0.f;
	for (const FMTAFloorDef& F : Floors)
	{
		if (F.Level < Level) { Z += F.Height + SlabThickness; }
	}
	return Z;
}

TArray<EMTAEntranceKind> FMTAInteriorTemplate::GetEntranceKinds() const
{
	TArray<EMTAEntranceKind> Out;
	for (const FMTAFloorDef& F : Floors)
	{
		for (const FMTADoorDef& D : F.Doors)
		{
			EMTAEntranceKind K;
			if (!D.Entrance.IsNone() && MTAInterior::EntranceKindFromName(D.Entrance, K)) { Out.AddUnique(K); }
		}
	}
	return Out;
}

bool FMTAInteriorTemplate::AllowsVehicles() const
{
	for (const FMTAFloorDef& F : Floors)
	{
		for (const FMTADoorDef& D : F.Doors)
		{
			if (D.bVehicles) { return true; }
		}
	}
	return false;
}

bool FMTAInteriorDatabase::Load(const FString& InteriorsPath, const FString& FurnishingPath, FString& OutError)
{
	bLoaded = false;
	Templates.Reset();
	TemplateOrder.Reset();
	RoomTypes.Reset();
	Shops.Reset();
	DistrictStyles.Reset();

	const TSharedPtr<FJsonObject> FurnRoot = LoadFile(FurnishingPath, OutError);
	if (!FurnRoot.IsValid() || !Furnishing.LoadFromJson(FurnRoot, OutError)) { return false; }

	const TSharedPtr<FJsonObject> Root = LoadFile(InteriorsPath, OutError);
	if (!Root.IsValid()) { return false; }

	if (const TSharedPtr<FJsonObject> Def = Object(Root, TEXT("defaults")))
	{
		Defaults.WallThickness = (float)(Num(Def, TEXT("wallThickness"), 0.2) * M);
		Defaults.DoorHeight = (float)(Num(Def, TEXT("doorHeight"), 2.2) * M);
		Defaults.WindowSill = (float)(Num(Def, TEXT("windowSill"), 0.9) * M);
		Defaults.WindowTop = (float)(Num(Def, TEXT("windowTop"), 2.2) * M);
		Defaults.SlabThickness = (float)(Num(Def, TEXT("slabThickness"), 0.2) * M);
	}

	if (const TSharedPtr<FJsonObject> DS = Object(Root, TEXT("districtStyles")))
	{
		for (int32 i = 0; i < MTADistrict::Num(); ++i)
		{
			const FName DName = MTAInterior::DistrictToName((EMTADistrict)i);
			const FString StyleId = Str(DS, *DName.ToString());
			if (!StyleId.IsEmpty()) { DistrictStyles.Add(DName, FName(*StyleId)); }
		}
	}

	for (const TSharedPtr<FJsonObject>& J : Objects(Root, TEXT("roomTypes")))
	{
		FMTARoomTypeDef RT;
		RT.Id = FName(*Str(J, TEXT("id")));
		RT.Label = Str(J, TEXT("label"));
		RT.Role = FName(*Str(J, TEXT("role")));
		RT.Activities = Names(J, TEXT("activities"));
		RoomTypes.Add(RT.Id, MoveTemp(RT));
	}

	for (const TSharedPtr<FJsonObject>& J : Objects(Root, TEXT("shops")))
	{
		FMTAShopDef S;
		S.Id = FName(*Str(J, TEXT("id")));
		S.Label = Str(J, TEXT("label"));
		S.OwnerNpcId = FName(*Str(J, TEXT("owner")));
		S.Category = FName(*Str(J, TEXT("category")));
		const TArray<double> Open = Nums(J, TEXT("open"), 2, 0.0);
		S.OpenHour = (float)Open[0];
		S.CloseHour = (float)Open[1];
		for (const TSharedPtr<FJsonObject>& W : Objects(J, TEXT("wares")))
		{
			FMTAShopWare Ware;
			Ware.ItemId = FName(*Str(W, TEXT("item")));
			Ware.Label = Str(W, TEXT("label"));
			Ware.BasePrice = (int32)Num(W, TEXT("price"), 0.0);
			Ware.Stock = Ware.MaxStock = (int32)Num(W, TEXT("stock"), 0.0);
			S.Wares.Add(Ware);
		}
		Shops.Add(S.Id, MoveTemp(S));
	}

	for (const TSharedPtr<FJsonObject>& J : Objects(Root, TEXT("templates")))
	{
		FMTAInteriorTemplate T;
		T.Id = FName(*Str(J, TEXT("id")));
		T.RoomType = FName(*Str(J, TEXT("roomType")));
		T.Label = Str(J, TEXT("label"));
		T.Districts = Names(J, TEXT("districts"));
		T.Style = FName(*Str(J, TEXT("style")));
		T.Shop = FName(*Str(J, TEXT("shop")));
		const TArray<double> Fp = Nums(J, TEXT("footprint"), 2, 10.0);
		T.Footprint = FVector2D(Fp[0], Fp[1]) * M;
		T.Access = AccessFromJson(Object(J, TEXT("access")));

		for (const TSharedPtr<FJsonObject>& FJ : Objects(J, TEXT("floors")))
		{
			FMTAFloorDef F;
			F.Level = (int32)Num(FJ, TEXT("level"), 0.0);
			F.Height = (float)(Num(FJ, TEXT("height"), 3.0) * M);
			for (const TSharedPtr<FJsonObject>& RJ : Objects(FJ, TEXT("rooms")))
			{
				FMTARoomDef R;
				R.Id = FName(*Str(RJ, TEXT("id")));
				R.Kind = FName(*Str(RJ, TEXT("kind")));
				R.Rect = RectFromJson(RJ, TEXT("rect"));
				F.Rooms.Add(R);
			}
			for (const TSharedPtr<FJsonObject>& DJ : Objects(FJ, TEXT("doors")))
			{
				FMTADoorDef D;
				const TArray<FName> Rooms = Names(DJ, TEXT("rooms"));
				D.RoomA = Rooms.IsValidIndex(0) ? Rooms[0] : NAME_None;
				D.RoomB = (Rooms.IsValidIndex(1) && Rooms[1] != FName(TEXT("outside"))) ? Rooms[1] : NAME_None;
				const TArray<double> At = Nums(DJ, TEXT("at"), 2);
				D.At = FVector2D(At[0], At[1]) * M;
				D.Width = (float)(Num(DJ, TEXT("width"), 1.0) * M);
				D.Entrance = FName(*Str(DJ, TEXT("entrance")));
				D.bVehicles = Bool(DJ, TEXT("vehicles"), false);
				D.bPanel = Bool(DJ, TEXT("panel"), false);
				if (const TSharedPtr<FJsonObject> Lock = Object(DJ, TEXT("lock")))
				{
					D.KeyId = FName(*Str(Lock, TEXT("key")));
					D.HackDifficulty = (int32)Num(Lock, TEXT("hack"), 0.0);
					D.bPanel = true; // verschlossene Tueren brauchen ein Tuerblatt
				}
				F.Doors.Add(D);
			}
			for (const TSharedPtr<FJsonObject>& WJ : Objects(FJ, TEXT("windows")))
			{
				FMTAWindowDef W;
				W.Room = FName(*Str(WJ, TEXT("room")));
				const TArray<double> At = Nums(WJ, TEXT("at"), 2);
				W.At = FVector2D(At[0], At[1]) * M;
				W.Width = (float)(Num(WJ, TEXT("width"), 1.0) * M);
				F.Windows.Add(W);
			}
			for (const TSharedPtr<FJsonObject>& SJ : Objects(FJ, TEXT("stairs")))
			{
				FMTAStairDef S;
				S.Room = FName(*Str(SJ, TEXT("room")));
				S.Rect = RectFromJson(SJ, TEXT("rect"));
				S.ToLevel = (int32)Num(SJ, TEXT("toLevel"), F.Level + 1);
				S.bAlongX = Str(SJ, TEXT("axis"), TEXT("x")) == TEXT("x");
				F.Stairs.Add(S);
			}
			T.Floors.Add(MoveTemp(F));
		}
		T.Floors.Sort([](const FMTAFloorDef& A, const FMTAFloorDef& B) { return A.Level < B.Level; });

		for (const TSharedPtr<FJsonObject>& PJ : Objects(J, TEXT("props")))
		{
			FMTAPropDef P;
			ReadPoint(PJ, P);
			P.Item = FName(*Str(PJ, TEXT("item")));
			T.Props.Add(P);
		}
		for (const TSharedPtr<FJsonObject>& SJ : Objects(J, TEXT("secrets")))
		{
			FMTASecretDef S;
			ReadPoint(SJ, S);
			S.Id = FName(*Str(SJ, TEXT("id")));
			S.Kind = FName(*Str(SJ, TEXT("kind"), TEXT("collectible")));
			S.bHidden = Bool(SJ, TEXT("hidden"), false);
			T.Secrets.Add(S);
		}
		for (const TSharedPtr<FJsonObject>& NJ : Objects(J, TEXT("npcSlots")))
		{
			FMTANPCSlotDef N;
			ReadPoint(NJ, N);
			N.Activity = FName(*Str(NJ, TEXT("activity")));
			N.Role = FName(*Str(NJ, TEXT("role"), TEXT("resident")));
			const TArray<double> H = Nums(NJ, TEXT("hours"), 2, 0.0);
			N.FromHour = (float)H[0];
			N.ToHour = (float)H[1];
			N.Chance = (float)Num(NJ, TEXT("chance"), 1.0);
			T.NpcSlots.Add(N);
		}
		for (const TSharedPtr<FJsonObject>& IJ : Objects(J, TEXT("interactives")))
		{
			FMTAInteractiveDef I;
			ReadPoint(IJ, I);
			I.Kind = InteractiveKindFromString(Str(IJ, TEXT("kind")));
			I.Id = FName(*Str(IJ, TEXT("id")));
			I.HackDifficulty = (int32)Num(IJ, TEXT("hack"), 0.0);
			I.Links = FName(*Str(IJ, TEXT("links")));
			I.KeyId = FName(*Str(IJ, TEXT("key")));
			T.Interactives.Add(I);
		}

		if (T.Id.IsNone() || T.Floors.Num() == 0) { continue; }
		TemplateOrder.Add(T.Id);
		Templates.Add(T.Id, MoveTemp(T));
	}

	if (Templates.Num() == 0)
	{
		OutError = TEXT("interiors.json enthaelt keine Vorlagen");
		return false;
	}
	bLoaded = true;
	return true;
}

const FMTAStyleDef* FMTAInteriorDatabase::ResolveStyle(const FMTAInteriorTemplate& T, EMTADistrict District, FName Override) const
{
	if (!Override.IsNone()) { if (const FMTAStyleDef* S = Furnishing.FindStyle(Override)) return S; }
	if (!T.Style.IsNone()) { if (const FMTAStyleDef* S = Furnishing.FindStyle(T.Style)) return S; }
	if (const FName* DS = DistrictStyles.Find(MTAInterior::DistrictToName(District)))
	{
		if (const FMTAStyleDef* S = Furnishing.FindStyle(*DS)) return S;
	}
	// Fallback: Stil des ersten Bezirks der Vorlage
	for (const FName& D : T.Districts)
	{
		if (const FName* DS = DistrictStyles.Find(D)) { if (const FMTAStyleDef* S = Furnishing.FindStyle(*DS)) return S; }
	}
	return nullptr;
}

void FMTAInteriorDatabase::GetTemplates(EMTADistrict District, FName RoomType, TArray<const FMTAInteriorTemplate*>& Out) const
{
	const FName DName = MTAInterior::DistrictToName(District);
	for (const FName& Id : TemplateOrder)
	{
		const FMTAInteriorTemplate& T = Templates.FindChecked(Id);
		if ((RoomType.IsNone() || T.RoomType == RoomType) && (DName.IsNone() || T.Districts.Contains(DName))) { Out.Add(&T); }
	}
}

const FMTAInteriorTemplate* FMTAInteriorDatabase::PickTemplate(EMTADistrict District, FName RoomType, int32 Seed, FVector2D MaxFootprint) const
{
	auto Fits = [&MaxFootprint](const FMTAInteriorTemplate* T)
	{
		return MaxFootprint.X <= 0.f || MaxFootprint.Y <= 0.f
			|| (T->Footprint.X <= MaxFootprint.X && T->Footprint.Y <= MaxFootprint.Y)
			|| (T->Footprint.Y <= MaxFootprint.X && T->Footprint.X <= MaxFootprint.Y);
	};
	TArray<const FMTAInteriorTemplate*> Candidates;
	GetTemplates(District, RoomType, Candidates);
	Candidates.RemoveAll([&Fits](const FMTAInteriorTemplate* T) { return !Fits(T); });
	if (Candidates.Num() == 0)
	{
		// Bezirk egal, Raumtyp bleibt
		GetTemplates(EMTADistrict::Count, RoomType, Candidates);
		Candidates.RemoveAll([&Fits](const FMTAInteriorTemplate* T) { return !Fits(T); });
	}
	if (Candidates.Num() == 0) { return nullptr; }
	FRandomStream Rng(Seed);
	return Candidates[Rng.RandRange(0, Candidates.Num() - 1)];
}
