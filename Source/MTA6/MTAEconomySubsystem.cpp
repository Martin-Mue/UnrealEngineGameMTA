#include "MTAEconomySubsystem.h"
#include "MTAProgressionSubsystem.h"
#include "MTAHeatSubsystem.h"
#include "Engine/GameInstance.h"

namespace MTAP = MTAProgression;
#include "Engine/World.h"

namespace
{
	int64 MTAEconRoundMoney(double V) { return (int64)FMath::FloorToDouble(V + 0.5); }

	/** Naeherungsweise normalverteilte Zufallszahl (Mittel 0, Varianz ca. 1). */
	float MTAEconGauss(const FRandomStream& R) { return (R.FRand() + R.FRand() + R.FRand() + R.FRand() - 2.f) * 1.732f; }

	const TCHAR* const MTAEconDistrictIds[] = { TEXT("core"), TEXT("saltmarket"), TEXT("lanternhill"), TEXT("greenmoor"), TEXT("rustline"), TEXT("sunstrip") };
}

void UMTAEconomySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Rng.Initialize(20261120);
	LoadEconomy();
}

bool UMTAEconomySubsystem::LoadEconomy(const FString& RelativePath)
{
	TSharedPtr<FJsonObject> Parsed;
	if (!MTAProgression::LoadJsonFile(RelativePath, Parsed)) return false;
	Data = Parsed;
	ResetToStart(-1);
	return true;
}

int32 UMTAEconomySubsystem::TxCapacity() const
{
	return FMath::Max(16, (int32)MTAProgression::Num(MTAProgression::Obj(Data, TEXT("history")), TEXT("transactionRingSize"), 256.0));
}

int32 UMTAEconomySubsystem::BalanceCapacity() const
{
	return FMath::Max(16, (int32)MTAProgression::Num(MTAProgression::Obj(Data, TEXT("history")), TEXT("balanceRingSize"), 512.0));
}

void UMTAEconomySubsystem::ResetToStart(int64 StartMoney)
{
	Money = StartMoney >= 0 ? StartMoney : (int64)MTAProgression::Num(Data, TEXT("startMoney"), 1000.0);
	TotalEarned = 0;
	TotalSpent = 0;
	SpentByCategory.Reset();
	Day = 1;
	Hour = 8.f;
	HoursSinceSample = 0.f;
	Loan = FMTALoanState();
	TxRing.SetNum(TxCapacity());
	TxHead = 0; TxCount = 0; NextTxId = 1;
	BalanceRing.SetNum(BalanceCapacity());
	BalanceHead = 0; BalanceCount = 0;

	Scarcity.Reset();
	if (const TSharedPtr<FJsonObject> S = MTAProgression::Obj(Data, TEXT("scarcity")))
	{
		for (const auto& P : S->Values) { Scarcity.Add(FName(*P.Key), 1.f); }
	}
	BlackMarketRates.Reset();
	if (const TSharedPtr<FJsonObject> G = MTAProgression::Obj(MTAProgression::Obj(Data, TEXT("blackMarket")), TEXT("goods")))
	{
		for (const auto& P : G->Values) { BlackMarketRates.Add(FName(*P.Key), 1.f); }
	}
	PushBalanceSample();
}

// ---------------------------------------------------------------------------------------------
// Konto
// ---------------------------------------------------------------------------------------------

void UMTAEconomySubsystem::PushTransaction(int64 Amount, EMTATransactionCategory Category, const FString& Note, FName District)
{
	if (Amount > 0 && Category != EMTATransactionCategory::Loan) { TotalEarned += Amount; }
	if (Amount < 0 && Category != EMTATransactionCategory::Loan)
	{
		TotalSpent += -Amount;
		SpentByCategory.FindOrAdd(Category) += -Amount;
	}
	if (bSimulating) return; // Simulation fuellt das Protokoll nicht

	if (TxRing.Num() == 0) { TxRing.SetNum(TxCapacity()); }
	FMTATransaction T;
	T.Id = NextTxId++;
	T.Day = Day;
	T.Hour = Hour;
	T.Amount = Amount;
	T.BalanceAfter = Money;
	T.Category = Category;
	T.District = District;
	T.Note = Note;
	TxRing[TxHead] = T;
	TxHead = (TxHead + 1) % TxRing.Num();
	TxCount = FMath::Min(TxCount + 1, TxRing.Num());
	OnTransaction.Broadcast(T);
	OnMoneyChanged.Broadcast(Money, Amount);
}

void UMTAEconomySubsystem::PushBalanceSample()
{
	if (bSimulating) return;
	if (BalanceRing.Num() == 0) { BalanceRing.SetNum(BalanceCapacity()); }
	FMTABalanceSample S;
	S.Day = Day;
	S.Hour = Hour;
	S.Balance = Money;
	S.Debt = GetDebt();
	S.PriceIndex = GetPriceIndex();
	BalanceRing[BalanceHead] = S;
	BalanceHead = (BalanceHead + 1) % BalanceRing.Num();
	BalanceCount = FMath::Min(BalanceCount + 1, BalanceRing.Num());
}

void UMTAEconomySubsystem::AddMoney(int64 Amount, EMTATransactionCategory Category, const FString& Note, FName District)
{
	if (Amount <= 0) return;
	Money += Amount;
	PushTransaction(Amount, Category, Note, District);

	// Lohnpfaendung: offene Bussgelder werden zur Haelfte aus Einnahmen beglichen.
	if (Loan.UnpaidFines > 0 && Category != EMTATransactionCategory::Loan)
	{
		const int64 Garnish = FMath::Min(Loan.UnpaidFines, FMath::Max<int64>(1, Amount / 2));
		Loan.UnpaidFines -= Garnish;
		Money -= Garnish;
		PushTransaction(-Garnish, EMTATransactionCategory::Fine, TEXT("Pfaendung offener Bussgelder"), District);
	}
}

bool UMTAEconomySubsystem::TrySpend(int64 Amount, EMTATransactionCategory Category, const FString& Note, FName District)
{
	if (Amount < 0 || Amount > Money) return false;
	if (Amount == 0) return true;
	Money -= Amount;
	PushTransaction(-Amount, Category, Note, District);
	return true;
}

int64 UMTAEconomySubsystem::ForceCharge(int64 Amount, EMTATransactionCategory Category, const FString& Note, FName District)
{
	if (Amount <= 0) return 0;
	const int64 Paid = FMath::Min(Amount, FMath::Max<int64>(0, Money));
	if (Paid > 0)
	{
		Money -= Paid;
		PushTransaction(-Paid, Category, Note, District);
	}
	const int64 Rest = Amount - Paid;
	if (Rest > 0 && MTAProgression::Bool(MTAProgression::Obj(Data, TEXT("fines")), TEXT("unpaidToDebt"), true))
	{
		Loan.UnpaidFines += Rest;
		if (!bSimulating) { OnLoanChanged.Broadcast(Loan); }
	}
	return Paid;
}

// ---------------------------------------------------------------------------------------------
// Zeit und Maerkte
// ---------------------------------------------------------------------------------------------

void UMTAEconomySubsystem::SetGameTime(int32 InDay, float InHour)
{
	Day = FMath::Max(1, InDay);
	Hour = FMath::Fmod(FMath::Max(0.f, InHour), 24.f);
}

void UMTAEconomySubsystem::AdvanceTime(float Hours)
{
	float Left = FMath::Max(0.f, Hours);
	const float SampleEvery = (float)MTAProgression::Num(MTAProgression::Obj(Data, TEXT("history")), TEXT("balanceSampleHours"), 6.0);
	while (Left > UE_KINDA_SMALL_NUMBER)
	{
		const float Step = FMath::Min(1.f, Left);
		Left -= Step;
		StepMarkets(Step);
		Hour += Step;
		HoursSinceSample += Step;
		if (Hour >= 24.f)
		{
			Hour -= 24.f;
			++Day;
			OnNewDay();
		}
		if (HoursSinceSample >= SampleEvery)
		{
			HoursSinceSample = 0.f;
			PushBalanceSample();
		}
	}
}

