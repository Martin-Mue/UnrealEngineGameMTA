#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MTATrafficTypes.h"
#include "MTAHeatSubsystem.generated.h"

/** Fahndungszustand eines Spielers: global (treibt die Polizei) und je Bezirk (Ruf). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAPlayerHeat
{
	GENERATED_BODY()

	/** Globale Fahndung 0..5 (kontinuierlich; Sterne = aufgerundet). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Law")
	float GlobalHeat = 0.f;

	/** Bezirks-Ruf 0..5 je Bezirk. Kuehlt langsamer ab als die globale Fahndung. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Law")
	TMap<FName, float> DistrictHeat;

	/** Weltzeit der letzten Straftat (s). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Law")
	float LastCrimeTime = -1000.f;

	/** Bezirk, in dem sich der Spieler zuletzt befand. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Law")
	FName CurrentDistrict = NAME_None;

	/** Zuletzt gemeldete Werte (fuer Delegates mit Schwelle). */
	float LastBroadcastHeat = 0.f;
	int32 LastBroadcastStars = 0;
};

/**
 * Fahndung (Modul M6): pro Spieler global + je Bezirk (Bezirks-Ruf, docs/WELTKONZEPT.md).
 *
 * VERHALTENSPROTOKOLL FAHNDUNG:
 *   AddPlayerHeat(P, x, D): Global = clamp(Global + x, 0, 5); Bezirk[D] = clamp(Bezirk[D] + x, 0, 5);
 *                           LastCrimeTime = jetzt. (D = None -> aktueller Bezirk des Spielers)
 *   TickCooldown(dt, PolizeiNah): wenn Global > 0 UND jetzt - LastCrimeTime > CooldownDelay UND
 *                           NICHT PolizeiNah: Global -= CooldownRate * dt (Godot: 0,12/s nach 8 s).
 *                           Bezirke kuehlen immer (nach CooldownDelay) mit DistrictCooldownRate ab.
 *   NotifyPlayerDistrict(P, D): Bezirkswechsel; ist Bezirk[D] >= RecognitionThreshold und Global == 0,
 *                           wird Global auf RecognitionHeat gesetzt ("man kennt dich hier").
 *   ClearHeat(P): Festnahme -> Global = 0; Bezirke * DistrictKeepOnArrest.
 *   Sterne = ceil(Global - 0.001), also 0..5.
 *   Delegates (nur Spieler 0 = lokaler Spieler): OnHeatChanged bei Aenderung >= 0,05 oder Sternwechsel
 *   oder Erreichen von 0; OnWantedLevelChanged bei Sternwechsel.
 *
 * Die Abkuehlung tickt NICHT selbst, sondern wird vom AMTAWorldAIManager (fester Schritt) aufgerufen.
 * Ohne Manager kann der GameMode TickCooldown selbst aufrufen.
 */
UCLASS()
class MTA6_API UMTAHeatSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// --- Kompatible Alt-API (Spieler 0) -------------------------------------------------------

	/** Erhoeht die Fahndung in einem Bezirk (0..5 Sterne) fuer Spieler 0 (global + Bezirk). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Law")
	void AddHeat(FName DistrictId, float Amount);

	/** Globale Fahndung von Spieler 0 (DistrictId wird ignoriert; Kompatibilitaet zum GameMode). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Law")
	float GetHeat(FName DistrictId) const;

	// --- Pro Spieler ----------------------------------------------------------------------------

	/** Straftat melden. DistrictId = None -> aktueller Bezirk des Spielers. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Law")
	void AddPlayerHeat(int32 PlayerIndex, float Amount, FName DistrictId = NAME_None);

	/** Setzt die globale Fahndung direkt (Debug/Story). Zaehlt nicht als Straftat. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Law")
	void SetGlobalHeat(int32 PlayerIndex, float NewHeat);

	UFUNCTION(BlueprintPure, Category = "MTA6|Law")
	float GetGlobalHeat(int32 PlayerIndex = 0) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Law")
	float GetDistrictHeat(int32 PlayerIndex, FName DistrictId) const;

	/** Fahndungsstufe in Sternen 0..5. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Law")
	int32 GetWantedLevel(int32 PlayerIndex = 0) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Law")
	float GetSecondsSinceLastCrime(int32 PlayerIndex = 0) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Law")
	FName GetCurrentDistrict(int32 PlayerIndex = 0) const;

	/** Abkuehlung; wird vom Manager im festen Schritt aufgerufen. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Law")
	void TickCooldown(float DeltaSeconds, bool bPoliceNearby, int32 PlayerIndex = 0);

	/** Bezirkswechsel melden (Bezirks-Ruf / Wiedererkennung). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Law")
	void NotifyPlayerDistrict(int32 PlayerIndex, FName DistrictId);

	/** Festnahme/Neustart: globale Fahndung loeschen, Bezirks-Ruf reduzieren. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Law")
	void ClearHeat(int32 PlayerIndex = 0);

	/** Liefert eine Kopie des Zustands (fuer Speicherstaende/Debug). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Law")
	FMTAPlayerHeat GetPlayerHeatState(int32 PlayerIndex = 0) const;

	/** Globale Fahndung von Spieler 0 hat sich geaendert. */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Law")
	FOnHeatChanged OnHeatChanged;

	/** Sterne von Spieler 0 haben sich geaendert. */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Law")
	FOnWantedLevelChanged OnWantedLevelChanged;

	// --- Feinwerte ------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Law")
	float MaxHeat = 5.f;

	/** Sekunden ohne Straftat, bevor die Abkuehlung beginnt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Law")
	float CooldownDelay = 8.f;

	/** Globale Abkuehlung pro Sekunde. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Law")
	float CooldownRate = 0.12f;

	/** Bezirks-Abkuehlung pro Sekunde (langsamer: "die Leute vergessen nicht so schnell"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Law")
	float DistrictCooldownRate = 0.03f;

	/** Wiedererkennung im Bezirk ab diesem Bezirks-Ruf (> MaxHeat = aus). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Law")
	float RecognitionThreshold = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Law")
	float RecognitionHeat = 1.f;

	/** Anteil des Bezirks-Rufs, der eine Festnahme ueberdauert. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Law")
	float DistrictKeepOnArrest = 0.5f;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }

private:
	FMTAPlayerHeat& FindOrAddPlayer(int32 PlayerIndex);
	const FMTAPlayerHeat* FindPlayer(int32 PlayerIndex) const;
	float Now() const;
	void BroadcastIfNeeded(int32 PlayerIndex, FMTAPlayerHeat& State, bool bForce);
	static int32 HeatToStars(float Heat);

	UPROPERTY()
	TMap<int32, FMTAPlayerHeat> Players;
};
