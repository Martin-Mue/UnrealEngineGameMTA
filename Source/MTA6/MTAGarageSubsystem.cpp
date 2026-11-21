#include "MTAGarageSubsystem.h"
#include "MTAEconomySubsystem.h"
#include "MTAVehicleCatalog.h"
#include "MTAProgressionSubsystem.h"
#include "MTAInventorySubsystem.h"
#include "Engine/GameInstance.h"

namespace MTAP = MTAProgression;

void UMTAGarageSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UMTAEconomySubsystem* Econ = Collection.InitializeDependency<UMTAEconomySubsystem>();
	Collection.InitializeDependency<UMTAVehicleCatalog>();
	UMTAProgressionSubsystem* Prog = Collection.InitializeDependency<UMTAProgressionSubsystem>();
	if (Econ) { DayHandle = Econ->OnDayPassedNative.AddUObject(this, &UMTAGarageSubsystem::HandleDayPassed); }
	if (Prog) { ModsHandle = Prog->OnModifiersChanged.AddUObject(this, &UMTAGarageSubsystem::HandleModifiersChanged); }
	Rng.Initialize(4471);
	ResetGarage();
}

void UMTAGarageSubsystem::Deinitialize()
{
	if (UMTAEconomySubsystem* Econ = Economy()) { Econ->OnDayPassedNative.Remove(DayHandle); }
	if (UMTAProgressionSubsystem* Prog = Progression()) { Prog->OnModifiersChanged.Remove(ModsHandle); }
	Super::Deinitialize();
}

void UMTAGarageSubsystem::ResetGarage()
{
	Vehicles.Reset();
	GarageLevels.Reset();
	ActiveVehicle = NAME_None;
	NextInstance = 1;
	OnGarageChanged.Broadcast();
}

UMTAEconomySubsystem* UMTAGarageSubsystem::Economy() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UMTAEconomySubsystem>() : nullptr;
}

UMTAVehicleCatalog* UMTAGarageSubsystem::Catalog() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UMTAVehicleCatalog>() : nullptr;
}

UMTAProgressionSubsystem* UMTAGarageSubsystem::Progression() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UMTAProgressionSubsystem>() : nullptr;
}

FMTAOwnedVehicle* UMTAGarageSubsystem::Find(FName InstanceId)
{
	return Vehicles.FindByPredicate([InstanceId](const FMTAOwnedVehicle& V) { return V.InstanceId == InstanceId; });
}

const FMTAOwnedVehicle* UMTAGarageSubsystem::Find(FName InstanceId) const
{
	return Vehicles.FindByPredicate([InstanceId](const FMTAOwnedVehicle& V) { return V.InstanceId == InstanceId; });
}

void UMTAGarageSubsystem::Changed(FName InstanceId)
{
	OnGarageChanged.Broadcast();
	if (!InstanceId.IsNone()) { OnVehicleStatsChanged.Broadcast(InstanceId); }
}

void UMTAGarageSubsystem::HandleModifiersChanged()
{
	if (!ActiveVehicle.IsNone()) { OnVehicleStatsChanged.Broadcast(ActiveVehicle); }
}

TSharedPtr<FJsonObject> UMTAGarageSubsystem::GarageLevelData(int32 Level) const
{
	const UMTAEconomySubsystem* Econ = Economy();
	const TArray<TSharedPtr<FJsonValue>>* Levels = Econ ? MTAP::Arr(MTAP::Obj(Econ->GetData(), TEXT("garages")), TEXT("levels")) : nullptr;
	return (Levels && Levels->IsValidIndex(Level)) ? (*Levels)[Level]->AsObject() : nullptr;
}

int32 UMTAGarageSubsystem::CountInGarage(FName District) const
{
	int32 N = 0;
	for (const FMTAOwnedVehicle& V : Vehicles) { if (V.bInGarage && V.District == District && !V.bImpounded) ++N; }
	return N;
}

FString UMTAGarageSubsystem::MakePlate()
{
	const FString Letters = TEXT("ABCDEFGHJKLMNPRSTVWXZ");
	FString Prefix;
	Prefix.AppendChar(Letters[Rng.RandRange(0, Letters.Len() - 1)]);
	Prefix.AppendChar(Letters[Rng.RandRange(0, Letters.Len() - 1)]);
	return FString::Printf(TEXT("KB-%s %04d"), *Prefix, Rng.RandRange(0, 9999));
}

// ---------------------------------------------------------------------------------------------
// Besitz
// ---------------------------------------------------------------------------------------------

FName UMTAGarageSubsystem::AddVehicle(FName ModelId, FName District, bool bStolen)
{
	const UMTAVehicleCatalog* Cat = Catalog();
	const FMTAVehicleModel* Model = Cat ? Cat->FindModel(ModelId) : nullptr;
	if (!Model) return NAME_None;
	FMTAOwnedVehicle V;
	V.InstanceId = FName(*FString::Printf(TEXT("veh_%03d"), NextInstance++));
	V.ModelId = ModelId;
	V.Plate = MakePlate();
	V.bStolen = bStolen;
	V.bHot = bStolen || Model->Class == EMTAVehicleClass::Patrol;
	V.PlateHeat = V.bHot ? 60.f : 0.f;
	V.District = District;
	V.PurchasePrice = bStolen ? 0 : Model->Price;
	const FMTAGarageInfo G = GetGarage(District);
	V.bInGarage = G.Level >= 0 && G.Used < G.Capacity;
	Vehicles.Add(V);
	if (ActiveVehicle.IsNone()) { ActiveVehicle = V.InstanceId; }
	Changed(V.InstanceId);
	return V.InstanceId;
}