void UMTAEconomySubsystem::StepMarkets(float Hours)
{
	const float DayFrac = Hours / 24.f;
	const TSharedPtr<FJsonObject> S = MTAProgression::Obj(Data, TEXT("scarcity"));
	for (auto& P : Scarcity)
	{
		const TSharedPtr<FJsonObject> C = MTAProgression::Obj(S, P.Key.ToString());
		const float Vol = (float)MTAProgression::Num(C, TEXT("volatility"), 0.04);
		const float Rev = (float)MTAProgression::Num(C, TEXT("meanReversion"), 0.1);
		P.Value += (1.f - P.Value) * Rev * Hours / 4.f + MTAEconGauss(Rng) * Vol * FMath::Sqrt(DayFrac);
		P.Value = FMath::Clamp(P.Value, (float)MTAProgression::Num(C, TEXT("min"), 0.7), (float)MTAProgression::Num(C, TEXT("max"), 1.6));
	}
	const TSharedPtr<FJsonObject> Goods = MTAProgression::Obj(MTAProgression::Obj(Data, TEXT("blackMarket")), TEXT("goods"));
	for (auto& P : BlackMarketRates)
	{
		const TSharedPtr<FJsonObject> G = MTAProgression::Obj(Goods, P.Key.ToString());
		const float Vol = (float)MTAProgression::Num(G, TEXT("volatility"), 0.06);
		// Schwarzmarkt folgt der legalen Knappheit seiner Kategorie (Streik -> Schmuggel lohnt).
		const float Target = GetScarcity(FName(*MTAProgression::Str(G, TEXT("category"), TEXT("goods"))));
		P.Value += (Target - P.Value) * 0.1f * Hours / 4.f + MTAEconGauss(Rng) * Vol * FMath::Sqrt(DayFrac);
		P.Value = FMath::Clamp(P.Value, (float)MTAProgression::Num(G, TEXT("min"), 0.5), (float)MTAProgression::Num(G, TEXT("max"), 2.0));
	}
}

void UMTAEconomySubsystem::OnNewDay()
{
	if (const TArray<TSharedPtr<FJsonValue>>* Events = MTAProgression::Arr(Data, TEXT("scarcityEvents")))
	{
		for (const TSharedPtr<FJsonValue>& V : *Events)
		{
			const TSharedPtr<FJsonObject> E = V->AsObject();
			if (Rng.FRand() < (float)MTAProgression::Num(E, TEXT("chancePerDay"), 0.0))
			{
				ApplyScarcityShock(FName(*MTAProgression::Str(E, TEXT("category"))), (float)MTAProgression::Num(E, TEXT("shock"), 0.2));
			}
		}
	}
	ProcessLoanDay();
	if (!bSimulating)
	{
		OnDayPassed.Broadcast(Day);
		OnDayPassedNative.Broadcast(Day);
	}
}

void UMTAEconomySubsystem::ProcessLoanDay()
{
	if (Loan.Outstanding <= 0) { Loan.PaidToday = 0; return; }
	const TSharedPtr<FJsonObject> L = MTAProgression::Obj(Data, TEXT("loan"));
	const int64 Interest = FMath::Max<int64>(1, MTAEconRoundMoney((double)Loan.Outstanding * GetLoanDailyRate()));
	Loan.Outstanding += Interest;
	Loan.InterestPaid += Interest;
	++Loan.DaysOpen;

	const int32 Grace = (int32)MTAProgression::Num(L, TEXT("graceDays"), 3.0);
	const int64 MinPay = MTAEconRoundMoney((double)Loan.Outstanding * MTAProgression::Num(L, TEXT("minDailyPaymentPct"), 0.05));
	if (Loan.DaysOpen > Grace && Loan.PaidToday < MinPay)
	{
		// Aurelia zieht die Mindestrate automatisch ein, sofern Deckung besteht.
		const int64 Due = MinPay - Loan.PaidToday;
		if (Money >= Due)
		{
			Money -= Due;
			Loan.Outstanding -= Due;
			PushTransaction(-Due, EMTATransactionCategory::Loan, TEXT("Aurelia: automatischer Einzug"), NAME_None);
			Loan.MissedDays = 0;
		}
		else
		{
			++Loan.MissedDays;
		}
	}
	else
	{
		Loan.MissedDays = 0;
	}

	if (!Loan.bDefaulted && Loan.MissedDays >= (int32)MTAProgression::Num(L, TEXT("defaultAfterMissedDays"), 5.0))
	{
		Loan.bDefaulted = true;
		const int64 Collect = MTAEconRoundMoney((double)Loan.Outstanding * MTAProgression::Num(L, TEXT("collectionPct"), 0.3));
		const int64 Paid = FMath::Min(Collect, FMath::Max<int64>(0, Money));
		if (Paid > 0)
		{
			Money -= Paid;
			Loan.Outstanding -= Paid;
			PushTransaction(-Paid, EMTATransactionCategory::Loan, TEXT("Aurelia-Inkasso"), NAME_None);
		}
		const float AegisHit = (float)MTAProgression::Num(L, TEXT("defaultAegis"), 8.0);
		const float StandingHit = (float)MTAProgression::Num(L, TEXT("defaultStanding"), -10.0);
		if (bSimulating)
		{
			AegisOverride = FMath::Clamp(AegisOverride + AegisHit, 0.f, 100.f);
		}
		else
		{
			// Zahlungsausfall landet im Aegis-Score ("Phase Zwei": Kredit an Verhalten gekoppelt).
			MTAProgression::PatchStoryState(GetGameInstance(), [AegisHit, StandingHit](const TSharedPtr<FJsonObject>& State)
			{
				State->SetNumberField(TEXT("aegis"), FMath::Clamp(MTAProgression::Num(State, TEXT("aegis"), 50.0) + AegisHit, 0.0, 100.0));
				if (const TSharedPtr<FJsonObject> St = MTAProgression::Obj(State, TEXT("standing")))
				{
					St->SetNumberField(TEXT("aurelia"), FMath::Clamp(MTAProgression::Num(St, TEXT("aurelia"), 0.0) + StandingHit, -100.0, 100.0));
				}
			});
		}
	}
	Loan.PaidToday = 0;
	if (!bSimulating) { OnLoanChanged.Broadcast(Loan); }
}

// ---------------------------------------------------------------------------------------------
// Preise
// ---------------------------------------------------------------------------------------------

TSharedPtr<FJsonObject> UMTAEconomySubsystem::DistrictData(FName District) const
{
	return MTAProgression::Obj(MTAProgression::Obj(Data, TEXT("districts")), District.ToString());
}

float UMTAEconomySubsystem::GetDistrictFactor(FName District, FName Kind) const
{
	const TSharedPtr<FJsonObject> D = DistrictData(District);
	if (!D.IsValid()) return 1.f;
	return (float)MTAProgression::Num(D, Kind.ToString() + TEXT("Factor"), 1.0);
}

float UMTAEconomySubsystem::GetTimeOfDayFactor(FName Kind) const
{
	if (const TArray<TSharedPtr<FJsonValue>>* Slots = MTAProgression::Arr(Data, TEXT("timeOfDay")))
	{
		for (const TSharedPtr<FJsonValue>& V : *Slots)
		{
			const TSharedPtr<FJsonObject> S = V->AsObject();
			if (Hour >= MTAProgression::Num(S, TEXT("from"), 0.0) && Hour < MTAProgression::Num(S, TEXT("to"), 24.0))
			{
				return (float)MTAProgression::Num(S, Kind.ToString(), 1.0);
			}
		}
	}
	return 1.f;
}

float UMTAEconomySubsystem::GetScarcity(FName Category) const
{
	const float* V = Scarcity.Find(Category);
	return V ? *V : 1.f;
}

