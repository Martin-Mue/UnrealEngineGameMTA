#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MTADistrict.h"
#include "MTAInteriorTypes.generated.h"

class APawn;
class AActor;

/**
 * Gemeinsame Typen der Innenraeume (siehe docs/INNENRAEUME.md).
 * Architektur: Innenraeume sind echte, versteckte Raumzellen im "Interior Grid" unter der
 * Stadt. Tueren am Aussenbau (AMTABuildingEntry) teleportieren mit Ueberblende hinein.
 */

/** Art eines Gebaeudeeingangs. Die Namen entsprechen "entrance" in interiors.json. */
UENUM(BlueprintType)
enum class EMTAEntranceKind : uint8
{
	Front      UMETA(DisplayName = "Vordertuer"),
	Back       UMETA(DisplayName = "Hinterhof"),
	Roof       UMETA(DisplayName = "Dach"),
	Emergency  UMETA(DisplayName = "Notausgang"),
	Garage     UMETA(DisplayName = "Garage (Fahrzeuge)")
};

/** Ergebnis der Zugangspruefung eines Eingangs oder einer Innentuer. */
UENUM(BlueprintType)
enum class EMTALockState : uint8
{
	Open           UMETA(DisplayName = "Offen"),
	ClosedHours    UMETA(DisplayName = "Geschlossen (Uhrzeit)"),
	FactionDenied  UMETA(DisplayName = "Kein Zutritt (Fraktion)"),
	NeedsKey       UMETA(DisplayName = "Schluessel noetig"),
	ExitOnly       UMETA(DisplayName = "Nur Ausgang"),
	VehicleDenied  UMETA(DisplayName = "Nicht mit Fahrzeug"),
	Sealed         UMETA(DisplayName = "Versiegelt")
};

/** Arten interaktiver Objekte im Innenraum. */
UENUM(BlueprintType)
enum class EMTAInteractiveKind : uint8
{
	Door        UMETA(DisplayName = "Tuer"),
	Switch      UMETA(DisplayName = "Schalter"),
	Terminal    UMETA(DisplayName = "Terminal (Hack)"),
	HidingSpot  UMETA(DisplayName = "Versteck"),
	Secret      UMETA(DisplayName = "Geheimnis/Sammelobjekt"),
	Exit        UMETA(DisplayName = "Ausgang")
};

/** Grundformen aus /Engine/BasicShapes (Platzhalter bis echte Meshes existieren). */
enum class EMTAPrimShape : uint8 { Cube, Cylinder, Sphere, Cone, Plane, Count };

/** Render-/Kollisionsgruppe eines Primitivs (je Gruppe und Form eine ISM-Komponente). */
enum class EMTAPrimLayer : uint8 { Solid, Deco, Glass, Backdrop, Count };

namespace MTAInterior
{
	MTA6_API FName EntranceKindToName(EMTAEntranceKind Kind);
	MTA6_API bool EntranceKindFromName(FName Name, EMTAEntranceKind& OutKind);
	MTA6_API FName DistrictToName(EMTADistrict District);

	/** True, wenn Stunde Hour (0..24) im Fenster [Open, Close) liegt; Close < Open = ueber Mitternacht. */
	inline bool IsHourInWindow(float Hour, float Open, float Close)
	{
		if (Open == Close || (Open <= 0.f && Close >= 24.f)) return true;
		Hour = FMath::Fmod(FMath::Fmod(Hour, 24.f) + 24.f, 24.f);
		return Open < Close ? (Hour >= Open && Hour < Close) : (Hour >= Open || Hour < Close);
	}
}

/** Zugangsregel eines Eingangs (Uhrzeit, Fraktionsstand, Schluessel, Hacking). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAAccessRule
{
	GENERATED_BODY()

	/** Ohne Oeffnungszeiten ist der Eingang nur mit Schluessel/Hack/Fraktion offen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Access")
	bool bHasOpeningHours = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Access", meta = (ClampMin = "0", ClampMax = "24"))
	float OpenHour = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Access", meta = (ClampMin = "0", ClampMax = "24"))
	float CloseHour = 24.f;

	/** Fraktions-ID wie in story.json (aurelia, kcg, union, collective, wrenchers); leer = egal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Access")
	FString Faction;

	/** Mindeststand bei der Fraktion (UMTAStorySubsystem::GetStanding). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Access")
	float MinStanding = -1000.f;

	/** Schluessel, der immer Zugang gibt (IMTAKeyHolder::HasKey). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Access")
	FName KeyId;

	/** 0 = nicht hackbar, 1..5 = Schwierigkeit des Hack-Minispiels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Access", meta = (ClampMin = "0", ClampMax = "5"))
	int32 HackDifficulty = 0;

	/** Notausgaenge: von aussen nicht zu oeffnen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Access")
	bool bExitOnly = false;
};

/** NPC-Slot fuer Hintergrundleben im Innenraum (wird vom NPC-Agenten belegt). */
USTRUCT(BlueprintType)
struct MTA6_API FMTANPCSlot
{
	GENERATED_BODY()

