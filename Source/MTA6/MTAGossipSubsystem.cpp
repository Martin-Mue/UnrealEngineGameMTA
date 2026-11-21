#include "MTAGossipSubsystem.h"
#include "MTANPCSubsystem.h"
#include "MTAHeatSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

DEFINE_LOG_CATEGORY_STATIC(LogMTAGossip, Log, All);

namespace
{
	const FName PlayerSubject(TEXT("player"));
}

void UMTAGossipSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMTANPCSubsystem>();
	LoadRumorTypes(MTANPC::LoadJsonFile(TEXT("barks.json")));
}

UMTANPCSubsystem* UMTAGossipSubsystem::NPCs() const
{
	const UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UMTANPCSubsystem>() : nullptr;
}

bool UMTAGossipSubsystem::LoadRumorTypes(const TSharedPtr<FJsonObject>& BarksRoot)
{
	Types.Reset();
	const TSharedPtr<FJsonObject>* RT = nullptr;
	if (!BarksRoot.IsValid() || !BarksRoot->TryGetObjectField(TEXT("rumorTypes"), RT))
	{
		UE_LOG(LogMTAGossip, Error, TEXT("barks.json: rumorTypes fehlt"));
		return false;
	}
	for (const auto& Pair : (*RT)->Values)
	{
		const TSharedPtr<FJsonObject> O = Pair.Value->AsObject();
		FRumorTypeDef Def;
		double D = 0.0;
		if (O->TryGetNumberField(TEXT("price"), D)) { Def.Price = (float)D; }
		if (O->TryGetNumberField(TEXT("attitude"), D)) { Def.Attitude = (float)D; }
		if (O->TryGetNumberField(TEXT("heat"), D)) { Def.Heat = (float)D; }
		const TSharedPtr<FJsonObject>* F = nullptr;
		if (O->TryGetObjectField(TEXT("factions"), F))
		{
			for (const auto& FP : (*F)->Values) { Def.Factions.Add(FName(*FP.Key), (float)FP.Value->AsNumber()); }
		}
		const TArray<TSharedPtr<FJsonValue>>* Levels = nullptr;
		if (O->TryGetArrayField(TEXT("text"), Levels))
		{
			for (const TSharedPtr<FJsonValue>& L : *Levels)
			{
				TArray<FString>& Variants = Def.Text.AddDefaulted_GetRef();
				for (const TSharedPtr<FJsonValue>& V : L->AsArray()) { Variants.Add(V->AsString()); }
			}
		}
		Types.Add(FName(*Pair.Key), MoveTemp(Def));
	}
	UE_LOG(LogMTAGossip, Log, TEXT("Geruechtetypen geladen: %d"), Types.Num());
	return Types.Num() > 0;
}

const FMTARumor* UMTAGossipSubsystem::FindRumor(int32 Id) const
{
	return Rumors.FindByPredicate([Id](const FMTARumor& R) { return R.Id == Id; });
}

FMTARumor* UMTAGossipSubsystem::FindRumorMut(int32 Id)
{
	return Rumors.FindByPredicate([Id](const FMTARumor& R) { return R.Id == Id; });
}

// ------------------------------------------------------------------------------------------------
// Entstehung
// ------------------------------------------------------------------------------------------------

int32 UMTAGossipSubsystem::ReportPlayerEvent(const FMTAPlayerEventReport& Report)
{
	const FName Teller = Report.Witnesses.Num() > 0 ? Report.Witnesses[0] : NAME_None;
	const int32 Id = CreateRumor(Report.Type, PlayerSubject, Teller, Report.District, Report.Location, Report.Severity);
	if (FMTARumor* R = FindRumorMut(Id))
	{
		for (const FName& W : Report.Witnesses) { R->KnownBy.FindOrAdd(W, 0.f); }
		float& Reach = R->DistrictReach.FindOrAdd(Report.District);
		Reach = FMath::Max(Reach, FMath::Min(Reach + 0.02f * (float)Report.AnonymousWitnesses, 0.4f));
		RecomputeDistrictEffect(Report.District);
	}
	return Id;
}

