#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MTAWeatherTypes.generated.h"

/** Wetterzustaende von Kestrel Bay (Reihenfolge = Index in weather.json-Matrizen, nicht umsortieren). */
UENUM(BlueprintType)
enum class EMTAWeatherState : uint8
{
	Clear        UMETA(DisplayName = "Klar"),
	Cloudy       UMETA(DisplayName = "Bewoelkt"),
	Haze         UMETA(DisplayName = "Dunst"),
	Rain         UMETA(DisplayName = "Regen"),
	Thunderstorm UMETA(DisplayName = "Gewitter"),
	SmogFog      UMETA(DisplayName = "Smog / Nebel"),
	DustStorm    UMETA(DisplayName = "Staubsturm"),
	Count        UMETA(Hidden)
};

/**
 * Aktuelle (geblendete) Wetterwirkung als reine Parameter. Alle Systeme (Fahrzeug, Audio, Verkehr,
 * Materialien) lesen diese Struktur, nie den Zustand direkt.
 */
USTRUCT(BlueprintType)
struct MTA6_API FMTAWeatherParams
{
	GENERATED_BODY()

	/** Dominanter Zustand (der, aus dem gerade geblendet wird, bzw. der aktuelle). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	EMTAWeatherState State = EMTAWeatherState::Clear;

	/** Zielzustand eines laufenden Uebergangs (gleich State, wenn keiner laeuft). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	EMTAWeatherState TargetState = EMTAWeatherState::Clear;

	/** 0 = Start des Uebergangs, 1 = Ziel erreicht (geglaettet). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather")
	float TransitionAlpha = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Weather") float RainIntensity = 0.f;
	/** Nasse der Strasse 0..1 (traege: steigt bei Regen, trocknet langsam). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float Wetness = 0.f;
	/** Strassen-Roughness fuers Material (trocken ~0.85, nass ~0.18). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float RoadRoughness = 0.85f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float FogDensity = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float WindStrength = 0.1f;
	/** Windstoesse (Basiswind mit Perlin-Modulation), 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float WindGust = 0.1f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float WindDirectionDegrees = 255.f;
	/** Sichtweite in Metern. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float VisibilityMeters = 8000.f;
	/** Multiplikator auf die Reifenhaftung (1 = trocken). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float GripMultiplier = 1.f;
	/** Geraeuschdaempfung der Umgebung 0..1 (Nebel/Regen/Staub). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float SoundDamping = 0.f;
	/** Multiplikator auf die Verkehrsdichte. */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float TrafficDensity = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float CloudCover = 0.1f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float LightningActivity = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float DustAmount = 0.f;
	/** Himmel-/Lichtabdunklung 0..1 (fuer Sonne/Himmel-Systeme). */
	UPROPERTY(BlueprintReadOnly, Category = "Weather") float SkyDarkening = 0.f;
};

UINTERFACE(BlueprintType, MinimalAPI)
class UMTAWeatherConsumer : public UInterface
{
	GENERATED_BODY()
};

/**
 * Schnittstelle fuer Systeme, die auf Wetter reagieren. Registrierung ueber
 * AMTAWeatherManager::RegisterConsumer(this). Der Aufruf kommt hoechstens ~10x pro Sekunde.
 */
class MTA6_API IMTAWeatherConsumer
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MTA6|Weather")
	void OnWeatherUpdated(const FMTAWeatherParams& Params);
};

namespace MTAWeather
{
	/** Name des Zustands wie in weather.json ("Clear", "Cloudy", ...). */
	MTA6_API FName StateName(EMTAWeatherState State);
	MTA6_API bool StateFromName(FName Name, EMTAWeatherState& OutState);
	/** Deutscher Anzeigename (fuer Radio-/Aegis-Texte). */
	MTA6_API FString StateDisplayName(EMTAWeatherState State);
	constexpr int32 Num() { return static_cast<int32>(EMTAWeatherState::Count); }
}
