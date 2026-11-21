#include "MTANPCSubsystem.h"
#include "MTANPCActor.h"
#include "MTARelationshipSubsystem.h"
#include "MTAGossipSubsystem.h"
#include "MTADialogueSubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Math/RandomStream.h"

DEFINE_LOG_CATEGORY_STATIC(LogMTANPC, Log, All);

// ------------------------------------------------------------------------------------------------
// Hilfsfunktionen
// ------------------------------------------------------------------------------------------------

namespace MTANPC
{
	static const TCHAR* GActivityNames[] = {
		TEXT("Sleep"), TEXT("Work"), TEXT("Shift"), TEXT("Patrol"), TEXT("Eat"), TEXT("Bar"), TEXT("Market"), TEXT("Leisure"),
		TEXT("Home"), TEXT("Commute"), TEXT("Perform"), TEXT("Worship"), TEXT("Hide"), TEXT("Strike"), TEXT("Shelter"), TEXT("Talk") };

	FString ActivityToString(EMTAActivity A)
	{
		const int32 I = (int32)A;
		return (I >= 0 && I < UE_ARRAY_COUNT(GActivityNames)) ? FString(GActivityNames[I]) : FString(TEXT("Home"));
	}

	EMTAActivity ActivityFromString(const FString& S)
	{
		for (int32 I = 0; I < UE_ARRAY_COUNT(GActivityNames); ++I)
		{
			if (S.Equals(GActivityNames[I], ESearchCase::IgnoreCase)) { return (EMTAActivity)I; }
		}
		UE_LOG(LogMTANPC, Warning, TEXT("Unbekannte Aktivitaet '%s' -> Home"), *S);
		return EMTAActivity::Home;
	}

	FString InteractionToString(EMTAInteractionType T)
	{
		switch (T)
		{
		case EMTAInteractionType::Chat: return TEXT("chat");
		case EMTAInteractionType::Trade: return TEXT("trade");
		case EMTAInteractionType::Argue: return TEXT("argue");
		case EMTAInteractionType::Mediate: return TEXT("mediate");
		case EMTAInteractionType::Cooperate: return TEXT("cooperate");
		case EMTAInteractionType::Shadow: return TEXT("shadow");
		case EMTAInteractionType::Betray: return TEXT("betray");
		default: return TEXT("none");
		}
	}

	FString RelationKindToString(EMTARelationKind K)
	{
		switch (K)
		{
		case EMTARelationKind::Friendship: return TEXT("friendship");
		case EMTARelationKind::Rivalry: return TEXT("rivalry");
		case EMTARelationKind::Debt: return TEXT("debt");
		case EMTARelationKind::Love: return TEXT("love");
		default: return TEXT("kin");
		}
	}

	bool RelationKindFromString(const FString& S, EMTARelationKind& Out)
	{
		if (S == TEXT("friendship")) { Out = EMTARelationKind::Friendship; return true; }
		if (S == TEXT("rivalry")) { Out = EMTARelationKind::Rivalry; return true; }
		if (S == TEXT("debt")) { Out = EMTARelationKind::Debt; return true; }
		if (S == TEXT("love")) { Out = EMTARelationKind::Love; return true; }
		if (S == TEXT("kin")) { Out = EMTARelationKind::Kin; return true; }
		return false;
	}

	TSharedPtr<FJsonObject> LoadJsonFile(const FString& RelativeOrAbsolutePath)
	{
		const FString File = FPaths::IsRelative(RelativeOrAbsolutePath)
			? FPaths::ProjectContentDir() / TEXT("Data") / RelativeOrAbsolutePath
			: RelativeOrAbsolutePath;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			UE_LOG(LogMTANPC, Error, TEXT("Datei nicht lesbar: %s"), *File);
			return nullptr;
		}
		TSharedPtr<FJsonObject> Obj = JsonFromString(Text);
		if (!Obj.IsValid())
		{
			UE_LOG(LogMTANPC, Error, TEXT("Kein gueltiges JSON: %s"), *File);
		}
		return Obj;
	}

	FString JsonToString(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		if (Obj.IsValid())
		{
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		}
		return Out;
	}

	TSharedPtr<FJsonObject> JsonFromString(const FString& Text)
	{
		TSharedPtr<FJsonObject> Obj;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Obj))
		{
			return nullptr;
		}
		return Obj;
	}
}

namespace
{
	FName StrField(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		FString S;
		return (O.IsValid() && O->TryGetStringField(Key, S)) ? FName(*S) : NAME_None;
	}

	float NumField(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, float Default)
	{
		double D = Default;
		return (O.IsValid() && O->TryGetNumberField(Key, D)) ? (float)D : Default;
	}

	TArray<FString> StrArray(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (O.IsValid() && O->TryGetArrayField(Key, Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr) { FString S; if (V.IsValid() && V->TryGetString(S)) { Out.Add(S); } }
		}
		return Out;
	}

	TArray<FName> NameArray(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		TArray<FName> Out;
		for (const FString& S : StrArray(O, Key)) { Out.Add(FName(*S)); }
		return Out;
	}
}

// ------------------------------------------------------------------------------------------------
// Lebenszyklus und Laden
// ------------------------------------------------------------------------------------------------

void UMTANPCSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadNPCs();
	// Optionale Eigenuhr; laeuft nur, wenn AutoGameMinutesPerSecond > 0.
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMTANPCSubsystem::Tick), 0.25f);
	bInitialized = true;
}

void UMTANPCSubsystem::Deinitialize()
{
	FTSTicker::RemoveTicker(TickHandle);
	Super::Deinitialize();
}

bool UMTANPCSubsystem::Tick(float DeltaSeconds)
{
	if (AutoGameMinutesPerSecond > 0.f)
	{
		AutoAccumulator += (double)DeltaSeconds * AutoGameMinutesPerSecond / 60.0;
		if (AutoAccumulator >= 0.05)
		{
			AdvanceTime((float)AutoAccumulator);
			AutoAccumulator = 0.0;
		}
	}
	return true;
}

bool UMTANPCSubsystem::LoadNPCs(const FString& Path)
{
	const TSharedPtr<FJsonObject> Root = MTANPC::LoadJsonFile(Path.IsEmpty() ? FString(TEXT("npcs.json")) : Path);
	if (!Root.IsValid())
	{
		return false;
	}

	Profiles.Reset(); NPCOrder.Reset(); Runtime.Reset(); Schedules.Reset(); WeatherRules.Reset(); EventRules.Reset();
	LocationDistrict.Reset(); LocationNames.Reset(); DistrictNeighbors.Reset(); DistrictIds.Reset(); DistrictNames.Reset();

	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Root->TryGetArrayField(TEXT("districts"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> D = V->AsObject();
			const FName Id = StrField(D, TEXT("id"));
			DistrictIds.Add(Id);
			FString DName;
			if (D->TryGetStringField(TEXT("name"), DName)) { DistrictNames.Add(Id, DName); }
			TMap<FName, float>& N = DistrictNeighbors.Add(Id);
			const TSharedPtr<FJsonObject>* NObj = nullptr;
			if (D->TryGetObjectField(TEXT("neighbors"), NObj))
			{
				for (const auto& Pair : (*NObj)->Values) { N.Add(FName(*Pair.Key), (float)Pair.Value->AsNumber()); }
			}
		}
	}

	TMap<FName, FName> LocationKind;
	if (Root->TryGetArrayField(TEXT("locations"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> L = V->AsObject();
			const FName Id = StrField(L, TEXT("id"));
			LocationDistrict.Add(Id, StrField(L, TEXT("district")));
			LocationKind.Add(Id, StrField(L, TEXT("kind")));
			FString Name; L->TryGetStringField(TEXT("name"), Name);
			LocationNames.Add(Id, Name);
		}
	}

	const TSharedPtr<FJsonObject>* SchedObj = nullptr;
	if (Root->TryGetObjectField(TEXT("schedules"), SchedObj))
	{
		for (const auto& Pair : (*SchedObj)->Values)
		{
			TArray<FMTAScheduleBlock>& Blocks = Schedules.Add(FName(*Pair.Key));
			for (const TSharedPtr<FJsonValue>& BV : Pair.Value->AsArray())
			{
				const TArray<TSharedPtr<FJsonValue>>& B = BV->AsArray();
				if (B.Num() < 4) { continue; }
				FMTAScheduleBlock Block;
				Block.StartHour = (float)B[0]->AsNumber();
				Block.EndHour = (float)B[1]->AsNumber();
				Block.Activity = MTANPC::ActivityFromString(B[2]->AsString());
				Block.Location = FName(*B[3]->AsString());
				const FString Flags = B.Num() > 4 ? B[4]->AsString() : FString();
				Block.bTalkable = Flags.Contains(TEXT("t"));
				Block.bTrading = Flags.Contains(TEXT("h"));
				Blocks.Add(Block);
			}
		}
	}

	const TSharedPtr<FJsonObject>* Rules = nullptr;
	if (Root->TryGetObjectField(TEXT("scheduleRules"), Rules))
	{
		const TSharedPtr<FJsonObject>* W = nullptr;
		if ((*Rules)->TryGetObjectField(TEXT("weather"), W))
		{
			for (const auto& Pair : (*W)->Values)
			{
				TMap<EMTAActivity, EMTAActivity>& Map = WeatherRules.Add(FName(*Pair.Key));
				for (const auto& R : Pair.Value->AsObject()->Values)
				{
					Map.Add(MTANPC::ActivityFromString(FString(*R.Key)), MTANPC::ActivityFromString(R.Value->AsString()));
				}
			}
		}
		const TSharedPtr<FJsonObject>* E = nullptr;
		if ((*Rules)->TryGetObjectField(TEXT("events"), E))
		{
			for (const auto& Pair : (*E)->Values)
			{
				const TSharedPtr<FJsonObject> EO = Pair.Value->AsObject();
				FMTAScheduleEventRule Rule;
				Rule.Factions = NameArray(EO, TEXT("faction"));
				Rule.Location = StrField(EO, TEXT("loc"));
				const TSharedPtr<FJsonObject>* Remap = nullptr;
				if (EO->TryGetObjectField(TEXT("remap"), Remap))
				{
					for (const auto& R : (*Remap)->Values)
					{
						Rule.Remap.Add(MTANPC::ActivityFromString(FString(*R.Key)), MTANPC::ActivityFromString(R.Value->AsString()));
					}
				}
				EventRules.Add(FName(*Pair.Key), Rule);
			}
		}
	}

	if (Root->TryGetArrayField(TEXT("npcs"), Arr))
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject> N = V->AsObject();
			FMTANPCProfile P;
			P.Id = StrField(N, TEXT("id"));
			N->TryGetStringField(TEXT("name"), P.DisplayName);
			P.Age = (int32)NumField(N, TEXT("age"), 30.f);
			N->TryGetStringField(TEXT("occupation"), P.Occupation);
			P.Role = StrField(N, TEXT("role"));
			P.District = StrField(N, TEXT("district"));
			P.Faction = StrField(N, TEXT("faction"));
			N->TryGetStringField(TEXT("storyKey"), P.StoryKey);
			const TSharedPtr<FJsonObject>* Pers = nullptr;
			if (N->TryGetObjectField(TEXT("personality"), Pers))
			{
				P.Personality.Curiosity = NumField(*Pers, TEXT("curiosity"), 0.5f);
				P.Personality.Suspicion = NumField(*Pers, TEXT("suspicion"), 0.5f);
				P.Personality.Humor = NumField(*Pers, TEXT("humor"), 0.5f);
				P.Personality.Courage = NumField(*Pers, TEXT("courage"), 0.5f);
				P.Personality.Greed = NumField(*Pers, TEXT("greed"), 0.5f);
				P.Personality.Loyalty = NumField(*Pers, TEXT("loyalty"), 0.5f);
				P.Personality.Talk = NumField(*Pers, TEXT("talk"), 0.5f);
				P.Personality.Temper = NumField(*Pers, TEXT("temper"), 0.5f);
			}
			P.Habits = StrArray(N, TEXT("habits"));
			const TArray<TSharedPtr<FJsonValue>>* Sec = nullptr;
			if (N->TryGetArrayField(TEXT("secrets"), Sec))
			{
				for (const TSharedPtr<FJsonValue>& SV : *Sec)
				{
					const TSharedPtr<FJsonObject> SO = SV->AsObject();
					FMTANPCSecret S;
					S.Id = StrField(SO, TEXT("id"));
					SO->TryGetStringField(TEXT("text"), S.Text);
					S.Severity = NumField(SO, TEXT("severity"), 0.5f);
					P.Secrets.Add(S);
				}
			}
			P.HomeLocation = StrField(N, TEXT("home"));
			P.WorkLocation = StrField(N, TEXT("work"));
			P.Haunts = NameArray(N, TEXT("haunts"));
			P.AppearanceArchetype = StrField(N, TEXT("appearance"));
			P.ScheduleId = StrField(N, TEXT("schedule"));
			const TSharedPtr<FJsonObject>* Trader = nullptr;
			if (N->TryGetObjectField(TEXT("trader"), Trader))
			{
				P.ShopId = StrField(*Trader, TEXT("shop"));
				P.Goods = NameArray(*Trader, TEXT("goods"));
			}
			N->TryGetStringField(TEXT("bio"), P.Bio);
			P.Conflicts = StrArray(N, TEXT("conflicts"));

			if (P.Id.IsNone() || Profiles.Contains(P.Id))
			{
				UE_LOG(LogMTANPC, Warning, TEXT("NPC ohne/mit doppelter ID uebersprungen: %s"), *P.Id.ToString());
				continue;
			}
			if (!Schedules.Contains(P.ScheduleId))
			{
				UE_LOG(LogMTANPC, Warning, TEXT("NPC %s: Zeitplan '%s' fehlt"), *P.Id.ToString(), *P.ScheduleId.ToString());
			}
			NPCOrder.Add(P.Id);
			Profiles.Add(P.Id, P);
			Runtime.Add(P.Id, FMTANPCRuntime());
		}
	}

	// Unterstand: erster Stammort, der ein Dach hat, sonst Zuhause (wird in ResolveLocationToken genutzt).
	ShelterKinds = { FName(TEXT("bar")), FName(TEXT("food")), FName(TEXT("shop")), FName(TEXT("work")) };
	LocationKinds = MoveTemp(LocationKind);

	SimulatedUntil = FMath::FloorToDouble(TotalHours);
	RefreshSchedules(false);

	// Beim Neuladen zur Laufzeit auch die Startkanten neu setzen (beim Start laedt das Beziehungs-Subsystem selbst).
	if (UGameInstance* GI = bInitialized ? GetGameInstance() : nullptr)
	{
		if (UMTARelationshipSubsystem* Rel = GI->GetSubsystem<UMTARelationshipSubsystem>())
		{
			Rel->LoadFromNPCData(Root);
		}
	}

	UE_LOG(LogMTANPC, Log, TEXT("NPCs geladen: %d Figuren, %d Zeitplaene, %d Orte"), Profiles.Num(), Schedules.Num(), LocationDistrict.Num());
	return Profiles.Num() > 0;
}

