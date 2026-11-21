#include "MTAWorldEventSubsystem.h"
#include "MTADiscoverySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString EvStr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		FString S;
		if (O.IsValid()) { O->TryGetStringField(Key, S); }
		return S;
	}

	double EvNum(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default)
	{
		double V = Default;
		if (O.IsValid()) { O->TryGetNumberField(Key, V); }
		return V;
	}

	bool EvInWindow(float H, float From, float To)
	{
		if (FMath::IsNearlyEqual(From, To) || (From <= 0.f && To >= 24.f)) return true;
		return From < To ? (H >= From && H < To) : (H >= From || H < To);
	}

	const TCHAR* GAllDistricts[] = { TEXT("core"), TEXT("saltmarket"), TEXT("lantern"), TEXT("greenmoor"), TEXT("rustline"), TEXT("sunstrip") };

	TArray<FMTADiscoveryReward> EvParseRewards(const TArray<TSharedPtr<FJsonValue>>* Arr)
	{
		TArray<FMTADiscoveryReward> Out;
		if (!Arr) return Out;
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid()) continue;
			FMTADiscoveryReward R;
			R.Type = MTADiscovery::RewardTypeFromString(EvStr(O, TEXT("type")));
			R.Id = FName(*EvStr(O, TEXT("id")));
			R.Amount = (int32)EvNum(O, TEXT("amount"), 0.0);
			R.Text = EvStr(O, TEXT("text"));
			Out.Add(R);
		}
		return Out;
	}
}

void UMTAWorldEventSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMTADiscoverySubsystem>();
	Rng.Initialize(Seed);
	LoadEvents();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UMTAWorldEventSubsystem::HandleTick), 1.0f);
}

void UMTAWorldEventSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickHandle);
	TickHandle.Reset();
	Super::Deinitialize();
}