void UMTAEconomySubsystem::ApplyScarcityShock(FName Category, float Amount)
{
	if (float* V = Scarcity.Find(Category))
	{
		const TSharedPtr<FJsonObject> C = MTAProgression::Obj(MTAProgression::Obj(Data, TEXT("scarcity")), Category.ToString());
		*V = FMath::Clamp(*V + Amount, (float)MTAProgression::Num(C, TEXT("min"), 0.7), (float)MTAProgression::Num(C, TEXT("max"), 1.6));
	}
}

float UMTAEconomySubsystem::GetPriceIndex() const
{
	if (Scarcity.Num() == 0) return 1.f;
	float Sum = 0.f;
	for (const auto& P : Scarcity) { Sum += P.Value; }
	return Sum / Scarcity.Num();
}

float UMTAEconomySubsystem::Stat(FName StatName, float Base) const
{
	if (bSimulating) return Base; // Simulation rechnet mit neutralem Charakter
	const UGameInstance* GI = GetGameInstance();
	const UMTAProgressionSubsystem* P = GI ? GI->GetSubsystem<UMTAProgressionSubsystem>() : nullptr;
	return P ? P->ApplyStat(StatName, Base) : Base;
}

float UMTAEconomySubsystem::GetStanding(const FString& Faction) const
{
	if (bSimulating || Faction.IsEmpty()) return 0.f;
	return MTAProgression::GetStoryStanding(GetGameInstance(), Faction);
}

int32 UMTAEconomySubsystem::GetPlayerLevel() const
{
	if (bSimulating) return SimLevel;
	const UGameInstance* GI = GetGameInstance();
	const UMTAProgressionSubsystem* P = GI ? GI->GetSubsystem<UMTAProgressionSubsystem>() : nullptr;
	return P ? P->GetLevel() : 1;
}

float UMTAEconomySubsystem::FactionDiscount(FName District) const
{
	const FString Faction = MTAProgression::Str(DistrictData(District), TEXT("faction"));
	const float S = FMath::Clamp(GetStanding(Faction), -100.f, 100.f);
	return 1.f - S * 0.0015f; // +100 -> 15 % Rabatt, -100 -> 15 % Aufschlag
}

int64 UMTAEconomySubsystem::GetPrice(int64 BasePrice, FName District, FName ScarcityCategory) const
{
	double P = (double)BasePrice * GetDistrictFactor(District, TEXT("price")) * GetTimeOfDayFactor(TEXT("price")) * FactionDiscount(District);
	if (!ScarcityCategory.IsNone()) { P *= GetScarcity(ScarcityCategory); }
	return FMath::Max<int64>(0, MTAEconRoundMoney(Stat(TEXT("econ.buyPrice"), (float)P)));
}

int64 UMTAEconomySubsystem::GetSellPrice(int64 BasePrice, FName District, FName ScarcityCategory, float ResaleFactor) const
{
	const double P = (double)GetPrice(BasePrice, District, ScarcityCategory) * ResaleFactor;
	return FMath::Max<int64>(0, MTAEconRoundMoney(Stat(TEXT("econ.sellPrice"), (float)P)));
}

int64 UMTAEconomySubsystem::GetMissionReward(FName MissionClass, int32 Tier, FName District, float TimeBonus, float GoalBonus, int64 DamageCost) const
{
	const TSharedPtr<FJsonObject> Income = MTAProgression::Obj(Data, TEXT("income"));
	double Base = 0.0;
	if (MissionClass == TEXT("gig"))
	{
		if (const TArray<TSharedPtr<FJsonValue>>* T = MTAProgression::Arr(Income, TEXT("gigTiers")))
		{
			if (T->Num() > 0) { Base = (*T)[FMath::Clamp(Tier - 1, 0, T->Num() - 1)]->AsNumber(); }
		}
	}
	else if (const TArray<TSharedPtr<FJsonValue>>* R = MTAProgression::Arr(Income, MissionClass.ToString()))
	{
		if (R->Num() >= 2) { Base = FMath::Lerp((*R)[0]->AsNumber(), (*R)[1]->AsNumber(), FMath::Clamp((Tier - 1) / 2.0, 0.0, 1.0)); }
	}
	const double Gross = Base * GetDistrictFactor(District, TEXT("reward")) * (1.0 + TimeBonus + GoalBonus);
	return FMath::Max<int64>(0, MTAEconRoundMoney(Stat(TEXT("econ.rewardMoney"), (float)Gross)) - DamageCost);
}

int64 UMTAEconomySubsystem::PayMissionReward(FName MissionClass, int32 Tier, FName District, float TimeBonus, float GoalBonus, int64 DamageCost, bool bSuccess)
{
	if (bSuccess)
	{
		const int64 R = GetMissionReward(MissionClass, Tier, District, TimeBonus, GoalBonus, DamageCost);
		AddMoney(R, MissionClass == TEXT("gig") ? EMTATransactionCategory::Gig : EMTATransactionCategory::Mission, MissionClass.ToString(), District);
		return R;
	}
	const double Pct = MTAProgression::Num(MTAProgression::Obj(Data, TEXT("income")), TEXT("failPenaltyPct"), 0.25);
	const int64 Penalty = MTAEconRoundMoney(GetMissionReward(MissionClass, Tier, District, 0.f, 0.f, 0) * Pct);
	return -ForceCharge(Penalty, EMTATransactionCategory::Mission, MissionClass.ToString() + TEXT(" (Fehlschlag)"), District);
}

float UMTAEconomySubsystem::GetPriceParam(const FString& Key, float Default) const
{
	return (float)MTAProgression::Num(MTAProgression::Obj(Data, TEXT("prices")), Key, Default);
}

int64 UMTAEconomySubsystem::GetRepairCost(float DamageFraction, int64 VehicleValue, FName District) const
{
	const float D = FMath::Clamp(DamageFraction, 0.f, 1.f);
	if (D <= 0.f) return 0;
	double C = GetPriceParam(TEXT("repairBase"), 50.f) + D * 100.0 * GetPriceParam(TEXT("repairPerDamagePct"), 3.2f)
		+ (double)VehicleValue * GetPriceParam(TEXT("repairValuePct"), 0.006f) * D;
	C *= GetDistrictFactor(District, TEXT("repair")) * GetTimeOfDayFactor(TEXT("repair")) * GetScarcity(TEXT("parts"));
	return FMath::Max<int64>(1, MTAEconRoundMoney(Stat(TEXT("econ.repairCost"), (float)C)));
}

int64 UMTAEconomySubsystem::GetFuelCost(float Liters, FName District) const
{
	const double C = FMath::Max(0.f, Liters) * GetPriceParam(TEXT("fuelPerLiter"), 2.4f) * GetDistrictFactor(District, TEXT("fuel")) * GetScarcity(TEXT("fuel"));
	return MTAEconRoundMoney(Stat(TEXT("econ.fuelCost"), (float)C));
}

int64 UMTAEconomySubsystem::GetTowCost(float DistanceKm, FName District) const
{
	const double C = (GetPriceParam(TEXT("towBase"), 180.f) + FMath::Max(0.f, DistanceKm) * GetPriceParam(TEXT("towPerKm"), 14.f)) * GetDistrictFactor(District, TEXT("price"));
	return MTAEconRoundMoney(Stat(TEXT("econ.towCost"), (float)C));
}

// ---------------------------------------------------------------------------------------------
// Gesetz
// ---------------------------------------------------------------------------------------------

int64 UMTAEconomySubsystem::IssueFine(FName FineType, FName District)
{
	const TSharedPtr<FJsonObject> F = MTAProgression::Obj(Data, TEXT("fines"));
	const double Base = MTAProgression::Num(F, FineType.ToString(), 100.0);
	const double Surcharge = 1.0 + GetAegisProfile() * MTAProgression::Num(F, TEXT("aegisSurchargePerPoint"), 0.008);
	const int64 Amount = MTAEconRoundMoney(Stat(TEXT("econ.fineAmount"), (float)(Base * GetDistrictFactor(District, TEXT("fine")) * Surcharge)));
	ForceCharge(Amount, EMTATransactionCategory::Fine, FString::Printf(TEXT("Bussgeld: %s"), *FineType.ToString()), District);
	return Amount;
}

