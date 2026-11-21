#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "Math/RandomStream.h"
#include "MTAProgressionTypes.h"
#include "MTAInventorySubsystem.generated.h"

UENUM(BlueprintType)
enum class EMTAItemCategory : uint8
{
	Tool       UMETA(DisplayName = "Werkzeug"),
	HackDongle UMETA(DisplayName = "Hacking-Dongle"),
	Plate      UMETA(DisplayName = "Kennzeichen"),
	Evidence   UMETA(DisplayName = "Beweis"),
	Contraband UMETA(DisplayName = "Schmuggelware"),
	RepairKit  UMETA(DisplayName = "Reparaturset"),
	Consumable UMETA(DisplayName = "Verbrauchsgut"),
	Key        UMETA(DisplayName = "Schluessel")
};

/** Personal = am Koerper, Trunk = Kofferraum des aktiven Fahrzeugs. */
UENUM(BlueprintType)
enum class EMTAInventoryContainer : uint8
{
	Personal UMETA(DisplayName = "Tasche"),
	Trunk    UMETA(DisplayName = "Kofferraum")
};

/** Gegenstandsdefinition aus economy.json "items". */
USTRUCT(BlueprintType)
struct MTA6_API FMTAItemDef
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") EMTAItemCategory Category = EMTAItemCategory::Tool;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") float WeightKg = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") int32 StackMax = 1;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") int64 BasePrice = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") bool bLegal = true;
	/** Beschlagnahmerisiko 0..1 bei Festnahme. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") float Risk = 0.f;
	/** Story-Gegenstand: nicht verkaeuflich, nicht beschlagnahmbar. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") bool bStoryItem = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") int32 Charges = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") FName ScarcityCategory;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") FName UseTag;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") float UseRepair = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") float UseFuel = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") bool bUsePlateSwap = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") FName UseTreeXpTree;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") float UseTreeXpAmount = 0.f;
};

USTRUCT(BlueprintType)
struct MTA6_API FMTAItemStack
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") FName ItemId;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") int32 Count = 0;
	/** Restladungen des angebrochenen Stuecks (Dongles). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") int32 Charges = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") EMTAInventoryContainer Container = EMTAInventoryContainer::Personal;
};

USTRUCT(BlueprintType)
struct MTA6_API FMTAConfiscationReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") TArray<FMTAItemStack> Confiscated;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") int64 ValueLost = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") int64 ArrestFine = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") int64 ContrabandFine = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") bool bVehicleImpounded = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") FName District;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Inventory") int32 Stars = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMTAInventoryChangedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAConfiscatedSignature, const FMTAConfiscationReport&, Report);

/**
 * Inventar mit Slots und Gewicht (Tasche + Kofferraum je Fahrzeug), Gegenstandsnutzung, Handel
 * (legal und Schwarzmarkt) und Beschlagnahmung bei Festnahme. Die Festnahme kommt ueber
 * HandleArrested()/HandleHeatChanged(), die an die Delegates der KI-Gruppe (FOnArrested,
 * FOnHeatChanged in MTATrafficTypes.h) gebunden werden, ohne deren Dateien zu aendern.
 */