int32 UMTAGossipSubsystem::CreateRumor(FName Type, FName Subject, FName Teller, FName District, FName Location, float Severity, FName SecretId)
{
	if (!Types.Contains(Type))
	{
		UE_LOG(LogMTAGossip, Warning, TEXT("Unbekannter Geruechtetyp '%s' -> generic"), *Type.ToString());
		Type = TEXT("generic");
	}
	const UMTANPCSubsystem* N = NPCs();
	const double Now = N ? N->GetTotalHours() : 0.0;

	// Zusammenfassen: gleiche Sache, kurz hintereinander, ist dasselbe Geruecht.
	for (FMTARumor& R : Rumors)
	{
		if (R.Type == Type && R.Subject == Subject && R.SecretId == SecretId && Now - R.CreatedHour < 6.0)
		{
			if (!Teller.IsNone()) { R.KnownBy.FindOrAdd(Teller, 0.f); }
			float& Reach = R.DistrictReach.FindOrAdd(District);
			Reach = FMath::Min(1.f, FMath::Max(Reach, 0.05f) + 0.05f);
			R.Severity = FMath::Clamp(FMath::Max(R.Severity, Severity), 0.f, 1.f);
			RecomputeDistrictEffect(District);
			return R.Id;
		}
	}

	FMTARumor R;
	R.Id = NextId++;
	R.Type = Type;
	R.Subject = Subject;
	R.SecretId = SecretId;
	R.OriginDistrict = District;
	R.Location = Location;
	R.Severity = FMath::Clamp(Severity, 0.f, 1.f);
	R.CreatedHour = Now;
	if (!Teller.IsNone()) { R.KnownBy.Add(Teller, 0.f); }
	if (!District.IsNone()) { R.DistrictReach.Add(District, 0.05f); }
	Rumors.Add(R);
	++StatCreated;
	OnRumorCreated.Broadcast(R);
	Prune();
	RecomputeDistrictEffect(District);
	return R.Id;
}

// ------------------------------------------------------------------------------------------------
// Verbreitung
// ------------------------------------------------------------------------------------------------

int32 UMTAGossipSubsystem::ShareBetween(FName Teller, FName Listener, FRandomStream& Rng)
{
	const UMTANPCSubsystem* N = NPCs();
	const FMTANPCProfile* PT = N ? N->FindProfile(Teller) : nullptr;
	const FMTANPCProfile* PL = N ? N->FindProfile(Listener) : nullptr;
	if (!PT || !PL) { return 0; }
	const FMTANPCRuntime* RL = N->FindRuntime(Listener);
	int32 Shared = 0;
	for (FMTARumor& R : Rumors)
	{
		if (Shared >= 3) { break; }
		const float* TellerDist = R.KnownBy.Find(Teller);
		if (!TellerDist || R.KnownBy.Contains(Listener) || R.Subject == Listener) { continue; }
		const float P = PT->Personality.Talk * R.Credibility * 0.6f;
		if (Rng.FRand() >= P) { continue; }
		// Jede Weitererzaehlung verzerrt: wenig loyale, witzige Leute schmuecken aus.
		const float Add = (1.f - PT->Personality.Loyalty * 0.5f) * 0.12f * (0.5f + PT->Personality.Humor);
		const float NewDist = FMath::Min(1.f, *TellerDist + Add);
		R.KnownBy.Add(Listener, NewDist);
		R.Distortion = FMath::Max(R.Distortion, NewDist);
		R.Severity = FMath::Clamp(R.Severity + Add * 0.15f, 0.f, 1.f);
		R.Credibility = FMath::Clamp(1.f - 0.6f * R.Distortion, 0.2f, 1.f);
		++R.Retellings;
		++StatRetellings;
		if (RL && !RL->Current.District.IsNone())
		{
			float& Reach = R.DistrictReach.FindOrAdd(RL->Current.District);
			Reach = FMath::Min(1.f, Reach + 0.03f);
		}
		++Shared;
	}
	return Shared;
}

