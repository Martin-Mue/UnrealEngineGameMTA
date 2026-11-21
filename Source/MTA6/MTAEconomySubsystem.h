#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "Math/RandomStream.h"
#include "MTAProgressionTypes.h"
#include "MTAEconomySubsystem.generated.h"

class UMTAProgressionSubsystem;

/** Ein Eintrag im Transaktionsprotokoll (Ring-Puffer, fuer UI/Telefon-App "Konto"). */
USTRUCT(BlueprintType)
struct MTA6_API FMTATransaction
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 Id = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int32 Day = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") float Hour = 0.f;
	/** Positiv = Einnahme, negativ = Ausgabe. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 Amount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 BalanceAfter = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") EMTATransactionCategory Category = EMTATransactionCategory::Other;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") FName District;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") FString Note;
};

/** Kontostand-Stichprobe fuer den Verlaufsgraphen. */
USTRUCT(BlueprintType)
struct MTA6_API FMTABalanceSample
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int32 Day = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") float Hour = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 Balance = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 Debt = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") float PriceIndex = 1.f;
};

/** Kredit bei Aurelia Holdings. Zinssatz koppelt an das Aegis-Profil ("Phase Zwei"). */
USTRUCT(BlueprintType)
struct MTA6_API FMTALoanState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 Outstanding = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 TotalBorrowed = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 InterestPaid = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 PaidToday = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int32 DaysOpen = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int32 MissedDays = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") bool bDefaulted = false;
	/** Offene Bussgelder, die nicht bezahlt werden konnten (laufen ohne Zinsen, blockieren neue Kredite). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 UnpaidFines = 0;
};

USTRUCT(BlueprintType)
struct MTA6_API FMTABribeResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") bool bAttempted = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 Cost = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") float Chance = 0.f;
	/** Veraenderung der Bezirks-Heat (negativ = gesenkt). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") float HeatDelta = 0.f;
};

/** Ergebnis von RunEconomySimulation (Balancing-Pruefung, 12.10). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAEconomySimReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int32 Hours = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 StartMoney = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 EndMoney = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 MinMoney = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 MaxMoney = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 TotalEarned = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int64 TotalSpent = 0;
	/** Brutto-Einkommen je Stunde in den Abschnitten 0-10 h, 10-50 h, 50-200 h, 200+ h. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") TArray<float> IncomePerHourBySegment;
	/** Netto-Vermoegenszuwachs je Stunde in denselben Abschnitten. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") TArray<float> NetPerHourBySegment;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") float PriceIndexStart = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") float PriceIndexEnd = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") float PriceIndexMin = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") float PriceIndexMax = 1.f;
	/** Anteil der Stunden mit Kontostand unter der Reparaturreserve. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") float BrokeShare = 0.f;
	/** Spielstunde, in der das erste Mittelklasseauto gekauft wurde (-1 = nie). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") float MidCarHour = -1.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int32 VehiclesBought = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int32 LoansTaken = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") int32 Arrests = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") bool bInflation = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") bool bDeflation = false;
	/** "OK" oder Liste der Befunde (deutsch). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") FString Verdict;
	/** Mehrzeiliger Text (CSV-artiger Verlauf alle 25 h) fuer Log/Balancing-Tabelle. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Economy") FString Log;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAMoneyChangedSignature, int64, NewBalance, int64, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTATransactionSignature, const FMTATransaction&, Transaction);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTADayPassedSignature, int32, Day);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTALoanSignature, const FMTALoanState&, Loan);
/** C++-Variante fuer andere Subsysteme (Garage: Versicherung/Miete/Verwahrung; Inventar). */
DECLARE_MULTICAST_DELEGATE_OneParam(FMTADayPassedNative, int32 /*Day*/);

/**
 * Wirtschaft von Kestrel Bay (M8, Kapitel 12.4): Kontostand, Preise je Bezirk/Knappheit/Tageszeit,
 * Bussgelder, Bestechung, Reparatur-/Tank-/Abschleppkosten, Schwarzmarktkurse, Aurelia-Kredit
 * (Zins koppelt an Aegis-Profil und Aurelia-Stand), Transaktionsprotokoll und Kontostand-Verlauf.
 * Alle Zahlen stehen in Content/Data/economy.json.
 */