UCLASS()
class MTA6_API UMTAInventorySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static constexpr int32 SchemaVersion = 1;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	void LoadItemDefs();

	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	void ResetInventory();

	UFUNCTION(BlueprintPure, Category = "MTA6|Inventory")
	bool GetItemDef(FName ItemId, FMTAItemDef& Out) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Inventory")
	TArray<FMTAItemDef> GetAllItemDefs() const;

	// --- Bestand ----------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|Inventory")
	bool CanAdd(FName ItemId, int32 Count, EMTAInventoryContainer Container, FString& OutReason) const;

	/** Fuegt so viel wie moeglich hinzu; gibt die tatsaechlich hinzugefuegte Menge zurueck. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	int32 AddItem(FName ItemId, int32 Count, EMTAInventoryContainer Container);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	int32 RemoveItem(FName ItemId, int32 Count, EMTAInventoryContainer Container);

	/** Entfernt zuerst aus der Tasche, dann aus dem Kofferraum. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	int32 RemoveFromAny(FName ItemId, int32 Count);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	int32 MoveItem(FName ItemId, int32 Count, EMTAInventoryContainer From, EMTAInventoryContainer To);

	/** Menge in Tasche + Kofferraum des aktiven Fahrzeugs. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Inventory")
	int32 GetItemCount(FName ItemId) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Inventory")
	int32 GetCountIn(FName ItemId, EMTAInventoryContainer Container) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Inventory")
	TArray<FMTAItemStack> GetItems(EMTAInventoryContainer Container) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Inventory") int32 GetUsedSlots(EMTAInventoryContainer Container) const;
	UFUNCTION(BlueprintPure, Category = "MTA6|Inventory") int32 GetMaxSlots(EMTAInventoryContainer Container) const;
	UFUNCTION(BlueprintPure, Category = "MTA6|Inventory") float GetWeight(EMTAInventoryContainer Container) const;
	UFUNCTION(BlueprintPure, Category = "MTA6|Inventory") float GetMaxWeight(EMTAInventoryContainer Container) const;

	/** Ausbaustufe der Tasche (economy.json inventory.slotUpgrades: 12/20/30). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	void SetSlotUpgradeLevel(int32 InLevel);

	// --- Nutzung und Handel -----------------------------------------------------------------

	/** Reparaturset, Kanister, Dongle (Ladung, Baum-XP, Faehigkeits-Tag), Kennzeichen (Garage). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	bool UseItem(FName ItemId, FName District, FString& OutMessage);

	/** Legale Gueter beim Haendler, illegale nur auf dem Schwarzmarkt (Bezirke mit blackMarket). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	bool BuyItem(FName ItemId, int32 Count, FName District, EMTAInventoryContainer Container, FString& OutReason);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	int64 SellItem(FName ItemId, int32 Count, FName District);

	UFUNCTION(BlueprintPure, Category = "MTA6|Inventory")
	int64 GetBuyPrice(FName ItemId, FName District) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Inventory")
	int64 GetSellPrice(FName ItemId, FName District) const;

	// --- Gesetz -----------------------------------------------------------------------------

	/** Festnahme: Strafe, Beschlagnahmung illegaler Gegenstaende, ab 3 Sternen Fahrzeug in Verwahrung. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	FMTAConfiscationReport ProcessArrest(FName District, int32 Stars);

	/** Bindeziel fuer FOnArrested (keine Parameter). Nutzt zuletzt bekannte Heat und Bezirk. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	void HandleArrested();

	/** Bindeziel fuer FOnHeatChanged(float Heat): merkt sich die hoechste Heat der aktuellen Verfolgung. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	void HandleHeatChanged(float Heat);

	/** Bindeziel fuer FOnWantedLevelChanged(int32 Stars). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	void HandleWantedLevelChanged(int32 Stars);

	/** Aktueller Bezirk des Spielers (vom GameMode bei Bezirkswechsel gesetzt). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	void SetCurrentDistrict(FName District) { CurrentDistrict = District; }

	/** Summe des Risikos aller illegalen Gegenstaende (z. B. fuer Kontrollen an Checkpoints). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Inventory")
	float GetContrabandExposure() const;

	// --- Persistenz -------------------------------------------------------------------------

	TSharedRef<FJsonObject> ToJson() const;
	bool FromJson(const TSharedPtr<FJsonObject>& Json);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	FString SaveStateToString() const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Inventory")
	bool LoadStateFromString(const FString& Json);

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Inventory") FMTAInventoryChangedSignature OnInventoryChanged;
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Inventory") FMTAConfiscatedSignature OnItemsConfiscated;

private:
	TMap<FName, FMTAItemDef> Defs;
	TArray<FName> DefOrder;
	TArray<FMTAItemStack> Personal;
	/** Kofferraum je Fahrzeug-Instanz (Garage). */
	TMap<FName, TArray<FMTAItemStack>> Trunks;
	int32 SlotUpgradeLevel = 0;
	FName CurrentDistrict = TEXT("core");
	float PursuitPeakHeat = 0.f;
	FRandomStream Rng;

	TArray<FMTAItemStack>* GetContainer(EMTAInventoryContainer Container);
	const TArray<FMTAItemStack>* GetContainer(EMTAInventoryContainer Container) const;
	FName ActiveVehicleId() const;
	float Stat(FName StatName, float Base) const;
	static int32 SlotsFor(const FMTAItemDef& Def, int32 Count);
	static TArray<TSharedPtr<FJsonValue>> WriteStacks(const TArray<FMTAItemStack>& Stacks);
	TArray<FMTAItemStack> ReadStacks(const TArray<TSharedPtr<FJsonValue>>* Arr, EMTAInventoryContainer Container) const;
};
