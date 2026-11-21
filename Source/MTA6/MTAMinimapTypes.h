#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MTAMinimapTypes.generated.h"

/**
 * Datenschnittstelle Minimap <-> Welt-KI.
 * Diese Datei wird nur vom Spieler/Spielfluss-Teil gepflegt. Die KI (Verkehr, Polizei,
 * Fussgaenger) liefert ihre Positionen ueber IMTAMinimapProvider oder ueber den
 * Delegate AMTA6GameMode::OnGatherMinimapBlips. Der GameMode sammelt einmal pro Frame
 * alle Punkte ein; HUD (Minimap) und Festnahme-Logik lesen daraus.
 */

/**
 * Stadtraster wie im Godot-Prototyp (dort 80 m Periode, 8x8 Bloecke), hier in cm.
 * Strassenachsen/Kreuzungen liegen bei R(k) = k * Period - Half fuer k = 0..Blocks.
 * Blockmitten liegen bei R(i) + Period / 2 fuer i = 0..Blocks-1.
 */
namespace MTAGrid
{
	constexpr float Period = 8000.f;
	constexpr int32 Blocks = 8;
	constexpr float Half = Period * Blocks * 0.5f;
	/** Halbe Kantenlaenge eines bebauten Blocks (Godot: 32 m). */
	constexpr float BlockHalfSize = 3200.f;

	inline float R(int32 K) { return K * Period - Half; }

	/** Kreuzung (CellX, CellY) -> Weltposition (X, Y) in cm. */
	inline FVector2D CellToWorld(int32 CellX, int32 CellY) { return FVector2D(R(CellX), R(CellY)); }
}

/** Art eines Minimap-Punkts. */
UENUM(BlueprintType)
enum class EMTAMinimapBlipKind : uint8
{
	Traffic     UMETA(DisplayName = "Verkehr"),
	Police      UMETA(DisplayName = "Polizei"),
	Pedestrian  UMETA(DisplayName = "Fussgaenger"),
	Other       UMETA(DisplayName = "Sonstiges")
};

/** Ein Punkt auf der Minimap in Weltkoordinaten (cm, X/Y-Ebene). */
USTRUCT(BlueprintType)
struct FMTAMinimapBlip
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Minimap")
	FVector2D WorldPos = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Minimap")
	EMTAMinimapBlipKind Kind = EMTAMinimapBlipKind::Traffic;

	FMTAMinimapBlip() = default;
	FMTAMinimapBlip(const FVector2D& InPos, EMTAMinimapBlipKind InKind) : WorldPos(InPos), Kind(InKind) {}
};

/** Alternative zur Schnittstelle: reine C++-Delegate, an die sich die KI haengen kann. */
DECLARE_MULTICAST_DELEGATE_OneParam(FMTAGatherMinimapBlips, TArray<FMTAMinimapBlip>& /*OutBlips*/);

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UMTAMinimapProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Wird von KI-Systemen (Actor, Komponente oder Subsystem) implementiert und per
 * AMTA6GameMode::RegisterMinimapProvider(this) angemeldet.
 */
class IMTAMinimapProvider
{
	GENERATED_BODY()

public:
	/** Haengt alle aktuell sichtbaren Punkte an OutBlips an (nicht leeren!). */
	virtual void GatherMinimapBlips(TArray<FMTAMinimapBlip>& OutBlips) const {}
};