bool UMTAWorldEventSubsystem::LoadEvents(const FString& Path)
{
	Defs.Reset();
	Runtime.Reset();
	const FString File = Path.IsEmpty() ? FPaths::ProjectContentDir() / TEXT("Data/world_events.json") : Path;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("MTA6: world_events.json nicht lesbar/gueltig: %s"), *File);
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;
	if (!Root->TryGetArrayField(TEXT("events"), Events)) return false;

	for (const TSharedPtr<FJsonValue>& V : *Events)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		if (!O.IsValid()) continue;
		FMTAWorldEventDef D;
		FMTAWorldEventRuntime RT;
		D.Id = FName(*EvStr(O, TEXT("id")));
		D.Title = EvStr(O, TEXT("title"));
		D.Kind = FName(*EvStr(O, TEXT("kind")));
		D.Description = EvStr(O, TEXT("description"));
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (O->TryGetArrayField(TEXT("districts"), A))
		{
			for (const TSharedPtr<FJsonValue>& X : *A) { D.Districts.Add(FName(*X->AsString())); }
		}

		const TSharedPtr<FJsonObject>* Trig = nullptr;
		if (O->TryGetObjectField(TEXT("trigger"), Trig))
		{
			if ((*Trig)->TryGetArrayField(TEXT("weekday"), A)) { for (const TSharedPtr<FJsonValue>& X : *A) { D.Weekdays.Add((int32)X->AsNumber()); } }
			if ((*Trig)->TryGetArrayField(TEXT("hours"), A) && A->Num() == 2) { D.HourFrom = (float)(*A)[0]->AsNumber(); D.HourTo = (float)(*A)[1]->AsNumber(); }
			if ((*Trig)->TryGetArrayField(TEXT("weather"), A)) { for (const TSharedPtr<FJsonValue>& X : *A) { D.Weather.Add(FName(*X->AsString())); } }
			if ((*Trig)->TryGetArrayField(TEXT("when"), A))
			{
				RT.When = *A;
				FJsonSerializer::Serialize(RT.When, TJsonWriterFactory<>::Create(&D.TriggerWhenJson));
			}
			D.ChancePerHour = (float)EvNum(*Trig, TEXT("chancePerHour"), 0.1);
			D.CooldownHours = (float)EvNum(*Trig, TEXT("cooldownHours"), 24.0);
		}
		if (O->TryGetArrayField(TEXT("durationHours"), A) && A->Num() == 2)
		{
			D.DurationMinHours = (float)(*A)[0]->AsNumber();
			D.DurationMaxHours = (float)(*A)[1]->AsNumber();
		}

		const TSharedPtr<FJsonObject>* Eff = nullptr;
		if (O->TryGetObjectField(TEXT("effects"), Eff))
		{
			FMTADistrictModifiers& M = D.Effects;
			M.Traffic = (float)EvNum(*Eff, TEXT("traffic"), 1.0);
			M.Pedestrians = (float)EvNum(*Eff, TEXT("pedestrians"), 1.0);
			M.Police = (float)EvNum(*Eff, TEXT("police"), 1.0);
			M.HeatGain = (float)EvNum(*Eff, TEXT("heatGain"), 1.0);
			M.Cameras = (float)EvNum(*Eff, TEXT("cameras"), 1.0);
			M.Lights = (float)EvNum(*Eff, TEXT("lights"), 1.0);
			M.Visibility = (float)EvNum(*Eff, TEXT("visibility"), 1.0);
			bool bLights = true;
			if ((*Eff)->TryGetBoolField(TEXT("trafficLights"), bLights)) { M.bTrafficLightsOff = !bLights; }
			const TSharedPtr<FJsonObject>* Prices = nullptr;
			if ((*Eff)->TryGetObjectField(TEXT("prices"), Prices))
			{
				// Bekannte Warengruppen abfragen statt ueber FJsonObject::Values zu iterieren (Schluesseltyp ab 5.8 variabel).
				static const TCHAR* Categories[] = { TEXT("food"), TEXT("drinks"), TEXT("fuel"), TEXT("batteries"), TEXT("parts"),
					TEXT("black_market"), TEXT("taxi"), TEXT("repairs"), TEXT("imported_goods"), TEXT("fish"), TEXT("clothing"),
					TEXT("services"), TEXT("medicine"), TEXT("ice"), TEXT("electricity"), TEXT("delivery") };
				for (const TCHAR* Cat : Categories)
				{
					double F = 1.0;
					if ((*Prices)->TryGetNumberField(Cat, F)) { M.Prices.Add(FName(Cat), (float)F); }
				}
			}
			if ((*Eff)->TryGetArrayField(TEXT("roadClosures"), A)) { for (const TSharedPtr<FJsonValue>& X : *A) { M.RoadClosures.Add(X->AsString()); } }
			if ((*Eff)->TryGetArrayField(TEXT("worldTags"), A)) { for (const TSharedPtr<FJsonValue>& X : *A) { M.WorldTags.Add(FName(*X->AsString())); } }
		}

		if (O->TryGetArrayField(TEXT("playerOptions"), A))
		{
			for (const TSharedPtr<FJsonValue>& X : *A)
			{
				const TSharedPtr<FJsonObject> OO = X->AsObject();
				if (!OO.IsValid()) continue;
				FMTAWorldEventOption Opt;
				Opt.Id = FName(*EvStr(OO, TEXT("id")));
				Opt.Text = EvStr(OO, TEXT("text"));
				const TArray<TSharedPtr<FJsonValue>>* R = nullptr;
				OO->TryGetArrayField(TEXT("rewards"), R);
				Opt.Rewards = EvParseRewards(R);
				D.Options.Add(Opt);
			}
		}
		if (O->TryGetArrayField(TEXT("hints"), A))
		{
			for (const TSharedPtr<FJsonValue>& X : *A)
			{
				const TSharedPtr<FJsonObject> HO = X->AsObject();
				if (!HO.IsValid()) continue;
				FMTADiscoveryHint H;
				H.Kind = MTADiscovery::HintKindFromString(EvStr(HO, TEXT("kind")));
				H.Text = EvStr(HO, TEXT("text"));
				D.Hints.Add(H);
			}
		}
		if (O->TryGetArrayField(TEXT("storyEffects"), A)) { RT.StoryEffects = *A; }

		Defs.Add(D);
		Runtime.Add(RT);
	}
	return true;
}

