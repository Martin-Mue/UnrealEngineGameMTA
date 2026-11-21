#include "MTAInventorySubsystem.h"
#include "MTAEconomySubsystem.h"
#include "MTAGarageSubsystem.h"
#include "MTAProgressionSubsystem.h"
#include "Engine/GameInstance.h"

namespace MTAP = MTAProgression;

namespace
{
	EMTAItemCategory MTAItemCategoryFromString(const FString& S)
	{
		if (S == TEXT("hackdongle")) return EMTAItemCategory::HackDongle;
		if (S == TEXT("plate")) return EMTAItemCategory::Plate;
		if (S == TEXT("evidence")) return EMTAItemCategory::Evidence;
		if (S == TEXT("contraband")) return EMTAItemCategory::Contraband;
		if (S == TEXT("repairkit")) return EMTAItemCategory::RepairKit;
		if (S == TEXT("consumable")) return EMTAItemCategory::Consumable;
		if (S == TEXT("key")) return EMTAItemCategory::Key;
		return EMTAItemCategory::Tool;
	}
}

void UMTAInventorySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMTAEconomySubsystem>();
	Collection.InitializeDependency<UMTAGarageSubsystem>();
	Rng.Initialize(911);
	LoadItemDefs();
	ResetInventory();
}

void UMTAInventorySubsystem::LoadItemDefs()
{
	Defs.Reset();
	DefOrder.Reset();
	const UGameInstance* GI = GetGameInstance();
	const UMTAEconomySubsystem* Econ = GI ? GI->GetSubsystem<UMTAEconomySubsystem>() : nullptr;
	const TSharedPtr<FJsonObject> Items = Econ ? MTAP::Obj(Econ->GetData(), TEXT("items")) : nullptr;
	if (!Items.IsValid()) return;
	for (const auto& P : Items->Values)
	{
		const TSharedPtr<FJsonObject> O = P.Value->AsObject();
		FMTAItemDef D;
		D.Id = FName(*P.Key);
		D.Name = MTAP::Str(O, TEXT("name"), D.Id.ToString());
		D.Category = MTAItemCategoryFromString(MTAP::Str(O, TEXT("category")));
		D.WeightKg = (float)MTAP::Num(O, TEXT("kg"), 0.0);
		D.StackMax = FMath::Max(1, (int32)MTAP::Num(O, TEXT("stack"), 1.0));
		D.BasePrice = (int64)MTAP::Num(O, TEXT("price"), 0.0);
		D.bLegal = MTAP::Bool(O, TEXT("legal"), true);
		D.Risk = (float)MTAP::Num(O, TEXT("risk"), D.bLegal ? 0.0 : 0.5);
		D.bStoryItem = MTAP::Bool(O, TEXT("story"), false);
		D.Charges = (int32)MTAP::Num(O, TEXT("charges"), 0.0);
		D.ScarcityCategory = FName(*MTAP::Str(O, TEXT("scarcity")));
		if (const TSharedPtr<FJsonObject> Use = MTAP::Obj(O, TEXT("use")))
		{
			D.UseTag = FName(*MTAP::Str(Use, TEXT("tag")));
			D.UseRepair = (float)MTAP::Num(Use, TEXT("repair"), 0.0);
			D.UseFuel = (float)MTAP::Num(Use, TEXT("fuel"), 0.0);
			D.bUsePlateSwap = MTAP::Bool(Use, TEXT("plateSwap"), false);
			if (const TArray<TSharedPtr<FJsonValue>>* Tx = MTAP::Arr(Use, TEXT("treeXp")))
			{
				if (Tx->Num() == 2)
				{
					D.UseTreeXpTree = FName(*(*Tx)[0]->AsString());
					D.UseTreeXpAmount = (float)(*Tx)[1]->AsNumber();
				}
			}
		}
		DefOrder.Add(D.Id);
		Defs.Add(D.Id, D);
	}
}

void UMTAInventorySubsystem::ResetInventory()
{
	Personal.Reset();
	Trunks.Reset();
	SlotUpgradeLevel = 0;
	PursuitPeakHeat = 0.f;
	OnInventoryChanged.Broadcast();
}

bool UMTAInventorySubsystem::GetItemDef(FName ItemId, FMTAItemDef& Out) const
{
	if (const FMTAItemDef* D = Defs.Find(ItemId)) { Out = *D; return true; }
	return false;
}

