#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/Interface.h"
#include "MTADistrict.h"
#include "MTAShopComponent.generated.h"

/** Eine Ware im Sortiment (aus interiors.json "shops[].wares"). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAShopWare
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop") FName ItemId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop") FString Label;
	/** Grundpreis in Credits; der Endpreis kommt vom IMTAPriceProvider. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop", meta = (ClampMin = "0")) int32 BasePrice = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop", meta = (ClampMin = "0")) int32 Stock = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop", meta = (ClampMin = "0")) int32 MaxStock = 0;
};

/** Angebot fuer die UI (Endpreis bereits berechnet). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAShopOffer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Shop") FName ItemId;
	UPROPERTY(BlueprintReadOnly, Category = "Shop") FString Label;
	UPROPERTY(BlueprintReadOnly, Category = "Shop") int32 Price = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shop") int32 Stock = 0;
};

/** Preisanfrage an die Wirtschaft. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAPriceQuery
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Shop") FName ShopId;
	UPROPERTY(BlueprintReadOnly, Category = "Shop") FName ItemId;
	/** Knappheitskategorie des Ladens (food, drink, medical, vehicle, electronics, clothing, media). */
	UPROPERTY(BlueprintReadOnly, Category = "Shop") FName Category;
	UPROPERTY(BlueprintReadOnly, Category = "Shop") int32 BasePrice = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Shop") EMTADistrict District = EMTADistrict::Core;
	UPROPERTY(BlueprintReadOnly, Category = "Shop") int32 Quantity = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Shop") TObjectPtr<AActor> Buyer = nullptr;
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UMTAPriceProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anbindung an die Wirtschaft (Wirtschafts-Agent). Registrierung:
 *   UMTAInteriorSubsystem::Get(this)->SetPriceProvider(MyEconomyObject);
 * Ohne Provider gilt: Grundpreis * Bezirksfaktor, Bezahlen gelingt immer (Prototyp).
 */
class MTA6_API IMTAPriceProvider
{
	GENERATED_BODY()

public:
	/** Stueckpreis in Credits. */
	virtual int32 GetPrice(const FMTAPriceQuery& Query) const = 0;
	/** Zieht Amount vom Kaeufer ab; false = zu wenig Geld. */
	virtual bool TryCharge(AActor* Buyer, int32 Amount) = 0;
	/** Gibt dem Kaeufer die Ware (Inventar). */
	virtual void GrantItem(AActor* Buyer, FName ItemId, int32 Quantity) = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FMTAOnShopPurchase, FName, ShopId, FName, ItemId, int32, Quantity, int32, TotalPrice);

/** Warum ein Kauf scheiterte. */
UENUM(BlueprintType)
enum class EMTAShopResult : uint8
{
	Ok, Closed, OwnerAbsent, UnknownItem, OutOfStock, NotEnoughMoney
};

/**
 * Laden/Handel im Innenraum. Wird von AMTAInteriorInstance automatisch angehaengt, wenn die
 * Vorlage "shop" setzt; kann auch frei an Actors gesetzt werden (EditAnywhere-Felder).
 */
UCLASS(ClassGroup = (MTA6), meta = (BlueprintSpawnableComponent))
class MTA6_API UMTAShopComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMTAShopComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop") FName ShopId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop") FString DisplayName;
	/** NPC-ID des Ladenbesitzers (NPC-Agent, npcs.json). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop") FName OwnerNpcId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop") FName Category;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop", meta = (ClampMin = "0", ClampMax = "24")) float OpenHour = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop", meta = (ClampMin = "0", ClampMax = "24")) float CloseHour = 22.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop") EMTADistrict District = EMTADistrict::Core;
	/** Ohne anwesenden Besitzer wird nicht verkauft (NPC-Agent meldet per SetOwnerPresent). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop") bool bRequiresOwner = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop") TArray<FMTAShopWare> Wares;

	UPROPERTY(BlueprintAssignable, Category = "Shop") FMTAOnShopPurchase OnPurchased;

	/** Laedt Sortiment, Zeiten und Besitzer aus interiors.json. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Shop")
	bool InitFromData(FName InShopId, EMTADistrict InDistrict);

	UFUNCTION(BlueprintPure, Category = "MTA6|Shop")
	bool IsOpenAt(float Hour) const;

	/** Nutzt die Uhrzeit des UMTAInteriorSubsystem. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Shop")
	bool IsOpenNow() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Shop")
	int32 GetPrice(FName ItemId, AActor* Buyer = nullptr, int32 Quantity = 1) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Shop")
	TArray<FMTAShopOffer> GetOffers(AActor* Buyer = nullptr) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Shop")
	EMTAShopResult Buy(AActor* Buyer, FName ItemId, int32 Quantity, int32& OutTotalPrice);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Shop")
	void SetOwnerPresent(bool bPresent) { bOwnerPresent = bPresent; }

	/** Fuellt Lager auf (z. B. einmal pro Spieltag vom Subsystem). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Shop")
	void Restock();

private:
	bool bOwnerPresent = true;
	const FMTAShopWare* FindWare(FName ItemId) const;
	IMTAPriceProvider* GetProvider() const;
};
