#pragma once

#include "CoreMinimal.h"
#include "MTAInteriorTypes.h"

struct FMTAInteriorTemplate;
struct FMTAStyleDef;
class FMTAInteriorDatabase;

/**
 * Reiner Rechenteil der Innenraum-Erzeugung: aus Grundriss + Moebelregeln + Stil wird eine
 * Liste von Primitiven, Lichtern, interaktiven Objekten und NPC-Slots (lokaler Raum, cm).
 * Keine UObjects -> deterministisch, testbar, spaeter auf einen Worker-Thread verschiebbar.
 */

struct MTA6_API FMTAPrimInstance
{
	EMTAPrimShape Shape = EMTAPrimShape::Cube;
	EMTAPrimLayer Layer = EMTAPrimLayer::Solid;
	FTransform Xf;
	FLinearColor Color = FLinearColor::White;
	float Emissive = 0.f;
};

struct MTA6_API FMTALightSpec
{
	FVector Pos = FVector::ZeroVector;
	FRotator Rot = FRotator::ZeroRotator;
	FLinearColor Color = FLinearColor::White;
	float Intensity = 1000.f;
	float Radius = 600.f;
	/** Fensterlicht (Rect Light, Tageszeit-abhaengig). */
	bool bWindow = false;
	float Width = 100.f;
	float Height = 100.f;
	int32 RoomIndex = INDEX_NONE;
	bool bFlicker = false;
	bool bShadows = false;
};

struct MTA6_API FMTAInteractiveSpec
{
	EMTAInteractiveKind Kind = EMTAInteractiveKind::Terminal;
	FTransform Xf;
	FName Id;
	FName Links;
	FName KeyId;
	int32 HackDifficulty = 0;
	FName SecretId;
	FName SecretKind;
	bool bHidden = false;
	EMTAEntranceKind Entrance = EMTAEntranceKind::Front;
	/** Tuerbreite/-hoehe bzw. Groesse des Nutzvolumens (cm). */
	FVector2D Size = FVector2D(100.f, 220.f);
	int32 RoomIndex = INDEX_NONE;
};

struct MTA6_API FMTABuiltRoom
{
	FName Id;
	FName Kind;
	int32 Floor = 0;
	FBox Bounds = FBox(ForceInit);
};

struct MTA6_API FMTAInteriorBuildSettings
{
	int32 MaxLights = 24;
	int32 MaxShadowLights = 4;
	int32 MaxWindowLights = 8;
	/** Abstand der Fensterkulisse vor dem Fenster (cm). */
	float BackdropDistance = 150.f;
};

struct MTA6_API FMTAInteriorBuildResult
{
	TArray<FMTAPrimInstance> Prims;
	TArray<FMTALightSpec> Lights;
	TArray<FMTAInteractiveSpec> Interactives;
	TArray<FMTANPCSlot> NpcSlots;
	TMap<EMTAEntranceKind, FTransform> EntranceSpawns;
	TArray<FMTABuiltRoom> Rooms;
	FBox Bounds = FBox(ForceInit);

	/** Grobe Speicherschaetzung in KB (Instanzdaten, Lichter, Actors) fuer das Budget. */
	float EstimateCostKB() const;
};

class MTA6_API FMTAInteriorBuilder
{
public:
	static bool Build(const FMTAInteriorTemplate& Template, const FMTAInteriorDatabase& Db, const FMTAStyleDef& Style,
		int32 Seed, const FMTAInteriorBuildSettings& Settings, FMTAInteriorBuildResult& Out);
};
