#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "MTASaveGame.generated.h"

/**
 * Spielstand (Port von save_game/load_game aus godot/main.gd).
 * Gespeichert wird ueber UGameplayStatics in den Slot "MTA6_Slot1" (F5/F9, Autosave nach Missionen).
 */
UCLASS()
class MTA6_API UMTASaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** Aktuelles Format. Version 1 (ohne Story) wird beim Laden noch akzeptiert. */
	static constexpr int32 CurrentVersion = 2;
	static const TCHAR* SlotName() { return TEXT("MTA6_Slot1"); }
	static constexpr int32 UserIndex = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "MTA6|Save")
	int32 Version = CurrentVersion;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "MTA6|Save")
	int32 Money = 0;

	/** Fahndungsstufe 0..5. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "MTA6|Save")
	float Heat = 0.f;

	/** Tageszeit in Stunden 0..24. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "MTA6|Save")
	float Hour = 18.f;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "MTA6|Save")
	FTransform VehicleTransform = FTransform::Identity;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "MTA6|Save")
	float VehicleHealth = 100.f;

	/** Freie Kurierfahrt: Zielposition und Restzeit (Story-Missionen werden aus dem Story-Zustand neu bestimmt). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "MTA6|Save")
	FVector MissionTarget = FVector::ZeroVector;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "MTA6|Save")
	float MissionTimeLeft = 0.f;

	/** UMTAStorySubsystem::StateToJson(); leer = Story-Zustand zuruecksetzen. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "MTA6|Save")
	FString StoryJson;
};