int64 UMTAEconomySubsystem::GetArrestFine(int32 Stars, FName District) const
{
	const TSharedPtr<FJsonObject> F = MTAProgression::Obj(Data, TEXT("fines"));
	const double C = MTAProgression::Num(F, TEXT("arrestBase"), 300.0) + MTAProgression::Num(F, TEXT("arrestPerStar"), 250.0) * FMath::Max(0, Stars)
		+ (double)FMath::Max<int64>(0, Money) * MTAProgression::Num(F, TEXT("arrestMoneyPct"), 0.06);
	return MTAEconRoundMoney(Stat(TEXT("econ.fineAmount"), (float)(C * GetDistrictFactor(District, TEXT("fine")))));
}

int64 UMTAEconomySubsystem::ChargeArrest(int32 Stars, FName District)
{
	return ForceCharge(GetArrestFine(Stars, District), EMTATransactionCategory::Fine, FString::Printf(TEXT("Festnahme (%d Sterne)"), Stars), District);
}

int64 UMTAEconomySubsystem::GetBribeCost(FName District, int32 Stars) const
{
	const TSharedPtr<FJsonObject> B = MTAProgression::Obj(Data, TEXT("bribe"));
	const double C = MTAProgression::Num(B, TEXT("basePerStar"), 320.0) * FMath::Pow((double)FMath::Max(1, Stars), MTAProgression::Num(B, TEXT("exponent"), 1.55))
		* GetDistrictFactor(District, TEXT("bribe")) * (1.0 + GetAegisProfile() / 200.0);
	return MTAEconRoundMoney(Stat(TEXT("econ.bribeCost"), (float)C));
}

float UMTAEconomySubsystem::GetBribeChance(FName District, int32 Stars) const
{
	const TSharedPtr<FJsonObject> B = MTAProgression::Obj(Data, TEXT("bribe"));
	if (Stars <= 0 || Stars > (int32)MTAProgression::Num(B, TEXT("maxStars"), 4.0)) return 0.f;
	float C = (float)(MTAProgression::Num(B, TEXT("successBase"), 0.9) + MTAProgression::Num(B, TEXT("successPerStar"), -0.13) * (Stars - 1)
		+ GetStanding(TEXT("kcg")) * MTAProgression::Num(B, TEXT("kcgStandingPerPoint"), 0.003)
		- (GetAegisProfile() - 50.f) * MTAProgression::Num(B, TEXT("aegisPenaltyPerPoint"), 0.002));
	C = Stat(TEXT("econ.bribeSuccess"), C);
	return FMath::Clamp(C, (float)MTAProgression::Num(B, TEXT("successMin"), 0.1), (float)MTAProgression::Num(B, TEXT("successMax"), 0.97));
}

FMTABribeResult UMTAEconomySubsystem::TryBribe(FName District, int32 Stars)
{
	FMTABribeResult R;
	R.Cost = GetBribeCost(District, Stars);
	R.Chance = GetBribeChance(District, Stars);
	if (R.Chance <= 0.f || !TrySpend(R.Cost, EMTATransactionCategory::Bribe, TEXT("Bestechung KCG"), District)) return R;
	R.bAttempted = true;
	R.bSuccess = Rng.FRand() < R.Chance;
	R.HeatDelta = R.bSuccess ? -(float)Stars : (float)MTAProgression::Num(MTAProgression::Obj(Data, TEXT("bribe")), TEXT("failHeat"), 1.0);
	if (!bSimulating)
	{
		const UGameInstance* GI = GetGameInstance();
		UWorld* World = GI ? GI->GetWorld() : nullptr;
		if (UMTAHeatSubsystem* Heat = World ? World->GetSubsystem<UMTAHeatSubsystem>() : nullptr)
		{
			Heat->AddHeat(District, R.HeatDelta);
		}
	}
	return R;
}

// ---------------------------------------------------------------------------------------------
// Schwarzmarkt
// ---------------------------------------------------------------------------------------------

bool UMTAEconomySubsystem::IsBlackMarketAvailable(FName District) const
{
	return MTAProgression::Bool(DistrictData(District), TEXT("blackMarket"), false);
}

float UMTAEconomySubsystem::GetBlackMarketRate(FName Good) const
{
	const float* V = BlackMarketRates.Find(Good);
	return V ? *V : 0.f;
}

int64 UMTAEconomySubsystem::GetBlackMarketBuyPrice(FName Good, FName District) const
{
	if (!IsBlackMarketAvailable(District)) return 0;
	const TSharedPtr<FJsonObject> G = MTAProgression::Obj(MTAProgression::Obj(MTAProgression::Obj(Data, TEXT("blackMarket")), TEXT("goods")), Good.ToString());
	if (!G.IsValid()) return 0;
	const double P = MTAProgression::Num(G, TEXT("base"), 0.0) * GetBlackMarketRate(Good) * GetTimeOfDayFactor(TEXT("blackMarket")) * GetDistrictFactor(District, TEXT("price"));
	return MTAEconRoundMoney(Stat(TEXT("econ.blackMarketBuy"), (float)P));
}

int64 UMTAEconomySubsystem::GetBlackMarketSellPrice(FName Good, FName District) const
{
	const int64 Buy = GetBlackMarketBuyPrice(Good, District);
	if (Buy <= 0) return 0;
	const double Spread = MTAProgression::Num(MTAProgression::Obj(Data, TEXT("blackMarket")), TEXT("sellSpread"), 0.78);
	return MTAEconRoundMoney(Stat(TEXT("econ.blackMarketSell"), (float)(Buy * Spread)));
}

float UMTAEconomySubsystem::GetBlackMarketAegisRisk() const
{
	return Stat(TEXT("aegis.gain"), (float)MTAProgression::Num(MTAProgression::Obj(Data, TEXT("blackMarket")), TEXT("aegisRiskPerDeal"), 0.8));
}

// ---------------------------------------------------------------------------------------------
// Kredit
// ---------------------------------------------------------------------------------------------

void UMTAEconomySubsystem::SetAegisOverride(bool bEnable, float Aegis)
{
	bAegisOverride = bEnable;
	AegisOverride = FMath::Clamp(Aegis, 0.f, 100.f);
}

float UMTAEconomySubsystem::GetAegisProfile() const
{
	if (bAegisOverride || bSimulating) return AegisOverride;
	return MTAProgression::GetStoryAegis(GetGameInstance());
}

int64 UMTAEconomySubsystem::GetLoanLimit() const
{
	if (Loan.bDefaulted || Loan.UnpaidFines > 0) return 0;
	const TSharedPtr<FJsonObject> L = MTAProgression::Obj(Data, TEXT("loan"));
	double Limit = MTAProgression::Num(L, TEXT("maxBase"), 5000.0) + MTAProgression::Num(L, TEXT("maxPerLevel"), 1500.0) * (GetPlayerLevel() - 1)
		+ GetStanding(TEXT("aurelia")) * MTAProgression::Num(L, TEXT("maxStandingFactor"), 60.0);
	Limit *= FMath::Lerp(1.2, 0.6, FMath::Clamp(GetAegisProfile() / 100.0, 0.0, 1.0)); // hohes Profil = weniger Kredit
	return FMath::Max<int64>(0, MTAEconRoundMoney(Limit) - Loan.Outstanding);
}

