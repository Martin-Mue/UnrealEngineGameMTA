#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "MTAProgressionTypes.h"
#include "MTACharacterOriginSubsystem.generated.h"

/** Startvorteil oder -nachteil einer Herkunft. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAOriginTrait
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") bool bPositive = true;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") TArray<FMTAStatModifier> Effects;
};

/** Herkunftsgeschichte fuer die Charaktererstellung (Content/Data/origins.json). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAOrigin
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") FString Summary;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") FString Backstory;
	/** Variante der Prolog-Mission (gleiche Perlenkette, andere Szene). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") FName PrologVariant;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") int64 StartMoney = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") int64 StartDebt = 0;
	/** Wird auf das Story-Start-Aegis (50) addiert. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") float AegisDelta = 0.f;
	/** Startstaende je Fraktion (auf Story-Startwerte addiert). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") TMap<FName, float> Standing;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") FName StartVehicle;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") FName StartDistrict;
	/** None = keine Startgarage. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") FName GarageDistrict;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") int32 GarageLevel = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") int32 PerkPoints = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") TArray<FName> StartSkills;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") TMap<FName, float> TreeXp;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") TArray<FName> ItemIds;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") TArray<int32> ItemCounts;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") TArray<FMTAOriginTrait> Traits;
	/** Eigene Dialogflags (werden als Story-Flags gesetzt, Werte als Text). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Origin") TMap<FString, FString> DialogFlags;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAOriginAppliedSignature, FName, OriginId);

/**
 * Charaktererstellung: laedt Herkuenfte und setzt beim Spielstart Geld, Schulden, Fraktionsstaende,
 * Aegis, Startfahrzeug/-garage, Skills, Gegenstaende, Eigenschaften (Modifikatorquelle "origin")
 * und Dialogflags (ueber die Story-Bruecke).
 */
UCLASS()
class MTA6_API UMTACharacterOriginSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 SchemaVersion = 1;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Origin")
	bool LoadOrigins(const FString& RelativePath = TEXT("Data/origins.json"));

	UFUNCTION(BlueprintPure, Category = "MTA6|Origin")
	TArray<FMTAOrigin> GetOrigins() const { return Origins; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Origin")
	bool GetOrigin(FName OriginId, FMTAOrigin& Out) const;

	/**
	 * Neues Spiel mit Herkunft starten. Setzt alle Fortschrittssysteme zurueck und wendet die Startwerte an.
	 * Reihenfolge: Story-Reset -> Wirtschaft -> Fortschritt -> Garage -> Inventar -> Story-Patch (Stand, Aegis, Flags).
	 */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Origin")
	bool ApplyOrigin(FName OriginId, const FString& CharacterName, int32 AppearanceIndex);

	UFUNCTION(BlueprintPure, Category = "MTA6|Origin") FName GetSelectedOrigin() const { return SelectedOrigin; }
	UFUNCTION(BlueprintPure, Category = "MTA6|Origin") FString GetCharacterName() const { return CharacterName; }
	UFUNCTION(BlueprintPure, Category = "MTA6|Origin") int32 GetAppearanceIndex() const { return Appearance; }

	/** Dialogflag der gewaehlten Herkunft (z. B. "knowsWrenchers"). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Origin")
	bool HasOriginFlag(const FString& Flag) const;

	TSharedRef<FJsonObject> ToJson() const;
	bool FromJson(const TSharedPtr<FJsonObject>& Json);

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Origin") FMTAOriginAppliedSignature OnOriginApplied;

private:
	TArray<FMTAOrigin> Origins;
	FName SelectedOrigin;
	FString CharacterName;
	int32 Appearance = 0;

	const FMTAOrigin* Find(FName Id) const;
	void ApplyTraits(const FMTAOrigin& O) const;
};
