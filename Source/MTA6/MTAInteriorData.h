#pragma once

#include "CoreMinimal.h"
#include "MTAInteriorTypes.h"
#include "MTAFurnishing.h"
#include "MTAShopComponent.h"

/**
 * Datenbank der Gebaeudevorlagen aus Content/Data/interiors.json (+ furnishing.json).
 * Alle Laengen in cm. Grundriss-Koordinaten: lokales X/Y ab der Ecke (0,0) des Grundrisses,
 * Raster 50 cm (siehe tools/ue_interiors/validate_interiors.py).
 */

struct MTA6_API FMTARoomDef
{
	FName Id;
	FName Kind;
	FBox2D Rect = FBox2D(ForceInit);
};

struct MTA6_API FMTADoorDef
{
	FName RoomA;
	/** NAME_None = Aussenwand. */
	FName RoomB;
	FVector2D At = FVector2D::ZeroVector;
	float Width = 100.f;
	/** front/back/roof/emergency/garage oder None. */
	FName Entrance;
	bool bVehicles = false;
	/** Tuerblatt als interaktiver Actor (sonst offene Durchgangsoeffnung mit Zarge). */
	bool bPanel = false;
	FName KeyId;
	int32 HackDifficulty = 0;
	bool IsLocked() const { return !KeyId.IsNone() || HackDifficulty > 0; }
};

struct MTA6_API FMTAWindowDef
{
	FName Room;
	FVector2D At = FVector2D::ZeroVector;
	float Width = 100.f;
};

struct MTA6_API FMTAStairDef
{
	FName Room;
	FBox2D Rect = FBox2D(ForceInit);
	int32 ToLevel = 1;
	/** Lauf entlang X (sonst Y), steigt von Min nach Max. */
	bool bAlongX = true;
};

struct MTA6_API FMTAFloorDef
{
	int32 Level = 0;
	float Height = 300.f;
	TArray<FMTARoomDef> Rooms;
	TArray<FMTADoorDef> Doors;
	TArray<FMTAWindowDef> Windows;
	TArray<FMTAStairDef> Stairs;

	const FMTARoomDef* FindRoom(FName Id) const { return Rooms.FindByPredicate([Id](const FMTARoomDef& R) { return R.Id == Id; }); }
};

/** Gemeinsame Basis fuer Punktangaben (Props, Geheimnisse, NPC-Slots, Interaktive). */
struct MTA6_API FMTAPointDef
{
	int32 Floor = 0;
	FName Room;
	FVector At = FVector::ZeroVector; // Z optional (Hoehe ueber Boden)
	float Yaw = 0.f;
};

struct MTA6_API FMTAPropDef : FMTAPointDef { FName Item; };

struct MTA6_API FMTASecretDef : FMTAPointDef
{
	FName Id;
	FName Kind;      // collectible | note | stash
	bool bHidden = false;
};

struct MTA6_API FMTANPCSlotDef : FMTAPointDef
{
	FName Activity;
	FName Role;
	float FromHour = 0.f;
	float ToHour = 0.f;
	float Chance = 1.f;
};

struct MTA6_API FMTAInteractiveDef : FMTAPointDef
{
	EMTAInteractiveKind Kind = EMTAInteractiveKind::Terminal;
	FName Id;
	int32 HackDifficulty = 0;
	FName Links;
	FName KeyId;
};

struct MTA6_API FMTAInteriorTemplate
{
	FName Id;
	FName RoomType;
	FString Label;
	TArray<FName> Districts;
	FName Style;
	FName Shop;
	FVector2D Footprint = FVector2D::ZeroVector;
	FMTAAccessRule Access;
	TArray<FMTAFloorDef> Floors;
	TArray<FMTAPropDef> Props;
	TArray<FMTASecretDef> Secrets;
	TArray<FMTANPCSlotDef> NpcSlots;
	TArray<FMTAInteractiveDef> Interactives;

	/** Basis-Hoehe (cm, Oberkante Rohboden) der Etage Level. */
	float GetFloorBaseZ(int32 Level, float SlabThickness) const;
	const FMTAFloorDef* FindFloor(int32 Level) const { return Floors.FindByPredicate([Level](const FMTAFloorDef& F) { return F.Level == Level; }); }
	/** Alle Eingangsarten, die der Grundriss anbietet. */
	TArray<EMTAEntranceKind> GetEntranceKinds() const;
	bool AllowsVehicles() const;
};

struct MTA6_API FMTARoomTypeDef
{
	FName Id;
	FString Label;
	FName Role;
	TArray<FName> Activities;
};

struct MTA6_API FMTAShopDef
{
	FName Id;
	FString Label;
	FName OwnerNpcId;
	float OpenHour = 8.f;
	float CloseHour = 22.f;
	FName Category;
	TArray<FMTAShopWare> Wares;
};

/** Baumasse (cm) aus "defaults". */
struct MTA6_API FMTAInteriorDefaults
{
	float WallThickness = 20.f;
	float DoorHeight = 220.f;
	float WindowSill = 90.f;
	float WindowTop = 220.f;
	float SlabThickness = 20.f;
};

class MTA6_API FMTAInteriorDatabase
{
public:
	bool Load(const FString& InteriorsPath, const FString& FurnishingPath, FString& OutError);
	bool IsLoaded() const { return bLoaded; }

	const FMTAInteriorTemplate* FindTemplate(FName Id) const { return Templates.Find(Id); }
	const FMTARoomTypeDef* FindRoomType(FName Id) const { return RoomTypes.Find(Id); }
	const FMTAShopDef* FindShop(FName Id) const { return Shops.Find(Id); }
	const FMTAFurnishingLibrary& GetFurnishing() const { return Furnishing; }
	const FMTAInteriorDefaults& GetDefaults() const { return Defaults; }

	/** Stil fuer eine Vorlage: Vorlagen-Stil, sonst Bezirksstil, sonst erster Stil. */
	const FMTAStyleDef* ResolveStyle(const FMTAInteriorTemplate& T, EMTADistrict District, FName Override = NAME_None) const;

	/** Alle Vorlagen fuer Bezirk (und optional Raumtyp; None = alle Typen). */
	void GetTemplates(EMTADistrict District, FName RoomType, TArray<const FMTAInteriorTemplate*>& Out) const;
	/**
	 * Deterministische Auswahl: Bezirk + Raumtyp + Seed. MaxFootprint (cm, 0 = egal) filtert
	 * Vorlagen, die groesser als die Parzelle sind. Fallback: beliebiger Bezirk.
	 */
	const FMTAInteriorTemplate* PickTemplate(EMTADistrict District, FName RoomType, int32 Seed, FVector2D MaxFootprint) const;

	void GetAllTemplateIds(TArray<FName>& Out) const { Templates.GenerateKeyArray(Out); }
	int32 NumTemplates() const { return Templates.Num(); }
	int32 NumRoomTypes() const { return RoomTypes.Num(); }

private:
	bool bLoaded = false;
	TMap<FName, FMTAInteriorTemplate> Templates;
	TArray<FName> TemplateOrder;
	TMap<FName, FMTARoomTypeDef> RoomTypes;
	TMap<FName, FMTAShopDef> Shops;
	TMap<FName, FName> DistrictStyles;
	FMTAInteriorDefaults Defaults;
	FMTAFurnishingLibrary Furnishing;
};