float UMTAEconomySubsystem::GetLoanDailyRate() const
{
	const TSharedPtr<FJsonObject> L = MTAProgression::Obj(Data, TEXT("loan"));
	float R = (float)(MTAProgression::Num(L, TEXT("baseDailyRate"), 0.01)
		+ (GetAegisProfile() - MTAProgression::Num(L, TEXT("aegisPivot"), 50.0)) * MTAProgression::Num(L, TEXT("aegisRatePerPoint"), 0.00025)
		+ GetStanding(TEXT("aurelia")) * MTAProgression::Num(L, TEXT("standingRatePerPoint"), -0.00006));
	R = Stat(TEXT("econ.loanRate"), R);
	return FMath::Clamp(R, (float)MTAProgression::Num(L, TEXT("minDailyRate"), 0.004), (float)MTAProgression::Num(L, TEXT("maxDailyRate"), 0.045));
}

bool UMTAEconomySubsystem::TakeLoan(int64 Amount)
{
	if (Amount <= 0 || Amount > GetLoanLimit()) return false;
	if (Loan.Outstanding <= 0) { Loan.DaysOpen = 0; Loan.MissedDays = 0; }
	Loan.Outstanding += Amount;
	Loan.TotalBorrowed += Amount;
	AddMoney(Amount, EMTATransactionCategory::Loan, TEXT("Aurelia-Kredit"), NAME_None);
	if (!bSimulating) { OnLoanChanged.Broadcast(Loan); }
	return true;
}

void UMTAEconomySubsystem::AddExistingDebt(int64 Amount)
{
	if (Amount <= 0) return;
	if (Loan.Outstanding <= 0) { Loan.DaysOpen = 0; Loan.MissedDays = 0; }
	Loan.Outstanding += Amount;
	Loan.TotalBorrowed += Amount;
	if (!bSimulating) { OnLoanChanged.Broadcast(Loan); }
}

int64 UMTAEconomySubsystem::RepayLoan(int64 Amount)
{
	const int64 Pay = FMath::Min3(Amount, Loan.Outstanding, Money);
	if (Pay <= 0) return 0;
	Money -= Pay;
	Loan.Outstanding -= Pay;
	Loan.PaidToday += Pay;
	PushTransaction(-Pay, EMTATransactionCategory::Loan, TEXT("Kredittilgung"), NAME_None);
	if (Loan.Outstanding <= 0)
	{
		Loan.Outstanding = 0;
		Loan.bDefaulted = false;
		Loan.MissedDays = 0;
		Loan.DaysOpen = 0;
	}
	if (!bSimulating) { OnLoanChanged.Broadcast(Loan); }
	return Pay;
}

// ---------------------------------------------------------------------------------------------
// Verlauf
// ---------------------------------------------------------------------------------------------

TArray<FMTATransaction> UMTAEconomySubsystem::GetRecentTransactions(int32 MaxCount) const
{
	TArray<FMTATransaction> Result;
	const int32 N = MaxCount <= 0 ? TxCount : FMath::Min(MaxCount, TxCount);
	for (int32 I = 0; I < N; ++I)
	{
		const int32 Idx = (TxHead - 1 - I + TxRing.Num()) % TxRing.Num();
		Result.Add(TxRing[Idx]);
	}
	return Result;
}

TArray<FMTABalanceSample> UMTAEconomySubsystem::GetBalanceHistory() const
{
	TArray<FMTABalanceSample> Result;
	for (int32 I = BalanceCount - 1; I >= 0; --I)
	{
		const int32 Idx = (BalanceHead - 1 - I + BalanceRing.Num()) % BalanceRing.Num();
		Result.Add(BalanceRing[Idx]);
	}
	return Result;
}

int64 UMTAEconomySubsystem::GetSpentByCategory(EMTATransactionCategory Category) const
{
	const int64* V = SpentByCategory.Find(Category);
	return V ? *V : 0;
}

// ---------------------------------------------------------------------------------------------
// Persistenz
// ---------------------------------------------------------------------------------------------

TSharedRef<FJsonObject> UMTAEconomySubsystem::ToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema"), SchemaVersion);
	Root->SetNumberField(TEXT("money"), (double)Money);
	Root->SetNumberField(TEXT("earned"), (double)TotalEarned);
	Root->SetNumberField(TEXT("spent"), (double)TotalSpent);
	Root->SetNumberField(TEXT("day"), Day);
	Root->SetNumberField(TEXT("hour"), Hour);

	TSharedRef<FJsonObject> Cat = MakeShared<FJsonObject>();
	for (const auto& P : SpentByCategory) { Cat->SetNumberField(FString::FromInt((int32)P.Key), (double)P.Value); }
	Root->SetObjectField(TEXT("spentByCategory"), Cat);

	TMap<FName, float> S = Scarcity;
	Root->SetObjectField(TEXT("scarcity"), MTAProgression::WriteNumberMap(S));
	TMap<FName, float> B = BlackMarketRates;
	Root->SetObjectField(TEXT("blackMarket"), MTAProgression::WriteNumberMap(B));

	TSharedRef<FJsonObject> L = MakeShared<FJsonObject>();
	L->SetNumberField(TEXT("outstanding"), (double)Loan.Outstanding);
	L->SetNumberField(TEXT("borrowed"), (double)Loan.TotalBorrowed);
	L->SetNumberField(TEXT("interest"), (double)Loan.InterestPaid);
	L->SetNumberField(TEXT("paidToday"), (double)Loan.PaidToday);
	L->SetNumberField(TEXT("daysOpen"), Loan.DaysOpen);
	L->SetNumberField(TEXT("missed"), Loan.MissedDays);
	L->SetBoolField(TEXT("defaulted"), Loan.bDefaulted);
	L->SetNumberField(TEXT("unpaidFines"), (double)Loan.UnpaidFines);
	Root->SetObjectField(TEXT("loan"), L);

	// Protokoll und Verlauf (aelteste zuerst), damit die Konto-App nach dem Laden gefuellt ist.
	TArray<TSharedPtr<FJsonValue>> Tx;
	const TArray<FMTATransaction> Recent = GetRecentTransactions(0);
	for (int32 I = Recent.Num() - 1; I >= 0; --I)
	{
		const FMTATransaction& T = Recent[I];
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("id"), (double)T.Id);
		O->SetNumberField(TEXT("d"), T.Day);
		O->SetNumberField(TEXT("h"), T.Hour);
		O->SetNumberField(TEXT("a"), (double)T.Amount);
		O->SetNumberField(TEXT("b"), (double)T.BalanceAfter);
		O->SetNumberField(TEXT("c"), (int32)T.Category);
		O->SetStringField(TEXT("dist"), T.District.ToString());
		O->SetStringField(TEXT("n"), T.Note);
		Tx.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("transactions"), Tx);
	TArray<TSharedPtr<FJsonValue>> Hist;
	for (const FMTABalanceSample& S2 : GetBalanceHistory())
	{
		TArray<TSharedPtr<FJsonValue>> Row;
		Row.Add(MakeShared<FJsonValueNumber>(S2.Day));
		Row.Add(MakeShared<FJsonValueNumber>(S2.Hour));
		Row.Add(MakeShared<FJsonValueNumber>((double)S2.Balance));
		Row.Add(MakeShared<FJsonValueNumber>((double)S2.Debt));
		Row.Add(MakeShared<FJsonValueNumber>(S2.PriceIndex));
		Hist.Add(MakeShared<FJsonValueArray>(Row));
	}
	Root->SetArrayField(TEXT("balanceHistory"), Hist);
	Root->SetBoolField(TEXT("aegisOverride"), bAegisOverride);
	Root->SetNumberField(TEXT("aegisOverrideValue"), AegisOverride);
	return Root;
}