int32 UMTAWorldEventSubsystem::FindDef(FName Id) const
{
	for (int32 I = 0; I < Defs.Num(); ++I) { if (Defs[I].Id == Id) return I; }
	return INDEX_NONE;
}

bool UMTAWorldEventSubsystem::GetEventDef(FName EventId, FMTAWorldEventDef& OutDef) const
{
	const int32 I = FindDef(EventId);
	if (I == INDEX_NONE) return false;
	OutDef = Defs[I];
	return true;
}

bool UMTAWorldEventSubsystem::HandleTick(float DeltaSeconds)
{
	if (!bAutoSchedule) return true;
	const UGameInstance* GI = GetGameInstance();
	const UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld() || World->IsPaused()) return true;
	if (const UMTADiscoverySubsystem* Disc = GI->GetSubsystem<UMTADiscoverySubsystem>())
	{
		UpdateSchedule(Disc->GetHour());
	}
	return true;
}

int32 UMTAWorldEventSubsystem::CountInDistrict(FName DistrictId) const
{
	int32 N = 0;
	for (const FMTAActiveWorldEvent& E : Active) { if (E.DistrictId == DistrictId) ++N; }
	return N;
}

bool UMTAWorldEventSubsystem::CanTrigger(int32 DefIndex, float Hour) const
{
	const FMTAWorldEventDef& D = Defs[DefIndex];
	if (IsEventActive(D.Id)) return false;
	if (const double* Until = CooldownUntil.Find(D.Id)) { if (AbsHours < *Until) return false; }
	if (!EvInWindow(Hour, D.HourFrom, D.HourTo)) return false;

	const UGameInstance* GI = GetGameInstance();
	const UMTADiscoverySubsystem* Disc = GI ? GI->GetSubsystem<UMTADiscoverySubsystem>() : nullptr;
	if (Disc)
	{
		if (D.Weekdays.Num() > 0 && !D.Weekdays.Contains(Disc->GetWeekday())) return false;
		if (D.Weather.Num() > 0 && !D.Weather.Contains(Disc->GetWeather())) return false;
		if (!Disc->EvaluateConditions(Runtime[DefIndex].When)) return false;
	}
	else if (Runtime[DefIndex].When.Num() > 0)
	{
		return false;
	}
	return true;
}

void UMTAWorldEventSubsystem::UpdateSchedule(float CurrentHour)
{
	// Spielzeit fortschreiben (Tageswechsel erkennen).
	double DeltaHours = 0.0;
	if (LastHour >= 0.f)
	{
		DeltaHours = CurrentHour - LastHour;
		if (DeltaHours < -12.0) DeltaHours += 24.0;
		DeltaHours = FMath::Max(0.0, DeltaHours);
	}
	LastHour = CurrentHour;
	AbsHours += DeltaHours;

	for (int32 I = Active.Num() - 1; I >= 0; --I)
	{
		if (Active[I].EndHours <= AbsHours) { EndEventAt(I); }
	}
	if (DeltaHours <= 0.0) return;

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UMTADiscoverySubsystem* Disc = GI->GetSubsystem<UMTADiscoverySubsystem>()) { Disc->RefreshStorySnapshot(); }
	}

	for (int32 I = 0; I < Defs.Num(); ++I)
	{
		if (!CanTrigger(I, CurrentHour)) continue;
		const double P = 1.0 - FMath::Pow(1.0 - FMath::Clamp((double)Defs[I].ChancePerHour, 0.0, 1.0), DeltaHours);
		if (Rng.FRand() >= P) continue;

		// Bezirk waehlen: "any" = zufaellig; Bezirke mit zu vielen Ereignissen ueberspringen.
		TArray<FName> Candidates;
		for (const FName& Dist : Defs[I].Districts)
		{
			if (Dist == TEXT("any")) { for (const TCHAR* A : GAllDistricts) { Candidates.AddUnique(FName(A)); } }
			else { Candidates.AddUnique(Dist); }
		}
		Candidates.RemoveAll([this](const FName& Dist) { return CountInDistrict(Dist) >= MaxConcurrentPerDistrict; });
		if (Candidates.Num() == 0) continue;
		const FName District = Candidates[Rng.RandRange(0, Candidates.Num() - 1)];
		StartEventInternal(I, District, -1.f);
	}
}