// ------------------------------------------------------------------------------------------------
// Abfragen
// ------------------------------------------------------------------------------------------------

TArray<FName> UMTANPCSubsystem::GetAllNPCIds() const
{
	return NPCOrder;
}

bool UMTANPCSubsystem::GetProfile(FName NPCId, FMTANPCProfile& OutProfile) const
{
	if (const FMTANPCProfile* P = Profiles.Find(NPCId)) { OutProfile = *P; return true; }
	return false;
}

bool UMTANPCSubsystem::GetRuntime(FName NPCId, FMTANPCRuntime& OutRuntime) const
{
	if (const FMTANPCRuntime* R = Runtime.Find(NPCId)) { OutRuntime = *R; return true; }
	return false;
}

TArray<FName> UMTANPCSubsystem::GetNPCsAtLocation(FName LocationTag) const
{
	TArray<FName> Out;
	for (const FName& Id : NPCOrder)
	{
		const FMTANPCRuntime& R = Runtime.FindChecked(Id);
		if (R.bActive && R.Current.Location == LocationTag) { Out.Add(Id); }
	}
	return Out;
}

TArray<FName> UMTANPCSubsystem::GetNPCsInDistrict(FName District) const
{
	TArray<FName> Out;
	for (const FName& Id : NPCOrder)
	{
		const FMTANPCRuntime& R = Runtime.FindChecked(Id);
		if (R.bActive && R.Current.District == District) { Out.Add(Id); }
	}
	return Out;
}