UCLASS()
class MTA6_API UMTAEconomySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 SchemaVersion = 1;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	bool LoadEconomy(const FString& RelativePath = TEXT("Data/economy.json"));

	/** Setzt Konto, Kredit, Verlauf und Knappheit auf Startwerte (StartMoney < 0: Wert aus economy.json). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	void ResetToStart(int64 StartMoney = -1);

	// --- Konto ------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	int64 GetMoney() const { return Money; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	bool CanAfford(int64 Amount) const { return Amount <= Money; }

	/** Gutschrift (Amount > 0). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	void AddMoney(int64 Amount, EMTATransactionCategory Category, const FString& Note, FName District = NAME_None);

	/** Abbuchung nur bei Deckung. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	bool TrySpend(int64 Amount, EMTATransactionCategory Category, const FString& Note, FName District = NAME_None);

	/** Zwangsabbuchung (Bussgeld, Inkasso): bucht, was da ist; Rest wird zu offenen Bussgeldern. Gibt den gezahlten Betrag zurueck. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	int64 ForceCharge(int64 Amount, EMTATransactionCategory Category, const FString& Note, FName District = NAME_None);

	// --- Zeit -------------------------------------------------------------------------------

	/** Spielzeit setzen (GameMode/Tag-Nacht-System). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	void SetGameTime(int32 Day, float Hour);

	/** Spielzeit vorruecken: Knappheit, Schwarzmarkt, Zinsen, Tageswechsel (Versicherung/Miete ueber OnDayPassed). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	void AdvanceTime(float Hours);

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy") int32 GetDay() const { return Day; }
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy") float GetHour() const { return Hour; }

	// --- Preise -----------------------------------------------------------------------------

	/** Bezirksfaktor; Kind = price|reward|fine|bribe|repair|fuel|rent. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	float GetDistrictFactor(FName District, FName Kind) const;

	/** Tageszeitfaktor; Kind = price|blackMarket|repair. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	float GetTimeOfDayFactor(FName Kind) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	float GetScarcity(FName Category) const;

	/** Einmaliger Knappheitsschock (Streik, Stromausfall, Raffineriebrand). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	void ApplyScarcityShock(FName Category, float Amount);

	/** Gewichteter Preisindex ueber alle Knappheitskategorien (1 = normal). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	float GetPriceIndex() const;

	/**
	 * Kaufpreis = Basis * Bezirksfaktor * Knappheit * Tageszeit * Fraktionsrabatt * Skill (econ.buyPrice).
	 * ScarcityCategory: fuel|parts|electronics|goods|vehicles (None = keine Knappheit).
	 */
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	int64 GetPrice(int64 BasePrice, FName District, FName ScarcityCategory) const;

	/** Verkaufspreis (legal) = Kaufpreis * Wiederverkaufsquote * Skill (econ.sellPrice). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	int64 GetSellPrice(int64 BasePrice, FName District, FName ScarcityCategory, float ResaleFactor = 0.5f) const;

	/** Belohnungsformel 10.3: Basis(Klasse, Tier) * Bezirksfaktor * (1 + Zeitbonus + Zielbonus) - Schadenskosten. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	int64 GetMissionReward(FName MissionClass, int32 Tier, FName District, float TimeBonus, float GoalBonus, int64 DamageCost) const;

	/** Berechnet und bucht eine Belohnung; Fehlschlag zahlt nur den Strafanteil (negativ, nie Totalverlust). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	int64 PayMissionReward(FName MissionClass, int32 Tier, FName District, float TimeBonus, float GoalBonus, int64 DamageCost, bool bSuccess);

	// --- Kosten -----------------------------------------------------------------------------

	/** Reparatur: Basis + Schaden% * Satz + Fahrzeugwert-Anteil, mal Bezirk/Tageszeit/Teileknappheit/Skill. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	int64 GetRepairCost(float DamageFraction, int64 VehicleValue, FName District) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	int64 GetFuelCost(float Liters, FName District) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	int64 GetTowCost(float DistanceKm, FName District) const;

	/** Liest einen Wert aus "prices" (z. B. plateSwapLegal). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	float GetPriceParam(const FString& Key, float Default = 0.f) const;

	// --- Gesetz -----------------------------------------------------------------------------

	/** Bussgeld nach Typ (economy.json "fines"); Aufschlag je Aegis-Punkt; wird sofort (ggf. teilweise) eingezogen. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	int64 IssueFine(FName FineType, FName District);

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	int64 GetArrestFine(int32 Stars, FName District) const;

	/** Festnahme: Grundstrafe + je Stern + Anteil des Kontostands. Gibt den eingezogenen Betrag zurueck. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	int64 ChargeArrest(int32 Stars, FName District);

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	int64 GetBribeCost(FName District, int32 Stars) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	float GetBribeChance(FName District, int32 Stars) const;

	/** Bestechungsversuch bei der KCG. Erfolg senkt die Bezirks-Heat (UMTAHeatSubsystem), Misserfolg erhoeht sie und kostet ein Bussgeld. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	FMTABribeResult TryBribe(FName District, int32 Stars);

	// --- Schwarzmarkt -----------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	bool IsBlackMarketAvailable(FName District) const;

	/** Ankaufspreis (Spieler kauft) je Stueck; 0 = in diesem Bezirk nicht handelbar. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	int64 GetBlackMarketBuyPrice(FName Good, FName District) const;

	/** Verkaufserloes (Spieler verkauft) je Stueck; 0 = nicht handelbar. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	int64 GetBlackMarketSellPrice(FName Good, FName District) const;

	/** Aktueller Kursfaktor eines Schwarzmarktguts (1 = Basis). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	float GetBlackMarketRate(FName Good) const;

	/** Aegis-Risiko je Deal (wird vom Aufrufer auf das Aegis-Profil gebucht). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	float GetBlackMarketAegisRisk() const;

	// --- Aurelia-Kredit ---------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	int64 GetLoanLimit() const;

	/** Tageszins: Basis + (Aegis - Pivot) * Faktor + Aurelia-Stand * Faktor, geklemmt. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	float GetLoanDailyRate() const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	bool TakeLoan(int64 Amount);

	/** Tilgt bis zu Amount; gibt den getilgten Betrag zurueck. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	int64 RepayLoan(int64 Amount);

	/** Altschuld ohne Auszahlung (Herkunft, Story): erhoeht nur den offenen Kreditbetrag. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	void AddExistingDebt(int64 Amount);

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	int64 GetDebt() const { return Loan.Outstanding + Loan.UnpaidFines; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	FMTALoanState GetLoanState() const { return Loan; }

	/** Optional: Aegis-Profil von aussen setzen (sonst wird es aus der Story-Engine gelesen). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	void SetAegisOverride(bool bEnable, float Aegis);

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	float GetAegisProfile() const;

	// --- Verlauf ----------------------------------------------------------------------------

	/** Neueste zuerst. MaxCount <= 0: alle im Ring-Puffer. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	TArray<FMTATransaction> GetRecentTransactions(int32 MaxCount = 20) const;

	/** Aelteste zuerst (fuer Graph). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	TArray<FMTABalanceSample> GetBalanceHistory() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy") int64 GetTotalEarned() const { return TotalEarned; }
	UFUNCTION(BlueprintPure, Category = "MTA6|Economy") int64 GetTotalSpent() const { return TotalSpent; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Economy")
	int64 GetSpentByCategory(EMTATransactionCategory Category) const;

	// --- Persistenz -------------------------------------------------------------------------

	TSharedRef<FJsonObject> ToJson() const;
	bool FromJson(const TSharedPtr<FJsonObject>& Json);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	FString SaveStateToString() const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	bool LoadStateFromString(const FString& Json);

	// --- Test -------------------------------------------------------------------------------

	/**
	 * Simuliert Hours Spielstunden mit zufaelligen Aktionen (Gigs, Rennen, Vertraege, Schwarzmarkt,
	 * Unfaelle, Bussgelder, Festnahmen, Fahrzeug-/Tuningkaeufe, Kredit) auf einer Kopie des Zustands
	 * und prueft den Geldverlauf auf Inflation/Deflation. Der echte Spielstand bleibt unveraendert.
	 */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Economy")
	FMTAEconomySimReport RunEconomySimulation(int32 Hours = 1000, int32 Seed = 1337);

	/** Zugriff auf economy.json fuer andere Subsysteme (Gegenstaende, Garagen, Inventar). */
	const TSharedPtr<FJsonObject>& GetData() const { return Data; }

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Economy") FMTAMoneyChangedSignature OnMoneyChanged;
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Economy") FMTATransactionSignature OnTransaction;
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Economy") FMTADayPassedSignature OnDayPassed;
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Economy") FMTALoanSignature OnLoanChanged;
	FMTADayPassedNative OnDayPassedNative;