FName UMTAGarageSubsystem::BuyVehicle(FName ModelId, FName District, FString& OutReason)
{
	UMTAEconomySubsystem* Econ = Economy();
	const UMTAVehicleCatalog* Cat = Catalog();
	const FMTAVehicleModel* Model = Cat ? Cat->FindModel(ModelId) : nullptr;
	if (!Econ || !Model) { OutReason = TEXT("Unbekanntes Modell"); return NAME_None; }
	if (!Model->bPurchasable || !Model->Dealers.Contains(District)) { OutReason = TEXT("Hier nicht kaeuflich"); return NAME_None; }
	for (const auto& Req : Model->FactionReq)
	{
		if (MTAP::GetStoryStanding(GetGameInstance(), Req.Key.ToString()) < Req.Value)
		{
			OutReason = FString::Printf(TEXT("Benoetigt Stand %.0f bei %s"), Req.Value, *Req.Key.ToString());
			return NAME_None;
		}
	}
	const int64 Price = Econ->GetPrice(Model->Price, District, TEXT("vehicles"));
	if (!Econ->TrySpend(Price, EMTATransactionCategory::Vehicle, Model->BrandName + TEXT(" ") + Model->ModelName, District))
	{
		OutReason = FString::Printf(TEXT("Zu wenig Geld ($%lld)"), Price);
		return NAME_None;
	}
	const FName Id = AddVehicle(ModelId, District, false);
	if (FMTAOwnedVehicle* V = Find(Id)) { V->PurchasePrice = Price; }
	OutReason.Reset();
	return Id;
}

int64 UMTAGarageSubsystem::GetVehicleValue(FName InstanceId) const
{
	const FMTAOwnedVehicle* V = Find(InstanceId);
	const UMTAVehicleCatalog* Cat = Catalog();
	const FMTAVehicleModel* Model = (V && Cat) ? Cat->FindModel(V->ModelId) : nullptr;
	if (!Model) return 0;
	const double Wear = FMath::Clamp(1.0 - V->OdometerKm / 400000.0, 0.4, 1.0);
	double Value = (double)Model->Price * Cat->GetRarityResale(Model->Rarity) * Wear * (1.0 - 0.6 * V->Damage);
	for (const FName& P : V->InstalledParts)
	{
		if (const FMTATuningPart* Part = Cat->FindTuningPart(P)) { Value += Part->Price * 0.5; }
	}
	return (int64)Value;
}

int64 UMTAGarageSubsystem::SellVehicle(FName InstanceId, FName District)
{
	UMTAEconomySubsystem* Econ = Economy();
	const FMTAOwnedVehicle* V = Find(InstanceId);
	if (!Econ || !V) return 0;
	const UMTAVehicleCatalog* Cat = Catalog();
	const FMTAVehicleModel* Model = Cat ? Cat->FindModel(V->ModelId) : nullptr;
	if (Model && Model->Rarity == EMTARarity::Unique) return 0; // Leihgabe, nicht verkaeuflich
	int64 Amount = 0;
	if (V->bStolen)
	{
		if (!Econ->IsBlackMarketAvailable(District)) return 0;
		Amount = (int64)(GetVehicleValue(InstanceId) * Econ->GetPriceParam(TEXT("stolenResalePct"), 0.22f));
		Econ->AddMoney(Amount, EMTATransactionCategory::BlackMarket, TEXT("Hehler: Fahrzeug"), District);
	}
	else
	{
		Amount = Econ->GetSellPrice(GetVehicleValue(InstanceId), District, TEXT("vehicles"), Econ->GetPriceParam(TEXT("vehicleResalePct"), 0.55f));
		Econ->AddMoney(Amount, EMTATransactionCategory::Sale, TEXT("Fahrzeugverkauf"), District);
	}
	Vehicles.RemoveAll([InstanceId](const FMTAOwnedVehicle& X) { return X.InstanceId == InstanceId; });
	if (ActiveVehicle == InstanceId) { ActiveVehicle = Vehicles.Num() > 0 ? Vehicles[0].InstanceId : NAME_None; }
	Changed(NAME_None);
	return Amount;
}

bool UMTAGarageSubsystem::GetVehicle(FName InstanceId, FMTAOwnedVehicle& Out) const
{
	if (const FMTAOwnedVehicle* V = Find(InstanceId)) { Out = *V; return true; }
	return false;
}

bool UMTAGarageSubsystem::SetActiveVehicle(FName InstanceId)
{
	const FMTAOwnedVehicle* V = Find(InstanceId);
	if (!V || V->bImpounded) return false;
	ActiveVehicle = InstanceId;
	Changed(InstanceId);
	return true;
}

// ---------------------------------------------------------------------------------------------
// Garagen
// ---------------------------------------------------------------------------------------------

