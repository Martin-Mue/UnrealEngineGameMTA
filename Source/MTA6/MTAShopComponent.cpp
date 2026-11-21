#include "MTAShopComponent.h"
#include "MTAInteriorSubsystem.h"
#include "MTAInteriorData.h"

DEFINE_LOG_CATEGORY_STATIC(LogMTAShop, Log, All);

namespace
{
	/** Fallback-Preisfaktor je Bezirk, solange kein IMTAPriceProvider angebunden ist (Weltkonzept 8.2a). */
	float DistrictPriceFactor(EMTADistrict D)
	{
		switch (D)
		{
		case EMTADistrict::Core:        return 1.6f;
		case EMTADistrict::Saltmarket:  return 0.9f;
		case EMTADistrict::LanternHill: return 1.0f;
		case EMTADistrict::Greenmoor:   return 1.8f;
		case EMTADistrict::Rustline:    return 0.75f;
		case EMTADistrict::Sunstrip:    return 1.3f;
		default:                        return 1.f;
		}
	}
}

UMTAShopComponent::UMTAShopComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UMTAShopComponent::InitFromData(FName InShopId, EMTADistrict InDistrict)
{
	District = InDistrict;
	const UMTAInteriorSubsystem* Sub = UMTAInteriorSubsystem::Get(this);
	const FMTAShopDef* Def = Sub ? Sub->GetDatabase().FindShop(InShopId) : nullptr;
	if (!Def)
	{
		UE_LOG(LogMTAShop, Warning, TEXT("Laden %s nicht in interiors.json"), *InShopId.ToString());
		return false;
	}
	ShopId = Def->Id;
	DisplayName = Def->Label;
	OwnerNpcId = Def->OwnerNpcId;
	Category = Def->Category;
	OpenHour = Def->OpenHour;
	CloseHour = Def->CloseHour;
	Wares = Def->Wares;
	return true;
}

bool UMTAShopComponent::IsOpenAt(float Hour) const
{
	return MTAInterior::IsHourInWindow(Hour, OpenHour, CloseHour);
}

bool UMTAShopComponent::IsOpenNow() const
{
	const UMTAInteriorSubsystem* Sub = UMTAInteriorSubsystem::Get(this);
	return IsOpenAt(Sub ? Sub->GetHourOfDay() : 12.f);
}

const FMTAShopWare* UMTAShopComponent::FindWare(FName ItemId) const
{
	return Wares.FindByPredicate([ItemId](const FMTAShopWare& W) { return W.ItemId == ItemId; });
}

IMTAPriceProvider* UMTAShopComponent::GetProvider() const
{
	const UMTAInteriorSubsystem* Sub = UMTAInteriorSubsystem::Get(this);
	return Sub ? Cast<IMTAPriceProvider>(Sub->GetPriceProvider()) : nullptr;
}

int32 UMTAShopComponent::GetPrice(FName ItemId, AActor* Buyer, int32 Quantity) const
{
	const FMTAShopWare* W = FindWare(ItemId);
	if (!W) { return -1; }
	if (const IMTAPriceProvider* P = GetProvider())
	{
		FMTAPriceQuery Q;
		Q.ShopId = ShopId;
		Q.ItemId = ItemId;
		Q.Category = Category;
		Q.BasePrice = W->BasePrice;
		Q.District = District;
		Q.Quantity = FMath::Max(1, Quantity);
		Q.Buyer = Buyer;
		return FMath::Max(0, P->GetPrice(Q));
	}
	return FMath::Max(0, FMath::RoundToInt(W->BasePrice * DistrictPriceFactor(District)));
}

TArray<FMTAShopOffer> UMTAShopComponent::GetOffers(AActor* Buyer) const
{
	TArray<FMTAShopOffer> Out;
	for (const FMTAShopWare& W : Wares)
	{
		FMTAShopOffer O;
		O.ItemId = W.ItemId;
		O.Label = W.Label;
		O.Price = GetPrice(W.ItemId, Buyer, 1);
		O.Stock = W.Stock;
		Out.Add(O);
	}
	return Out;
}

EMTAShopResult UMTAShopComponent::Buy(AActor* Buyer, FName ItemId, int32 Quantity, int32& OutTotalPrice)
{
	OutTotalPrice = 0;
	Quantity = FMath::Max(1, Quantity);
	if (!IsOpenNow()) { return EMTAShopResult::Closed; }
	if (bRequiresOwner && !bOwnerPresent) { return EMTAShopResult::OwnerAbsent; }
	FMTAShopWare* W = Wares.FindByPredicate([ItemId](const FMTAShopWare& X) { return X.ItemId == ItemId; });
	if (!W) { return EMTAShopResult::UnknownItem; }
	if (W->Stock < Quantity) { return EMTAShopResult::OutOfStock; }

	OutTotalPrice = GetPrice(ItemId, Buyer, Quantity) * Quantity;
	if (IMTAPriceProvider* P = GetProvider())
	{
		if (!P->TryCharge(Buyer, OutTotalPrice)) { return EMTAShopResult::NotEnoughMoney; }
		P->GrantItem(Buyer, ItemId, Quantity);
	}
	else
	{
		UE_LOG(LogMTAShop, Verbose, TEXT("Kein IMTAPriceProvider: %s x%d fuer %d Credits ohne Abbuchung"), *ItemId.ToString(), Quantity, OutTotalPrice);
	}
	W->Stock -= Quantity;
	OnPurchased.Broadcast(ShopId, ItemId, Quantity, OutTotalPrice);
	return EMTAShopResult::Ok;
}

void UMTAShopComponent::Restock()
{
	for (FMTAShopWare& W : Wares) { W.Stock = FMath::Max(W.Stock, W.MaxStock); }
}
