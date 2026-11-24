#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Templates/Function.h"
#include "MTANPCTypes.h"

/**
 * Strassen-Figuren ("Nebenfiguren" ohne Profil in npcs.json) fuer die lebendige Welt.
 *
 * Erzeugt deterministisch (Seed) generische Passanten mit Zufallsnamen, einem Archetyp aus
 * Content/Data/appearance.json (nur als Name/Beruf-Text, das Aussehen bleibt das Mannequin)
 * und einem einfachen Tagesablauf (Tagarbeiter, Nachteule, Spaziergaenger, Haendler).
 * Die Orte sind Orts-Tags aus npcs.json (Treffpunkte mit den Schluesselfiguren) oder freie
 * Gehwegpunkte im Bezirk (Wohnung, Arbeitsplatz).
 *
 * Reine Datenschicht ohne UObjects; AMTALivingWorldManager macht daraus sichtbare Figuren.
 */

/** Ein Punkt auf der Gehweg-Laufspur mit Ausrichtung. */
struct MTA6_API FMTALivingSpot
{
	/** Weltposition auf der Laufspur (Z = Gehweg-Oberkante). */
	FVector Pos = FVector::ZeroVector;
	/** Richtung entlang des Gehwegs. */
	FVector Tangent = FVector(1.f, 0.f, 0.f);
	/** Richtung von der Laufspur zur Hauswand (Blockinneres). */
	FVector Normal = FVector(0.f, 1.f, 0.f);
	bool bValid = false;
};

/** Ein Block im Tagesablauf einer Strassenfigur: [StartHour, EndHour). */
struct MTA6_API FMTALivingScheduleEntry
{
	float StartHour = 0.f;
	float EndHour = 24.f;
	EMTAActivity Activity = EMTAActivity::Home;
	/** Orts-Tag aus npcs.json (None = freier Gehwegpunkt Spot). */
	FName Tag;
	FMTALivingSpot Spot;
	bool bTalkable = false;
	bool bTrading = false;
	/** Figur schlendert in der Naehe des Ortes hin und her. */
	bool bWander = false;
};

/** Grundmuster des Tagesablaufs. */
enum class EMTAStreetRoutine : uint8
{
	DayWorker,
	NightOwl,
	Wanderer,
	Vendor
};

/** Archetyp aus appearance.json (nur die fuer Text und Verteilung noetigen Felder). */
struct MTA6_API FMTAStreetArchetype
{
	FName Id;
	FString Name;
	FName Faction;
	/** Gewicht je Bezirk in der Reihenfolge core, saltmarket, lantern, greenmoor, rustline, sunstrip. */
	float DistrictWeights[6] = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };
	float DayWeight = 1.f;
	float NightWeight = 1.f;
};

/** Orts-Tags eines Bezirks, nach Verwendung sortiert. */
struct MTA6_API FMTAStreetTagPool
{
	TArray<FName> Social;
	TArray<FName> Bars;
	TArray<FName> Food;
	TArray<FName> Markets;
	TArray<FName> Public;
};

/** Eine generische Strassenfigur. */
struct MTA6_API FMTAStreetProfile
{
	FName Id;
	FString DisplayName;
	/** Berufs-/Archetyp-Text, z. B. "Dockarbeiterin". */
	FString Occupation;
	FName ArchetypeId;
	FName District;
	FName Faction;
	EMTAStreetRoutine Routine = EMTAStreetRoutine::DayWorker;
	/** Gespraechigkeit 0..1 (Wahrscheinlichkeit fuer Plaudereien). */
	float Talk = 0.5f;
	/** Reizbarkeit 0..1 (Streit). */
	float Temper = 0.3f;
	/** Gehgeschwindigkeit in cm/s. */
	float WalkSpeed = 150.f;
	TArray<FMTALivingScheduleEntry> Schedule;
};

namespace MTAStreetCrowd
{
	/** Bezirks-IDs wie in npcs.json, in der Reihenfolge von appearance.json/districtOrder. */
	MTA6_API const TArray<FName>& GetDistrictIds();

	/** Laedt die Archetypen aus Content/Data/appearance.json; ohne Datei eine kleine eingebaute Liste. */
	MTA6_API TArray<FMTAStreetArchetype> LoadArchetypes();

	/** Liefert einen Gehwegpunkt im Bezirk; Salt waehlt deterministisch Block, Seite und Position. */
	using FSpotPicker = TFunctionRef<FMTALivingSpot(FName District, uint32 Salt)>;

	/**
	 * Erzeugt Count Strassenfiguren, gleichmaessig auf die Bezirke verteilt.
	 * Pools: Orts-Tags je Bezirk (Treffpunkte), PickSpot: freie Gehwegpunkte (Wohnung/Arbeit).
	 */
	MTA6_API void GenerateProfiles(int32 Count, int32 Seed, const TArray<FMTAStreetArchetype>& Archetypes,
		const TMap<FName, FMTAStreetTagPool>& Pools, FSpotPicker PickSpot, TArray<FMTAStreetProfile>& OutProfiles);

	/** Block des Tagesablaufs zur Stunde (0..24); nullptr bei leerem Plan. */
	MTA6_API const FMTALivingScheduleEntry* FindEntry(const TArray<FMTALivingScheduleEntry>& Schedule, float Hour);

	/** Zufallsname (Vor- und Nachname). */
	MTA6_API FString MakeRandomName(FRandomStream& Rng);
}