bool UMTAEconomySubsystem::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid()) return false;
	const int32 Schema = (int32)MTAP::Num(Json, TEXT("schema"), 0.0);
	if (Schema < 1 || Schema > SchemaVersion)
	{
		UE_LOG(LogTemp, Error, TEXT("MTA6 Economy: Schema %d nicht unterstuetzt"), Schema);
		return false;
	}
	ResetToStart(0);
	Money = (int64)MTAP::Num(Json, TEXT("money"), 0.0);
	TotalEarned = (int64)MTAP::Num(Json, TEXT("earned"), 0.0);
	TotalSpent = (int64)MTAP::Num(Json, TEXT("spent"), 0.0);
	Day = (int32)MTAP::Num(Json, TEXT("day"), 1.0);
	Hour = (float)MTAP::Num(Json, TEXT("hour"), 8.0);
	if (const TSharedPtr<FJsonObject> Cat = MTAP::Obj(Json, TEXT("spentByCategory")))
	{
		for (const auto& P : Cat->Values)
		{
			SpentByCategory.Add((EMTATransactionCategory)FCString::Atoi(*P.Key), (int64)P.Value->AsNumber());
		}
	}
	for (const auto& P : MTAP::ReadNumberMap(MTAP::Obj(Json, TEXT("scarcity")))) { Scarcity.Add(P.Key, P.Value); }
	for (const auto& P : MTAP::ReadNumberMap(MTAP::Obj(Json, TEXT("blackMarket")))) { BlackMarketRates.Add(P.Key, P.Value); }
	if (const TSharedPtr<FJsonObject> L = MTAP::Obj(Json, TEXT("loan")))
	{
		Loan.Outstanding = (int64)MTAP::Num(L, TEXT("outstanding"), 0.0);
		Loan.TotalBorrowed = (int64)MTAP::Num(L, TEXT("borrowed"), 0.0);
		Loan.InterestPaid = (int64)MTAP::Num(L, TEXT("interest"), 0.0);
		Loan.PaidToday = (int64)MTAP::Num(L, TEXT("paidToday"), 0.0);
		Loan.DaysOpen = (int32)MTAP::Num(L, TEXT("daysOpen"), 0.0);
		Loan.MissedDays = (int32)MTAP::Num(L, TEXT("missed"), 0.0);
		Loan.bDefaulted = MTAP::Bool(L, TEXT("defaulted"), false);
		Loan.UnpaidFines = (int64)MTAP::Num(L, TEXT("unpaidFines"), 0.0);
	}
	BalanceHead = 0; BalanceCount = 0;
	if (const TArray<TSharedPtr<FJsonValue>>* Tx = MTAP::Arr(Json, TEXT("transactions")))
	{
		for (const TSharedPtr<FJsonValue>& V : *Tx)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FMTATransaction T;
			T.Id = (int64)MTAP::Num(O, TEXT("id"), 0.0);
			T.Day = (int32)MTAP::Num(O, TEXT("d"), 1.0);
			T.Hour = (float)MTAP::Num(O, TEXT("h"), 0.0);
			T.Amount = (int64)MTAP::Num(O, TEXT("a"), 0.0);
			T.BalanceAfter = (int64)MTAP::Num(O, TEXT("b"), 0.0);
			T.Category = (EMTATransactionCategory)(uint8)MTAP::Num(O, TEXT("c"), 0.0);
			T.District = FName(*MTAP::Str(O, TEXT("dist")));
			T.Note = MTAP::Str(O, TEXT("n"));
			TxRing[TxHead] = T;
			TxHead = (TxHead + 1) % TxRing.Num();
			TxCount = FMath::Min(TxCount + 1, TxRing.Num());
			NextTxId = FMath::Max(NextTxId, T.Id + 1);
		}
	}
	if (const TArray<TSharedPtr<FJsonValue>>* H = MTAP::Arr(Json, TEXT("balanceHistory")))
	{
		for (const TSharedPtr<FJsonValue>& V : *H)
		{
			const TArray<TSharedPtr<FJsonValue>>& Row = V->AsArray();
			if (Row.Num() < 5) continue;
			FMTABalanceSample S;
			S.Day = (int32)Row[0]->AsNumber();
			S.Hour = (float)Row[1]->AsNumber();
			S.Balance = (int64)Row[2]->AsNumber();
			S.Debt = (int64)Row[3]->AsNumber();
			S.PriceIndex = (float)Row[4]->AsNumber();
			BalanceRing[BalanceHead] = S;
			BalanceHead = (BalanceHead + 1) % BalanceRing.Num();
			BalanceCount = FMath::Min(BalanceCount + 1, BalanceRing.Num());
		}
	}
	bAegisOverride = MTAP::Bool(Json, TEXT("aegisOverride"), false);
	AegisOverride = (float)MTAP::Num(Json, TEXT("aegisOverrideValue"), 50.0);
	if (!bSimulating) { OnMoneyChanged.Broadcast(Money, 0); OnLoanChanged.Broadcast(Loan); }
	return true;
}

FString UMTAEconomySubsystem::SaveStateToString() const { return MTAProgression::JsonToString(ToJson()); }
bool UMTAEconomySubsystem::LoadStateFromString(const FString& Json) { return FromJson(MTAProgression::JsonFromString(Json)); }

// ---------------------------------------------------------------------------------------------
// Simulation (Balancing, 12.10)
// ---------------------------------------------------------------------------------------------

