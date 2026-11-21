#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "MTADistrict.h"
#include "MTAAudioTypes.generated.h"

/**
 * Spielzustand, auf den Radio, Ambience und Aegis-Durchsagen reagieren. Wird von aussen gefuellt
 * (HUD/GameMode/Heat/Story) und ist bewusst reine Daten.
 */
USTRUCT(BlueprintType)
struct MTA6_API FMTAAudioContext
{
	GENERATED_BODY()

	/** Fahndungsstufe 0..5. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") int32 WantedLevel = 0;
	/** Bezirks-ID in Kleinschreibung: core, saltmarket, lanternhill, greenmoor, rustline, sunstrip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") FName District = TEXT("core");
	/** Wetterzustand wie in weather.json ("Clear", "Rain", ...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") FName Weather = TEXT("Clear");
	/** Spielstunde 0..24. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") float Hour = 12.f;
	/** Aegis-Kameradichte des Bezirks 0..1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") float AegisDensity = 1.f;
	/** Gesetzte Story-Flags (z. B. leakedLedger, savedJonas, killedCivilians). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio") TSet<FName> Flags;
};

/** Bedingung ("when") aus den JSON-Dateien: alle gesetzten Felder muessen zutreffen (UND). */
struct MTA6_API FMTAAudioCondition
{
	int32 WantedMin = 0;
	int32 WantedMax = 5;
	TArray<FName> Weather;
	TArray<FName> District;
	TArray<FName> FlagsAll;
	TArray<FName> FlagsNone;
	float HourFrom = -1.f;
	float HourTo = -1.f;
	float MinAegisDensity = 0.f;

	static FMTAAudioCondition FromJson(const TSharedPtr<FJsonObject>& Obj);
	bool Evaluate(const FMTAAudioContext& Ctx) const;
};

namespace MTAAudio
{
	/** Bezirks-ID (Kleinschreibung) passend zu EMTADistrict. */
	MTA6_API FName DistrictId(EMTADistrict District);
	MTA6_API bool DistrictFromId(FName Id, EMTADistrict& Out);
	MTA6_API FString DistrictDisplayName(FName Id);
	/** Stunde in [From, To) mit Mitternachtsueberlauf. */
	MTA6_API bool HourInRange(float Hour, float From, float To);
	/** Liest eine JSON-Datei relativ zu Content/ (z. B. "Data/radio.json"). */
	MTA6_API TSharedPtr<FJsonObject> LoadJsonFile(const FString& RelativeContentPath);
	/** Ersetzt {district} {weather} {hour} {wanted}. */
	MTA6_API FString FormatText(const FString& Template, const FMTAAudioContext& Ctx);
	MTA6_API TArray<FName> ReadNameArray(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field);
	MTA6_API float ReadNumber(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, float Default);
	MTA6_API FString ReadString(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, const FString& Default = FString());
}
