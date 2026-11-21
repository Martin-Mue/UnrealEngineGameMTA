#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MTALocalizationSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE(FMTALanguageChanged);

/**
 * Lokalisierung ohne Editor-Assets: Texte stehen in Content/Data/text.<code>.json
 * (flache Schluessel-Wert-Objekte, UTF-8). Fehlende Schluessel fallen auf Deutsch
 * (text.de.json) und zuletzt auf den Schluessel selbst zurueck. Die Sprache kann zur
 * Laufzeit umgeschaltet werden; Widgets binden Texte per Lambda (MTAMenuStyle::TrAttr)
 * und aktualisieren sich dadurch ohne Neuaufbau.
 *
 * Kurzform im Code:   MTATr("menu.main.continue")
 */
UCLASS()
class MTA6_API UMTALocalizationSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Aktive Instanz (nullptr, solange kein GameInstance existiert). */
	static UMTALocalizationSubsystem* GetActive() { return ActiveInstance; }

	/** Text zum Schluessel in der aktuellen Sprache. */
	FText Tr(const FString& Key) const;

	/** Wie Tr, aber als FString. */
	FString TrString(const FString& Key) const;

	/** Text mit Platzhaltern {0}, {1}, ... */
	FText TrFormat(const FString& Key, const TArray<FString>& Args) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Localization")
	bool HasKey(const FString& Key) const;

	/** Schaltet die Sprache um (z. B. "de", "en"), speichert sie in den Einstellungen. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Localization")
	bool SetLanguage(const FString& Code);

	UFUNCTION(BlueprintPure, Category = "MTA6|Localization")
	FString GetLanguage() const { return CurrentLanguage; }

	/** Sprachcodes, fuer die eine text.<code>.json existiert. */
	TArray<FString> GetAvailableLanguages() const;

	/** Anzahl geladener Schluessel der aktuellen Sprache. */
	int32 GetKeyCount() const { return CurrentTable.Num(); }

	/** Wird nach jedem Sprachwechsel ausgeloest. */
	FMTALanguageChanged OnLanguageChanged;

private:
	bool LoadTable(const FString& Code, TMap<FString, FText>& OutTable) const;

	TMap<FString, FText> CurrentTable;
	TMap<FString, FText> FallbackTable;
	FString CurrentLanguage;

	static UMTALocalizationSubsystem* ActiveInstance;
};

/** Kurzform: Text zum Schluessel (gibt den Schluessel zurueck, falls kein Subsystem aktiv ist). */
MTA6_API FText MTATr(const FString& Key);

/** Kurzform mit Platzhaltern {0}, {1}, ... */
MTA6_API FText MTATrF(const FString& Key, const TArray<FString>& Args);
