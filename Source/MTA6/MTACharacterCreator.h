#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UObject/WeakInterfacePtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SComboBox.h"
#include "MTAAppearance.h"
#include "MTACharacterCreator.generated.h"

class UMTAAppearanceSubsystem;
class UMTAWardrobeSubsystem;
class SVerticalBox;

/** Eine waehlbare Herkunft (Daten liefert der Fortschritts-Agent ueber IMTAOriginProvider). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAOriginChoice
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Origin") FName Id;
	UPROPERTY(BlueprintReadWrite, Category = "Origin") FText DisplayName;
	UPROPERTY(BlueprintReadWrite, Category = "Origin") FText Description;
};

UINTERFACE(MinimalAPI)
class UMTAOriginProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Schnittstelle zwischen Charaktererstellung und Herkunftsdaten (Implementierung: Fortschritts-Agent,
 * z. B. ein Subsystem). Der Creator kennt nur diese Methoden.
 */
class MTA6_API IMTAOriginProvider
{
	GENERATED_BODY()

public:
	/** Alle waehlbaren Herkuenfte (z. B. Rennfahrerin, Hafenkind, Konzern-Aussteigerin). */
	virtual void GetOriginChoices(TArray<FMTAOriginChoice>& OutChoices) const = 0;

	/** Aktuell gewaehlte Herkunft (NAME_None = keine). */
	virtual FName GetSelectedOrigin() const = 0;

	/** Wird aufgerufen, wenn der Spieler eine Herkunft waehlt. */
	virtual void SelectOrigin(FName OriginId) = 0;
};

DECLARE_DELEGATE_OneParam(FMTAOnAppearanceChanged, const FMTAAppearance&);
DECLARE_DELEGATE_OneParam(FMTAOnCreatorConfirmed, const FMTAAppearance&);

/**
 * Eigenstaendiges Slate-Widget (keine UMG-Assets) zur Charaktererstellung:
 * Schieberegler, Auswahllisten, Zufall, Presets, Speichern/Laden als JSON, Live-Vorschau per Delegate.
 *
 * Nutzung (z. B. im HUD/Menue): GEngine->GameViewport->AddViewportWidgetContent(SNew(SMTACharacterCreator)...);
 */
class MTA6_API SMTACharacterCreator : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMTACharacterCreator) {}
		/** Startwert (leer = Standard-Preset aus appearance.json bzw. Zufall). */
		SLATE_ARGUMENT(FMTAAppearance, InitialAppearance)
		SLATE_ARGUMENT(TWeakObjectPtr<UMTAAppearanceSubsystem>, Appearances)
		SLATE_ARGUMENT(TWeakObjectPtr<UMTAWardrobeSubsystem>, Wardrobe)
		/** Optional: Herkunftsauswahl. Ohne Provider wird der Abschnitt ausgeblendet. */
		SLATE_ARGUMENT(TWeakInterfacePtr<IMTAOriginProvider>, OriginProvider)
		/** Live-Vorschau: wird bei jeder Aenderung gerufen. */
		SLATE_EVENT(FMTAOnAppearanceChanged, OnAppearanceChanged)
		SLATE_EVENT(FMTAOnCreatorConfirmed, OnConfirmed)
		SLATE_EVENT(FSimpleDelegate, OnCancelled)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	const FMTAAppearance& GetAppearance() const { return Current; }

	/** Setzt das Aussehen von aussen (z. B. nach Laden) und aktualisiert alle Anzeigen. */
	void SetAppearance(const FMTAAppearance& NewAppearance, bool bNotify = true);

private:
	typedef TArray<TSharedPtr<FString>> FOptionList;
	typedef SComboBox<TSharedPtr<FString>> FStringCombo;

	struct FComboHandle
	{
		TSharedRef<FOptionList> Options = MakeShared<FOptionList>();
		TSharedPtr<FStringCombo> Combo;
	};

	// Aufbau
	TSharedRef<SWidget> BuildContent();
	TSharedRef<SWidget> MakeSection(const FString& Title, const TSharedRef<SWidget>& Body) const;
	TSharedRef<SWidget> MakeSlider(const FString& Label, float Min, float Max, TFunction<float()> Get, TFunction<void(float)> Set, int32 Decimals = 2);
	TSharedRef<SWidget> MakeCombo(const FString& Label, const TSharedRef<FComboHandle>& Handle, TFunction<FString()> GetId, TFunction<void(const FString&)> SetId);
	TSharedRef<SWidget> MakeSwatchRow(const FString& Label, TFunction<TArray<FLinearColor>()> GetColors, TFunction<void(const FLinearColor&)> OnPick);

	// Daten -> Optionslisten
	void RefreshDependentOptions();
	void RefreshMarksList();
	void FillOptions(FComboHandle& Handle, const TArray<TPair<FString, FString>>& IdLabel, bool bAddNone);

	// Aktionen
	void Notify();
	void ApplyRandom();
	void ApplyCreatorPreset(const FString& PresetId);
	void ApplyOutfitPreset(const FString& PresetId);
	void AddMark(const FString& MarkId);
	bool SaveCurrent();
	bool LoadCurrent();
	void SetStatus(const FString& Text);
	FString GetFilePath() const;

	FMTAAppearance Current;
	TWeakObjectPtr<UMTAAppearanceSubsystem> Appearances;
	TWeakObjectPtr<UMTAWardrobeSubsystem> Wardrobe;
	TWeakInterfacePtr<IMTAOriginProvider> Origins;
	FMTAOnAppearanceChanged OnAppearanceChanged;
	FMTAOnCreatorConfirmed OnConfirmed;
	FSimpleDelegate OnCancelled;

	TSharedRef<FComboHandle> FrameCombo = MakeShared<FComboHandle>();
	TSharedRef<FComboHandle> BodyCombo = MakeShared<FComboHandle>();
	TSharedRef<FComboHandle> HairCombo = MakeShared<FComboHandle>();
	TSharedRef<FComboHandle> BeardCombo = MakeShared<FComboHandle>();
	TSharedRef<FComboHandle> MarkCombo = MakeShared<FComboHandle>();
	TSharedRef<FComboHandle> PresetCombo = MakeShared<FComboHandle>();
	TSharedRef<FComboHandle> OutfitPresetCombo = MakeShared<FComboHandle>();
	TSharedRef<FComboHandle> OriginCombo = MakeShared<FComboHandle>();
	TMap<EMTAWardrobeSlot, TSharedRef<FComboHandle>> SlotCombos;

	TSharedPtr<SVerticalBox> MarksBox;
	FString MarkToAdd;
	FString FileName = TEXT("mira_custom");
	FString StatusText;
};