FMTAGarageInfo UMTAGarageSubsystem::GetGarage(FName District) const
{
	FMTAGarageInfo G;
	G.District = District;
	const int32* L = GarageLevels.Find(District);
	G.Level = L ? *L : -1;
	const UMTAEconomySubsystem* Econ = Economy();
	if (const TSharedPtr<FJsonObject> D = GarageLevelData(G.Level))
	{
		G.Capacity = (int32)MTAP::Num(D, TEXT("capacity"), 2.0);
		G.RentPerDay = (int64)(MTAP::Num(D, TEXT("rentPerDay"), 0.0) * (Econ ? Econ->GetDistrictFactor(District, TEXT("rent")) : 1.f));
	}
	if (const TSharedPtr<FJsonObject> N = GarageLevelData(G.Level + 1))
	{
		G.NextLevelPrice = Econ ? Econ->GetPrice((int64)MTAP::Num(N, TEXT("price"), 8000.0), District, NAME_None) : 0;
	}
	G.Used = CountInGarage(District);
	return G;
}

TArray<FMTAGarageInfo> UMTAGarageSubsystem::GetAllGarages() const
{
	TArray<FMTAGarageInfo> Result;
	for (int32 I = 0; I < MTADistrict::Num(); ++I) { Result.Add(GetGarage(MTAP::DistrictToId(static_cast<EMTADistrict>(I)))); }
	return Result;
}

bool UMTAGarageSubsystem::BuyOrUpgradeGarage(FName District, FString& OutReason)
{
	UMTAEconomySubsystem* Econ = Economy();
	const FMTAGarageInfo G = GetGarage(District);
	if (!Econ || !GarageLevelData(G.Level + 1).IsValid()) { OutReason = TEXT("Hoechste Ausbaustufe"); return false; }
	if (!Econ->TrySpend(G.NextLevelPrice, EMTATransactionCategory::Garage, TEXT("Garage ") + District.ToString(), District))
	{
		OutReason = TEXT("Zu wenig Geld");
		return false;
	}
	GarageLevels.Add(District, G.Level + 1);
	OutReason.Reset();
	Changed(NAME_None);
	return true;
}

void UMTAGarageSubsystem::GrantGarage(FName District, int32 Level)
{
	GarageLevels.Add(District, FMath::Max(Level, GetGarage(District).Level));
	Changed(NAME_None);
}

bool UMTAGarageSubsystem::StoreVehicle(FName InstanceId, FName District)
{
	FMTAOwnedVehicle* V = Find(InstanceId);
	if (!V || V->bImpounded) return false;
	if (V->bInGarage && V->District == District) return true;
	const FMTAGarageInfo G = GetGarage(District);
	if (G.Level < 0 || G.Used >= G.Capacity) return false;
	V->bInGarage = true;
	V->District = District;
	Changed(InstanceId);
	return true;
}

void UMTAGarageSubsystem::ParkOnStreet(FName InstanceId, FName District, FVector Location, float Yaw)
{
	if (FMTAOwnedVehicle* V = Find(InstanceId))
	{
		V->bInGarage = false;
		V->District = District;
		V->ParkedLocation = Location;
		V->ParkedYaw = Yaw;
		Changed(NAME_None);
	}
}

// ---------------------------------------------------------------------------------------------
// Kennzeichen
// ---------------------------------------------------------------------------------------------

bool UMTAGarageSubsystem::ChangePlate(FName InstanceId, bool bBlackMarket, FName District, FString& OutReason)
{
	FMTAOwnedVehicle* V = Find(InstanceId);
	UMTAEconomySubsystem* Econ = Economy();
	if (!V || !Econ) { OutReason = TEXT("Unbekanntes Fahrzeug"); return false; }
	if (bBlackMarket && !Econ->IsBlackMarketAvailable(District)) { OutReason = TEXT("Kein Schwarzmarkt in diesem Bezirk"); return false; }
	if (!bBlackMarket && V->bStolen) { OutReason = TEXT("Werkstatt verweigert gestohlenes Fahrzeug"); return false; }

	const UMTAProgressionSubsystem* Prog = Progression();
	UGameInstance* GI = GetGameInstance();
	UMTAInventorySubsystem* Inv = GI ? GI->GetSubsystem<UMTAInventorySubsystem>() : nullptr;
	// Eigener Rohling/Klon im Inventar ersetzt die Teilkosten (nur Arbeitslohn).
	FName UsedItem = NAME_None;
	if (Inv && Inv->GetItemCount(TEXT("plate_cloned")) > 0) { UsedItem = TEXT("plate_cloned"); }
	else if (Inv && Inv->GetItemCount(TEXT("plate_blank")) > 0) { UsedItem = TEXT("plate_blank"); }

	float Cost = Econ->GetPriceParam(bBlackMarket ? TEXT("plateSwapBlack") : TEXT("plateSwapLegal"), bBlackMarket ? 1400.f : 450.f);
	if (!UsedItem.IsNone()) { Cost *= 0.35f; }
	Cost *= Econ->GetDistrictFactor(District, TEXT("price"));
	if (Prog) { Cost = Prog->ApplyStat(TEXT("econ.plateCost"), Cost); }
	if (!Econ->TrySpend((int64)Cost, EMTATransactionCategory::Plate, TEXT("Kennzeichenwechsel"), District)) { OutReason = TEXT("Zu wenig Geld"); return false; }
	if (Inv && !UsedItem.IsNone()) { Inv->RemoveFromAny(UsedItem, 1); }

	float AegisDrop = bBlackMarket ? 12.f : 6.f;
	AegisDrop += V->PlateHeat * 0.1f;
	if (UsedItem == TEXT("plate_cloned")) { AegisDrop += 4.f; }
	if (Prog) { AegisDrop = Prog->ApplyStat(TEXT("aegis.plateSwap"), AegisDrop); }
	MTAP::AddStoryAegis(GI, -AegisDrop);

	V->Plate = MakePlate();
	V->PlateHeat = 0.f;
	// Gestohlene Fahrzeuge bleiben am Fahrgestell erkennbar, aber nicht mehr per Kamera "heiss".
	V->bHot = false;
	if (Prog)
	{
		if (UMTAProgressionSubsystem* P = Progression()) { P->AwardXpFromSource(TEXT("plate_swap")); }
	}
	OutReason.Reset();
	OnVehicleEvent.Broadcast(InstanceId, FString::Printf(TEXT("Neues Kennzeichen %s, Aegis -%.0f"), *V->Plate, AegisDrop));
	Changed(InstanceId);
	return true;
}

