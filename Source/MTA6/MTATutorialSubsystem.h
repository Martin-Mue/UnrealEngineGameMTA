#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MTATutorialSubsystem.generated.h"

class SWidget;

/** Ein Hinweis aus Content/Data/tutorial.json. */
struct FMTATutorialHint
{
	FName Id;
	/** Ausloeser-Name, z. B. "game_start", "first_heat", "enter_garage". */
	FName Trigger;
	/** Basisschluessel; angezeigt wird <TextKey>.kb bzw. <TextKey>.pad (Fallback: <TextKey>). */
	FString TextKey;
	/** Bedingung ("heat>=1 && speed>20", "!in_mission"); leer = immer. */
	FString Condition;
	bool bOnce = true;
	float Delay = 0.f;
	float Duration = 8.f;
	FString Category;
	int32 Priority = 0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FMTATutorialHintShown, const FMTATutorialHint&);

/**
 * Datengetriebenes Onboarding (Konzept 12.7): Hinweise stehen in tutorial.json, werden ueber
 * FireTrigger() ausgeloest, per Bedingung gefiltert und nur einmal gezeigt (gesehene IDs liegen
 * in UMTAGameUserSettings::SeenTutorialHints). Texte kommen aus der Lokalisierung, getrennt
 * fuer Tastatur und Controller; die Eingabeart wird automatisch erkannt.
 *
 * Anbindung:
 *   if (auto* T = GetGameInstance()->GetSubsystem<UMTATutorialSubsystem>()) {
 *       T->SetContextValue("heat", 2.f);   // Kontextwerte fuer Bedingungen
 *       T->FireTrigger("first_heat");      // Ausloeser
 *   }
 * F1 oeffnet die Kontexthilfe (alle Hinweise, Umschalter Tastatur/Controller).
 */
UCLASS()
class MTA6_API UMTATutorialSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UMTATutorialSubsystem* Get(const UObject* WorldContext);

	/** Laedt tutorial.json neu; liefert die Zahl der Hinweise. */
	int32 LoadHints();

	UFUNCTION(BlueprintCallable, Category = "MTA6|Tutorial")
	void FireTrigger(FName Trigger);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Tutorial")
	void SetContextValue(FName Key, float Value);

	/** Wertet eine Bedingung gegen die Kontextwerte aus (leer = wahr). */
	bool EvaluateCondition(const FString& Condition) const;

	/** Kontext-Hilfe an-/ausschalten. */
	void ToggleHelp();
	bool IsHelpOpen() const { return HelpWidget.IsValid(); }

	/** Hinweise vergessen (Optionen: "Tutorial zuruecksetzen"). */
	void ResetSeenHints();

	const TArray<FMTATutorialHint>& GetHints() const { return Hints; }
	bool IsUsingGamepad() const { return bUsingGamepad; }
	void SetUsingGamepad(bool bGamepad) { bUsingGamepad = bGamepad; bAutoDetect = false; }

	/** Text eines Hinweises passend zur aktuellen Eingabeart. */
	FText ResolveText(const FMTATutorialHint& Hint, bool bGamepad) const;

	FMTATutorialHintShown OnHintShown;

private:
	bool Tick(float DeltaTime);
	void ShowHint(const FMTATutorialHint& Hint);
	void HideToast();
	void BuildHelp();
	void CloseHelp();
	void DetectInputDevice();

	TArray<FMTATutorialHint> Hints;
	TMap<FName, float> Context;

	struct FPending { FMTATutorialHint Hint; float TimeLeft; };
	TArray<FPending> Pending;

	TSharedPtr<SWidget> ToastWidget;
	TSharedPtr<SWidget> HelpWidget;
	float ToastTimeLeft = 0.f;
	float ToastAlpha = 0.f;
	FString ToastId;
	bool bUsingGamepad = false;
	bool bAutoDetect = true;
	bool bHelpGamepadView = false;
	FTSTicker::FDelegateHandle TickHandle;
	TSet<FName> SessionShown;
};
