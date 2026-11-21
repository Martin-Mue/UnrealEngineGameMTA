#pragma once

#include "CoreMinimal.h"
#include "MTAInteriorTypes.h"

class FJsonObject;

/**
 * Moebel- und Dekobibliothek aus Content/Data/furnishing.json (reines C++, kein UObject).
 * Alle Laengen werden beim Laden von Metern in Zentimeter umgerechnet.
 */

/** Kleine JSON-Helfer fuer die Innenraum-Daten (Meter -> Zentimeter erfolgt beim Aufrufer). */
namespace MTAInteriorJson
{
	MTA6_API double Num(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, double Default = 0.0);
	MTA6_API FString Str(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, const FString& Default = FString());
	MTA6_API bool Bool(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, bool Default = false);
	/** Zahlenarray (z. B. [x, y, w, d]); fehlende Eintraege werden mit Default aufgefuellt. */
	MTA6_API TArray<double> Nums(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, int32 MinCount = 0, double Default = 0.0);
	MTA6_API TArray<FName> Names(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key);
	/** Objekt-Array; leeres Array, wenn das Feld fehlt. */
	MTA6_API TArray<TSharedPtr<FJsonObject>> Objects(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key);
	MTA6_API TSharedPtr<FJsonObject> Object(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key);
	MTA6_API TSharedPtr<FJsonObject> LoadFile(const FString& Path, FString& OutError);
}

/** Ein Primitiv-Teil eines Moebelstuecks (lokal zum Objektursprung am Boden). */
struct MTA6_API FMTAFurnPart
{
	EMTAPrimShape Shape = EMTAPrimShape::Cube;
	FVector Pos = FVector::ZeroVector;   // cm, Teilmitte
	FVector Size = FVector(100.f);       // cm, volle Ausdehnung
	FRotator Rot = FRotator::ZeroRotator;
	FLinearColor Color = FLinearColor::White;
	float Emissive = 0.f;
};

/** Lichtquelle eines Moebelstuecks (Lampe, Neon, Feuer). */
struct MTA6_API FMTAFurnLight
{
	bool bValid = false;
	FVector Pos = FVector::ZeroVector;
	FLinearColor Color = FLinearColor::White;
	float Intensity = 800.f;
	float Radius = 450.f;
};

enum class EMTAFurnMount : uint8 { Floor, Wall, Ceiling, Surface };

struct MTA6_API FMTAFurnItem
{
	FName Id;
	FString Label;
	FName Category;
	EMTAFurnMount Mount = EMTAFurnMount::Floor;
	/** Grundflaeche in cm: X = Tiefe (Front +X), Y = Breite. */
	FVector2D Footprint = FVector2D(50.f);
	float Height = 0.f;
	bool bCollision = true;
	TArray<FName> Styles;
	TArray<FMTAFurnPart> Parts;
	FMTAFurnLight Light;
	FName Activity;       // erzeugt NPC-Slot vor dem Objekt
	FName Interactive;    // hiding | terminal | switch

	bool AllowedFor(const TArray<FName>& StyleTags) const;
};

/** Wohnstil (arm/reich, sauber/verwahrlost, Neon/warm). */
struct MTA6_API FMTAStyleDef
{
	FName Id;
	FString Label;
	TArray<FName> Tags;
	float Wear = 0.3f;
	float Clutter = 1.f;
	FIntPoint Trash = FIntPoint(0, 2);
	float Plants = 0.3f;
	float Neon = 0.1f;
	float Flicker = 0.f;
	FLinearColor Wall = FLinearColor(0.7f, 0.7f, 0.7f);
	FLinearColor Floor = FLinearColor(0.4f, 0.4f, 0.4f);
	FLinearColor Ceiling = FLinearColor(0.85f, 0.85f, 0.85f);
	FLinearColor LightColor = FLinearColor(1.f, 0.9f, 0.8f);
	float LightIntensity = 1000.f;
	float LightsPer10m2 = 0.8f;
};

enum class EMTAFurnPlace : uint8 { Wall, Corner, Center, Scatter, WallDecor, Ceiling, Surface };

/** Fuellregel einer Raumart. */
struct MTA6_API FMTAFurnRule
{
	TArray<FName> Pick;
	EMTAFurnPlace Place = EMTAFurnPlace::Wall;
	FIntPoint Count = FIntPoint(1, 1);
	float Chance = 1.f;
	bool bClutter = false;
};

class MTA6_API FMTAFurnishingLibrary
{
public:
	bool LoadFromJson(const TSharedPtr<FJsonObject>& Root, FString& OutError);

	const FMTAFurnItem* FindItem(FName Id) const { return Items.Find(Id); }
	const FMTAStyleDef* FindStyle(FName Id) const { return Styles.Find(Id); }
	const TArray<FMTAFurnRule>* FindRules(FName RoomKind) const { return RoomKinds.Find(RoomKind); }

	/** Waehlt aus Pick ein zum Stil passendes Objekt (Fallback: beliebiges aus der Liste). */
	const FMTAFurnItem* PickItem(const TArray<FName>& Pick, const FMTAStyleDef& Style, FRandomStream& Rng) const;
	/** Alle Objekte mit Tag "trash" passend zum Stil. */
	void GetTrashItems(const FMTAStyleDef& Style, TArray<const FMTAFurnItem*>& Out) const;

	int32 NumItems() const { return Items.Num(); }
	int32 NumStyles() const { return Styles.Num(); }

	static FLinearColor ParseColor(const FString& Hex, const FLinearColor& Fallback = FLinearColor::White);
	static EMTAPrimShape ParseShape(const FString& S);

private:
	TMap<FName, FMTAFurnItem> Items;
	TMap<FName, FMTAStyleDef> Styles;
	TMap<FName, TArray<FMTAFurnRule>> RoomKinds;
	TArray<FName> TrashIds;
};