void UMTAGarageSubsystem::RegisterPlateSighting(FName InstanceId, float Amount, bool bDuringCrime)
{
	FMTAOwnedVehicle* V = Find(InstanceId);
	if (!V) return;
	float A = Amount;
	if (const UMTAProgressionSubsystem* Prog = Progression()) { A = Prog->ApplyStat(TEXT("aegis.plateDetect"), A); }
	V->PlateHeat = FMath::Clamp(V->PlateHeat + A, 0.f, 100.f);
	if (bDuringCrime)
	{
		V->bHot = V->bHot || V->PlateHeat > 40.f;
		float Gain = A * 0.25f;
		if (const UMTAProgressionSubsystem* Prog = Progression()) { Gain = Prog->ApplyStat(TEXT("aegis.gain"), Gain); }
		MTAP::AddStoryAegis(GetGameInstance(), Gain);
	}
}

// ---------------------------------------------------------------------------------------------
// Tuning
// ---------------------------------------------------------------------------------------------

bool UMTAGarageSubsystem::CanInstallPart(FName InstanceId, FName PartId, FString& OutReason) const
{
	const FMTAOwnedVehicle* V = Find(InstanceId);
	const UMTAVehicleCatalog* Cat = Catalog();
	const FMTATuningPart* Part = Cat ? Cat->FindTuningPart(PartId) : nullptr;
	if (!V || !Part) { OutReason = TEXT("Unbekanntes Teil"); return false; }
	if (V->bImpounded) { OutReason = TEXT("Fahrzeug in Verwahrung"); return false; }
	if (!Cat->IsTuningSlotAllowed(V->ModelId, Part->Category)) { OutReason = TEXT("Passt nicht zu dieser Fahrzeugklasse"); return false; }
	if (V->InstalledParts.Contains(PartId)) { OutReason = TEXT("Bereits verbaut"); return false; }
	for (const auto& Req : Part->StandingReq)
	{
		if (MTAP::GetStoryStanding(GetGameInstance(), Req.Key.ToString()) < Req.Value)
		{
			OutReason = FString::Printf(TEXT("Benoetigt Stand %.0f bei %s"), Req.Value, *Req.Key.ToString());
			return false;
		}
	}
	OutReason.Reset();
	return true;
}

bool UMTAGarageSubsystem::InstallPart(FName InstanceId, FName PartId, FName District, FString& OutReason)
{
	if (!CanInstallPart(InstanceId, PartId, OutReason)) return false;
	UMTAEconomySubsystem* Econ = Economy();
	const FMTATuningPart* Part = Catalog()->FindTuningPart(PartId);
	float Cost = Econ ? (float)Econ->GetPrice(Part->Price, District, TEXT("parts")) : (float)Part->Price;
	if (const UMTAProgressionSubsystem* Prog = Progression()) { Cost = Prog->ApplyStat(TEXT("econ.tuningCost"), Cost); }
	if (Econ && !Econ->TrySpend((int64)Cost, EMTATransactionCategory::Tuning, Part->Name, District)) { OutReason = TEXT("Zu wenig Geld"); return false; }
	FMTAOwnedVehicle* V = Find(InstanceId);
	RemovePart(InstanceId, Part->Category); // ein Teil je Kategorie, altes Teil wird ersetzt
	V->InstalledParts.Add(PartId);
	Changed(InstanceId);
	return true;
}

bool UMTAGarageSubsystem::RemovePart(FName InstanceId, FName Category)
{
	FMTAOwnedVehicle* V = Find(InstanceId);
	const UMTAVehicleCatalog* Cat = Catalog();
	if (!V || !Cat) return false;
	const int32 Removed = V->InstalledParts.RemoveAll([Cat, Category](const FName& P)
	{
		const FMTATuningPart* Part = Cat->FindTuningPart(P);
		return Part && Part->Category == Category;
	});
	if (Removed > 0) { Changed(InstanceId); }
	return Removed > 0;
}