TArray<FMTAItemDef> UMTAInventorySubsystem::GetAllItemDefs() const
{
	TArray<FMTAItemDef> Result;
	for (const FName& Id : DefOrder) { Result.Add(Defs.FindChecked(Id)); }
	return Result;
}

// ---------------------------------------------------------------------------------------------
// Container
// ---------------------------------------------------------------------------------------------

FName UMTAInventorySubsystem::ActiveVehicleId() const
{
	const UGameInstance* GI = GetGameInstance();
	const UMTAGarageSubsystem* Garage = GI ? GI->GetSubsystem<UMTAGarageSubsystem>() : nullptr;
	return Garage ? Garage->GetActiveVehicleId() : NAME_None;
}

TArray<FMTAItemStack>* UMTAInventorySubsystem::GetContainer(EMTAInventoryContainer Container)
{
	if (Container == EMTAInventoryContainer::Personal) return &Personal;
	const FName Veh = ActiveVehicleId();
	return Veh.IsNone() ? nullptr : &Trunks.FindOrAdd(Veh);
}

const TArray<FMTAItemStack>* UMTAInventorySubsystem::GetContainer(EMTAInventoryContainer Container) const
{
	if (Container == EMTAInventoryContainer::Personal) return &Personal;
	return Trunks.Find(ActiveVehicleId());
}

float UMTAInventorySubsystem::Stat(FName StatName, float Base) const
{
	const UGameInstance* GI = GetGameInstance();
	const UMTAProgressionSubsystem* P = GI ? GI->GetSubsystem<UMTAProgressionSubsystem>() : nullptr;
	return P ? P->ApplyStat(StatName, Base) : Base;
}

int32 UMTAInventorySubsystem::SlotsFor(const FMTAItemDef& Def, int32 Count)
{
	return Count <= 0 ? 0 : (Count + Def.StackMax - 1) / Def.StackMax;
}

int32 UMTAInventorySubsystem::GetUsedSlots(EMTAInventoryContainer Container) const
{
	int32 N = 0;
	if (const TArray<FMTAItemStack>* C = GetContainer(Container))
	{
		for (const FMTAItemStack& S : *C) { if (const FMTAItemDef* D = Defs.Find(S.ItemId)) N += SlotsFor(*D, S.Count); }
	}
	return N;
}

int32 UMTAInventorySubsystem::GetMaxSlots(EMTAInventoryContainer Container) const
{
	if (Container == EMTAInventoryContainer::Trunk)
	{
		const UGameInstance* GI = GetGameInstance();
		const UMTAGarageSubsystem* Garage = GI ? GI->GetSubsystem<UMTAGarageSubsystem>() : nullptr;
		return (Garage && !ActiveVehicleId().IsNone()) ? Garage->GetActiveVehicleStats().CargoSlots : 0;
	}
	const UGameInstance* GI = GetGameInstance();
	const UMTAEconomySubsystem* Econ = GI ? GI->GetSubsystem<UMTAEconomySubsystem>() : nullptr;
	const TSharedPtr<FJsonObject> Inv = Econ ? MTAP::Obj(Econ->GetData(), TEXT("inventory")) : nullptr;
	int32 Base = (int32)MTAP::Num(Inv, TEXT("personalSlots"), 12.0);
	if (const TArray<TSharedPtr<FJsonValue>>* Up = MTAP::Arr(Inv, TEXT("slotUpgrades")))
	{
		if (Up->Num() > 0) { Base = (int32)(*Up)[FMath::Clamp(SlotUpgradeLevel, 0, Up->Num() - 1)]->AsNumber(); }
	}
	return FMath::RoundToInt(Stat(TEXT("inv.slots"), (float)Base));
}

float UMTAInventorySubsystem::GetWeight(EMTAInventoryContainer Container) const
{
	float W = 0.f;
	if (const TArray<FMTAItemStack>* C = GetContainer(Container))
	{
		for (const FMTAItemStack& S : *C) { if (const FMTAItemDef* D = Defs.Find(S.ItemId)) W += D->WeightKg * S.Count; }
	}
	return W;
}

