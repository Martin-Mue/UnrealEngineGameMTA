#include "MTAVehicleCatalog.h"

namespace MTAP = MTAProgression;

void UMTAVehicleCatalog::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadCatalog();
}

bool UMTAVehicleCatalog::LoadCatalog(const FString& RelativePath)
{
	TSharedPtr<FJsonObject> Parsed;
	if (!MTAP::LoadJsonFile(RelativePath, Parsed)) return false;
	Data = Parsed;
	Models.Reset();
	ModelOrder.Reset();
	Parts.Reset();
	PartOrder.Reset();

	const TSharedPtr<FJsonObject> Brands = MTAP::Obj(Data, TEXT("brands"));
	const TSharedPtr<FJsonObject> ClassRules = MTAP::Obj(Data, TEXT("classRules"));
	if (const TArray<TSharedPtr<FJsonValue>>* Arr0 = MTAP::Arr(Data, TEXT("models")))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr0)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FMTAVehicleModel M;
			M.Id = FName(*MTAP::Str(O, TEXT("id")));
			M.Brand = FName(*MTAP::Str(O, TEXT("brand")));
			M.BrandName = MTAP::Str(MTAP::Obj(Brands, M.Brand.ToString()), TEXT("name"), M.Brand.ToString());
			M.ModelName = MTAP::Str(O, TEXT("model"));
			const FString ClassStr = MTAP::Str(O, TEXT("class"), TEXT("sedan"));
			M.Class = MTAP::VehicleClassFromString(ClassStr);
			M.Rarity = MTAP::RarityFromString(MTAP::Str(O, TEXT("rarity"), TEXT("common")));
			M.Price = (int64)MTAP::Num(O, TEXT("price"), 0.0);
			M.Seats = (int32)MTAP::Num(O, TEXT("seats"), 4.0);
			M.Dealers = MTAP::ReadNameArray(MTAP::Arr(O, TEXT("dealers")));
			M.bPurchasable = MTAP::Bool(O, TEXT("purchasable"), true);
			M.Acquire = MTAP::Str(O, TEXT("acquire"));
			M.FactionReq = MTAP::ReadNumberMap(MTAP::Obj(O, TEXT("factionReq")));
			M.Description = MTAP::Str(O, TEXT("desc"));
			M.TuningSlots = MTAP::ReadNameArray(MTAP::Arr(MTAP::Obj(ClassRules, ClassStr), TEXT("tuningSlots")));

			FMTAVehicleStats& S = M.BaseStats;
			S.MassKg = (float)MTAP::Num(O, TEXT("massKg"), 1400.0);
			S.EnginePowerKw = (float)MTAP::Num(O, TEXT("powerKw"), 100.0);
			S.MaxTorqueNm = (float)MTAP::Num(O, TEXT("torqueNm"), 250.0);
			S.MaxRPM = (float)MTAP::Num(O, TEXT("maxRpm"), 6000.0);
			S.TopSpeedKmh = (float)MTAP::Num(O, TEXT("topSpeed"), 180.0);
			S.Grip = (float)MTAP::Num(O, TEXT("grip"), 1.0);
			S.CorneringStiffness = 1000.f * S.Grip;
			S.BrakeTorqueNm = (float)MTAP::Num(O, TEXT("brakeNm"), 1500.0);
			S.SpringRate = (float)MTAP::Num(O, TEXT("springRate"), 250.0);
			S.SuspensionDamping = (float)MTAP::Num(O, TEXT("damping"), 0.5);
			S.SteerAngleDeg = (float)MTAP::Num(O, TEXT("steer"), 40.0);
			S.DragCoefficient = (float)MTAP::Num(O, TEXT("drag"), 0.32);
			S.Downforce = M.Class == EMTAVehicleClass::Sports ? 0.25f : 0.1f;
			S.Durability = (float)MTAP::Num(O, TEXT("durability"), 100.0);
			S.AegisVisibility = (float)MTAP::Num(O, TEXT("visibility"), 0.5);
			S.Noise = (float)MTAP::Num(O, TEXT("noise"), 0.5);
			S.CargoSlots = (int32)MTAP::Num(O, TEXT("cargoSlots"), 4.0);
			S.CargoKg = (float)MTAP::Num(O, TEXT("cargoKg"), 80.0);
			S.FuelCapacityL = (float)MTAP::Num(O, TEXT("fuelL"), 50.0);
			S.FuelUseL100 = (float)MTAP::Num(O, TEXT("fuelUse"), 8.0);
			S.GearShiftTime = M.Class == EMTAVehicleClass::Truck ? 0.8f : (M.Class == EMTAVehicleClass::Sports ? 0.25f : 0.4f);
			S.Finalize();

			ModelOrder.Add(M.Id);
			Models.Add(M.Id, M);
		}
	}

	if (const TArray<TSharedPtr<FJsonValue>>* Arr1 = MTAP::Arr(Data, TEXT("tuning")))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr1)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FMTATuningPart P;
			P.Id = FName(*MTAP::Str(O, TEXT("id")));
			P.Category = FName(*MTAP::Str(O, TEXT("category")));
			P.Level = (int32)MTAP::Num(O, TEXT("level"), 0.0);
			P.Name = MTAP::Str(O, TEXT("name"));
			P.Price = (int64)MTAP::Num(O, TEXT("price"), 0.0);
			P.StandingReq = MTAP::ReadNumberMap(MTAP::Obj(O, TEXT("standing")));
			P.IllegalIn = MTAP::ReadNameArray(MTAP::Arr(O, TEXT("illegalIn")));
			P.MaintenancePerDay = (int64)MTAP::Num(O, TEXT("maintenancePerDay"), 0.0);
			P.Effects = MTAP::ReadModifiers(MTAP::Arr(O, TEXT("effects")));
			P.Tags = MTAP::ReadNameArray(MTAP::Arr(O, TEXT("tags")));
			PartOrder.Add(P.Id);
			Parts.Add(P.Id, P);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("MTA6: %d Fahrzeugmodelle, %d Tuning-Teile geladen"), Models.Num(), Parts.Num());
	return true;
}

