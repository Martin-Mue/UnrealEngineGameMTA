#include "MTAAudioTypes.h"
#include "MTAWeatherTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace MTAAudio
{
	FName DistrictId(EMTADistrict District)
	{
		static const TCHAR* Ids[] = { TEXT("core"), TEXT("saltmarket"), TEXT("lanternhill"), TEXT("greenmoor"), TEXT("rustline"), TEXT("sunstrip") };
		const int32 I = static_cast<int32>(District);
		return Ids[FMath::Clamp(I, 0, 5)];
	}

	bool DistrictFromId(FName Id, EMTADistrict& Out)
	{
		for (int32 I = 0; I < MTADistrict::Num(); ++I)
		{
			if (DistrictId(static_cast<EMTADistrict>(I)) == Id)
			{
				Out = static_cast<EMTADistrict>(I);
				return true;
			}
		}
		return false;
	}

	FString DistrictDisplayName(FName Id)
	{
		static const TMap<FName, FString> Names = {
			{ TEXT("core"), TEXT("Kestrel Core") }, { TEXT("saltmarket"), TEXT("Saltmarket") },
			{ TEXT("lanternhill"), TEXT("Lantern Hill") }, { TEXT("greenmoor"), TEXT("Greenmoor") },
			{ TEXT("rustline"), TEXT("Rustline") }, { TEXT("sunstrip"), TEXT("Sunstrip") } };
		const FString* N = Names.Find(Id);
		return N ? *N : Id.ToString();
	}

	bool HourInRange(float Hour, float From, float To)
	{
		if (From <= To)
		{
			return Hour >= From && Hour < To;
		}
		return Hour >= From || Hour < To;
	}

	TSharedPtr<FJsonObject> LoadJsonFile(const FString& RelativeContentPath)
	{
		FString Text;
		const FString File = FPaths::ProjectContentDir() / RelativeContentPath;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			UE_LOG(LogTemp, Warning, TEXT("MTAAudio: Datei nicht gefunden: %s"), *File);
			return nullptr;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("MTAAudio: JSON ungueltig: %s"), *File);
			return nullptr;
		}
		return Root;
	}

	FString FormatText(const FString& Template, const FMTAAudioContext& Ctx)
	{
		FString Out = Template;
		EMTAWeatherState W = EMTAWeatherState::Clear;
		MTAWeather::StateFromName(Ctx.Weather, W);
		Out.ReplaceInline(TEXT("{district}"), *DistrictDisplayName(Ctx.District));
		Out.ReplaceInline(TEXT("{weather}"), *MTAWeather::StateDisplayName(W));
		Out.ReplaceInline(TEXT("{hour}"), *FString::FromInt(FMath::FloorToInt(Ctx.Hour) % 24));
		Out.ReplaceInline(TEXT("{wanted}"), *FString::FromInt(Ctx.WantedLevel));
		return Out;
	}

	TArray<FName> ReadNameArray(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field)
	{
		TArray<FName> Out;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj.IsValid() && Obj->TryGetArrayField(Field, Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				Out.Add(FName(*V->AsString()));
			}
		}
		return Out;
	}

	float ReadNumber(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, float Default)
	{
		double V = Default;
		if (Obj.IsValid() && Obj->TryGetNumberField(Field, V))
		{
			return static_cast<float>(V);
		}
		return Default;
	}

	FString ReadString(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, const FString& Default)
	{
		FString V;
		if (Obj.IsValid() && Obj->TryGetStringField(Field, V))
		{
			return V;
		}
		return Default;
	}
}

FMTAAudioCondition FMTAAudioCondition::FromJson(const TSharedPtr<FJsonObject>& Obj)
{
	FMTAAudioCondition C;
	if (!Obj.IsValid())
	{
		return C;
	}
	C.WantedMin = static_cast<int32>(MTAAudio::ReadNumber(Obj, TEXT("wantedMin"), 0));
	C.WantedMax = static_cast<int32>(MTAAudio::ReadNumber(Obj, TEXT("wantedMax"), 5));
	C.Weather = MTAAudio::ReadNameArray(Obj, TEXT("weather"));
	C.District = MTAAudio::ReadNameArray(Obj, TEXT("district"));
	C.FlagsAll = MTAAudio::ReadNameArray(Obj, TEXT("flagsAll"));
	C.FlagsNone = MTAAudio::ReadNameArray(Obj, TEXT("flagsNone"));
	C.HourFrom = static_cast<float>(MTAAudio::ReadNumber(Obj, TEXT("hourFrom"), -1.0));
	C.HourTo = static_cast<float>(MTAAudio::ReadNumber(Obj, TEXT("hourTo"), -1.0));
	C.MinAegisDensity = static_cast<float>(MTAAudio::ReadNumber(Obj, TEXT("districtMinDensity"), 0.0));
	return C;
}

bool FMTAAudioCondition::Evaluate(const FMTAAudioContext& Ctx) const
{
	if (Ctx.WantedLevel < WantedMin || Ctx.WantedLevel > WantedMax)
	{
		return false;
	}
	if (Weather.Num() > 0 && !Weather.Contains(Ctx.Weather))
	{
		return false;
	}
	if (District.Num() > 0 && !District.Contains(Ctx.District))
	{
		return false;
	}
	for (const FName& F : FlagsAll)
	{
		if (!Ctx.Flags.Contains(F))
		{
			return false;
		}
	}
	for (const FName& F : FlagsNone)
	{
		if (Ctx.Flags.Contains(F))
		{
			return false;
		}
	}
	if (HourFrom >= 0.f && HourTo >= 0.f && !MTAAudio::HourInRange(Ctx.Hour, HourFrom, HourTo))
	{
		return false;
	}
	return Ctx.AegisDensity >= MinAegisDensity;
}