float UMTAInventorySubsystem::GetMaxWeight(EMTAInventoryContainer Container) const
{
	const UGameInstance* GI = GetGameInstance();
	if (Container == EMTAInventoryContainer::Trunk)
	{
		const UMTAGarageSubsystem* Garage = GI ? GI->GetSubsystem<UMTAGarageSubsystem>() : nullptr;
		return (Garage && !ActiveVehicleId().IsNone()) ? Garage->GetActiveVehicleStats().CargoKg : 0.f;
	}
	const UMTAEconomySubsystem* Econ = GI ? GI->GetSubsystem<UMTAEconomySubsystem>() : nullptr;
	return Stat(TEXT("inv.weight"), (float)MTAP::Num(Econ ? MTAP::Obj(Econ->GetData(), TEXT("inventory")) : nullptr, TEXT("personalKg"), 25.0));
}

void UMTAInventorySubsystem::SetSlotUpgradeLevel(int32 InLevel)
{
	SlotUpgradeLevel = FMath::Clamp(InLevel, 0, 2);
	OnInventoryChanged.Broadcast();
}

bool UMTAInventorySubsystem::CanAdd(FName ItemId, int32 Count, EMTAInventoryContainer Container, FString& OutReason) const
{
	const FMTAItemDef* D = Defs.Find(ItemId);
	if (!D || Count <= 0) { OutReason = TEXT("Unbekannter Gegenstand"); return false; }
	if (Container == EMTAInventoryContainer::Trunk && ActiveVehicleId().IsNone()) { OutReason = TEXT("Kein Fahrzeug"); return false; }
	const int32 Have = GetCountIn(ItemId, Container);
	const int32 ExtraSlots = SlotsFor(*D, Have + Count) - SlotsFor(*D, Have);
	if (GetUsedSlots(Container) + ExtraSlots > GetMaxSlots(Container)) { OutReason = TEXT("Kein Platz"); return false; }
	if (GetWeight(Container) + D->WeightKg * Count > GetMaxWeight(Container) + UE_KINDA_SMALL_NUMBER) { OutReason = TEXT("Zu schwer"); return false; }
	OutReason.Reset();
	return true;
}

int32 UMTAInventorySubsystem::AddItem(FName ItemId, int32 Count, EMTAInventoryContainer Container)
{
	const FMTAItemDef* D = Defs.Find(ItemId);
	TArray<FMTAItemStack>* C = GetContainer(Container);
	if (!D || !C || Count <= 0) return 0;
	// Story-Gegenstaende passen immer (keine Sackgassen)
	int32 Added = 0;
	FString Reason;
	if (D->bStoryItem) { Added = Count; }
	while (Added < Count && CanAdd(ItemId, Added + 1, Container, Reason)) { ++Added; }
	if (Added <= 0) return 0;
	FMTAItemStack* S = C->FindByPredicate([ItemId](const FMTAItemStack& X) { return X.ItemId == ItemId; });
	if (!S)
	{
		FMTAItemStack New;
		New.ItemId = ItemId;
		New.Container = Container;
		New.Charges = D->Charges;
		C->Add(New);
		S = &C->Last();
	}
	S->Count += Added;
	OnInventoryChanged.Broadcast();
	return Added;
}

int32 UMTAInventorySubsystem::RemoveItem(FName ItemId, int32 Count, EMTAInventoryContainer Container)
{
	TArray<FMTAItemStack>* C = GetContainer(Container);
	if (!C || Count <= 0) return 0;
	const int32 Idx = C->IndexOfByPredicate([ItemId](const FMTAItemStack& X) { return X.ItemId == ItemId; });
	if (Idx == INDEX_NONE) return 0;
	const int32 Removed = FMath::Min(Count, (*C)[Idx].Count);
	(*C)[Idx].Count -= Removed;
	if ((*C)[Idx].Count <= 0) { C->RemoveAt(Idx); }
	OnInventoryChanged.Broadcast();
	return Removed;
}

int32 UMTAInventorySubsystem::RemoveFromAny(FName ItemId, int32 Count)
{
	int32 Removed = RemoveItem(ItemId, Count, EMTAInventoryContainer::Personal);
	if (Removed < Count) { Removed += RemoveItem(ItemId, Count - Removed, EMTAInventoryContainer::Trunk); }
	return Removed;
}