bool UMTANPCSubsystem::IsAvailableForDialogue(FName NPCId) const
{
	const FMTANPCRuntime* R = Runtime.Find(NPCId);
	if (!R || !R->bActive) { return false; }
	if (R->BusyUntil > TotalHours) { return false; }
	return R->Current.bTalkable;
}

bool UMTANPCSubsystem::IsTrading(FName NPCId) const
{
	const FMTANPCRuntime* R = Runtime.Find(NPCId);
	return R && R->bActive && R->Current.bTrading;
}

FName UMTANPCSubsystem::GetLocationDistrict(FName LocationTag) const
{
	const FName* D = LocationDistrict.Find(LocationTag);
	return D ? *D : NAME_None;
}

FString UMTANPCSubsystem::GetLocationName(FName LocationTag) const
{
	const FString* N = LocationNames.Find(LocationTag);
	return N ? *N : LocationTag.ToString();
}

FName UMTANPCSubsystem::ResolveLocationToken(const FMTANPCProfile& P, FName Token) const
{
	const FString S = Token.ToString();
	if (!S.StartsWith(TEXT("@"))) { return Token; }
	if (S == TEXT("@home")) { return P.HomeLocation; }
	if (S == TEXT("@work")) { return P.WorkLocation; }
	if (S == TEXT("@haunt")) { return P.Haunts.Num() > 0 ? P.Haunts[0] : P.HomeLocation; }
	if (S == TEXT("@haunt2")) { return P.Haunts.Num() > 1 ? P.Haunts[1] : (P.Haunts.Num() > 0 ? P.Haunts[0] : P.HomeLocation); }
	if (S == TEXT("@shelter"))
	{
		for (const FName& H : P.Haunts)
		{
			const FName* Kind = LocationKinds.Find(H);
			if (Kind && ShelterKinds.Contains(*Kind)) { return H; }
		}
		return P.HomeLocation;
	}
	return P.HomeLocation;
}

FMTAScheduleState UMTANPCSubsystem::EvaluateSchedule(FName NPCId, float AbsoluteHour) const
{
	FMTAScheduleState State;
	const FMTANPCProfile* P = Profiles.Find(NPCId);
	if (!P) { return State; }

	const float H = FMath::Fmod(FMath::Max(0.f, AbsoluteHour), 24.f);
	const TArray<FMTAScheduleBlock>* Blocks = Schedules.Find(P->ScheduleId);
	FMTAScheduleBlock Block;
	Block.Location = P->HomeLocation;
	if (Blocks)
	{
		for (const FMTAScheduleBlock& B : *Blocks)
		{
			if (H >= B.StartHour && H < B.EndHour) { Block = B; break; }
		}
	}

	EMTAActivity Act = Block.Activity;
	FName LocToken = Block.Location;
	FName Reason = NAME_None;

	// Wetterunterbrechung
	if (const TMap<EMTAActivity, EMTAActivity>* WRule = WeatherRules.Find(Weather))
	{
		if (const EMTAActivity* NewAct = WRule->Find(Act))
		{
			if (*NewAct != Act)
			{
				Act = *NewAct;
				Reason = Weather;
				if (Act == EMTAActivity::Shelter) { LocToken = TEXT("@shelter"); }
				else if (Act == EMTAActivity::Home) { LocToken = TEXT("@home"); }
				else if (Act == EMTAActivity::Bar) { LocToken = TEXT("@shelter"); }
			}
		}
	}

	// Bezirksereignisse (gelten fuer den Heimatbezirk der Figur und den Bezirk des geplanten Ortes)
	const FName PlannedDistrict = GetLocationDistrict(ResolveLocationToken(*P, LocToken));
	for (const FName& D : { P->District, PlannedDistrict })
	{
		const TSet<FName>* Active = DistrictEvents.Find(D);
		if (!Active) { continue; }
		for (const FName& EventId : *Active)
		{
			const FMTAScheduleEventRule* Rule = EventRules.Find(EventId);
			if (!Rule) { continue; }
			if (Rule->Factions.Num() > 0 && !Rule->Factions.Contains(P->Faction)) { continue; }
			if (const EMTAActivity* NewAct = Rule->Remap.Find(Act))
			{
				Act = *NewAct;
				Reason = EventId;
				if (!Rule->Location.IsNone()) { LocToken = Rule->Location; }
			}
		}
	}

	State.Activity = Act;
	State.Location = ResolveLocationToken(*P, LocToken);
	const FName LocDistrict = GetLocationDistrict(State.Location);
	State.District = LocDistrict.IsNone() ? P->District : LocDistrict;
	const bool bAsleepOrHidden = Act == EMTAActivity::Sleep || Act == EMTAActivity::Hide;
	State.bTalkable = Block.bTalkable && !bAsleepOrHidden;
	// Bei Unterstand/Streik darf man ansprechen, wenn der Block es erlaubte oder es ein oeffentlicher Ausweichort ist.
	if (!Reason.IsNone() && (Act == EMTAActivity::Shelter || Act == EMTAActivity::Strike || Act == EMTAActivity::Bar)) { State.bTalkable = true; }
	State.bTrading = Block.bTrading && Reason.IsNone();
	State.OverrideReason = Reason;
	State.BlockEndHour = Block.EndHour;
	return State;
}

