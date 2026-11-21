#pragma once

#include "CoreMinimal.h"
#include "MTATrafficTypes.h"
#include "MTAPoliceTypes.h"

/**
 * Interne Agentendaten der Welt-KI als Struct-of-Arrays (keine UObjects, keine Actors).
 * Alle Arrays einer Struktur haben stets dieselbe Laenge (Resize). Ein Agent ist ein Index.
 *
 * Spurmodell (wie Godot):
 *   Axis   0 = faehrt entlang X (Strasse liegt bei Y = R(Line)), 1 = entlang Y (X = R(Line)).
 *   Sign   +1/-1 Fahrtrichtung auf der Achse.
 *   Pos    Koordinate entlang der Achse (cm).
 *   Off    seitlicher Versatz zur Strassenmitte: Verkehr Rechtsverkehr +-400, Polizei 0, Fussgaenger +-950.
 *   Sim-Position (2D):  Axis 0 -> (Pos, R(Line) + Off);  Axis 1 -> (R(Line) + Off, Pos).
 *   Rechtsverkehr in UE (X vor, Y rechts): rechts von Richtung (dx,dy) ist (-dy, dx)
 *   -> Axis 0: Off = +400*Sign; Axis 1: Off = -400*Sign (identisch zur Godot-Formel).
 */

/** Kleine Rechenhilfen (im Header, damit Unity-Builds keine doppelten anonymen Funktionen sehen). */
namespace MTAAI
{
	/** Godot move_toward: naehert Current um hoechstens MaxDelta an Target an. */
	FORCEINLINE float MoveTowards(float Current, float Target, float MaxDelta)
	{
		if (FMath::Abs(Target - Current) <= MaxDelta)
		{
			return Target;
		}
		return Current + FMath::Sign(Target - Current) * MaxDelta;
	}

	/** Gierwinkel (Grad) einer 2D-Richtung, UE-Konvention (0 = +X, 90 = +Y). */
	FORCEINLINE float YawFromDir(const FVector2f& Dir)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	}

	/** 2D-Richtung aus Gierwinkel (Grad). */
	FORCEINLINE FVector2f DirFromYaw(float YawDeg)
	{
		const float R = FMath::DegreesToRadians(YawDeg);
		return FVector2f(FMath::Cos(R), FMath::Sin(R));
	}
}

/** Fahrzeuge (Zivilverkehr und Polizei in einem Pool; Polizei-Slots liegen hinten). */
struct FMTAVehicleSoA
{
	TArray<uint8> Axis;
	TArray<int8> Sign;
	TArray<int32> Line;
	TArray<float> Pos;
	TArray<float> Off;
	TArray<float> Speed;
	TArray<float> Cruise;
	TArray<float> Stun;
	TArray<float> Wait;
	TArray<float> Ghost;
	TArray<float> LodAccum;
	TArray<EMTAAgentKind> Kind;
	TArray<EMTAPoliceState> PoliceState;   // nur fuer Kind == Police relevant
	TArray<uint8> bActive;                 // Zivilverkehr immer 1, Polizei nach Dispatch
	TArray<uint8> bSnapVisual;             // naechster Frame: Darstellung springt (Spawn/Respawn)
	TArray<uint8> ColorIndex;              // Farbgruppe (Instanzkomponente)
	TArray<int32> InstanceIndex;           // Index innerhalb der Instanzkomponente
	TArray<FVector2f> SimLoc;              // Sim-Position (2D, cm)
	TArray<FVector2f> SimDir;              // Fahrtrichtung (Einheitsvektor)
	TArray<FVector> VisLoc;                // geglaettete Darstellung
	TArray<float> VisYaw;                  // geglaettete Darstellung (Grad)
	TArray<int32> GridNext;                // verkettete Liste im Raster (pro Schritt neu)

	int32 Num() const { return Axis.Num(); }

	void Resize(int32 N)
	{
		Axis.SetNumZeroed(N); Sign.SetNumZeroed(N); Line.SetNumZeroed(N); Pos.SetNumZeroed(N);
		Off.SetNumZeroed(N); Speed.SetNumZeroed(N); Cruise.SetNumZeroed(N); Stun.SetNumZeroed(N);
		Wait.SetNumZeroed(N); Ghost.SetNumZeroed(N); LodAccum.SetNumZeroed(N);
		Kind.SetNumZeroed(N); PoliceState.SetNumZeroed(N); bActive.SetNumZeroed(N);
		bSnapVisual.SetNumZeroed(N); ColorIndex.SetNumZeroed(N); InstanceIndex.SetNumZeroed(N);
		SimLoc.SetNumZeroed(N); SimDir.SetNumZeroed(N); VisLoc.SetNumZeroed(N); VisYaw.SetNumZeroed(N);
		GridNext.SetNumZeroed(N);
	}
};

/** Fussgaenger. */
struct FMTAPedestrianSoA
{
	TArray<uint8> Axis;
	TArray<int8> Sign;
	TArray<int32> Line;
	TArray<int8> Side;                     // +1/-1: Buergersteig links/rechts der Strasse
	TArray<float> Pos;
	TArray<float> Speed;
	TArray<float> Cruise;
	TArray<float> Stun;                    // > 0: umgefallen
	TArray<float> LodAccum;
	TArray<uint8> bSnapVisual;
	TArray<uint8> ColorIndex;
	TArray<int32> InstanceIndex;
	TArray<FVector2f> SimLoc;
	TArray<FVector2f> SimDir;
	TArray<FVector> VisLoc;
	TArray<float> VisYaw;

	int32 Num() const { return Axis.Num(); }

	void Resize(int32 N)
	{
		Axis.SetNumZeroed(N); Sign.SetNumZeroed(N); Line.SetNumZeroed(N); Side.SetNumZeroed(N);
		Pos.SetNumZeroed(N); Speed.SetNumZeroed(N); Cruise.SetNumZeroed(N); Stun.SetNumZeroed(N);
		LodAccum.SetNumZeroed(N); bSnapVisual.SetNumZeroed(N); ColorIndex.SetNumZeroed(N);
		InstanceIndex.SetNumZeroed(N); SimLoc.SetNumZeroed(N); SimDir.SetNumZeroed(N);
		VisLoc.SetNumZeroed(N); VisYaw.SetNumZeroed(N);
	}
};

/** Gleichmaessiges Raster fuer die Vorausschau (Abstandsregel) - O(n) statt Allpaarvergleich. */
struct FMTAAgentGrid
{
	float Origin = -MTACity::Half - 2000.f;
	float CellSize = 1200.f;
	int32 Dim = 0;
	TArray<int32> Heads;

	void Init(float InCellSize)
	{
		CellSize = FMath::Max(InCellSize, 100.f);
		Origin = -MTACity::Half - 2000.f;
		Dim = FMath::CeilToInt((2.f * MTACity::Half + 4000.f) / CellSize);
		Heads.SetNumUninitialized(Dim * Dim);
	}

	void Clear()
	{
		for (int32& H : Heads) { H = INDEX_NONE; }
	}

	FORCEINLINE int32 CellCoord(float V) const
	{
		return FMath::Clamp(FMath::FloorToInt((V - Origin) / CellSize), 0, Dim - 1);
	}

	FORCEINLINE int32 CellIndex(int32 CX, int32 CY) const { return CY * Dim + CX; }
};