void UMTAGarageSubsystem::SetPaintColor(FName InstanceId, FLinearColor Color)
{
	if (FMTAOwnedVehicle* V = Find(InstanceId)) { V->PaintColor = Color; Changed(NAME_None); }
}

bool UMTAGarageSubsystem::IsVehicleIllegalIn(FName InstanceId, FName District) const
{
	const FMTAOwnedVehicle* V = Find(InstanceId);
	const UMTAVehicleCatalog* Cat = Catalog();
	if (!V || !Cat) return false;
	if (V->bHot) return true;
	for (const FName& P : V->InstalledParts)
	{
		const FMTATuningPart* Part = Cat->FindTuningPart(P);
		if (Part && Part->IllegalIn.Contains(District)) return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Zustand
// ---------------------------------------------------------------------------------------------

void UMTAGarageSubsystem::SyncFromPawn(FName InstanceId, float HealthFraction, float FuelFraction, float DrivenKm)
{
	if (FMTAOwnedVehicle* V = Find(InstanceId))
	{
		V->Damage = FMath::Clamp(1.f - HealthFraction, 0.f, 1.f);
		V->Fuel = FMath::Clamp(FuelFraction, 0.f, 1.f);
		V->OdometerKm += FMath::Max(0.f, DrivenKm);
	}
}

int64 UMTAGarageSubsystem::GetRepairCost(FName InstanceId, FName District) const
{
	const FMTAOwnedVehicle* V = Find(InstanceId);
	const UMTAEconomySubsystem* Econ = Economy();
	const UMTAVehicleCatalog* Cat = Catalog();
	const FMTAVehicleModel* Model = (V && Cat) ? Cat->FindModel(V->ModelId) : nullptr;
	if (!V || !Econ || !Model) return 0;
	return Econ->GetRepairCost(V->Damage, Model->Price, District);
}

bool UMTAGarageSubsystem::RepairVehicle(FName InstanceId, FName District)
{
	FMTAOwnedVehicle* V = Find(InstanceId);
	UMTAEconomySubsystem* Econ = Economy();
	if (!V || !Econ) return false;
	const int64 Cost = GetRepairCost(InstanceId, District);
	if (!Econ->TrySpend(Cost, EMTATransactionCategory::Repair, TEXT("Werkstatt"), District)) return false;
	V->Damage = 0.f;
	Changed(InstanceId);
	return true;
}

void UMTAGarageSubsystem::RepairByFraction(FName InstanceId, float Fraction)
{
	if (FMTAOwnedVehicle* V = Find(InstanceId))
	{
		V->Damage = FMath::Max(0.f, V->Damage - Fraction);
		Changed(InstanceId);
	}
}

void UMTAGarageSubsystem::Refuel(FName InstanceId, float Fraction, FName District, bool bFree)
{
	FMTAOwnedVehicle* V = Find(InstanceId);
	UMTAEconomySubsystem* Econ = Economy();
	if (!V) return;
	const float Add = FMath::Clamp(Fraction, 0.f, 1.f - V->Fuel);
	if (Add <= 0.f) return;
	if (!bFree && Econ)
	{
		const float Liters = Add * ComputeStats(InstanceId).FuelCapacityL;
		if (!Econ->TrySpend(Econ->GetFuelCost(Liters, District), EMTATransactionCategory::Fuel, TEXT("Tanken"), District)) return;
	}
	V->Fuel += Add;
}

int64 UMTAGarageSubsystem::GetInsurancePremium(FName InstanceId, int32 Tier) const
{
	const UMTAEconomySubsystem* Econ = Economy();
	if (!Econ || Tier < 0) return 0;
	const TArray<TSharedPtr<FJsonValue>>* Pct = MTAP::Arr(MTAP::Obj(Econ->GetData(), TEXT("prices")), TEXT("insuranceDailyPct"));
	if (!Pct || !Pct->IsValidIndex(Tier)) return 0;
	float Premium = (float)(GetVehicleValue(InstanceId) * (*Pct)[Tier]->AsNumber());
	// Hohes Aegis-Profil = hoehere Praemie (Versicherer sind an Aegis angebunden)
	Premium *= 1.f + FMath::Max(0.f, Econ->GetAegisProfile() - 50.f) / 100.f;
	if (const UMTAProgressionSubsystem* Prog = Progression()) { Premium = Prog->ApplyStat(TEXT("econ.insuranceCost"), Premium); }
	const FMTAOwnedVehicle* V = Find(InstanceId);
	if (V && V->bStolen) return 0; // gestohlene Fahrzeuge sind nicht versicherbar
	return FMath::Max<int64>(1, (int64)Premium);
}

bool UMTAGarageSubsystem::SetInsurance(FName InstanceId, int32 Tier)
{
	FMTAOwnedVehicle* V = Find(InstanceId);
	if (!V || V->bStolen) return false;
	V->InsuranceTier = FMath::Clamp(Tier, -1, 2);
	Changed(NAME_None);
	return true;
}

int64 UMTAGarageSubsystem::ReportWrecked(FName InstanceId, FName District)
{
	FMTAOwnedVehicle* V = Find(InstanceId);
	UMTAEconomySubsystem* Econ = Economy();
	if (!V || !Econ) return 0;
	V->Damage = FMath::Max(V->Damage, 0.95f);
	const int64 Repair = GetRepairCost(InstanceId, District);
	const int64 Tow = Econ->GetTowCost(2.f, District);
	int64 PlayerCost = Repair + Tow;
	if (V->InsuranceTier >= 0)
	{
		const TSharedPtr<FJsonObject> Prices = MTAP::Obj(Econ->GetData(), TEXT("prices"));
		const TArray<TSharedPtr<FJsonValue>>* Cov = MTAP::Arr(Prices, TEXT("insuranceCoverage"));
		const TArray<TSharedPtr<FJsonValue>>* Ded = MTAP::Arr(Prices, TEXT("insuranceDeductible"));
		const double Coverage = (Cov && Cov->IsValidIndex(V->InsuranceTier)) ? (*Cov)[V->InsuranceTier]->AsNumber() : 0.0;
		const int64 Deductible = (Ded && Ded->IsValidIndex(V->InsuranceTier)) ? (int64)(*Ded)[V->InsuranceTier]->AsNumber() : 0;
		PlayerCost = FMath::Max<int64>(Deductible, (int64)((Repair + Tow) * (1.0 - Coverage)));
	}
	Econ->ForceCharge(PlayerCost, EMTATransactionCategory::Insurance, TEXT("Totalschaden: Eigenanteil"), District);
	V->Damage = 0.f;
	V->bInGarage = false;
	// Fahrzeug steht danach in der naechsten Garage oder in der Verwahrstelle
	TowVehicle(InstanceId, 0.f, District);
	return PlayerCost;
}

int64 UMTAGarageSubsystem::TowVehicle(FName InstanceId, float DistanceKm, FName District)
{
	FMTAOwnedVehicle* V = Find(InstanceId);
	UMTAEconomySubsystem* Econ = Economy();
	if (!V) return 0;
	int64 Cost = 0;
	if (DistanceKm > 0.f && Econ)
	{
		Cost = Econ->GetTowCost(DistanceKm, District);
		Econ->ForceCharge(Cost, EMTATransactionCategory::Tow, TEXT("Abschleppdienst"), District);
	}
	// Bevorzugt Garage im selben Bezirk, sonst irgendeine freie
	FName Target = NAME_None;
	const FMTAGarageInfo Local = GetGarage(District);
	if (Local.Level >= 0 && Local.Used < Local.Capacity) { Target = District; }
	else
	{
		for (const FMTAGarageInfo& G : GetAllGarages()) { if (G.Level >= 0 && G.Used < G.Capacity) { Target = G.District; break; } }
	}
	if (Target.IsNone())
	{
		ImpoundVehicle(InstanceId);
	}
	else
	{
		V->bInGarage = true;
		V->District = Target;
		OnVehicleEvent.Broadcast(InstanceId, TEXT("Abgeschleppt in Garage ") + Target.ToString());
	}
	Changed(InstanceId);
	return Cost;
}

void UMTAGarageSubsystem::ImpoundVehicle(FName InstanceId)
{
	if (FMTAOwnedVehicle* V = Find(InstanceId))
	{
		V->bImpounded = true;
		V->bInGarage = false;
		V->ImpoundDays = 0;
		if (ActiveVehicle == InstanceId) { ActiveVehicle = NAME_None; }
		OnVehicleEvent.Broadcast(InstanceId, TEXT("Fahrzeug in der KCG-Verwahrstelle"));
		Changed(InstanceId);
	}
}

int64 UMTAGarageSubsystem::GetImpoundFee(FName InstanceId) const
{
	const FMTAOwnedVehicle* V = Find(InstanceId);
	const UMTAEconomySubsystem* Econ = Economy();
	if (!V || !V->bImpounded || !Econ) return 0;
	return (int64)(Econ->GetPriceParam(TEXT("impoundRelease"), 450.f) + Econ->GetPriceParam(TEXT("impoundPerDay"), 60.f) * V->ImpoundDays);
}

bool UMTAGarageSubsystem::ReleaseFromImpound(FName InstanceId)
{
	FMTAOwnedVehicle* V = Find(InstanceId);
	UMTAEconomySubsystem* Econ = Economy();
	if (!V || !V->bImpounded || !Econ) return false;
	if (V->bStolen) return false; // gestohlene Fahrzeuge bleiben verloren
	if (!Econ->TrySpend(GetImpoundFee(InstanceId), EMTATransactionCategory::Tow, TEXT("Verwahrstelle"), TEXT("core"))) return false;
	V->bImpounded = false;
	V->ImpoundDays = 0;
	V->District = TEXT("core");
	Changed(InstanceId);
	return true;
}

void UMTAGarageSubsystem::HandleDayPassed(int32 Day)
{
	UMTAEconomySubsystem* Econ = Economy();
	const UMTAVehicleCatalog* Cat = Catalog();
	if (!Econ) return;
	int64 Insurance = 0, Maintenance = 0;
	TArray<FName> StolenFromStreet;
	const TSharedPtr<FJsonObject> TheftChance = MTAP::Obj(MTAP::Obj(Econ->GetData(), TEXT("garages")), TEXT("streetParkingStolenChancePerDay"));
	for (FMTAOwnedVehicle& V : Vehicles)
	{
		if (V.InsuranceTier >= 0) { Insurance += GetInsurancePremium(V.InstanceId, V.InsuranceTier); }
		for (const FName& P : V.InstalledParts)
		{
			const FMTATuningPart* Part = Cat ? Cat->FindTuningPart(P) : nullptr;
			if (Part) { Maintenance += Part->MaintenancePerDay; }
		}
		if (V.bImpounded) { ++V.ImpoundDays; }
		// Unbewacht geparkte Fahrzeuge koennen gestohlen werden (nicht das aktive)
		if (!V.bInGarage && !V.bImpounded && V.InstanceId != ActiveVehicle && Rng.FRand() < (float)MTAP::Num(TheftChance, V.District.ToString(), 0.0))
		{
			StolenFromStreet.Add(V.InstanceId);
		}
		V.PlateHeat = FMath::Max(0.f, V.PlateHeat - 2.f); // Kennzeichen geraten langsam in Vergessenheit
	}
	if (Insurance > 0) { Econ->ForceCharge(Insurance, EMTATransactionCategory::Insurance, TEXT("Versicherungspraemien"), NAME_None); }
	if (Maintenance > 0) { Econ->ForceCharge(Maintenance, EMTATransactionCategory::Tuning, TEXT("Modul-Wartung"), NAME_None); }
	for (const auto& G : GarageLevels)
	{
		const int64 Rent = GetGarage(G.Key).RentPerDay;
		if (Rent > 0) { Econ->ForceCharge(Rent, EMTATransactionCategory::Garage, TEXT("Garagenmiete ") + G.Key.ToString(), G.Key); }
	}
	for (const FName& Id : StolenFromStreet)
	{
		FMTAOwnedVehicle* V = Find(Id);
		if (V && V->InsuranceTier >= 1)
		{
			// Versicherung ersetzt den Zeitwert anteilig
			Econ->AddMoney((int64)(GetVehicleValue(Id) * 0.6), EMTATransactionCategory::Insurance, TEXT("Diebstahl: Versicherungsleistung"), V->District);
		}
		OnVehicleEvent.Broadcast(Id, TEXT("Auf der Strasse gestohlen"));
		Vehicles.RemoveAll([Id](const FMTAOwnedVehicle& X) { return X.InstanceId == Id; });
	}
	Changed(NAME_None);
}

// ---------------------------------------------------------------------------------------------
// Werte
// ---------------------------------------------------------------------------------------------

FMTAVehicleStats UMTAGarageSubsystem::ComputeStats(FName InstanceId) const
{
	const FMTAOwnedVehicle* V = Find(InstanceId);
	const UMTAVehicleCatalog* Cat = Catalog();
	if (!V || !Cat) return FMTAVehicleStats();
	FMTAVehicleStats S = Cat->GetBaseStats(V->ModelId);
	// 1) Tuning-Teile
	for (const FName& P : V->InstalledParts)
	{
		if (const FMTATuningPart* Part = Cat->FindTuningPart(P))
		{
			for (const FMTAStatModifier& M : Part->Effects) { S.ApplyModifier(M); }
		}
	}
	// 2) Skills, Herkunft, Ruf (alle "vehicle.*"-Modifikatoren)
	if (const UMTAProgressionSubsystem* Prog = Progression())
	{
		for (const FMTAStatModifier& M : Prog->GetAllModifiers())
		{
			if (M.Stat.ToString().StartsWith(TEXT("vehicle."))) { S.ApplyModifier(M); }
		}
	}
	// 3) Schaden: weniger Leistung und Grip ab 30 % Schaden
	const float DamagePenalty = FMath::Clamp((V->Damage - 0.3f) / 0.7f, 0.f, 1.f);
	S.MaxTorqueNm *= 1.f - 0.35f * DamagePenalty;
	S.EnginePowerKw *= 1.f - 0.35f * DamagePenalty;
	S.Grip *= 1.f - 0.15f * DamagePenalty;
	S.TopSpeedKmh *= 1.f - 0.25f * DamagePenalty;
	// 4) Heisses Kennzeichen ist auffaelliger
	if (V->bHot) { S.AegisVisibility += 0.25f; }
	S.AegisVisibility += V->PlateHeat * 0.002f;
	S.Finalize();
	return S;
}

// ---------------------------------------------------------------------------------------------
// Persistenz
// ---------------------------------------------------------------------------------------------

TSharedRef<FJsonObject> UMTAGarageSubsystem::ToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema"), SchemaVersion);
	Root->SetNumberField(TEXT("next"), NextInstance);
	Root->SetStringField(TEXT("active"), ActiveVehicle.ToString());
	TMap<FName, float> Levels;
	for (const auto& P : GarageLevels) { Levels.Add(P.Key, (float)P.Value); }
	Root->SetObjectField(TEXT("garages"), MTAP::WriteNumberMap(Levels));
	TArray<TSharedPtr<FJsonValue>> Arr0;
	for (const FMTAOwnedVehicle& V : Vehicles)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), V.InstanceId.ToString());
		O->SetStringField(TEXT("def"), V.ModelId.ToString());
		O->SetStringField(TEXT("plate"), V.Plate);
		O->SetNumberField(TEXT("plateHeat"), V.PlateHeat);
		O->SetBoolField(TEXT("stolen"), V.bStolen);
		O->SetBoolField(TEXT("hot"), V.bHot);
		O->SetNumberField(TEXT("damage"), V.Damage);
		O->SetNumberField(TEXT("fuel"), V.Fuel);
		O->SetArrayField(TEXT("tuning"), MTAP::WriteNameArray(V.InstalledParts));
		O->SetStringField(TEXT("paint"), V.PaintColor.ToFColor(true).ToHex());
		O->SetNumberField(TEXT("insurance"), V.InsuranceTier);
		O->SetStringField(TEXT("district"), V.District.ToString());
		O->SetBoolField(TEXT("inGarage"), V.bInGarage);
		TArray<TSharedPtr<FJsonValue>> Pos;
		Pos.Add(MakeShared<FJsonValueNumber>(V.ParkedLocation.X));
		Pos.Add(MakeShared<FJsonValueNumber>(V.ParkedLocation.Y));
		Pos.Add(MakeShared<FJsonValueNumber>(V.ParkedLocation.Z));
		O->SetArrayField(TEXT("pos"), Pos);
		O->SetNumberField(TEXT("yaw"), V.ParkedYaw);
		O->SetBoolField(TEXT("impounded"), V.bImpounded);
		O->SetNumberField(TEXT("impoundDays"), V.ImpoundDays);
		O->SetNumberField(TEXT("km"), V.OdometerKm);
		O->SetNumberField(TEXT("paid"), (double)V.PurchasePrice);
		Arr0.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("vehicles"), Arr0);
	return Root;
}