FMTAEconomySimReport UMTAEconomySubsystem::RunEconomySimulation(int32 Hours, int32 Seed)
{
	FMTAEconomySimReport Rep;
	if (!Data.IsValid()) { Rep.Verdict = TEXT("economy.json fehlt"); return Rep; }

	// Zustand sichern, Simulation auf Kopie
	const TSharedRef<FJsonObject> Snapshot = ToJson();
	const FRandomStream SavedRng = Rng;
	const bool bSavedOverride = bAegisOverride;
	const float SavedAegis = AegisOverride;

	bSimulating = true;
	Rng.Initialize(Seed);
	ResetToStart(-1);
	AegisOverride = 50.f;
	SimLevel = 1;

	const TSharedPtr<FJsonObject> Sim = MTAP::Obj(Data, TEXT("simulation"));
	const TSharedPtr<FJsonObject> Bal = MTAP::Obj(Data, TEXT("balancing"));
	const TSharedPtr<FJsonObject> W = MTAP::Obj(Sim, TEXT("weights"));
	const TSharedPtr<FJsonObject> Mins = MTAP::Obj(Sim, TEXT("minutes"));
	const TSharedPtr<FJsonObject> Caps = MTAP::Obj(Sim, TEXT("caps"));
	const TCHAR* const Actions[] = { TEXT("gig"), TEXT("race"), TEXT("chase"), TEXT("contract"), TEXT("side"), TEXT("main"), TEXT("event"), TEXT("blackMarket"), TEXT("idleDrive") };
	const TCHAR* const FineTypes[] = { TEXT("speeding"), TEXT("redLight"), TEXT("reckless"), TEXT("propertyDamage"), TEXT("evading") };
	const TCHAR* const BmGoods[] = { TEXT("contraband_chips"), TEXT("contraband_fuel"), TEXT("contraband_meds"), TEXT("contraband_parts") };
	TMap<FString, int32> Used;

	TArray<double> TierHours;
	if (const TArray<TSharedPtr<FJsonValue>>* A = MTAP::Arr(Sim, TEXT("gigTierHours"))) { for (const auto& V : *A) TierHours.Add(V->AsNumber()); }
	TArray<int64> Ladder;
	if (const TArray<TSharedPtr<FJsonValue>>* A = MTAP::Arr(Sim, TEXT("vehicleLadder"))) { for (const auto& V : *A) Ladder.Add((int64)V->AsNumber()); }
	TArray<TSharedPtr<FJsonValue>> GarageLevels;
	if (const TArray<TSharedPtr<FJsonValue>>* A = MTAP::Arr(MTAP::Obj(Data, TEXT("garages")), TEXT("levels"))) { GarageLevels = *A; }

	const float CrashChance = (float)MTAP::Num(Sim, TEXT("crashChance"), 0.22);
	const float CrimeChance = (float)MTAP::Num(Sim, TEXT("crimeChance"), 0.12);
	const float ArrestChance = (float)MTAP::Num(Sim, TEXT("arrestChance"), 0.025);
	const float BribeShare = (float)MTAP::Num(Sim, TEXT("bribeShare"), 0.4);
	const float TuningShare = (float)MTAP::Num(Sim, TEXT("tuningShare"), 0.25);
	const int64 Reserve = (int64)MTAP::Num(Sim, TEXT("reserve"), 1500.0);
	const int32 MaxFleet = (int32)MTAP::Num(Sim, TEXT("maxFleet"), 8.0);
	const int32 PartsPerVehicle = (int32)MTAP::Num(Sim, TEXT("partsPerVehicle"), 8.0);
	const int64 MidCarPrice = (int64)MTAP::Num(Bal, TEXT("midCarPrice"), 18000.0);
	const bool bLoanWhenBroke = MTAP::Bool(Sim, TEXT("loanWhenBroke"), true);

	TArray<int64> Fleet; Fleet.Add(6000); // Startfahrzeug aus der Herkunft
	int32 NextLadder = 0;
	int32 PartsOnCurrent = 0;
	int64 CurrentValue = 6000;
	int32 GarageLevel = -1;
	int32 BrokeHours = 0;
	int32 LastDay = Day;

	const int32 SegBounds[] = { 0, 10, 50, 200, FMath::Max(Hours, 201) };
	TArray<int64> SegEarnStart, SegWealthStart;
	auto Wealth = [&]() -> int64
	{
		int64 FleetValue = 0;
		for (int64 V : Fleet) FleetValue += V;
		return Money + (int64)(FleetValue * 0.55) - GetDebt();
	};
	Rep.IncomePerHourBySegment.Init(0.f, 4);
	Rep.NetPerHourBySegment.Init(0.f, 4);
	Rep.StartMoney = Money;
	Rep.MinMoney = Money;
	Rep.MaxMoney = Money;
	Rep.PriceIndexStart = Rep.PriceIndexMin = Rep.PriceIndexMax = GetPriceIndex();
	Rep.Log = TEXT("stunde;geld;schuld;preisindex;flotte;verdient\n");
	int32 Seg = 0;
	int64 SegEarn0 = 0, SegWealth0 = Wealth();

	auto CurrentTier = [&](int32 H) { int32 T = 1; for (int32 I = 0; I < TierHours.Num(); ++I) if (H >= TierHours[I]) T = I + 1; return T; };
	auto RandDistrict = [&]() { return FName(MTAEconDistrictIds[Rng.RandRange(0, 5)]); };

	for (int32 H = 0; H < Hours; ++H)
	{
		SimLevel = FMath::Clamp(1 + (int32)(FMath::Sqrt((float)H) * 1.5f), 1, 50);
		const int32 Tier = CurrentTier(H);
		float Minutes = 0.f;
		while (Minutes < 60.f)
		{
			// gewichtete Aktionswahl (gedeckelte Inhalte fallen weg)
			float Total = 0.f;
			float Weights[9];
			for (int32 I = 0; I < 9; ++I)
			{
				const int32 Cap = (int32)MTAP::Num(Caps, Actions[I], -1.0);
				const bool bCapped = Cap >= 0 && Used.FindRef(FString(Actions[I])) >= Cap;
				Weights[I] = bCapped ? 0.f : (float)MTAP::Num(W, Actions[I], 0.0);
				Total += Weights[I];
			}
			float Pick = Rng.FRand() * Total;
			int32 A = 0;
			for (; A < 8; ++A) { if (Pick < Weights[A]) break; Pick -= Weights[A]; }
			const FString Act = Actions[A];
			Used.FindOrAdd(Act)++;
			Minutes += (float)MTAP::Num(Mins, Actions[A], 10.0);
			const FName Dist = RandDistrict();

			if (Act == TEXT("gig"))
			{
				PayMissionReward(TEXT("gig"), Tier, Dist, Rng.FRand() * 0.25f, Rng.FRand() * 0.2f, 0, Rng.FRand() < 0.88f);
			}
			else if (Act == TEXT("race"))
			{
				const int64 Stake = GetMissionReward(TEXT("race"), Tier, Dist, 0.f, 0.f, 0) / 10;
				if (TrySpend(Stake, EMTATransactionCategory::Activity, TEXT("Renneinsatz"), Dist) && Rng.FRand() < 0.45f)
				{
					AddMoney(GetMissionReward(TEXT("race"), Tier, Dist, 0.f, Rng.FRand() * 0.3f, 0), EMTATransactionCategory::Activity, TEXT("Rennen"), Dist);
				}
			}
			else if (Act == TEXT("blackMarket"))
			{
				const FName Good = BmGoods[Rng.RandRange(0, 3)];
				const int64 Buy = GetBlackMarketBuyPrice(Good, TEXT("rustline"));
				const int32 Units = Buy > 0 ? (int32)FMath::Min<int64>(5, (Money - Reserve) / Buy) : 0;
				if (Units > 0 && TrySpend(Buy * Units, EMTATransactionCategory::BlackMarket, TEXT("Ankauf"), TEXT("rustline")))
				{
					AdvanceTime(0.25f);
					const FName To = Rng.FRand() < 0.5f ? FName(TEXT("saltmarket")) : FName(TEXT("lanternhill"));
					AddMoney(GetBlackMarketSellPrice(Good, To) * Units, EMTATransactionCategory::BlackMarket, TEXT("Verkauf"), To);
					AegisOverride = FMath::Clamp(AegisOverride + GetBlackMarketAegisRisk(), 0.f, 100.f);
					Minutes += 15.f;
				}
			}
			else if (Act == TEXT("idleDrive"))
			{
				TrySpend(GetFuelCost(3.f, Dist), EMTATransactionCategory::Fuel, TEXT("Tanken"), Dist);
			}
			else
			{
				PayMissionReward(FName(*Act), Tier, Dist, Rng.FRand() * 0.2f, Rng.FRand() * 0.3f, 0, Rng.FRand() < 0.8f);
			}

			// Nebenwirkungen
			if (Rng.FRand() < CrashChance)
			{
				ForceCharge(GetRepairCost((float)Rng.FRandRange(0.1f, 0.6f), CurrentValue, Dist), EMTATransactionCategory::Repair, TEXT("Reparatur"), Dist);
			}
			if (Rng.FRand() < CrimeChance)
			{
				IssueFine(FineTypes[Rng.RandRange(0, 4)], Dist);
				AegisOverride = FMath::Clamp(AegisOverride + 1.f, 0.f, 100.f);
			}
			if (Rng.FRand() < ArrestChance)
			{
				const int32 Stars = Rng.RandRange(1, 4);
				FMTABribeResult B;
				if (Rng.FRand() < BribeShare) { B = TryBribe(Dist, Stars); }
				if (!B.bSuccess) { ChargeArrest(Stars, Dist); ++Rep.Arrests; }
			}
		}

		TrySpend(GetFuelCost(5.f, TEXT("rustline")), EMTATransactionCategory::Fuel, TEXT("Tanken"), TEXT("rustline"));
		AdvanceTime(1.f);
		AegisOverride = FMath::Max(20.f, AegisOverride - 0.4f); // Untertauchen/Kennzeichen senken das Profil langsam

		// Tageskosten (Versicherung + Garagenmiete)
		if (Day != LastDay)
		{
			LastDay = Day;
			int64 FleetValue = 0;
			for (int64 V : Fleet) FleetValue += V;
			ForceCharge(MTAEconRoundMoney(FleetValue * 0.0055), EMTATransactionCategory::Insurance, TEXT("Versicherung"), NAME_None);
			if (GarageLevel >= 0 && GarageLevels.IsValidIndex(GarageLevel))
			{
				ForceCharge((int64)MTAP::Num(GarageLevels[GarageLevel]->AsObject(), TEXT("rentPerDay"), 0.0), EMTATransactionCategory::Garage, TEXT("Miete"), NAME_None);
			}
		}

		// Kaeufe: Garage, Fahrzeugleiter, Tuning, Kennzeichen
		const int32 Capacity = GarageLevel >= 0 ? (int32)MTAP::Num(GarageLevels[GarageLevel]->AsObject(), TEXT("capacity"), 2.0) : 1;
		if (Fleet.Num() >= Capacity && GarageLevels.IsValidIndex(GarageLevel + 1))
		{
			const int64 P = GetPrice((int64)MTAP::Num(GarageLevels[GarageLevel + 1]->AsObject(), TEXT("price"), 8000.0), TEXT("saltmarket"), NAME_None);
			if (Money > P + Reserve && TrySpend(P, EMTATransactionCategory::Garage, TEXT("Garage"), TEXT("saltmarket"))) { ++GarageLevel; }
		}
		if (Ladder.IsValidIndex(NextLadder) && Fleet.Num() < MaxFleet && Fleet.Num() < Capacity + 1)
		{
			const int64 P = GetPrice(Ladder[NextLadder], TEXT("sunstrip"), TEXT("vehicles"));
			if (Money > (int64)(P * 1.15) + Reserve && TrySpend(P, EMTATransactionCategory::Vehicle, TEXT("Fahrzeugkauf"), TEXT("sunstrip")))
			{
				Fleet.Add(P);
				CurrentValue = P;
				PartsOnCurrent = 0;
				++Rep.VehiclesBought;
				if (Rep.MidCarHour < 0.f && Ladder[NextLadder] >= MidCarPrice) { Rep.MidCarHour = H + 1.f; }
				++NextLadder;
			}
		}
		if (PartsOnCurrent < PartsPerVehicle && Money > Reserve * 2)
		{
			const int64 Part = FMath::Clamp<int64>((int64)((Money - Reserve) * TuningShare), 300, 2000 * Tier);
			if (TrySpend(Part, EMTATransactionCategory::Tuning, TEXT("Tuning"), TEXT("rustline"))) { ++PartsOnCurrent; }
		}
		if (AegisOverride > 65.f && TrySpend((int64)GetPriceParam(TEXT("plateSwapBlack"), 1400.f), EMTATransactionCategory::Plate, TEXT("Kennzeichen"), TEXT("rustline")))
		{
			AegisOverride -= 15.f;
		}
		// Kredit
		if (bLoanWhenBroke && Money < 200 && GetLoanLimit() >= 1500 && TakeLoan(1500)) { ++Rep.LoansTaken; }
		if (Loan.Outstanding > 0 && Money > Loan.Outstanding * 2 + Reserve) { RepayLoan(Loan.Outstanding); }

		// Messwerte
		Rep.MinMoney = FMath::Min(Rep.MinMoney, Money);
		Rep.MaxMoney = FMath::Max(Rep.MaxMoney, Money);
		const float PriceIdx = GetPriceIndex();
		Rep.PriceIndexMin = FMath::Min(Rep.PriceIndexMin, PriceIdx);
		Rep.PriceIndexMax = FMath::Max(Rep.PriceIndexMax, PriceIdx);
		if (Money < 300) ++BrokeHours;
		if ((H + 1) % 25 == 0)
		{
			Rep.Log += FString::Printf(TEXT("%d;%lld;%lld;%.3f;%d;%lld\n"), H + 1, Money, GetDebt(), PI, Fleet.Num(), TotalEarned);
		}
		if (Seg < 4 && ((H + 1) >= SegBounds[Seg + 1] || (H + 1) == Hours))
		{
			const float Len = (float)FMath::Max(1, FMath::Min(H + 1, SegBounds[Seg + 1]) - SegBounds[Seg]);
			Rep.IncomePerHourBySegment[Seg] = (float)(TotalEarned - SegEarn0) / Len;
			Rep.NetPerHourBySegment[Seg] = (float)(Wealth() - SegWealth0) / Len;
			SegEarn0 = TotalEarned;
			SegWealth0 = Wealth();
			++Seg;
		}
	}

	Rep.Hours = Hours;
	Rep.EndMoney = Money;
	Rep.TotalEarned = TotalEarned;
	Rep.TotalSpent = TotalSpent;
	Rep.PriceIndexEnd = GetPriceIndex();
	Rep.BrokeShare = Hours > 0 ? (float)BrokeHours / Hours : 0.f;

	// Bewertung
	TArray<FString> Findings;
	const TArray<TSharedPtr<FJsonValue>>* Band = MTAP::Arr(Bal, TEXT("targetHourlyIncome"));
	if (Band && Band->Num() == 2)
	{
		const float Mid = Rep.IncomePerHourBySegment[1];
		if (Mid < (*Band)[0]->AsNumber()) Findings.Add(FString::Printf(TEXT("Mittelspiel-Einkommen %.0f/h unter Ziel"), Mid));
		if (Mid > (*Band)[1]->AsNumber()) Findings.Add(FString::Printf(TEXT("Mittelspiel-Einkommen %.0f/h ueber Ziel"), Mid));
	}
	const TArray<TSharedPtr<FJsonValue>>* CarBand = MTAP::Arr(Bal, TEXT("targetMidCarHours"));
	if (CarBand && CarBand->Num() == 2)
	{
		if (Rep.MidCarHour < 0.f) Findings.Add(TEXT("Mittelklassewagen nie erreicht"));
		else if (Rep.MidCarHour < (*CarBand)[0]->AsNumber() || Rep.MidCarHour > (*CarBand)[1]->AsNumber() * 2.0)
			Findings.Add(FString::Printf(TEXT("Mittelklassewagen nach %.0f h (Ziel %.0f-%.0f h)"), Rep.MidCarHour, (*CarBand)[0]->AsNumber(), (*CarBand)[1]->AsNumber()));
	}
	if (Rep.PriceIndexEnd > MTAP::Num(Bal, TEXT("inflationWarnPriceIndex"), 1.25)) { Rep.bInflation = true; Findings.Add(TEXT("Preisindex inflationaer")); }
	if (Rep.PriceIndexEnd < MTAP::Num(Bal, TEXT("deflationWarnPriceIndex"), 0.8)) { Rep.bDeflation = true; Findings.Add(TEXT("Preisindex deflationaer")); }
	if (Rep.NetPerHourBySegment[3] > MTAP::Num(Bal, TEXT("wealthGrowthWarnPerHour"), 9000.0))
	{
		Rep.bInflation = true;
		Findings.Add(FString::Printf(TEXT("Geldschwemme im Spaetspiel: +%.0f/h netto (Senken fehlen)"), Rep.NetPerHourBySegment[3]));
	}
	if (Rep.BrokeShare > MTAP::Num(Bal, TEXT("brokeWarnShare"), 0.1))
	{
		Rep.bDeflation = true;
		Findings.Add(FString::Printf(TEXT("Geldmangel in %.0f %% der Stunden"), Rep.BrokeShare * 100.f));
	}
	Rep.Verdict = Findings.Num() == 0 ? TEXT("OK") : FString::Join(Findings, TEXT("; "));
	UE_LOG(LogTemp, Display, TEXT("ECONSIM h=%d start=%lld ende=%lld min=%lld max=%lld einkommen/h=[%.0f %.0f %.0f %.0f] netto/h=[%.0f %.0f %.0f %.0f] PI=%.2f->%.2f pleite=%.1f%% auto=%.0fh -> %s"),
		Hours, Rep.StartMoney, Rep.EndMoney, Rep.MinMoney, Rep.MaxMoney,
		Rep.IncomePerHourBySegment[0], Rep.IncomePerHourBySegment[1], Rep.IncomePerHourBySegment[2], Rep.IncomePerHourBySegment[3],
		Rep.NetPerHourBySegment[0], Rep.NetPerHourBySegment[1], Rep.NetPerHourBySegment[2], Rep.NetPerHourBySegment[3],
		Rep.PriceIndexStart, Rep.PriceIndexEnd, Rep.BrokeShare * 100.f, Rep.MidCarHour, *Rep.Verdict);

	// Zustand wiederherstellen
	bSimulating = false;
	FromJson(Snapshot);
	Rng = SavedRng;
	bAegisOverride = bSavedOverride;
	AegisOverride = SavedAegis;
	return Rep;
}