void UMTAGossipSubsystem::SimulateHour(FRandomStream& Rng)
{
	UMTANPCSubsystem* N = NPCs();
	if (!N) { return; }
	const double Now = N->GetTotalHours();
	TSet<FName> Changed;

	for (FMTARumor& R : Rumors)
	{
		const double Age = Now - R.CreatedHour;
		// Ankuenfte in Nachbarbezirken.
		TArray<FName> Arrived;
		for (const TPair<FName, double>& P : R.PendingArrival)
		{
			if (Now >= P.Value) { Arrived.Add(P.Key); }
		}
		for (const FName& D : Arrived)
		{
			R.PendingArrival.Remove(D);
			float& Reach = R.DistrictReach.FindOrAdd(D);
			Reach = FMath::Max(Reach, 0.05f);
			R.Distortion = FMath::Min(1.f, R.Distortion + 0.1f);
			R.Credibility = FMath::Clamp(1.f - 0.6f * R.Distortion, 0.2f, 1.f);
			++StatArrivals;
			// Eine Figur im Bezirk hat es "aufgeschnappt" (Pendler, Taxifahrer, Radio).
			const TArray<FName> Locals = N->GetNPCsInDistrict(D);
			if (Locals.Num() > 0)
			{
				R.KnownBy.FindOrAdd(Locals[Rng.RandRange(0, Locals.Num() - 1)], R.Distortion);
			}
			OnRumorReachedDistrict.Broadcast(R, D);
			Changed.Add(D);
		}

		// Statistisches Wachstum je Bezirk (logistisch), Zerfall nach drei Tagen.
		for (TPair<FName, float>& P : R.DistrictReach)
		{
			const float Old = P.Value;
			if (Age < 72.0)
			{
				P.Value = FMath::Clamp(P.Value + 0.08f * R.Credibility * P.Value * (1.f - P.Value), 0.f, 1.f);
			}
			else
			{
				P.Value = FMath::Max(0.f, P.Value - 0.01f);
			}
			if (FMath::Abs(P.Value - Old) > 0.005f) { Changed.Add(P.Key); }

			// Ueberspringen in Nachbarbezirke planen.
			if (P.Value >= 0.35f)
			{
				if (const TMap<FName, float>* Neighbors = N->GetDistrictNeighbors(P.Key))
				{
					for (const TPair<FName, float>& Nb : *Neighbors)
					{
						if (R.DistrictReach.Contains(Nb.Key) || R.PendingArrival.Contains(Nb.Key)) { continue; }
						// Glaubwuerdige Geruechte reisen schneller.
						R.PendingArrival.Add(Nb.Key, Now + (double)Nb.Value * (1.5f - 0.5f * R.Credibility));
					}
				}
			}
			if (P.Value >= 0.5f) { ApplyHeatIfNeeded(R, P.Key); }
		}
	}

	Prune();
	for (const FName& D : Changed) { RecomputeDistrictEffect(D); }
}

void UMTAGossipSubsystem::ApplyHeatIfNeeded(FMTARumor& R, FName District)
{
	if (R.Subject != PlayerSubject || R.HeatApplied.Contains(District)) { return; }
	const FRumorTypeDef* Def = Types.Find(R.Type);
	if (!Def || Def->Heat <= 0.f) { return; }
	R.HeatApplied.Add(District);
	if (!bApplyHeat) { return; }
	const UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (UMTAHeatSubsystem* Heat = World ? World->GetSubsystem<UMTAHeatSubsystem>() : nullptr)
	{
		// Bezirks-Ruf (Weltkonzept M6): "man kennt dich hier".
		Heat->AddHeat(District, Def->Heat * R.Severity);
	}
}

void UMTAGossipSubsystem::Prune()
{
	const UMTANPCSubsystem* N = NPCs();
	const double Now = N ? N->GetTotalHours() : 0.0;
	Rumors.RemoveAll([Now](const FMTARumor& R)
	{
		if (Now - R.CreatedHour < 96.0 || R.PendingArrival.Num() > 0) { return false; }
		for (const TPair<FName, float>& P : R.DistrictReach) { if (P.Value > 0.01f) { return false; } }
		return true;
	});
	if (Rumors.Num() > MaxRumors)
	{
		// Schwaechste zuerst entfernen (Schwere * Glaubwuerdigkeit, aelteste bei Gleichstand).
		Rumors.Sort([](const FMTARumor& A, const FMTARumor& B)
		{
			const float SA = A.Severity * A.Credibility, SB = B.Severity * B.Credibility;
			return SA != SB ? SA > SB : A.CreatedHour > B.CreatedHour;
		});
		Rumors.SetNum(MaxRumors);
	}
}

// ------------------------------------------------------------------------------------------------
// Wirkung
// ------------------------------------------------------------------------------------------------

