#include "MTAProgressionTypes.h"
#include "MTAStorySubsystem.h"
#include "Engine/GameInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// ---------------------------------------------------------------------------------------------
// FMTAVehicleStats
// ---------------------------------------------------------------------------------------------

bool FMTAVehicleStats::ApplyModifier(const FMTAStatModifier& Mod)
{
	// Zuordnung Stat-Name -> Feld. Neue Werte hier und in docs/PROGRESSION.md (Abschnitt 4) ergaenzen.
	struct FEntry { const TCHAR* Name; float FMTAVehicleStats::* Field; };
	static const FEntry Table[] = {
		{ TEXT("vehicle.mass"),         &FMTAVehicleStats::MassKg },
		{ TEXT("vehicle.power"),        &FMTAVehicleStats::EnginePowerKw },
		{ TEXT("vehicle.torque"),       &FMTAVehicleStats::MaxTorqueNm },
		{ TEXT("vehicle.rpm"),          &FMTAVehicleStats::MaxRPM },
		{ TEXT("vehicle.topSpeed"),     &FMTAVehicleStats::TopSpeedKmh },
		{ TEXT("vehicle.shiftTime"),    &FMTAVehicleStats::GearShiftTime },
		{ TEXT("vehicle.grip"),         &FMTAVehicleStats::Grip },
		{ TEXT("vehicle.cornering"),    &FMTAVehicleStats::CorneringStiffness },
		{ TEXT("vehicle.brake"),        &FMTAVehicleStats::BrakeTorqueNm },
		{ TEXT("vehicle.springRate"),   &FMTAVehicleStats::SpringRate },
		{ TEXT("vehicle.damping"),      &FMTAVehicleStats::SuspensionDamping },
		{ TEXT("vehicle.steer"),        &FMTAVehicleStats::SteerAngleDeg },
		{ TEXT("vehicle.drag"),         &FMTAVehicleStats::DragCoefficient },
		{ TEXT("vehicle.downforce"),    &FMTAVehicleStats::Downforce },
		{ TEXT("vehicle.durability"),   &FMTAVehicleStats::Durability },
		{ TEXT("vehicle.damageResist"), &FMTAVehicleStats::DamageResist },
		{ TEXT("vehicle.visibility"),   &FMTAVehicleStats::AegisVisibility },
		{ TEXT("vehicle.noise"),        &FMTAVehicleStats::Noise },
		{ TEXT("vehicle.cargoKg"),      &FMTAVehicleStats::CargoKg },
		{ TEXT("vehicle.fuelUse"),      &FMTAVehicleStats::FuelUseL100 },
	};
	const FString Name = Mod.Stat.ToString();
	for (const FEntry& E : Table)
	{
		if (Name.Equals(E.Name, ESearchCase::IgnoreCase))
		{
			this->*(E.Field) = Mod.Apply(this->*(E.Field));
			return true;
		}
	}
	if (Name.Equals(TEXT("vehicle.cargoSlots"), ESearchCase::IgnoreCase))
	{
		CargoSlots = FMath::Max(0, FMath::RoundToInt(Mod.Apply((float)CargoSlots)));
		return true;
	}
	return false;
}

void FMTAVehicleStats::Finalize()
{
	MassKg = FMath::Max(80.f, MassKg);
	EnginePowerKw = FMath::Max(5.f, EnginePowerKw);
	MaxTorqueNm = FMath::Max(20.f, MaxTorqueNm);
	TopSpeedKmh = FMath::Clamp(TopSpeedKmh, 40.f, 360.f);
	GearShiftTime = FMath::Clamp(GearShiftTime, 0.05f, 1.5f);
	Grip = FMath::Clamp(Grip, 0.3f, 2.5f);
	SuspensionDamping = FMath::Clamp(SuspensionDamping, 0.05f, 1.f);
	DamageResist = FMath::Clamp(DamageResist, 0.f, 0.8f);
	AegisVisibility = FMath::Clamp(AegisVisibility, 0.f, 1.f);
	Noise = FMath::Clamp(Noise, 0.f, 1.f);
	Durability = FMath::Max(10.f, Durability);

	// Grobe Schaetzung 0-100 aus Leistungsgewicht (kW/t) und Grip; nur fuer Anzeige und Balancing.
	const float KwPerTon = EnginePowerKw / (MassKg / 1000.f);
	Accel0To100s = FMath::Clamp(32.f / FMath::Sqrt(FMath::Max(KwPerTon, 1.f)) / FMath::Lerp(0.85f, 1.15f, FMath::Clamp(Grip - 0.5f, 0.f, 1.f)) * 2.1f, 2.2f, 30.f);

	// Leistungsindex: gewichtete Mischung aus Beschleunigung, Hoechsttempo, Grip und Bremsen.
	const float AccelScore = FMath::Clamp((16.f - Accel0To100s) / 13.f, 0.f, 1.f);
	const float SpeedScore = FMath::Clamp((TopSpeedKmh - 100.f) / 220.f, 0.f, 1.f);
	const float GripScore = FMath::Clamp((Grip - 0.7f) / 0.9f, 0.f, 1.f);
	const float BrakeScore = FMath::Clamp((BrakeTorqueNm / (MassKg * 1.4f)) / 2.f, 0.f, 1.f);
	PerformanceIndex = 100 + FMath::RoundToInt(899.f * (0.35f * AccelScore + 0.30f * SpeedScore + 0.22f * GripScore + 0.13f * BrakeScore));
}