void UMTAWorldEventSubsystem::StartEventInternal(int32 DefIndex, FName DistrictId, float DurationHours)
{
	const FMTAWorldEventDef& D = Defs[DefIndex];
	FMTAActiveWorldEvent E;
	E.EventId = D.Id;
	E.DistrictId = DistrictId;
	E.Title = D.Title;
	E.StartHours = AbsHours;
	const float Dur = DurationHours > 0.f ? DurationHours : Rng.FRandRange(D.DurationMinHours, FMath::Max(D.DurationMinHours, D.DurationMaxHours));
	E.EndHours = AbsHours + Dur;
	Active.Add(E);
	OnWorldEventStarted.Broadcast(E);
}

void UMTAWorldEventSubsystem::EndEventAt(int32 ActiveIndex)
{
	const FMTAActiveWorldEvent E = Active[ActiveIndex];
	Active.RemoveAt(ActiveIndex);
	const int32 DefIndex = FindDef(E.EventId);
	if (DefIndex != INDEX_NONE)
	{
		CooldownUntil.Add(E.EventId, AbsHours + Defs[DefIndex].CooldownHours);
		PendingStoryEffects.Append(Runtime[DefIndex].StoryEffects);
	}
	OnWorldEventEnded.Broadcast(E);
}

bool UMTAWorldEventSubsystem::StartEvent(FName EventId, FName DistrictId, float DurationHours)
{
	const int32 I = FindDef(EventId);
	if (I == INDEX_NONE || IsEventActive(EventId)) return false;
	FName District = DistrictId;
	if (District.IsNone())
	{
		District = Defs[I].Districts.Num() > 0 ? Defs[I].Districts[0] : FName(TEXT("core"));
		if (District == TEXT("any")) District = TEXT("core");
	}
	StartEventInternal(I, District, DurationHours);
	return true;
}

bool UMTAWorldEventSubsystem::EndEvent(FName EventId)
{
	for (int32 I = 0; I < Active.Num(); ++I)
	{
		if (Active[I].EventId == EventId) { EndEventAt(I); return true; }
	}
	return false;
}

bool UMTAWorldEventSubsystem::ChooseOption(FName EventId, FName OptionId)
{
	const int32 DefIndex = FindDef(EventId);
	if (DefIndex == INDEX_NONE) return false;
	for (FMTAActiveWorldEvent& E : Active)
	{
		if (E.EventId != EventId || !E.ChosenOption.IsNone()) continue;
		for (const FMTAWorldEventOption& Opt : Defs[DefIndex].Options)
		{
			if (Opt.Id != OptionId) continue;
			E.ChosenOption = OptionId;
			UGameInstance* GI = GetGameInstance();
			if (UMTADiscoverySubsystem* Disc = GI ? GI->GetSubsystem<UMTADiscoverySubsystem>() : nullptr)
			{
				for (const FMTADiscoveryReward& R : Opt.Rewards) { Disc->ApplyReward(EventId, R); }
			}
			return true;
		}
	}
	return false;
}

bool UMTAWorldEventSubsystem::IsEventActive(FName EventId) const
{
	for (const FMTAActiveWorldEvent& E : Active) { if (E.EventId == EventId) return true; }
	return false;
}

TArray<FMTAActiveWorldEvent> UMTAWorldEventSubsystem::GetActiveEventsInDistrict(FName DistrictId) const
{
	TArray<FMTAActiveWorldEvent> Out;
	for (const FMTAActiveWorldEvent& E : Active) { if (E.DistrictId == DistrictId) Out.Add(E); }
	return Out;
}