void UMTAGossipSubsystem::RecomputeDistrictEffect(FName District)
{
	if (District.IsNone()) { return; }
	FMTAGossipDistrictEffect E;
	float Price = 0.f;
	for (const FMTARumor& R : Rumors)
	{
		if (R.Subject != PlayerSubject) { continue; }
		const float* Reach = R.DistrictReach.Find(District);
		const FRumorTypeDef* Def = Types.Find(R.Type);
		if (!Reach || !Def || *Reach <= 0.f) { continue; }
		const float W = R.Severity * *Reach;
		Price += Def->Price * W;
		E.Attitude += Def->Attitude * W;
		E.HeatPressure += Def->Heat * W * 5.f;
		for (const TPair<FName, float>& F : Def->Factions) { E.FactionShift.FindOrAdd(F.Key) += F.Value * W; }
	}
	E.PriceMultiplier = FMath::Clamp(1.f + Price, 0.7f, 1.5f);
	E.Attitude = FMath::Clamp(E.Attitude, -1.f, 1.f);
	E.HeatPressure = FMath::Clamp(E.HeatPressure, 0.f, 5.f);

	const FMTAGossipDistrictEffect* Old = Effects.Find(District);
	const bool bNotable = !Old || FMath::Abs(Old->PriceMultiplier - E.PriceMultiplier) > 0.005f
		|| FMath::Abs(Old->Attitude - E.Attitude) > 0.01f || FMath::Abs(Old->HeatPressure - E.HeatPressure) > 0.02f;
	Effects.Add(District, E);
	if (bNotable) { OnGossipEffectChanged.Broadcast(District, E); }
}

FMTAGossipDistrictEffect UMTAGossipSubsystem::GetDistrictEffect(FName District) const
{
	const FMTAGossipDistrictEffect* E = Effects.Find(District);
	return E ? *E : FMTAGossipDistrictEffect();
}

float UMTAGossipSubsystem::GetPriceModifier(FName District, FName Faction) const
{
	const FMTAGossipDistrictEffect E = GetDistrictEffect(District);
	const float* Shift = E.FactionShift.Find(Faction);
	// Fraktionen, bei denen der Spieler durch Geruechte beliebt ist, geben Rabatt.
	return FMath::Clamp(E.PriceMultiplier * (1.f - (Shift ? *Shift : 0.f) * 0.5f), 0.6f, 1.6f);
}

float UMTAGossipSubsystem::GetPlayerNotoriety(FName District) const
{
	float Sum = 0.f;
	for (const FMTARumor& R : Rumors)
	{
		if (R.Subject != PlayerSubject) { continue; }
		if (const float* Reach = R.DistrictReach.Find(District)) { Sum += *Reach * R.Severity; }
	}
	return FMath::Clamp(Sum, 0.f, 1.f);
}

// ------------------------------------------------------------------------------------------------
// Abfragen fuer Dialoge
// ------------------------------------------------------------------------------------------------

TArray<FMTARumor> UMTAGossipSubsystem::GetRumorsKnownBy(FName NPCId, FName SubjectFilter, FName TypeFilter) const
{
	TArray<FMTARumor> Out;
	for (const FMTARumor& R : Rumors)
	{
		if (!R.KnownBy.Contains(NPCId)) { continue; }
		if (!SubjectFilter.IsNone() && R.Subject != SubjectFilter) { continue; }
		if (!TypeFilter.IsNone() && R.Type != TypeFilter) { continue; }
		Out.Add(R);
	}
	return Out;
}

bool UMTAGossipSubsystem::DoesNPCKnow(FName NPCId, FName Subject, FName Type) const
{
	const UMTANPCSubsystem* N = NPCs();
	const FMTANPCRuntime* RT = N ? N->FindRuntime(NPCId) : nullptr;
	const FMTANPCProfile* PF = N ? N->FindProfile(NPCId) : nullptr;
	for (const FMTARumor& R : Rumors)
	{
		if (R.Subject != Subject || (!Type.IsNone() && R.Type != Type)) { continue; }
		if (R.KnownBy.Contains(NPCId)) { return true; }
		// Was im Viertel jeder weiss, weiss auch diese Figur.
		for (const FName& D : { RT ? RT->Current.District : NAME_None, PF ? PF->District : NAME_None })
		{
			const float* Reach = D.IsNone() ? nullptr : R.DistrictReach.Find(D);
			if (Reach && *Reach >= 0.3f) { return true; }
		}
	}
	return false;
}