int32 UMTAInventorySubsystem::MoveItem(FName ItemId, int32 Count, EMTAInventoryContainer From, EMTAInventoryContainer To)
{
	if (From == To) return 0;
	const int32 Avail = FMath::Min(Count, GetCountIn(ItemId, From));
	int32 Moved = 0;
	FString Reason;
	while (Moved < Avail && CanAdd(ItemId, Moved + 1, To, Reason)) { ++Moved; }
	if (Moved <= 0) return 0;
	RemoveItem(ItemId, Moved, From);
	return AddItem(ItemId, Moved, To);
}

int32 UMTAInventorySubsystem::GetCountIn(FName ItemId, EMTAInventoryContainer Container) const
{
	const TArray<FMTAItemStack>* C = GetContainer(Container);
	if (!C) return 0;
	const FMTAItemStack* S = C->FindByPredicate([ItemId](const FMTAItemStack& X) { return X.ItemId == ItemId; });
	return S ? S->Count : 0;
}

int32 UMTAInventorySubsystem::GetItemCount(FName ItemId) const
{
	return GetCountIn(ItemId, EMTAInventoryContainer::Personal) + GetCountIn(ItemId, EMTAInventoryContainer::Trunk);
}

TArray<FMTAItemStack> UMTAInventorySubsystem::GetItems(EMTAInventoryContainer Container) const
{
	const TArray<FMTAItemStack>* C = GetContainer(Container);
	return C ? *C : TArray<FMTAItemStack>();
}

// ---------------------------------------------------------------------------------------------
// Nutzung und Handel
// ---------------------------------------------------------------------------------------------

bool UMTAInventorySubsystem::UseItem(FName ItemId, FName District, FString& OutMessage)
{
	const FMTAItemDef* D = Defs.Find(ItemId);
	if (!D || GetItemCount(ItemId) <= 0) { OutMessage = TEXT("Nicht vorhanden"); return false; }
	UGameInstance* GI = GetGameInstance();
	UMTAGarageSubsystem* Garage = GI ? GI->GetSubsystem<UMTAGarageSubsystem>() : nullptr;
	UMTAProgressionSubsystem* Prog = GI ? GI->GetSubsystem<UMTAProgressionSubsystem>() : nullptr;
	const FName Veh = ActiveVehicleId();

	if (D->UseRepair > 0.f)
	{
		if (!Garage || Veh.IsNone()) { OutMessage = TEXT("Kein Fahrzeug"); return false; }
		Garage->RepairByFraction(Veh, Stat(TEXT("item.repairKit"), D->UseRepair));
		RemoveFromAny(ItemId, 1);
		OutMessage = TEXT("Fahrzeug notduerftig repariert");
		return true;
	}
	if (D->UseFuel > 0.f)
	{
		if (!Garage || Veh.IsNone()) { OutMessage = TEXT("Kein Fahrzeug"); return false; }
		Garage->Refuel(Veh, D->UseFuel, District, true);
		RemoveFromAny(ItemId, 1);
		OutMessage = TEXT("Nachgetankt");
		return true;
	}
	if (D->bUsePlateSwap)
	{
		// Kennzeichentausch am Strassenrand: nutzt den Garagen-Ablauf mit Schwarzmarkt-Wirkung, Gegenstand wird dort verbraucht.
		if (!Garage || Veh.IsNone()) { OutMessage = TEXT("Kein Fahrzeug"); return false; }
		return Garage->ChangePlate(Veh, true, TEXT("rustline"), OutMessage);
	}
	if (D->Category == EMTAItemCategory::HackDongle)
	{
		// Eine Ladung verbrauchen; leerer Dongle verschwindet.
		for (EMTAInventoryContainer C : { EMTAInventoryContainer::Personal, EMTAInventoryContainer::Trunk })
		{
			TArray<FMTAItemStack>* Stacks = GetContainer(C);
			FMTAItemStack* S = Stacks ? Stacks->FindByPredicate([ItemId](const FMTAItemStack& X) { return X.ItemId == ItemId; }) : nullptr;
			if (!S) continue;
			S->Charges -= 1;
			if (S->Charges <= 0)
			{
				RemoveItem(ItemId, 1, C);
				if (FMTAItemStack* Rest = Stacks->FindByPredicate([ItemId](const FMTAItemStack& X) { return X.ItemId == ItemId; })) { Rest->Charges = D->Charges; }
			}
			break;
		}
		if (Prog && !D->UseTreeXpTree.IsNone()) { Prog->AddTreeXp(D->UseTreeXpTree, D->UseTreeXpAmount); }
		// Spuren im Aegis-Netz (Skill "Saubere Spur" senkt sie)
		MTAP::AddStoryAegis(GI, Stat(TEXT("tech.hackTrace"), 1.5f));
		OutMessage = D->UseTag.ToString();
		OnInventoryChanged.Broadcast();
		return true;
	}
	OutMessage = TEXT("Nicht benutzbar");
	return false;
}