// ------------------------------------------------------------------------------------------------
// Welt-Eingaenge
// ------------------------------------------------------------------------------------------------

void UMTANPCSubsystem::SetGameTime(int32 Day, float Hour)
{
	const double Target = (double)Day * 24.0 + (double)FMath::Clamp(Hour, 0.f, 23.999f);
	const double Delta = Target - TotalHours;
	if (Delta > 0.0 && Delta <= 48.0)
	{
		AdvanceTime((float)Delta);
	}
	else
	{
		// Rueckwaerts oder Sprung > 48 h (Laden, Schlafen): nur Zeitplaene neu setzen, keine Massen-Simulation.
		TotalHours = Target;
		SimulatedUntil = FMath::FloorToDouble(Target);
		RefreshSchedules(true);
	}
}

void UMTANPCSubsystem::AdvanceTime(float GameHours)
{
	if (GameHours <= 0.f) { return; }
	const double Target = TotalHours + (double)GameHours;
	while (SimulatedUntil + 1.0 <= Target)
	{
		SimulatedUntil += 1.0;
		TotalHours = SimulatedUntil;
		StepHour();
	}
	TotalHours = Target;
	RefreshSchedules(true);
}

void UMTANPCSubsystem::SetWeather(FName WeatherId)
{
	if (Weather != WeatherId)
	{
		Weather = WeatherId.IsNone() ? FName(TEXT("clear")) : WeatherId;
		RefreshSchedules(true);
	}
}

void UMTANPCSubsystem::SetDistrictEvent(FName District, FName EventId, bool bActive)
{
	TSet<FName>& Set = DistrictEvents.FindOrAdd(District);
	if (bActive) { Set.Add(EventId); } else { Set.Remove(EventId); }
	RefreshSchedules(true);
}

bool UMTANPCSubsystem::HasDistrictEvent(FName District, FName EventId) const
{
	const TSet<FName>* Set = DistrictEvents.Find(District);
	return Set && Set->Contains(EventId);
}

void UMTANPCSubsystem::SetPlayerDistrict(FName District)
{
	PlayerDistrict = District;
	UpdateLOD();
}

void UMTANPCSubsystem::RegisterNPCActor(FName NPCId, AActor* Actor)
{
	if (!Actor || !Profiles.Contains(NPCId)) { return; }
	Actors.Add(NPCId, Actor);
	if (Actor->GetClass()->ImplementsInterface(UMTANPCActor::StaticClass()))
	{
		IMTANPCActor::Execute_ApplyAppearance(Actor, Profiles[NPCId].AppearanceArchetype);
		IMTANPCActor::Execute_OnActivityChanged(Actor, Runtime[NPCId].Current);
	}
	UpdateLOD();
}

void UMTANPCSubsystem::UnregisterNPCActor(FName NPCId)
{
	Actors.Remove(NPCId);
	UpdateLOD();
}

AActor* UMTANPCSubsystem::GetNPCActor(FName NPCId) const
{
	const TWeakObjectPtr<AActor>* A = Actors.Find(NPCId);
	return A ? A->Get() : nullptr;
}

void UMTANPCSubsystem::SetNPCActive(FName NPCId, bool bActive)
{
	if (FMTANPCRuntime* R = Runtime.Find(NPCId)) { R->bActive = bActive; }
}

void UMTANPCSubsystem::SetBusy(FName NPCId, float GameHours)
{
	if (FMTANPCRuntime* R = Runtime.Find(NPCId)) { R->BusyUntil = FMath::Max(R->BusyUntil, TotalHours + GameHours); }
}

void UMTANPCSubsystem::AddMood(FName NPCId, float Delta)
{
	if (FMTANPCRuntime* R = Runtime.Find(NPCId)) { R->Mood = FMath::Clamp(R->Mood + Delta, -1.f, 1.f); }
}