FString UMTAGossipSubsystem::GetRumorText(int32 RumorId, FName Teller) const
{
	const FMTARumor* R = FindRumor(RumorId);
	if (!R) { return FString(); }
	const FRumorTypeDef* Def = Types.Find(R->Type);
	if (!Def || Def->Text.Num() == 0) { return FString(); }
	const float* Own = R->KnownBy.Find(Teller);
	const int32 Level = FMath::Min(DistortionLevel(Own ? *Own : R->Distortion), Def->Text.Num() - 1);
	const TArray<FString>& Variants = Def->Text[Level];
	if (Variants.Num() == 0) { return FString(); }
	const int32 Variant = (int32)((uint32)(R->Id * 31 + GetTypeHash(Teller)) % (uint32)Variants.Num());
	FString Text = Variants[Variant];

	const UMTANPCSubsystem* N = NPCs();
	FString Subject;
	FString Secret;
	if (R->Subject == PlayerSubject)
	{
		Subject = Level == 0 ? TEXT("eine Fahrerin") : TEXT("die Voss");
	}
	else if (const FMTANPCProfile* P = N ? N->FindProfile(R->Subject) : nullptr)
	{
		Subject = P->DisplayName;
		for (const FMTANPCSecret& S : P->Secrets) { if (S.Id == R->SecretId) { Secret = S.Text; } }
	}
	Text.ReplaceInline(TEXT("{subject}"), *Subject);
	Text.ReplaceInline(TEXT("{secret}"), *Secret);
	Text.ReplaceInline(TEXT("{district}"), N ? *N->GetDistrictName(R->OriginDistrict) : *R->OriginDistrict.ToString());
	Text.ReplaceInline(TEXT("{place}"), N ? *N->GetLocationName(R->Location) : *R->Location.ToString());
	// Satzanfang gross schreiben ("eine Fahrerin ..." am Anfang).
	if (Text.Len() > 0) { Text[0] = FChar::ToUpper(Text[0]); }
	return Text;
}

int32 UMTAGossipSubsystem::PickRumorForPlayer(FName Teller, FName SubjectFilter)
{
	int32 Best = -1;
	float BestScore = -1.f;
	for (const FMTARumor& R : Rumors)
	{
		if (!SubjectFilter.IsNone() && R.Subject != SubjectFilter) { continue; }
		if (R.Subject == Teller) { continue; } // niemand erzaehlt Geruechte ueber sich selbst
		if (!DoesNPCKnow(Teller, R.Subject, R.Type)) { continue; }
		// Neues zuerst; Spieler-Geruechte nur, wenn ausdruecklich gefragt.
		float Score = R.Severity * R.Credibility + (R.bHeardByPlayer ? 0.f : 1.f);
		if (SubjectFilter.IsNone() && R.Subject == PlayerSubject) { Score -= 0.5f; }
		if (Score > BestScore) { BestScore = Score; Best = R.Id; }
	}
	if (FMTARumor* R = FindRumorMut(Best))
	{
		R->bHeardByPlayer = true;
		R->KnownBy.FindOrAdd(Teller, R->Distortion);
	}
	return Best;
}

// ------------------------------------------------------------------------------------------------
// Persistenz
// ------------------------------------------------------------------------------------------------