int64 UMTAInventorySubsystem::GetBuyPrice(FName ItemId, FName District) const
{
	const FMTAItemDef* D = Defs.Find(ItemId);
	const UGameInstance* GI = GetGameInstance();
	const UMTAEconomySubsystem* Econ = GI ? GI->GetSubsystem<UMTAEconomySubsystem>() : nullptr;
	if (!D || !Econ || D->bStoryItem || D->BasePrice <= 0) return 0;
	if (D->bLegal) return Econ->GetPrice(D->BasePrice, District, D->ScarcityCategory);
	const int64 BM = Econ->GetBlackMarketBuyPrice(ItemId, District);
	if (BM > 0) return BM;
	// Illegale Werkzeuge ohne eigenen Kurs: Basispreis mit Schwarzmarkt-Aufschlag
	return Econ->IsBlackMarketAvailable(District) ? Econ->GetPrice((int64)(D->BasePrice * 1.2), District, D->ScarcityCategory) : 0;
}

int64 UMTAInventorySubsystem::GetSellPrice(FName ItemId, FName District) const
{
	const FMTAItemDef* D = Defs.Find(ItemId);
	const UGameInstance* GI = GetGameInstance();
	const UMTAEconomySubsystem* Econ = GI ? GI->GetSubsystem<UMTAEconomySubsystem>() : nullptr;
	if (!D || !Econ || D->bStoryItem || D->BasePrice <= 0 || D->Category == EMTAItemCategory::Evidence) return 0;
	if (D->bLegal) return Econ->GetSellPrice(D->BasePrice, District, D->ScarcityCategory, 0.45f);
	const int64 BM = Econ->GetBlackMarketSellPrice(ItemId, District);
	if (BM > 0) return BM;
	return Econ->IsBlackMarketAvailable(District) ? (int64)(D->BasePrice * 0.5) : 0;
}

bool UMTAInventorySubsystem::BuyItem(FName ItemId, int32 Count, FName District, EMTAInventoryContainer Container, FString& OutReason)
{
	UGameInstance* GI = GetGameInstance();
	UMTAEconomySubsystem* Econ = GI ? GI->GetSubsystem<UMTAEconomySubsystem>() : nullptr;
	const FMTAItemDef* D = Defs.Find(ItemId);
	const int64 Unit = GetBuyPrice(ItemId, District);
	if (!Econ || !D || Unit <= 0) { OutReason = TEXT("Hier nicht erhaeltlich"); return false; }
	if (!CanAdd(ItemId, Count, Container, OutReason)) return false;
	const bool bBlack = !D->bLegal;
	if (!Econ->TrySpend(Unit * Count, bBlack ? EMTATransactionCategory::BlackMarket : EMTATransactionCategory::Purchase, D->Name, District))
	{
		OutReason = TEXT("Zu wenig Geld");
		return false;
	}
	AddItem(ItemId, Count, Container);
	if (bBlack) { MTAP::AddStoryAegis(GI, Econ->GetBlackMarketAegisRisk()); }
	OutReason.Reset();
	return true;
}

int64 UMTAInventorySubsystem::SellItem(FName ItemId, int32 Count, FName District)
{
	UGameInstance* GI = GetGameInstance();
	UMTAEconomySubsystem* Econ = GI ? GI->GetSubsystem<UMTAEconomySubsystem>() : nullptr;
	const FMTAItemDef* D = Defs.Find(ItemId);
	const int64 Unit = GetSellPrice(ItemId, District);
	if (!Econ || !D || Unit <= 0) return 0;
	const int32 Sold = RemoveFromAny(ItemId, Count);
	const int64 Total = Unit * Sold;
	Econ->AddMoney(Total, D->bLegal ? EMTATransactionCategory::Sale : EMTATransactionCategory::BlackMarket, D->Name, District);
	if (!D->bLegal && Sold > 0)
	{
		MTAP::AddStoryAegis(GI, Econ->GetBlackMarketAegisRisk());
		if (UMTAProgressionSubsystem* Prog = GI->GetSubsystem<UMTAProgressionSubsystem>()) { Prog->AwardXpFromSource(TEXT("deal_closed")); }
	}
	return Total;
}