bool UMTANPCSubsystem::StartConversation(FName NPCId)
{
	UGameInstance* GI = GetGameInstance();
	UMTADialogueSubsystem* Dlg = GI ? GI->GetSubsystem<UMTADialogueSubsystem>() : nullptr;
	return Dlg && Dlg->StartConversation(NPCId);
}

// ------------------------------------------------------------------------------------------------
// Simulation
// ------------------------------------------------------------------------------------------------

void UMTANPCSubsystem::UpdateLOD()
{
	for (const FName& Id : NPCOrder)
	{
		FMTANPCRuntime& R = Runtime.FindChecked(Id);
		const TWeakObjectPtr<AActor>* A = Actors.Find(Id);
		if (A && A->IsValid()) { R.LOD = EMTASimLOD::Near; }
		else if (!PlayerDistrict.IsNone() && R.Current.District == PlayerDistrict) { R.LOD = EMTASimLOD::Mid; }
		else { R.LOD = EMTASimLOD::Far; }
	}
}

void UMTANPCSubsystem::RefreshSchedules(bool bBroadcast)
{
	const float Hour = (float)FMath::Fmod(TotalHours, 24.0);
	for (const FName& Id : NPCOrder)
	{
		FMTANPCRuntime& R = Runtime.FindChecked(Id);
		const FMTAScheduleState NewState = EvaluateSchedule(Id, Hour);
		const bool bChanged = NewState.Activity != R.Current.Activity || NewState.Location != R.Current.Location
			|| NewState.bTalkable != R.Current.bTalkable || NewState.bTrading != R.Current.bTrading;
		R.Current = NewState;
		if (!bChanged) { continue; }
		++LiveStats.ActivityChanges;
		// Weit entfernte NPCs melden nichts (spart Delegate-Aufrufe in langen Simulationen).
		if (bBroadcast && R.LOD != EMTASimLOD::Far)
		{
			OnNPCActivityChanged.Broadcast(Id, NewState);
			if (AActor* Actor = GetNPCActor(Id))
			{
				if (Actor->GetClass()->ImplementsInterface(UMTANPCActor::StaticClass()))
				{
					IMTANPCActor::Execute_OnActivityChanged(Actor, NewState);
				}
			}
		}
	}
	UpdateLOD();
}

void UMTANPCSubsystem::BroadcastInteraction(const FMTANPCInteractionEvent& Event)
{
	OnNPCInteraction.Broadcast(Event);
	for (const FName& Id : { Event.A, Event.B })
	{
		if (AActor* Actor = GetNPCActor(Id))
		{
			if (Actor->GetClass()->ImplementsInterface(UMTANPCActor::StaticClass()))
			{
				IMTANPCActor::Execute_PlayInteraction(Actor, Event);
			}
		}
	}
}

void UMTANPCSubsystem::StepHour()
{
	RefreshSchedules(true);

	UGameInstance* GI = GetGameInstance();
	if (!Relationships && GI) { Relationships = GI->GetSubsystem<UMTARelationshipSubsystem>(); }
	if (!Gossip && GI) { Gossip = GI->GetSubsystem<UMTAGossipSubsystem>(); }

	// Fester Seed je Spielstunde: gleiche Zeit + gleicher Zustand = gleiche Welt (reproduzierbare Tests).
	FRandomStream Rng((int32)(((int64)SimulatedUntil * 7919 + 104729) & 0x7fffffff));

	if (Relationships)
	{
		TArray<FMTANPCInteractionEvent> Events;
		Relationships->SimulateHour(Rng, Events);
		for (const FMTANPCInteractionEvent& E : Events)
		{
			++LiveStats.Interactions;
			if (E.bVisible) { ++LiveStats.DetailedInteractions; } else { ++LiveStats.StatisticalInteractions; }
			LiveStats.InteractionsByType.FindOrAdd(MTANPC::InteractionToString(E.Type))++;
			if (Gossip && (E.Type == EMTAInteractionType::Chat || E.Type == EMTAInteractionType::Trade || E.Type == EMTAInteractionType::Cooperate))
			{
				Gossip->ShareBetween(E.A, E.B, Rng);
				Gossip->ShareBetween(E.B, E.A, Rng);
			}
			if (E.bVisible) { BroadcastInteraction(E); }
		}
	}
	if (Gossip)
	{
		Gossip->SimulateHour(Rng);
	}

	for (TPair<FName, FMTANPCRuntime>& Pair : Runtime)
	{
		Pair.Value.Mood *= 0.95f;
	}

	OnSimulationHour.Broadcast(GetDay(), GetHour());
}

// ------------------------------------------------------------------------------------------------
// Test
// ------------------------------------------------------------------------------------------------