FString UMTAGossipSubsystem::ToJson() const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("nextId"), NextId);
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FMTARumor& R : Rumors)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("id"), R.Id);
		O->SetStringField(TEXT("type"), R.Type.ToString());
		O->SetStringField(TEXT("subject"), R.Subject.ToString());
		O->SetStringField(TEXT("secret"), R.SecretId.ToString());
		O->SetStringField(TEXT("origin"), R.OriginDistrict.ToString());
		O->SetStringField(TEXT("location"), R.Location.ToString());
		O->SetNumberField(TEXT("severity"), R.Severity);
		O->SetNumberField(TEXT("credibility"), R.Credibility);
		O->SetNumberField(TEXT("distortion"), R.Distortion);
		O->SetNumberField(TEXT("created"), R.CreatedHour);
		O->SetNumberField(TEXT("retellings"), R.Retellings);
		O->SetBoolField(TEXT("heard"), R.bHeardByPlayer);
		TSharedPtr<FJsonObject> K = MakeShared<FJsonObject>();
		for (const TPair<FName, float>& P : R.KnownBy) { K->SetNumberField(P.Key.ToString(), P.Value); }
		O->SetObjectField(TEXT("known"), K);
		TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
		for (const TPair<FName, float>& P : R.DistrictReach) { D->SetNumberField(P.Key.ToString(), P.Value); }
		O->SetObjectField(TEXT("reach"), D);
		TSharedPtr<FJsonObject> Pd = MakeShared<FJsonObject>();
		for (const TPair<FName, double>& P : R.PendingArrival) { Pd->SetNumberField(P.Key.ToString(), P.Value); }
		O->SetObjectField(TEXT("pending"), Pd);
		TArray<TSharedPtr<FJsonValue>> H;
		for (const FName& Hd : R.HeatApplied) { H.Add(MakeShared<FJsonValueString>(Hd.ToString())); }
		O->SetArrayField(TEXT("heatApplied"), H);
		Arr.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("rumors"), Arr);
	return MTANPC::JsonToString(Root);
}

bool UMTAGossipSubsystem::FromJson(const FString& Json)
{
	const TSharedPtr<FJsonObject> Root = MTANPC::JsonFromString(Json);
	if (!Root.IsValid()) { return false; }
	Rumors.Reset();
	Effects.Reset();
	double Num = 1.0;
	NextId = Root->TryGetNumberField(TEXT("nextId"), Num) ? (int32)Num : 1;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Root->TryGetArrayField(TEXT("rumors"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FMTARumor R;
			FString S;
			double D = 0.0;
			if (O->TryGetNumberField(TEXT("id"), D)) { R.Id = (int32)D; }
			if (O->TryGetStringField(TEXT("type"), S)) { R.Type = FName(*S); }
			if (O->TryGetStringField(TEXT("subject"), S)) { R.Subject = FName(*S); }
			if (O->TryGetStringField(TEXT("secret"), S)) { R.SecretId = FName(*S); }
			if (O->TryGetStringField(TEXT("origin"), S)) { R.OriginDistrict = FName(*S); }
			if (O->TryGetStringField(TEXT("location"), S)) { R.Location = FName(*S); }
			if (O->TryGetNumberField(TEXT("severity"), D)) { R.Severity = (float)D; }
			if (O->TryGetNumberField(TEXT("credibility"), D)) { R.Credibility = (float)D; }
			if (O->TryGetNumberField(TEXT("distortion"), D)) { R.Distortion = (float)D; }
			if (O->TryGetNumberField(TEXT("created"), D)) { R.CreatedHour = D; }
			if (O->TryGetNumberField(TEXT("retellings"), D)) { R.Retellings = (int32)D; }
			O->TryGetBoolField(TEXT("heard"), R.bHeardByPlayer);
			const TSharedPtr<FJsonObject>* Sub = nullptr;
			if (O->TryGetObjectField(TEXT("known"), Sub)) { for (const auto& P : (*Sub)->Values) { R.KnownBy.Add(FName(*P.Key), (float)P.Value->AsNumber()); } }
			if (O->TryGetObjectField(TEXT("reach"), Sub)) { for (const auto& P : (*Sub)->Values) { R.DistrictReach.Add(FName(*P.Key), (float)P.Value->AsNumber()); } }
			if (O->TryGetObjectField(TEXT("pending"), Sub)) { for (const auto& P : (*Sub)->Values) { R.PendingArrival.Add(FName(*P.Key), P.Value->AsNumber()); } }
			const TArray<TSharedPtr<FJsonValue>>* H = nullptr;
			if (O->TryGetArrayField(TEXT("heatApplied"), H)) { for (const TSharedPtr<FJsonValue>& HV : *H) { R.HeatApplied.Add(FName(*HV->AsString())); } }
			NextId = FMath::Max(NextId, R.Id + 1);
			Rumors.Add(MoveTemp(R));
		}
	}
	if (const UMTANPCSubsystem* N = NPCs())
	{
		for (const FName& D : N->GetDistricts()) { RecomputeDistrictEffect(D); }
	}
	return true;
}