// ---------------------------------------------------------------------------------------------
// JSON-Hilfen
// ---------------------------------------------------------------------------------------------

namespace MTAProgression
{
	bool LoadJsonFile(const FString& RelativePath, TSharedPtr<FJsonObject>& Out)
	{
		const FString File = FPaths::ProjectContentDir() / RelativePath;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			UE_LOG(LogTemp, Error, TEXT("MTA6: %s nicht lesbar"), *File);
			return false;
		}
		TSharedPtr<FJsonObject> Parsed;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Parsed) || !Parsed.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("MTA6: %s ist kein gueltiges JSON"), *File);
			return false;
		}
		Out = Parsed;
		return true;
	}

	FString JsonToString(const TSharedRef<FJsonObject>& InObj)
	{
		FString Out;
		FJsonSerializer::Serialize(InObj, TJsonWriterFactory<>::Create(&Out));
		return Out;
	}

	TSharedPtr<FJsonObject> JsonFromString(const FString& Text)
	{
		TSharedPtr<FJsonObject> Parsed;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Parsed) || !Parsed.IsValid())
		{
			return nullptr;
		}
		return Parsed;
	}

	double Num(const TSharedPtr<FJsonObject>& InObj, FStringView Field, double Default)
	{
		double V = Default;
		if (InObj.IsValid() && InObj->TryGetNumberField(Field, V)) { return V; }
		return Default;
	}

	FString Str(const TSharedPtr<FJsonObject>& InObj, FStringView Field, const FString& Default)
	{
		FString V;
		if (InObj.IsValid() && InObj->TryGetStringField(Field, V)) { return V; }
		return Default;
	}

	bool Bool(const TSharedPtr<FJsonObject>& InObj, FStringView Field, bool Default)
	{
		bool V = Default;
		if (InObj.IsValid() && InObj->TryGetBoolField(Field, V)) { return V; }
		return Default;
	}

	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& Parent, FStringView Field)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (Parent.IsValid() && Parent->TryGetObjectField(Field, O) && O) { return *O; }
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* Arr(const TSharedPtr<FJsonObject>& Parent, FStringView Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (Parent.IsValid() && Parent->TryGetArrayField(Field, A)) { return A; }
		return nullptr;
	}

	TArray<FMTAStatModifier> ReadModifiers(const TArray<TSharedPtr<FJsonValue>>* Array)
	{
		TArray<FMTAStatModifier> Result;
		if (!Array) return Result;
		for (const TSharedPtr<FJsonValue>& V : *Array)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid()) continue;
			FMTAStatModifier M;
			M.Stat = FName(*Str(O, TEXT("stat")));
			M.Add = (float)Num(O, TEXT("add"), 0.0);
			M.Mul = (float)Num(O, TEXT("mul"), 1.0);
			if (!M.Stat.IsNone()) { Result.Add(M); }
		}
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> WriteModifiers(const TArray<FMTAStatModifier>& Mods)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const FMTAStatModifier& M : Mods)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("stat"), M.Stat.ToString());
			O->SetNumberField(TEXT("add"), M.Add);
			O->SetNumberField(TEXT("mul"), M.Mul);
			Result.Add(MakeShared<FJsonValueObject>(O));
		}
		return Result;
	}

	TMap<FName, float> ReadNumberMap(const TSharedPtr<FJsonObject>& InObj)
	{
		TMap<FName, float> Result;
		if (!InObj.IsValid()) return Result;
		for (const auto& Pair : InObj->Values)
		{
			// *Pair.Key funktioniert fuer FString- und FSharedString-Schluessel (UE 5.8, UE_JSONOBJECT_LEGACY_STRING_KEYS).
			if (Pair.Value.IsValid()) { Result.Add(FName(*Pair.Key), (float)Pair.Value->AsNumber()); }
		}
		return Result;
	}

	TSharedRef<FJsonObject> WriteNumberMap(const TMap<FName, float>& Map)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		for (const auto& Pair : Map) { O->SetNumberField(Pair.Key.ToString(), Pair.Value); }
		return O;
	}

	TArray<FName> ReadNameArray(const TArray<TSharedPtr<FJsonValue>>* Array)
	{
		TArray<FName> Result;
		if (!Array) return Result;
		for (const TSharedPtr<FJsonValue>& V : *Array)
		{
			if (V.IsValid()) { Result.Add(FName(*V->AsString())); }
		}
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> WriteNameArray(const TArray<FName>& Names)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const FName& N : Names) { Result.Add(MakeShared<FJsonValueString>(N.ToString())); }
		return Result;
	}

	FName DistrictToId(EMTADistrict District)
	{
		switch (District)
		{
		case EMTADistrict::Core:        return FName(TEXT("core"));
		case EMTADistrict::Saltmarket:  return FName(TEXT("saltmarket"));
		case EMTADistrict::LanternHill: return FName(TEXT("lanternhill"));
		case EMTADistrict::Greenmoor:   return FName(TEXT("greenmoor"));
		case EMTADistrict::Rustline:    return FName(TEXT("rustline"));
		case EMTADistrict::Sunstrip:    return FName(TEXT("sunstrip"));
		default:                        return NAME_None;
		}
	}

	EMTADistrict DistrictFromId(FName Id)
	{
		for (int32 I = 0; I < MTADistrict::Num(); ++I)
		{
			const EMTADistrict D = static_cast<EMTADistrict>(I);
			if (DistrictToId(D) == Id) { return D; }
		}
		return EMTADistrict::Core;
	}

	EMTAVehicleClass VehicleClassFromString(const FString& S)
	{
		if (S == TEXT("compact")) return EMTAVehicleClass::Compact;
		if (S == TEXT("sedan")) return EMTAVehicleClass::Sedan;
		if (S == TEXT("suv")) return EMTAVehicleClass::SUV;
		if (S == TEXT("sports")) return EMTAVehicleClass::Sports;
		if (S == TEXT("muscle")) return EMTAVehicleClass::Muscle;
		if (S == TEXT("van")) return EMTAVehicleClass::Van;
		if (S == TEXT("truck")) return EMTAVehicleClass::Truck;
		if (S == TEXT("motorcycle")) return EMTAVehicleClass::Motorcycle;
		if (S == TEXT("patrol")) return EMTAVehicleClass::Patrol;
		return EMTAVehicleClass::Sedan;
	}

	EMTARarity RarityFromString(const FString& S)
	{
		if (S == TEXT("uncommon")) return EMTARarity::Uncommon;
		if (S == TEXT("rare")) return EMTARarity::Rare;
		if (S == TEXT("exotic")) return EMTARarity::Exotic;
		if (S == TEXT("unique")) return EMTARarity::Unique;
		return EMTARarity::Common;
	}

	// -----------------------------------------------------------------------------------------
	// Story-Bruecke
	// -----------------------------------------------------------------------------------------

	static UMTAStorySubsystem* GetStory(const UGameInstance* GI)
	{
		return GI ? GI->GetSubsystem<UMTAStorySubsystem>() : nullptr;
	}

	float GetStoryAegis(const UGameInstance* GI)
	{
		const UMTAStorySubsystem* Story = GetStory(GI);
		if (!Story) return 50.f;
		const TSharedPtr<FJsonObject> State = JsonFromString(Story->StateToJson());
		return (float)Num(State, TEXT("aegis"), 50.0);
	}

	float GetStoryStanding(const UGameInstance* GI, const FString& Faction)
	{
		const UMTAStorySubsystem* Story = GetStory(GI);
		return Story ? Story->GetStanding(Faction) : 0.f;
	}

	bool PatchStoryState(UGameInstance* GI, TFunctionRef<void(const TSharedPtr<FJsonObject>&)> Patch)
	{
		UMTAStorySubsystem* Story = GetStory(GI);
		if (!Story) return false;
		const TSharedPtr<FJsonObject> State = JsonFromString(Story->StateToJson());
		if (!State.IsValid()) return false;
		Patch(State);
		return Story->StateFromJson(JsonToString(State.ToSharedRef()));
	}

	bool AddStoryAegis(UGameInstance* GI, float Delta)
	{
		return PatchStoryState(GI, [Delta](const TSharedPtr<FJsonObject>& State)
		{
			const double Cur = Num(State, TEXT("aegis"), 50.0);
			State->SetNumberField(TEXT("aegis"), FMath::Clamp(Cur + Delta, 0.0, 100.0));
		});
	}
}