bool UMTAVehicleCatalog::GetModel(FName ModelId, FMTAVehicleModel& OutModel) const
{
	if (const FMTAVehicleModel* M = Models.Find(ModelId)) { OutModel = *M; return true; }
	return false;
}

TArray<FMTAVehicleModel> UMTAVehicleCatalog::GetAllModels() const
{
	TArray<FMTAVehicleModel> Result;
	for (const FName& Id : ModelOrder) { Result.Add(Models.FindChecked(Id)); }
	return Result;
}

TArray<FMTAVehicleModel> UMTAVehicleCatalog::GetModelsByClass(EMTAVehicleClass Class) const
{
	return GetAllModels().FilterByPredicate([Class](const FMTAVehicleModel& M) { return M.Class == Class; });
}

TArray<FMTAVehicleModel> UMTAVehicleCatalog::GetDealerModels(FName District) const
{
	return GetAllModels().FilterByPredicate([District](const FMTAVehicleModel& M) { return M.bPurchasable && M.Dealers.Contains(District); });
}

FMTAVehicleStats UMTAVehicleCatalog::GetBaseStats(FName ModelId) const
{
	const FMTAVehicleModel* M = Models.Find(ModelId);
	return M ? M->BaseStats : FMTAVehicleStats();
}

bool UMTAVehicleCatalog::GetTuningPart(FName PartId, FMTATuningPart& OutPart) const
{
	if (const FMTATuningPart* P = Parts.Find(PartId)) { OutPart = *P; return true; }
	return false;
}

TArray<FMTATuningPart> UMTAVehicleCatalog::GetTuningParts(FName Category) const
{
	TArray<FMTATuningPart> Result;
	for (const FName& Id : PartOrder)
	{
		const FMTATuningPart& P = Parts.FindChecked(Id);
		if (Category.IsNone() || P.Category == Category) { Result.Add(P); }
	}
	return Result;
}

bool UMTAVehicleCatalog::IsTuningSlotAllowed(FName ModelId, FName Category) const
{
	const FMTAVehicleModel* M = Models.Find(ModelId);
	return M && M->TuningSlots.Contains(Category);
}

float UMTAVehicleCatalog::GetRarityResale(EMTARarity Rarity) const
{
	static const TCHAR* const Names[] = { TEXT("common"), TEXT("uncommon"), TEXT("rare"), TEXT("exotic"), TEXT("unique") };
	const int32 Idx = FMath::Clamp((int32)Rarity, 0, 4);
	return (float)MTAP::Num(MTAP::Obj(MTAP::Obj(Data, TEXT("rarityRules")), Names[Idx]), TEXT("resale"), 1.0);
}

FString UMTAVehicleCatalog::BuildComparisonTable() const
{
	FString Out = TEXT("id;klasse;preis;masse;kW;0-100;vmax;grip;auffaellig;laderaum;PI\n");
	for (const FName& Id : ModelOrder)
	{
		const FMTAVehicleModel& M = Models.FindChecked(Id);
		const FMTAVehicleStats& S = M.BaseStats;
		Out += FString::Printf(TEXT("%s;%d;%lld;%.0f;%.0f;%.1f;%.0f;%.2f;%.2f;%d;%d\n"),
			*Id.ToString(), (int32)M.Class, M.Price, S.MassKg, S.EnginePowerKw, S.Accel0To100s, S.TopSpeedKmh, S.Grip, S.AegisVisibility, S.CargoSlots, S.PerformanceIndex);
	}
	return Out;
}