bool UMTAGarageSubsystem::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid()) return false;
	const int32 Schema = (int32)MTAP::Num(Json, TEXT("schema"), 0.0);
	if (Schema < 1 || Schema > SchemaVersion) return false;
	Vehicles.Reset();
	GarageLevels.Reset();
	NextInstance = (int32)MTAP::Num(Json, TEXT("next"), 1.0);
	ActiveVehicle = FName(*MTAP::Str(Json, TEXT("active"), TEXT("None")));
	for (const auto& P : MTAP::ReadNumberMap(MTAP::Obj(Json, TEXT("garages")))) { GarageLevels.Add(P.Key, (int32)P.Value); }
	const UMTAVehicleCatalog* Cat = Catalog();
	if (const TArray<TSharedPtr<FJsonValue>>* A = MTAP::Arr(Json, TEXT("vehicles")))
	{
		for (const TSharedPtr<FJsonValue>& Val : *A)
		{
			const TSharedPtr<FJsonObject> O = Val->AsObject();
			FMTAOwnedVehicle V;
			V.InstanceId = FName(*MTAP::Str(O, TEXT("id")));
			V.ModelId = FName(*MTAP::Str(O, TEXT("def")));
			if (Cat && !Cat->FindModel(V.ModelId))
			{
				UE_LOG(LogTemp, Warning, TEXT("MTA6 Garage: Modell %s nicht mehr im Katalog, Fahrzeug uebersprungen"), *V.ModelId.ToString());
				continue;
			}
			V.Plate = MTAP::Str(O, TEXT("plate"));
			V.PlateHeat = (float)MTAP::Num(O, TEXT("plateHeat"), 0.0);
			V.bStolen = MTAP::Bool(O, TEXT("stolen"), false);
			V.bHot = MTAP::Bool(O, TEXT("hot"), false);
			V.Damage = (float)MTAP::Num(O, TEXT("damage"), 0.0);
			V.Fuel = (float)MTAP::Num(O, TEXT("fuel"), 1.0);
			V.InstalledParts = MTAP::ReadNameArray(MTAP::Arr(O, TEXT("tuning")));
			V.PaintColor = FLinearColor(FColor::FromHex(MTAP::Str(O, TEXT("paint"), TEXT("808080FF"))));
			V.InsuranceTier = (int32)MTAP::Num(O, TEXT("insurance"), -1.0);
			V.District = FName(*MTAP::Str(O, TEXT("district")));
			V.bInGarage = MTAP::Bool(O, TEXT("inGarage"), false);
			if (const TArray<TSharedPtr<FJsonValue>>* Pos = MTAP::Arr(O, TEXT("pos")))
			{
				if (Pos->Num() == 3) { V.ParkedLocation = FVector((*Pos)[0]->AsNumber(), (*Pos)[1]->AsNumber(), (*Pos)[2]->AsNumber()); }
			}
			V.ParkedYaw = (float)MTAP::Num(O, TEXT("yaw"), 0.0);
			V.bImpounded = MTAP::Bool(O, TEXT("impounded"), false);
			V.ImpoundDays = (int32)MTAP::Num(O, TEXT("impoundDays"), 0.0);
			V.OdometerKm = (float)MTAP::Num(O, TEXT("km"), 0.0);
			V.PurchasePrice = (int64)MTAP::Num(O, TEXT("paid"), 0.0);
			Vehicles.Add(V);
		}
	}
	if (!Find(ActiveVehicle)) { ActiveVehicle = NAME_None; }
	Changed(ActiveVehicle);
	return true;
}

FString UMTAGarageSubsystem::SaveStateToString() const { return MTAP::JsonToString(ToJson()); }
bool UMTAGarageSubsystem::LoadStateFromString(const FString& Json) { return FromJson(MTAP::JsonFromString(Json)); }
