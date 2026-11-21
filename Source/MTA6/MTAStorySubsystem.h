#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "MTAStorySubsystem.generated.h"

/** Eine Entscheidungsoption (Anzeige im Dialog). */
USTRUCT(BlueprintType)
struct FMTAChoiceOption
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Id;
	UPROPERTY(BlueprintReadOnly) FString Text;
};

/** Kurzinfo zu einer Mission aus Content/Data/story.json. */
USTRUCT(BlueprintType)
struct FMTAMission
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Id;
	UPROPERTY(BlueprintReadOnly) FString Title;
	UPROPERTY(BlueprintReadOnly) FString Text;
	UPROPERTY(BlueprintReadOnly) bool bMain = false;
	UPROPERTY(BlueprintReadOnly) bool bFinale = false;
	UPROPERTY(BlueprintReadOnly) int32 Act = 0;
	/** Kreuzungsindex im Stadtraster (0..BlocksPerSide). */
	UPROPERTY(BlueprintReadOnly) int32 CellX = 0;
	UPROPERTY(BlueprintReadOnly) int32 CellY = 0;
	UPROPERTY(BlueprintReadOnly) int32 NumChoices = 0;
};

USTRUCT(BlueprintType)
struct FMTAEnding
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Id;
	UPROPERTY(BlueprintReadOnly) FString Title;
	UPROPERTY(BlueprintReadOnly) FString Epilogue;
};

/**
 * Story-Engine (Port von godot/story.gd): datengetriebene Missionen, Fraktionsstaende,
 * Entscheidungen und Enden. Inhalte stehen in Content/Data/story.json.
 */
UCLASS()
class MTA6_API UMTAStorySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Story")
	bool LoadStory(const FString& Path = TEXT(""));

	UFUNCTION(BlueprintCallable, Category = "MTA6|Story")
	void ResetState();

	UFUNCTION(BlueprintCallable, Category = "MTA6|Story")
	TArray<FMTAMission> GetAvailableMissions() const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Story")
	FString GetChoiceText(const FString& MissionId, int32 ChoiceIndex) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Story")
	TArray<FMTAChoiceOption> GetChoiceOptions(const FString& MissionId, int32 ChoiceIndex) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Story")
	bool ChooseOption(const FString& MissionId, int32 ChoiceIndex, const FString& OptionId);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Story")
	void CompleteMission(const FString& MissionId);

	/** Wendet ein JSON-Array von Effekten an (Schema wie in story.json, z. B. Belohnungen aus Entdeckungen). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Story")
	void ApplyEffectsJson(const FString& EffectsJson);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Story")
	FMTAEnding EvaluateEnding() const;

	/** Von der Engine angesammeltes Geld/Fahndung abholen (Spiel wendet es an). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Story")
	int32 ConsumeMoney();

	UFUNCTION(BlueprintCallable, Category = "MTA6|Story")
	float ConsumeHeat();

	UFUNCTION(BlueprintPure, Category = "MTA6|Story")
	float GetStanding(const FString& Faction) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Story")
	FString StateToJson() const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Story")
	bool StateFromJson(const FString& Json);

	/** Testhilfe: spielt n zufaellige Entscheidungswege durch, gibt Bericht zurueck. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Story")
	FString RunStorySimulation(int32 Runs = 3000);

private:
	TSharedPtr<FJsonObject> Data;
	TMap<FString, float> Standing;
	TMap<FString, float> NpcTrust;
	TMap<FString, FString> Flags;
	TMap<FString, TArray<FString>> Tags;
	TArray<FString> Completed;
	float Aegis = 50.f;
	int32 OutMoney = 0;
	float OutHeat = 0.f;

	bool Check(const TSharedPtr<FJsonObject>& Cond) const;
	bool CheckAll(const TArray<TSharedPtr<FJsonValue>>* Conds) const;
	void Apply(const TSharedPtr<FJsonObject>& Effect);
	void ApplyAll(const TArray<TSharedPtr<FJsonValue>>* Effects);
	TSharedPtr<FJsonObject> FindMission(const FString& Id) const;
	static FMTAMission MissionFromJson(const TSharedPtr<FJsonObject>& M);
	static FString ValueToString(const TSharedPtr<FJsonValue>& V);
	static bool CmpNum(const FString& Op, double A, double B);
};