// ---------------------------------------------------------------------------------------------
// Gesetz
// ---------------------------------------------------------------------------------------------

float UMTAInventorySubsystem::GetContrabandExposure() const
{
	float R = 0.f;
	for (EMTAInventoryContainer C : { EMTAInventoryContainer::Personal, EMTAInventoryContainer::Trunk })
	{
		if (const TArray<FMTAItemStack>* Stacks = GetContainer(C))
		{
			for (const FMTAItemStack& S : *Stacks)
			{
				const FMTAItemDef* D = Defs.Find(S.ItemId);
				if (D && !D->bLegal) { R += D->Risk * S.Count; }
			}
		}
	}
	return R;
}

FMTAConfiscationReport UMTAInventorySubsystem::ProcessArrest(FName District, int32 Stars)
{
	FMTAConfiscationReport Rep;
	Rep.District = District;
	Rep.Stars = Stars;
	UGameInstance* GI = GetGameInstance();
	UMTAEconomySubsystem* Econ = GI ? GI->GetSubsystem<UMTAEconomySubsystem>() : nullptr;
	UMTAGarageSubsystem* Garage = GI ? GI->GetSubsystem<UMTAGarageSubsystem>() : nullptr;
	const TSharedPtr<FJsonObject> Conf = Econ ? MTAP::Obj(MTAP::Obj(Econ->GetData(), TEXT("inventory")), TEXT("confiscation")) : nullptr;
	const float Base = (float)MTAP::Num(Conf, TEXT("baseChance"), 0.35) + (float)MTAP::Num(Conf, TEXT("perStar"), 0.12) * Stars;
	const float TrunkMul = (float)MTAP::Num(Conf, TEXT("trunkMul"), 1.25);
	const bool bProtectEvidence = MTAP::Bool(Conf, TEXT("evidenceProtected"), true);

	if (Econ) { Rep.ArrestFine = Econ->ChargeArrest(Stars, District); }

	for (EMTAInventoryContainer C : { EMTAInventoryContainer::Personal, EMTAInventoryContainer::Trunk })
	{
		TArray<FMTAItemStack>* Stacks = GetContainer(C);
		if (!Stacks) continue;
		for (int32 I = Stacks->Num() - 1; I >= 0; --I)
		{
			FMTAItemStack& S = (*Stacks)[I];
			const FMTAItemDef* D = Defs.Find(S.ItemId);
			if (!D || D->bLegal || D->bStoryItem) continue;
			if (bProtectEvidence && D->Category == EMTAItemCategory::Evidence && D->bStoryItem) continue;
			float Chance = Base * FMath::Max(0.1f, D->Risk);
			if (C == EMTAInventoryContainer::Trunk) { Chance = Stat(TEXT("inv.trunkHide"), Chance * TrunkMul); }
			Chance = FMath::Clamp(Stat(TEXT("inv.confiscateChance"), Chance), 0.f, 1.f);
			int32 Lost = 0;
			for (int32 U = 0; U < S.Count; ++U) { if (Rng.FRand() < Chance) ++Lost; }
			if (Lost <= 0) continue;
			FMTAItemStack Gone = S;
			Gone.Count = Lost;
			Rep.Confiscated.Add(Gone);
			Rep.ValueLost += D->BasePrice * Lost;
			S.Count -= Lost;
			if (S.Count <= 0) { Stacks->RemoveAt(I); }
		}
	}
	if (Econ && Rep.ValueLost > 0)
	{
		Rep.ContrabandFine = (int64)(Rep.ValueLost * MTAP::Num(Conf, TEXT("fineOnContrabandPct"), 0.5));
		Econ->ForceCharge(Rep.ContrabandFine, EMTATransactionCategory::Confiscation, TEXT("Strafe fuer Schmuggelware"), District);
	}
	// Ab 3 Sternen oder mit heissem Fahrzeug: Verwahrstelle
	if (Garage)
	{
		const FName Veh = Garage->GetActiveVehicleId();
		FMTAOwnedVehicle V;
		if (!Veh.IsNone() && Garage->GetVehicle(Veh, V) && (Stars >= 3 || V.bHot))
		{
			Garage->ImpoundVehicle(Veh);
			Rep.bVehicleImpounded = true;
		}
	}
	PursuitPeakHeat = 0.f;
	OnInventoryChanged.Broadcast();
	OnItemsConfiscated.Broadcast(Rep);
	return Rep;
}