private:
	TSharedPtr<FJsonObject> Data;

	int64 Money = 0;
	int64 TotalEarned = 0;
	int64 TotalSpent = 0;
	TMap<EMTATransactionCategory, int64> SpentByCategory;
	int32 Day = 1;
	float Hour = 8.f;
	float HoursSinceSample = 0.f;

	TMap<FName, float> Scarcity;
	TMap<FName, float> BlackMarketRates;
	FMTALoanState Loan;

	bool bAegisOverride = false;
	float AegisOverride = 50.f;

	// Ring-Puffer
	TArray<FMTATransaction> TxRing;
	int32 TxHead = 0;
	int32 TxCount = 0;
	int64 NextTxId = 1;
	TArray<FMTABalanceSample> BalanceRing;
	int32 BalanceHead = 0;
	int32 BalanceCount = 0;

	mutable FRandomStream Rng;
	/** Waehrend der Simulation: keine Delegates, keine Story-/Heat-Zugriffe. */
	bool bSimulating = false;
	int32 SimLevel = 1;

	void PushTransaction(int64 Amount, EMTATransactionCategory Category, const FString& Note, FName District);
	void PushBalanceSample();
	void OnNewDay();
	void StepMarkets(float Hours);
	void ProcessLoanDay();

	TSharedPtr<FJsonObject> DistrictData(FName District) const;
	float Stat(FName StatName, float Base) const;
	float FactionDiscount(FName District) const;
	float GetStanding(const FString& Faction) const;
	int32 GetPlayerLevel() const;
	int32 TxCapacity() const;
	int32 BalanceCapacity() const;
};
