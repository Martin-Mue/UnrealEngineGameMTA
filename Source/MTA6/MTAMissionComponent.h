#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MTAStorySubsystem.h"
#include "MTAMissionComponent.generated.h"

class AStaticMeshActor;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class AMTA6GameMode;

/** Phase des Missionsdialogs (Godot: dialog_stage). */
UENUM(BlueprintType)
enum class EMTADialogStage : uint8
{
	None,
	Text,    // Missionstext, weiter mit Enter
	Choice,  // Entscheidung, Tasten 1-4
	Ending   // Ende der Hauptgeschichte, schliessen mit Enter
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMTAMissionChangedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMTADialogChangedSignature);

/**
 * Missionsfluss (Port aus godot/main.gd: _new_mission, _complete_story_mission,
 * _next_choice_or_finish, _finish_story_mission, _unhandled_input).
 * Haengt am AMTA6GameMode. Story-Missionen kommen aus UMTAStorySubsystem; sind keine mehr
 * verfuegbar, folgen freie Kurierfahrten mit Zeitbonus.
 */
UCLASS(ClassGroup = (MTA6), meta = (BlueprintSpawnableComponent))
class MTA6_API UMTAMissionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMTAMissionComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Einstellungen --------------------------------------------------------------

	/** Ankunftsradius in cm (Godot: 9 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float ArrivalRadius = 900.f;

	/** Mindestentfernung eines Kurierziels (Godot: 150 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float CourierMinDistance = 15000.f;

	/** Angenommene Durchschnittsgeschwindigkeit fuer das Zeitlimit (cm/s, Godot: 14 m/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float CourierReferenceSpeed = 1400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float CourierExtraTime = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	int32 CourierBaseReward = 300;

	/** Bonus pro verbleibender Sekunde. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float CourierBonusPerSecond = 8.f;

	/** Marker-Mesh (Standard: /Engine/BasicShapes/Cylinder, 100 x 100 x 100 cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission|Marker")
	TObjectPtr<UStaticMesh> MarkerMesh;

	/** Marker-Material; ein Vektorparameter "Color" wird, falls vorhanden, auf MarkerColor gesetzt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission|Marker")
	TObjectPtr<UMaterialInterface> MarkerMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission|Marker")
	FLinearColor MarkerColor = FLinearColor(1.f, 0.82f, 0.2f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission|Marker")
	float MarkerDiameter = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission|Marker")
	float MarkerHeight = 4000.f;

	// --- Ereignisse -----------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Mission")
	FMTAMissionChangedSignature OnMissionChanged;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Mission")
	FMTADialogChangedSignature OnDialogChanged;

	// --- Steuerung ------------------------------------------------------------------

	/** Waehlt die naechste Story-Mission oder eine freie Kurierfahrt. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	void StartNextMission();

	/** Nach dem Laden: Kurierziel und Restzeit wiederherstellen (nur ohne Story-Mission). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	void RestoreCourier(const FVector& Target, float InTimeLeft);

	/** Tasten 1-4 (Index 0..3) im Entscheidungsdialog. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	void SelectOption(int32 Index);

	/** Enter: Missionstext bzw. Ende weiterschalten. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	void ConfirmDialog();

	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	void CloseDialog();

	// --- Abfragen (HUD, Speichern) ---------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	bool IsDialogOpen() const { return DialogStage != EMTADialogStage::None; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	EMTADialogStage GetDialogStage() const { return DialogStage; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	FString GetDialogTitle() const { return DialogTitle; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	FString GetDialogText() const { return DialogText; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	TArray<FMTAChoiceOption> GetDialogOptions() const { return DialogOptions; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	bool IsStoryMission() const { return bHasStoryMission; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	FMTAMission GetCurrentMission() const { return CurrentMission; }

	/** Anzeigename fuer das HUD ("Hauptstory: ..." bzw. "Kurierfahrt"). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	FString GetMissionDisplayTitle() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	FVector GetTargetLocation() const { return TargetLocation; }

	/** Horizontaler Abstand Spieler -> Ziel in cm (-1 ohne Spieler). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	float GetDistanceToTarget() const;

	/** Restzeit fuer den Kurierbonus in s (<= 0: kein Bonus mehr). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	float GetTimeLeft() const { return TimeLeft; }

private:
	void BeginStoryDialog();
	void NextChoiceOrFinish();
	void FinishStoryMission();
	void CompleteCourier();
	void ShowDialog(EMTADialogStage Stage, const FString& Title, const FString& Text, const TArray<FMTAChoiceOption>& Options);
	void ApplyStoryOutputs();
	void PlaceMarker();
	float FindGroundZ(float X, float Y) const;
	bool GetPlayerLocation(FVector& OutLocation) const;

	UMTAStorySubsystem* GetStory() const;
	AMTA6GameMode* GetMTAGameMode() const;

	UPROPERTY(Transient)
	TObjectPtr<AStaticMeshActor> MarkerActor;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MarkerMID;

	bool bHasStoryMission = false;
	bool bInitialized = false;
	FMTAMission CurrentMission;
	FVector TargetLocation = FVector::ZeroVector;
	float TimeLeft = 0.f;

	EMTADialogStage DialogStage = EMTADialogStage::None;
	FString DialogTitle;
	FString DialogText;
	TArray<FMTAChoiceOption> DialogOptions;
	FMTAMission DialogMission;
	TArray<int32> DialogQueue;
	int32 DialogChoiceIndex = INDEX_NONE;
};