FMTADistrictModifiers UMTAWorldEventSubsystem::GetDistrictModifiers(FName DistrictId) const
{
	FMTADistrictModifiers M;
	for (const FMTAActiveWorldEvent& E : Active)
	{
		if (E.DistrictId != DistrictId) continue;
		const int32 I = FindDef(E.EventId);
		if (I == INDEX_NONE) continue;
		const FMTADistrictModifiers& X = Defs[I].Effects;
		M.Traffic *= X.Traffic;
		M.Pedestrians *= X.Pedestrians;
		M.Police *= X.Police;
		M.HeatGain *= X.HeatGain;
		M.Cameras *= X.Cameras;
		M.Lights = FMath::Min(M.Lights, X.Lights);
		M.Visibility = FMath::Min(M.Visibility, X.Visibility);
		M.bTrafficLightsOff |= X.bTrafficLightsOff;
		for (const auto& P : X.Prices) { M.Prices.FindOrAdd(P.Key, 1.f) *= P.Value; }
		for (const FString& R : X.RoadClosures) { M.RoadClosures.AddUnique(R); }
		for (const FName& T : X.WorldTags) { M.WorldTags.AddUnique(T); }
	}
	return M;
}

float UMTAWorldEventSubsystem::GetPriceMultiplier(FName DistrictId, FName Category) const
{
	const float* F = GetDistrictModifiers(DistrictId).Prices.Find(Category);
	return F ? *F : 1.f;
}

FString UMTAWorldEventSubsystem::ConsumeStoryEffectsJson()
{
	FString Out;
	FJsonSerializer::Serialize(PendingStoryEffects, TJsonWriterFactory<>::Create(&Out));
	PendingStoryEffects.Reset();
	return Out;
}

FString UMTAWorldEventSubsystem::ToJson() const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema"), 1);
	Root->SetNumberField(TEXT("absHours"), AbsHours);
	TArray<TSharedPtr<FJsonValue>> Act;
	for (const FMTAActiveWorldEvent& E : Active)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), E.EventId.ToString());
		O->SetStringField(TEXT("district"), E.DistrictId.ToString());
		O->SetNumberField(TEXT("start"), E.StartHours);
		O->SetNumberField(TEXT("end"), E.EndHours);
		O->SetStringField(TEXT("option"), E.ChosenOption.IsNone() ? FString() : E.ChosenOption.ToString());
		Act.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("active"), Act);
	TArray<TSharedPtr<FJsonValue>> Cd;
	for (const auto& P : CooldownUntil)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("k"), P.Key.ToString());
		O->SetNumberField(TEXT("v"), P.Value);
		Cd.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("cooldowns"), Cd);
	Root->SetArrayField(TEXT("pendingStory"), PendingStoryEffects);
	FString Out;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Out));
	return Out;
}

bool UMTAWorldEventSubsystem::FromJson(const FString& Json)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()) return false;
	if ((int32)EvNum(Root, TEXT("schema"), 0.0) != 1) return false;
	Active.Reset();
	CooldownUntil.Reset();
	AbsHours = EvNum(Root, TEXT("absHours"), 0.0);
	LastHour = -1.f;
	const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
	if (Root->TryGetArrayField(TEXT("active"), A))
	{
		for (const TSharedPtr<FJsonValue>& V : *A)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (!O.IsValid()) continue;
			FMTAActiveWorldEvent E;
			E.EventId = FName(*EvStr(O, TEXT("id")));
			const int32 I = FindDef(E.EventId);
			if (I == INDEX_NONE) continue;   // Ereignis aus Daten entfernt
			E.DistrictId = FName(*EvStr(O, TEXT("district")));
			E.Title = Defs[I].Title;
			E.StartHours = EvNum(O, TEXT("start"), AbsHours);
			E.EndHours = EvNum(O, TEXT("end"), AbsHours);
			const FString Opt = EvStr(O, TEXT("option"));
			E.ChosenOption = Opt.IsEmpty() ? NAME_None : FName(*Opt);
			Active.Add(E);
		}
	}
	if (Root->TryGetArrayField(TEXT("cooldowns"), A))
	{
		for (const TSharedPtr<FJsonValue>& V : *A)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			if (O.IsValid()) { CooldownUntil.Add(FName(*EvStr(O, TEXT("k"))), EvNum(O, TEXT("v"), 0.0)); }
		}
	}
	PendingStoryEffects.Reset();
	if (Root->TryGetArrayField(TEXT("pendingStory"), A)) { PendingStoryEffects = *A; }
	return true;
}