void UMTAInventorySubsystem::HandleArrested()
{
	ProcessArrest(CurrentDistrict, FMath::Clamp(FMath::CeilToInt(PursuitPeakHeat), 1, 5));
}

void UMTAInventorySubsystem::HandleHeatChanged(float Heat)
{
	PursuitPeakHeat = Heat <= 0.f ? 0.f : FMath::Max(PursuitPeakHeat, Heat);
}

void UMTAInventorySubsystem::HandleWantedLevelChanged(int32 Stars)
{
	HandleHeatChanged((float)Stars);
}

// ---------------------------------------------------------------------------------------------
// Persistenz
// ---------------------------------------------------------------------------------------------

TArray<TSharedPtr<FJsonValue>> UMTAInventorySubsystem::WriteStacks(const TArray<FMTAItemStack>& Stacks)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FMTAItemStack& S : Stacks)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), S.ItemId.ToString());
		O->SetNumberField(TEXT("count"), S.Count);
		O->SetNumberField(TEXT("charges"), S.Charges);
		Out.Add(MakeShared<FJsonValueObject>(O));
	}
	return Out;
}

TArray<FMTAItemStack> UMTAInventorySubsystem::ReadStacks(const TArray<TSharedPtr<FJsonValue>>* Array, EMTAInventoryContainer Container) const
{
	TArray<FMTAItemStack> Out;
	if (!Array) return Out;
	for (const TSharedPtr<FJsonValue>& V : *Array)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FMTAItemStack S;
		S.ItemId = FName(*MTAP::Str(O, TEXT("id")));
		S.Count = (int32)MTAP::Num(O, TEXT("count"), 0.0);
		S.Charges = (int32)MTAP::Num(O, TEXT("charges"), 0.0);
		S.Container = Container;
		if (S.Count > 0 && Defs.Contains(S.ItemId)) { Out.Add(S); } // entfernte Gegenstaende fallen weg
	}
	return Out;
}

TSharedRef<FJsonObject> UMTAInventorySubsystem::ToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema"), SchemaVersion);
	Root->SetNumberField(TEXT("slotLevel"), SlotUpgradeLevel);
	Root->SetStringField(TEXT("district"), CurrentDistrict.ToString());
	Root->SetArrayField(TEXT("personal"), WriteStacks(Personal));
	TSharedRef<FJsonObject> T = MakeShared<FJsonObject>();
	for (const auto& P : Trunks) { if (P.Value.Num() > 0) T->SetArrayField(P.Key.ToString(), WriteStacks(P.Value)); }
	Root->SetObjectField(TEXT("trunks"), T);
	return Root;
}

bool UMTAInventorySubsystem::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	if (!Json.IsValid()) return false;
	const int32 Schema = (int32)MTAP::Num(Json, TEXT("schema"), 0.0);
	if (Schema < 1 || Schema > SchemaVersion) return false;
	ResetInventory();
	SlotUpgradeLevel = (int32)MTAP::Num(Json, TEXT("slotLevel"), 0.0);
	CurrentDistrict = FName(*MTAP::Str(Json, TEXT("district"), TEXT("core")));
	Personal = ReadStacks(MTAP::Arr(Json, TEXT("personal")), EMTAInventoryContainer::Personal);
	if (const TSharedPtr<FJsonObject> T = MTAP::Obj(Json, TEXT("trunks")))
	{
		for (const auto& P : T->Values)
		{
			const TArray<TSharedPtr<FJsonValue>>& A = P.Value->AsArray();
			Trunks.Add(FName(*P.Key), ReadStacks(&A, EMTAInventoryContainer::Trunk));
		}
	}
	OnInventoryChanged.Broadcast();
	return true;
}

FString UMTAInventorySubsystem::SaveStateToString() const { return MTAP::JsonToString(ToJson()); }
bool UMTAInventorySubsystem::LoadStateFromString(const FString& Json) { return FromJson(MTAP::JsonFromString(Json)); }