FMTANPCSimStats UMTANPCSubsystem::RunNPCSimulation(int32 Days, bool bRestoreState)
{
	Days = FMath::Clamp(Days, 1, 365);
	const FString Snapshot = SaveStateToJson();
	UGameInstance* GI = GetGameInstance();
	if (!Relationships && GI) { Relationships = GI->GetSubsystem<UMTARelationshipSubsystem>(); }
	if (!Gossip && GI) { Gossip = GI->GetSubsystem<UMTAGossipSubsystem>(); }

	LiveStats = FMTANPCSimStats();
	if (Relationships) { Relationships->MarkDriftBaseline(); }
	const int32 RumorsBefore = Gossip ? Gossip->StatCreated : 0;
	const int32 RetellBefore = Gossip ? Gossip->StatRetellings : 0;
	const int32 ArrivalsBefore = Gossip ? Gossip->StatArrivals : 0;

	// Testeingang: je Tag ein Spielerereignis in wechselndem Bezirk mit einem anwesenden Zeugen,
	// damit die Geruechtekette (Zeuge -> Bezirk -> Nachbarbezirk) messbar ist.
	static const TCHAR* TestTypes[] = { TEXT("reckless_driving"), TEXT("helped_someone"), TEXT("police_chase"), TEXT("crash"), TEXT("generosity"), TEXT("hacked_camera"), TEXT("escaped_police") };
	TMap<FName, int32> ActivityHours;
	for (int32 D = 0; D < Days; ++D)
	{
		if (Gossip && DistrictIds.Num() > 0)
		{
			const FName District = DistrictIds[D % DistrictIds.Num()];
			FMTAPlayerEventReport Report;
			Report.Type = TestTypes[D % UE_ARRAY_COUNT(TestTypes)];
			Report.District = District;
			const TArray<FName> Local = GetNPCsInDistrict(District);
			if (Local.Num() > 0) { Report.Witnesses.Add(Local[D % Local.Num()]); Report.Location = Runtime[Local[D % Local.Num()]].Current.Location; }
			Report.AnonymousWitnesses = 6;
			Report.Severity = 0.4f + 0.1f * (D % 4);
			Gossip->ReportPlayerEvent(Report);
		}
		for (int32 H = 0; H < 24; ++H)
		{
			AdvanceTime(1.f);
			for (const FName& Id : NPCOrder) { ActivityHours.FindOrAdd(FName(*MTANPC::ActivityToString(Runtime[Id].Current.Activity)))++; }
		}
	}

	LiveStats.Days = Days;
	if (Gossip)
	{
		LiveStats.RumorsCreated = Gossip->StatCreated - RumorsBefore;
		LiveStats.RumorRetellings = Gossip->StatRetellings - RetellBefore;
		LiveStats.RumorDistrictArrivals = Gossip->StatArrivals - ArrivalsBefore;
	}
	if (Relationships)
	{
		Relationships->ComputeDrift(LiveStats.MeanFriendshipDrift, LiveStats.MeanRivalryDrift, LiveStats.NewEdges);
	}

	FString R = FString::Printf(TEXT("NPC-Simulation: %d Tage, %d Figuren\n"), Days, NPCOrder.Num());
	R += FString::Printf(TEXT("Aktivitaetswechsel: %d\n"), LiveStats.ActivityChanges);
	R += FString::Printf(TEXT("Interaktionen: %d (detailliert %d, statistisch %d)\n"), LiveStats.Interactions, LiveStats.DetailedInteractions, LiveStats.StatisticalInteractions);
	for (const TPair<FString, int32>& P : LiveStats.InteractionsByType) { R += FString::Printf(TEXT("  %s: %d\n"), *P.Key, P.Value); }
	R += FString::Printf(TEXT("Geruechte: %d neu, %d Weitererzaehlungen, %d Bezirksankuenfte\n"), LiveStats.RumorsCreated, LiveStats.RumorRetellings, LiveStats.RumorDistrictArrivals);
	R += FString::Printf(TEXT("Beziehungsdrift: Freundschaft %.2f, Rivalitaet %.2f im Mittel je Kante; %d neue Kanten\n"), LiveStats.MeanFriendshipDrift, LiveStats.MeanRivalryDrift, LiveStats.NewEdges);
	R += TEXT("Aktivitaets-Stunden:");
	for (const TPair<FName, int32>& P : ActivityHours) { R += FString::Printf(TEXT(" %s=%d"), *P.Key.ToString(), P.Value); }
	if (Gossip)
	{
		R += TEXT("\nGeruechte-Wirkung je Bezirk:");
		for (const FName& D : DistrictIds)
		{
			const FMTAGossipDistrictEffect E = Gossip->GetDistrictEffect(D);
			R += FString::Printf(TEXT("\n  %s: Preis x%.2f, Haltung %+.2f, Fahndungsdruck %.2f"), *D.ToString(), E.PriceMultiplier, E.Attitude, E.HeatPressure);
		}
	}
	LiveStats.Report = R;
	UE_LOG(LogMTANPC, Log, TEXT("%s"), *R);

	const FMTANPCSimStats Result = LiveStats;
	if (bRestoreState)
	{
		LoadStateFromJson(Snapshot);
	}
	return Result;
}

// ------------------------------------------------------------------------------------------------
// Persistenz
// ------------------------------------------------------------------------------------------------