	/** Weltposition und Blickrichtung (nach dem Bau des Innenraums gueltig). */
	UPROPERTY(BlueprintReadOnly, Category = "NPC") FTransform Transform;
	/** Aktivitaet: sleep, cook, drink, dance, work, repair, pray, hack, guard, shopkeeper, ... */
	UPROPERTY(BlueprintReadOnly, Category = "NPC") FName Activity;
	/** Rolle: resident, shopkeeper, patron, worker, guard, medic, ... */
	UPROPERTY(BlueprintReadOnly, Category = "NPC") FName Role;
	/** Aktiv nur in diesem Stundenfenster (Von == Bis = immer). */
	UPROPERTY(BlueprintReadOnly, Category = "NPC") float FromHour = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "NPC") float ToHour = 0.f;
	/** Besetzungswahrscheinlichkeit 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "NPC") float Chance = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "NPC") FName RoomId;
	UPROPERTY(BlueprintReadOnly, Category = "NPC") int32 Floor = 0;
};

/** Delegates des Innenraumsystems. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSecretFound, FName, Id);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMTAOnHackRequested, UObject*, Target, int32, Difficulty, FName, ContextId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMTAOnHidingChanged, APawn*, Pawn, bool, bHidden, AActor*, Spot);

// ---------------------------------------------------------------------------
// Schnittstellen
// ---------------------------------------------------------------------------

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UMTAEnterable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Betretbares Objekt (Gebaeudeeingang). Der Zu-Fuss-Agent ruft das aus seiner
 * IMTAInteractable-Implementierung bzw. seinem Interaktions-Trace auf:
 *   if (IMTAEnterable* E = Cast<IMTAEnterable>(HitActor)) { Prompt = E->GetEnterPrompt(Pawn); ... E->Enter(Pawn); }
 */
class MTA6_API IMTAEnterable
{
	GENERATED_BODY()

public:
	/** Prueft Zugang fuer den naechstgelegenen Eingang; OutReason ist fuer das HUD gedacht. */
	virtual bool CanEnter(APawn* Pawn, FText& OutReason) const = 0;
	/** Interaktionstext, z. B. "Betreten: Kneipe Zum Ankerkettenriss" oder "Aufbrechen (Hack 2)". */
	virtual FText GetEnterPrompt(APawn* Pawn) const = 0;
	/** Startet den Uebergang (Ueberblende + Teleport). False, wenn verschlossen. */
	virtual bool Enter(APawn* Pawn) = 0;
	/** Index des Eingangs, den Pawn gerade benutzt (naechstgelegener in Reichweite), sonst INDEX_NONE. */
	virtual int32 FindEntranceFor(const APawn* Pawn) const = 0;
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UMTAInteriorUsable : public UInterface
{
	GENERATED_BODY()
};

/** Benutzbares Objekt im Innenraum (Tuer, Schalter, Terminal, Versteck, Geheimnis, Ausgang). */
class MTA6_API IMTAInteriorUsable
{
	GENERATED_BODY()

public:
	virtual FText GetUsePrompt(APawn* Pawn) const = 0;
	virtual bool CanUse(APawn* Pawn) const = 0;
	virtual bool Use(APawn* Pawn) = 0;
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UMTAKeyHolder : public UInterface
{
	GENERATED_BODY()
};

/** Vom Inventar-/Charakter-Agenten auf Pawn, Controller oder PlayerState zu implementieren. */
class MTA6_API IMTAKeyHolder
{
	GENERATED_BODY()

public:
	virtual bool HasKey(FName KeyId) const = 0;
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UMTAHackable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Ziel eines Hack-Minispiels. Ablauf: Ziel ruft UMTAInteriorSubsystem::RequestHack ->
 * Delegate OnHackRequested (UI/Minispiel) -> Minispiel ruft UMTAInteriorSubsystem::ResolveHack
 * -> OnHackResolved am Ziel.
 */
class MTA6_API IMTAHackable
{
	GENERATED_BODY()

public:
	virtual void OnHackResolved(bool bSuccess, FName ContextId) = 0;
};