FString UMTANPCSubsystem::ToJson() const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("totalHours"), TotalHours);
	Root->SetNumberField(TEXT("simulatedUntil"), SimulatedUntil);
	Root->SetStringField(TEXT("weather"), Weather.ToString());
	TSharedPtr<FJsonObject> Ev = MakeShared<FJsonObject>();
	for (const TPair<FName, TSet<FName>>& P : DistrictEvents)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FName& E : P.Value) { Arr.Add(MakeShared<FJsonValueString>(E.ToString())); }
		Ev->SetArrayField(P.Key.ToString(), Arr);
	}
	Root->SetObjectField(TEXT("events"), Ev);
	TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
	for (const TPair<FName, FMTANPCRuntime>& P : Runtime)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("mood"), P.Value.Mood);
		O->SetBoolField(TEXT("active"), P.Value.bActive);
		O->SetNumberField(TEXT("busyUntil"), P.Value.BusyUntil);
		N->SetObjectField(P.Key.ToString(), O);
	}
	Root->SetObjectField(TEXT("npcs"), N);
	return MTANPC::JsonToString(Root);
}

bool UMTANPCSubsystem::FromJson(const FString& Json)
{
	const TSharedPtr<FJsonObject> Root = MTANPC::JsonFromString(Json);
	if (!Root.IsValid()) { return false; }
	double D = 0.0;
	if (Root->TryGetNumberField(TEXT("totalHours"), D)) { TotalHours = D; }
	SimulatedUntil = Root->TryGetNumberField(TEXT("simulatedUntil"), D) ? D : FMath::FloorToDouble(TotalHours);
	Weather = StrField(Root, TEXT("weather"));
	if (Weather.IsNone()) { Weather = TEXT("clear"); }
	DistrictEvents.Reset();
	const TSharedPtr<FJsonObject>* Ev = nullptr;
	if (Root->TryGetObjectField(TEXT("events"), Ev))
	{
		for (const auto& P : (*Ev)->Values)
		{
			TSet<FName>& Set = DistrictEvents.Add(FName(*P.Key));
			for (const TSharedPtr<FJsonValue>& V : P.Value->AsArray()) { Set.Add(FName(*V->AsString())); }
		}
	}
	const TSharedPtr<FJsonObject>* N = nullptr;
	if (Root->TryGetObjectField(TEXT("npcs"), N))
	{
		for (const auto& P : (*N)->Values)
		{
			FMTANPCRuntime* R = Runtime.Find(FName(*P.Key));
			if (!R) { continue; } // Figur existiert in dieser Version nicht mehr
			const TSharedPtr<FJsonObject> O = P.Value->AsObject();
			R->Mood = NumField(O, TEXT("mood"), 0.f);
			bool bAct = true; O->TryGetBoolField(TEXT("active"), bAct); R->bActive = bAct;
			R->BusyUntil = NumField(O, TEXT("busyUntil"), 0.f);
		}
	}
	RefreshSchedules(false);
	return true;
}

FString UMTANPCSubsystem::SaveStateToJson() const
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schema"), 1);
	Root->SetObjectField(TEXT("npc"), MTANPC::JsonFromString(ToJson()));
	if (UGameInstance* GI = GetGameInstance())
	{
		if (const UMTARelationshipSubsystem* Rel = GI->GetSubsystem<UMTARelationshipSubsystem>())
		{
			Root->SetObjectField(TEXT("relationships"), MTANPC::JsonFromString(Rel->ToJson()));
		}
		if (const UMTAGossipSubsystem* Gos = GI->GetSubsystem<UMTAGossipSubsystem>())
		{
			Root->SetObjectField(TEXT("gossip"), MTANPC::JsonFromString(Gos->ToJson()));
		}
		if (const UMTADialogueSubsystem* Dlg = GI->GetSubsystem<UMTADialogueSubsystem>())
		{
			Root->SetObjectField(TEXT("dialogue"), MTANPC::JsonFromString(Dlg->ToJson()));
		}
	}
	return MTANPC::JsonToString(Root);
}

bool UMTANPCSubsystem::LoadStateFromJson(const FString& Json)
{
	const TSharedPtr<FJsonObject> Root = MTANPC::JsonFromString(Json);
	if (!Root.IsValid()) { return false; }
	const TSharedPtr<FJsonObject>* Part = nullptr;
	bool bOk = true;
	UGameInstance* GI = GetGameInstance();
	// Reihenfolge: Beziehungen und Geruechte zuerst, dann NPC-Zeit (RefreshSchedules nutzt keinen der beiden).
	if (GI && Root->TryGetObjectField(TEXT("relationships"), Part))
	{
		if (UMTARelationshipSubsystem* Rel = GI->GetSubsystem<UMTARelationshipSubsystem>()) { bOk &= Rel->FromJson(MTANPC::JsonToString(*Part)); }
	}
	if (GI && Root->TryGetObjectField(TEXT("gossip"), Part))
	{
		if (UMTAGossipSubsystem* Gos = GI->GetSubsystem<UMTAGossipSubsystem>()) { bOk &= Gos->FromJson(MTANPC::JsonToString(*Part)); }
	}
	if (GI && Root->TryGetObjectField(TEXT("dialogue"), Part))
	{
		if (UMTADialogueSubsystem* Dlg = GI->GetSubsystem<UMTADialogueSubsystem>()) { bOk &= Dlg->FromJson(MTANPC::JsonToString(*Part)); }
	}
	if (Root->TryGetObjectField(TEXT("npc"), Part))
	{
		bOk &= FromJson(MTANPC::JsonToString(*Part));
	}
	return bOk;
}
